#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "driver/i2s.h"

// ============================================================
// One ESP32 board doing mic recording + speaker playback + OLED
// status, using a SINGLE I2S peripheral (I2S_NUM_0), switched
// between RX (mic) and TX (speaker) at the natural handoff points:
//
//   1. Press RECORD button -> switch to mic mode, start recording
//   2. Press it again      -> stop recording, audio streams to PC
//   3. PC does STT -> backend -> TTS, then streams reply audio back
//   4. Right after recording stops, we switch to speaker mode and
//      wait for that reply; once it finishes playing, we switch
//      back to mic mode and return to idle.
//
//   Press REPEAT button (while idle) -> asks the PC to resend and
//      replay the last reply, without recording anything new.
//
// Some ESP32 boards/variants only expose one usable I2S peripheral,
// so this avoids ever requesting a second one (I2S_NUM_1).
// ============================================================

// ============================================================
// PINS
// ============================================================

#define BUTTON_PIN 4          // press to record / stop
#define REPEAT_BUTTON_PIN 18   // press to replay the last reply

#define OLED_SDA 21
#define OLED_SCL 22

#define I2S_PORT I2S_NUM_0

// Microphone (INMP441)
#define MIC_BCK_PIN 26
#define MIC_WS_PIN  25
#define MIC_SD_PIN  32

// Speaker (MAX98357A)
#define SPK_BCK_PIN  13
#define SPK_WS_PIN   33
#define SPK_DOUT_PIN 27

// ============================================================
// AUDIO CONFIG
// ============================================================

#define SAMPLE_RATE 16000
#define SERIAL_BAUD 921600

#define MIC_I2S_BUFFER_SAMPLES 256

// Digital volume gain applied to outgoing speaker audio.
// 1.0 = unchanged. Values above 1.0 boost volume but will clip
// (distort) if pushed too far, since 16-bit samples can't exceed
// +/-32767. Start around 1.3-1.8 and back off if it sounds harsh.
// For more volume than this can safely give you, look at the
// MAX98357A's GAIN pin instead (see README) - it's a hardware gain
// select (9/12/15/18 dB) independent of this software gain.
#define VOLUME_GAIN 1.6f

// Ring buffer for the SPEAKER path only.
// 64 KB ~= 2 seconds of 16kHz/16-bit/mono audio.
#define RING_BUFFER_SIZE 65536

#define FREE_REPORT_INTERVAL_MS 20

// ============================================================
// SYSTEM STATE
// ============================================================

enum SystemState {
    STATE_IDLE,
    STATE_RECORDING,
    STATE_WAITING_REPLY,
    STATE_PLAYING
};

volatile SystemState systemState = STATE_IDLE;

// True only while actively streaming AUD0..CHNK..AUD1 to the PC.
// The status task must stay silent during this window.
volatile bool isRecording = false;

// ============================================================
// I2S ROLE SWITCHING (single physical port, two configs)
// ============================================================

enum I2SRole {
    I2S_ROLE_NONE,
    I2S_ROLE_MIC,
    I2S_ROLE_SPEAKER
};

volatile I2SRole i2sRole = I2S_ROLE_NONE;

bool installMicRole()
{
    if (i2sRole == I2S_ROLE_MIC) return true;

    if (i2sRole != I2S_ROLE_NONE) {
        i2s_driver_uninstall(I2S_PORT);
        i2sRole = I2S_ROLE_NONE;
    }

    i2s_config_t config = {};
    config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = SAMPLE_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;

    esp_err_t result = i2s_driver_install(I2S_PORT, &config, 0, NULL);
    if (result != ESP_OK) {
        Serial.printf("[ERROR] Mic driver install failed: %d\n", result);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num = MIC_BCK_PIN;
    pins.ws_io_num = MIC_WS_PIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = MIC_SD_PIN;

    result = i2s_set_pin(I2S_PORT, &pins);
    if (result != ESP_OK) {
        Serial.printf("[ERROR] Mic set_pin failed: %d\n", result);
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    i2sRole = I2S_ROLE_MIC;
    return true;
}

bool installSpeakerRole(esp_err_t *errorOut = nullptr)
{
    if (i2sRole == I2S_ROLE_SPEAKER) return true;

    if (i2sRole != I2S_ROLE_NONE) {
        i2s_driver_uninstall(I2S_PORT);
        i2sRole = I2S_ROLE_NONE;
    }

    i2s_config_t config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    esp_err_t result = i2s_driver_install(I2S_PORT, &config, 0, NULL);
    if (result != ESP_OK) {
        Serial.printf("[ERROR] Speaker driver install failed: %d\n", result);
        if (errorOut) *errorOut = result;
        return false;
    }

    i2s_pin_config_t pins = {
        .bck_io_num = SPK_BCK_PIN,
        .ws_io_num = SPK_WS_PIN,
        .data_out_num = SPK_DOUT_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    result = i2s_set_pin(I2S_PORT, &pins);
    if (result != ESP_OK) {
        Serial.printf("[ERROR] Speaker set_pin failed: %d\n", result);
        if (errorOut) *errorOut = result;
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    delay(50); // let the MAX98357A settle
    i2sRole = I2S_ROLE_SPEAKER;
    return true;
}

// ============================================================
// OLED
// ============================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    U8X8_PIN_NONE
);

SemaphoreHandle_t oledMutex;

void setDisplay(const char *text)
{
    xSemaphoreTake(oledMutex, portMAX_DELAY);

    display.clearBuffer();

    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(0, 13, "NAYAK");

    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(0, 32, text);

    display.sendBuffer();

    xSemaphoreGive(oledMutex);
}

// ============================================================
// SPEAKER RING BUFFER (filled by serialReceiverTask,
// drained by speakerTask)
// ============================================================

uint8_t ringBuffer[RING_BUFFER_SIZE];

volatile size_t writePos = 0;
volatile size_t readPos  = 0;

portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

size_t bufferAvailable()
{
    portENTER_CRITICAL(&bufferMux);

    size_t available;
    if (writePos >= readPos) {
        available = writePos - readPos;
    } else {
        available = RING_BUFFER_SIZE - readPos + writePos;
    }

    portEXIT_CRITICAL(&bufferMux);
    return available;
}

size_t bufferFree()
{
    return RING_BUFFER_SIZE - bufferAvailable() - 1;
}

size_t bufferWrite(const uint8_t* data, size_t length)
{
    size_t written = 0;

    portENTER_CRITICAL(&bufferMux);

    while (written < length &&
           ((writePos + 1) % RING_BUFFER_SIZE) != readPos) {

        ringBuffer[writePos] = data[written];
        writePos++;
        if (writePos >= RING_BUFFER_SIZE) writePos = 0;
        written++;
    }

    portEXIT_CRITICAL(&bufferMux);

    written -= (written % 2);
    return written;
}

size_t bufferRead(uint8_t* data, size_t length)
{
    size_t bytesRead = 0;

    portENTER_CRITICAL(&bufferMux);

    while (bytesRead < length && readPos != writePos) {
        data[bytesRead] = ringBuffer[readPos];
        readPos++;
        if (readPos >= RING_BUFFER_SIZE) readPos = 0;
        bytesRead++;
    }

    portEXIT_CRITICAL(&bufferMux);
    return bytesRead;
}

void clearRingBuffer()
{
    portENTER_CRITICAL(&bufferMux);
    writePos = 0;
    readPos = 0;
    portEXIT_CRITICAL(&bufferMux);
}

// ============================================================
// SPEAKER PLAYBACK TASK (core 1)
// ============================================================
//
// Only actually calls i2s_write while i2sRole == I2S_ROLE_SPEAKER.
// It waits (without touching I2S at all) whenever the port is
// currently in mic mode.

void speakerTask(void* parameter)
{
    uint8_t i2sBuffer[1024];
    bool playing = false;

    while (true) {

        if (i2sRole != I2S_ROLE_SPEAKER) {
            // Port is busy being a microphone right now.
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t available = bufferAvailable();

        if (!playing) {
            if (available >= (SAMPLE_RATE * 2 / 5)) { // ~200ms buffered
                playing = true;

                if (systemState == STATE_WAITING_REPLY) {
                    systemState = STATE_PLAYING;
                    setDisplay("Speaking...");
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
        }

        size_t bytesToRead = min(sizeof(i2sBuffer), bufferAvailable());

        if (bytesToRead == 0) {
            // Reply finished playing.
            playing = false;
            i2s_zero_dma_buffer(I2S_PORT);

            if (systemState == STATE_PLAYING) {
                installMicRole();   // hand the port back to the mic
                systemState = STATE_IDLE;
                setDisplay("Press to record");
            }

            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t bytesRead = bufferRead(i2sBuffer, bytesToRead);

        int16_t* samples = (int16_t*)i2sBuffer;
        size_t sampleCount = bytesRead / 2;
        for (size_t i = 0; i < sampleCount; i++) {
            int32_t boosted = (int32_t)(samples[i] * VOLUME_GAIN);
            if (boosted > 32767) boosted = 32767;
            if (boosted < -32768) boosted = -32768;
            samples[i] = (int16_t)boosted;
        }

        size_t bytesWritten = 0;
        i2s_write(I2S_PORT, i2sBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
    }
}

// ============================================================
// SERIAL -> RING BUFFER TASK (core 0)
// ============================================================

void serialReceiverTask(void* parameter)
{
    uint8_t buf[1024];

    while (true) {
        int available = Serial.available();

        if (available > 0) {
            size_t toRead = min((int)sizeof(buf), available);
            size_t got = Serial.readBytes((char*)buf, toRead);

            if (got > 0) {
                bufferWrite(buf, got);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

// ============================================================
// STATUS / FLOW-CONTROL TASK (core 0)
// ============================================================

void statusTask(void* parameter)
{
    while (true) {
        if (!isRecording) {
            size_t freeBytes = bufferFree();
            Serial.printf("FREE:%u\n", (unsigned)freeBytes);
        }

        vTaskDelay(pdMS_TO_TICKS(FREE_REPORT_INTERVAL_MS));
    }
}

// ============================================================
// RECORDING PROTOCOL (mic -> PC)
// ============================================================
//
// AUD0
//   CHNK <uint16 length> <audio bytes>
//   ...
// AUD1 <uint32 total_audio_bytes>
//
// isRecording gates the status task's Serial output so this stays
// a clean binary stream.

void sendChunkHeader(uint16_t length)
{
    Serial.write((const uint8_t *)"CHNK", 4);
    Serial.write((const uint8_t *)&length, sizeof(length));
}

void recordAndStream()
{
    int32_t rawSamples[MIC_I2S_BUFFER_SAMPLES];
    uint32_t totalBytes = 0;

    isRecording = true;
    systemState = STATE_RECORDING;
    setDisplay("Listening...");

    Serial.write((const uint8_t *)"AUD0", 4);
    Serial.flush();

    // Make sure the button from the START press is fully released
    // before we start checking for the STOP press below — otherwise
    // a still-held button ends the recording after a single chunk.
    while (digitalRead(BUTTON_PIN) == LOW) {
        delay(10);
    }
    delay(50);

    bool recording = true;

    while (recording) {
        size_t bytesRead = 0;

        esp_err_t result = i2s_read(
            I2S_PORT,
            rawSamples,
            sizeof(rawSamples),
            &bytesRead,
            portMAX_DELAY
        );

        if (result != ESP_OK || bytesRead == 0) {
            continue;
        }

        sendChunkHeader((uint16_t)bytesRead);
        Serial.write((const uint8_t *)rawSamples, bytesRead);
        totalBytes += bytesRead;

        if (digitalRead(BUTTON_PIN) == LOW) {
            delay(30);
            if (digitalRead(BUTTON_PIN) == LOW) {
                recording = false;
            }
        }
    }

    Serial.write((const uint8_t *)"AUD1", 4);
    Serial.write((const uint8_t *)&totalBytes, sizeof(totalBytes));
    Serial.flush();

    isRecording = false;

    // Hand the I2S port over to the speaker and get ready to
    // receive + play the reply.
    clearRingBuffer();

    systemState = STATE_WAITING_REPLY;
    setDisplay("Thinking...");

    esp_err_t speakerError = ESP_OK;
    if (!installSpeakerRole(&speakerError)) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Spk FAIL code %d", (int)speakerError);
        setDisplay(msg);
    }
}

// ============================================================
// REPEAT PROTOCOL (asks the PC to resend the last reply)
// ============================================================
//
// RPT0
//
// Just a 4-byte marker — no binary framing follows it, so unlike
// AUD0 there's no need to silence the status task around it.

void requestRepeat()
{
    systemState = STATE_WAITING_REPLY;
    setDisplay("Repeating...");

    Serial.write((const uint8_t *)"RPT0", 4);
    Serial.flush();

    clearRingBuffer();

    esp_err_t speakerError = ESP_OK;
    if (!installSpeakerRole(&speakerError)) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Spk FAIL code %d", (int)speakerError);
        setDisplay(msg);
    }
}

// ============================================================
// BUTTON HELPERS
// ============================================================

void waitForButtonRelease(int pin)
{
    while (digitalRead(pin) == LOW) {
        delay(10);
    }
    delay(50);
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(1000);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(REPEAT_BUTTON_PIN, INPUT_PULLUP);

    oledMutex = xSemaphoreCreateMutex();

    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin();
    setDisplay("Booting...");

    if (!installMicRole()) {
        setDisplay("Mic init FAILED");
        while (true) delay(1000);
    }

    xTaskCreatePinnedToCore(serialReceiverTask, "SerialRx", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(statusTask,         "Status",   2048, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(speakerTask,        "Speaker",  4096, NULL, 3, NULL, 1);

    systemState = STATE_IDLE;
    setDisplay("Press to record");
}

// ============================================================
// LOOP (button handling + blocking recording)
// ============================================================

void loop()
{
    if (systemState == STATE_IDLE && i2sRole == I2S_ROLE_MIC) {

        if (digitalRead(BUTTON_PIN) == LOW) {
            delay(50);
            if (digitalRead(BUTTON_PIN) == LOW) {
                recordAndStream();   // blocks until second button press
                waitForButtonRelease(BUTTON_PIN);
            }
        }
        else if (digitalRead(REPEAT_BUTTON_PIN) == LOW) {
            delay(50);
            if (digitalRead(REPEAT_BUTTON_PIN) == LOW) {
                requestRepeat();
                waitForButtonRelease(REPEAT_BUTTON_PIN);
            }
        }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
}