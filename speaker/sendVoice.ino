#include <Arduino.h>
#include "driver/i2s.h"

// ============================================================
// CONFIGURATION
// ============================================================
0
#define I2S_PORT I2S_NUM_0

#define BCLK_PIN 13
#define LRC_PIN  33
#define DIN_PIN  27

#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16

// Serial connection to laptop
#define SERIAL_BAUD 921600

// Ring buffer size in bytes
// 64 KB = about 2 seconds of 16kHz/16-bit/mono audio
#define RING_BUFFER_SIZE 65536

// How often to report free buffer space back to the sender (ms)
#define FREE_REPORT_INTERVAL_MS 20

// ============================================================
// RING BUFFER
// ============================================================

uint8_t ringBuffer[RING_BUFFER_SIZE];

volatile size_t writePos = 0;
volatile size_t readPos  = 0;

portMUX_TYPE bufferMux = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
// BUFFER FUNCTIONS
// ============================================================

size_t bufferAvailable() {

    portENTER_CRITICAL(&bufferMux);

    size_t available;

    if (writePos >= readPos) {
        available = writePos - readPos;
    } else {
        available =
            RING_BUFFER_SIZE -
            readPos +
            writePos;
    }

    portEXIT_CRITICAL(&bufferMux);

    return available;
}


size_t bufferFree() {

    return RING_BUFFER_SIZE -
           bufferAvailable() -
           1;
}


// Writes up to `length` bytes into the ring buffer.
// IMPORTANT: always returns an EVEN number of bytes written,
// so a 16-bit sample is never split across a full buffer /
// partial write boundary. Any odd trailing byte is left
// unwritten (the caller should re-send it, or in practice
// this should rarely trigger once flow control is in place).
size_t bufferWrite(
    const uint8_t* data,
    size_t length
) {

    size_t written = 0;

    portENTER_CRITICAL(&bufferMux);

    while (
        written < length &&
        ((writePos + 1) % RING_BUFFER_SIZE) != readPos
    ) {

        ringBuffer[writePos] =
            data[written];

        writePos++;

        if (writePos >= RING_BUFFER_SIZE) {
            writePos = 0;
        }

        written++;
    }

    portEXIT_CRITICAL(&bufferMux);

    // Never leave a half-written 16-bit sample in the buffer.
    written -= (written % 2);

    return written;
}


size_t bufferRead(
    uint8_t* data,
    size_t length
) {

    size_t read = 0;

    portENTER_CRITICAL(&bufferMux);

    while (
        read < length &&
        readPos != writePos
    ) {

        data[read] =
            ringBuffer[readPos];

        readPos++;

        if (readPos >= RING_BUFFER_SIZE) {
            readPos = 0;
        }

        read++;
    }

    portEXIT_CRITICAL(&bufferMux);

    return read;
}

// ============================================================
// I2S INITIALIZATION
// ============================================================

bool audioInit() {

    Serial.println();
    Serial.println("[AUDIO] Initializing I2S...");

    i2s_config_t config = {

        .mode =
            (i2s_mode_t)(
                I2S_MODE_MASTER |
                I2S_MODE_TX
            ),

        .sample_rate =
            SAMPLE_RATE,

        .bits_per_sample =
            I2S_BITS_PER_SAMPLE_16BIT,

        .channel_format =
            I2S_CHANNEL_FMT_ONLY_LEFT,

        .communication_format =
            I2S_COMM_FORMAT_STAND_I2S,

        .intr_alloc_flags = 0,

        .dma_buf_count = 8,

        .dma_buf_len = 256,

        .use_apll = false,

        .tx_desc_auto_clear = true,

        .fixed_mclk = 0
    };


    i2s_pin_config_t pins = {

        .bck_io_num =
            BCLK_PIN,

        .ws_io_num =
            LRC_PIN,

        .data_out_num =
            DIN_PIN,

        .data_in_num =
            I2S_PIN_NO_CHANGE
    };


    esp_err_t result;


    // Install I2S driver
    result =
        i2s_driver_install(
            I2S_PORT,
            &config,
            0,
            NULL
        );


    if (result != ESP_OK) {

        Serial.printf(
            "[ERROR] I2S driver failed: %d\n",
            result
        );

        return false;
    }


    // Configure pins
    result =
        i2s_set_pin(
            I2S_PORT,
            &pins
        );


    if (result != ESP_OK) {

        Serial.printf(
            "[ERROR] I2S pins failed: %d\n",
            result
        );

        return false;
    }


    // Clear DMA memory
    i2s_zero_dma_buffer(
        I2S_PORT
    );


    Serial.println(
        "[AUDIO] I2S initialized"
    );


    // Give MAX98357A time to stabilize
    delay(100);


    Serial.println(
        "[AUDIO] MAX amplifier stabilized"
    );


    return true;
}

// ============================================================
// I2S PLAYBACK TASK
// ============================================================

void audioTask(
    void* parameter
) {

    uint8_t i2sBuffer[1024];

    bool playing = false;

    Serial.println(
        "[AUDIO] Playback task started"
    );


    while (true) {

        size_t available =
            bufferAvailable();


        // ----------------------------------------------------
        // Wait until we have enough audio buffered
        // ----------------------------------------------------

        if (!playing) {

            // Start after ~200 ms of audio
            if (
                available >=
                (SAMPLE_RATE * 2 / 5)
            ) {

                playing = true;

                Serial.println(
                    "[AUDIO] Playback started"
                );
            }
            else {

                vTaskDelay(
                    pdMS_TO_TICKS(5)
                );

                continue;
            }
        }


        // ----------------------------------------------------
        // Read PCM data from ring buffer
        // ----------------------------------------------------

        size_t bytesToRead =
            min(
                sizeof(i2sBuffer),
                bufferAvailable()
            );


        if (bytesToRead == 0) {

            // Buffer underrun
            playing = false;

            i2s_zero_dma_buffer(
                I2S_PORT
            );

            Serial.println(
                "[WARNING] Buffer underrun"
            );

            vTaskDelay(
                pdMS_TO_TICKS(10)
            );

            continue;
        }


        size_t bytesRead =
            bufferRead(
                i2sBuffer,
                bytesToRead
            );


        // ----------------------------------------------------
        // Volume reduction
        // ----------------------------------------------------

        int16_t* samples =
            (int16_t*)i2sBuffer;


        size_t sampleCount =
            bytesRead / 2;


        for (
            size_t i = 0;
            i < sampleCount;
            i++
        ) {

            // 75% volume
            samples[i] =
                samples[i] * 0.75f;
        }


        // ----------------------------------------------------
        // Send to MAX98357A
        // ----------------------------------------------------

        size_t bytesWritten = 0;


        i2s_write(
            I2S_PORT,
            i2sBuffer,
            bytesRead,
            &bytesWritten,
            portMAX_DELAY
        );
    }
}

// ============================================================
// SERIAL RECEIVER TASK
// ============================================================

void serialTask(
    void* parameter
) {

    uint8_t serialBuffer[1024];


    Serial.println(
        "[SERIAL] Receiver started"
    );


    while (true) {

        int available =
            Serial.available();


        if (available > 0) {

            size_t bytesToRead =
                min(
                    (int)sizeof(serialBuffer),
                    available
                );


            size_t bytesRead =
                Serial.readBytes(
                    (char*)serialBuffer,
                    bytesToRead
                );


            if (bytesRead > 0) {

                size_t written =
                    bufferWrite(
                        serialBuffer,
                        bytesRead
                    );


                // If buffer becomes full,
                // discard excess data rather than
                // corrupting existing audio.
                // With flow control on the PC side
                // (see stream_audio.py) this should
                // rarely, if ever, trigger.
                if (
                    written <
                    bytesRead
                ) {

                    Serial.println(
                        "[WARNING] Ring buffer full"
                    );
                }
            }
        }
        else {

            vTaskDelay(
                pdMS_TO_TICKS(1)
            );
        }
    }
}

// ============================================================
// STATUS / FLOW-CONTROL TASK
// ============================================================
//
// Periodically reports free buffer space as a machine-readable
// line: "FREE:<bytes>\n". The PC-side script reads this to pace
// how much audio it sends, so the ring buffer never overflows
// (see stream_audio.py).

void statusTask(
    void* parameter
) {

    while (true) {

        size_t free_bytes =
            bufferFree();

        Serial.printf(
            "FREE:%u\n",
            (unsigned)free_bytes
        );

        vTaskDelay(
            pdMS_TO_TICKS(FREE_REPORT_INTERVAL_MS)
        );
    }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(
        SERIAL_BAUD
    );


    delay(1000);


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        " ESP32 AUDIO PLAYER"
    );

    Serial.println(
        " MAX98357A"
    );

    Serial.println(
        " 16-bit / 16kHz / Mono"
    );

    Serial.println(
        "================================"
    );


    // Initialize audio
    if (!audioInit()) {

        Serial.println(
            "[FATAL] Audio initialization failed"
        );

        while (true) {
            delay(1000);
        }
    }


    // Start serial receiver
    xTaskCreatePinnedToCore(
        serialTask,
        "SerialReceiver",
        4096,
        NULL,
        2,
        NULL,
        0
    );


    // Start audio playback
    xTaskCreatePinnedToCore(
        audioTask,
        "AudioPlayback",
        4096,
        NULL,
        3,
        NULL,
        1
    );


    // Start status / flow-control monitor
    xTaskCreatePinnedToCore(
        statusTask,
        "Status",
        2048,
        NULL,
        1,
        NULL,
        0
    );


    Serial.println(
        "[SYSTEM] READY"
    );

    Serial.println(
        "[SYSTEM] Waiting for PCM audio..."
    );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

    // Everything runs in FreeRTOS tasks.

    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );
}
