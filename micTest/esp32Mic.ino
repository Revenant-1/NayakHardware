#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "driver/i2s.h"

// =====================================================
// PINS
// =====================================================

#define BUTTON_PIN 4

#define OLED_SDA 21
#define OLED_SCL 22

#define I2S_PORT I2S_NUM_0

#define I2S_SCK 26
#define I2S_WS  25
#define I2S_SD  32


// =====================================================
// AUDIO
// =====================================================

#define SAMPLE_RATE 16000

// We no longer have a fixed recording duration.
//
// Recording continues until the user presses the
// button a second time.

#define I2S_BUFFER_SAMPLES 256

// Number of bytes sent in one audio chunk.
#define AUDIO_CHUNK_BYTES \
    (I2S_BUFFER_SAMPLES * sizeof(int32_t))


// =====================================================
// OLED
// =====================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    U8X8_PIN_NONE
);


void showText(const char *text)
{
    display.clearBuffer();

    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(0, 13, "NAYAK");

    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(0, 32, text);

    display.sendBuffer();
}


// =====================================================
// I2S SETUP
// =====================================================

void setupMicrophone()
{
    i2s_config_t config = {};

    config.mode = (i2s_mode_t)(
        I2S_MODE_MASTER |
        I2S_MODE_RX
    );

    config.sample_rate = SAMPLE_RATE;

    config.bits_per_sample =
        I2S_BITS_PER_SAMPLE_32BIT;

    config.channel_format =
        I2S_CHANNEL_FMT_ONLY_LEFT;

    config.communication_format =
        I2S_COMM_FORMAT_I2S;

    config.intr_alloc_flags =
        ESP_INTR_FLAG_LEVEL1;

    config.dma_buf_count = 8;
    config.dma_buf_len = 256;

    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;


    esp_err_t result = i2s_driver_install(
        I2S_PORT,
        &config,
        0,
        NULL
    );

    if (result != ESP_OK)
    {
        Serial.println("ERROR: I2S driver install failed");

        while (true)
        {
            delay(1000);
        }
    }


    i2s_pin_config_t pins = {};

    pins.bck_io_num = I2S_SCK;
    pins.ws_io_num = I2S_WS;

    pins.data_out_num =
        I2S_PIN_NO_CHANGE;

    pins.data_in_num = I2S_SD;


    result = i2s_set_pin(
        I2S_PORT,
        &pins
    );

    if (result != ESP_OK)
    {
        Serial.println("ERROR: I2S pin setup failed");

        while (true)
        {
            delay(1000);
        }
    }


    i2s_zero_dma_buffer(I2S_PORT);
}


// =====================================================
// SERIAL PROTOCOL
// =====================================================
//
// Recording:
//
// AUD0
//   CHNK
//   uint16_t chunk_length
//   audio bytes
//   CHNK
//   uint16_t chunk_length
//   audio bytes
//   ...
// AUD1
//   uint32_t total_audio_bytes
//
// All integer values are little-endian.
//
// =====================================================


// =====================================================
// SEND CHUNK HEADER
// =====================================================

void sendChunkHeader(uint16_t length)
{
    Serial.write(
        (const uint8_t *)"CHNK",
        4
    );

    Serial.write(
        (const uint8_t *)&length,
        sizeof(length)
    );
}


// =====================================================
// SEND RECORDING START
// =====================================================

void sendRecordingStart()
{
    Serial.write(
        (const uint8_t *)"AUD0",
        4
    );

    Serial.flush();
}


// =====================================================
// SEND RECORDING END
// =====================================================

void sendRecordingEnd(uint32_t totalBytes)
{
    Serial.write(
        (const uint8_t *)"AUD1",
        4
    );

    Serial.write(
        (const uint8_t *)&totalBytes,
        sizeof(totalBytes)
    );

    Serial.flush();
}


// =====================================================
// RECORD AUDIO
// =====================================================
//
// This function keeps recording until the button is
// pressed again.
//
// Audio is streamed immediately to Serial.
// Nothing is stored as one giant recording in RAM.
// =====================================================

void recordRawAudio()
{
    int32_t rawSamples[I2S_BUFFER_SAMPLES];

    uint32_t totalBytes = 0;

    bool recording = true;


    // -------------------------------------------------
    // Tell Python that recording has started.
    // -------------------------------------------------

    sendRecordingStart();


    // -------------------------------------------------
    // Keep capturing until second button press.
    // -------------------------------------------------

    while (recording)
    {
        size_t bytesRead = 0;


        esp_err_t result = i2s_read(
            I2S_PORT,
            rawSamples,
            sizeof(rawSamples),
            &bytesRead,
            portMAX_DELAY
        );


        if (result != ESP_OK)
        {
            break;
        }


        if (bytesRead == 0)
        {
            continue;
        }


        // -------------------------------------------------
        // Send this audio chunk immediately.
        // -------------------------------------------------

        sendChunkHeader(
            (uint16_t)bytesRead
        );


        Serial.write(
            (const uint8_t *)rawSamples,
            bytesRead
        );


        totalBytes += bytesRead;


        // -------------------------------------------------
        // Check whether the user pressed the button
        // again.
        // -------------------------------------------------

        if (digitalRead(BUTTON_PIN) == LOW)
        {
            delay(30);


            if (digitalRead(BUTTON_PIN) == LOW)
            {
                recording = false;
            }
        }
    }


    // -------------------------------------------------
    // Finish the recording packet.
    // -------------------------------------------------

    sendRecordingEnd(totalBytes);
}


// =====================================================
// WAIT FOR BUTTON RELEASE
// =====================================================

void waitForButtonRelease()
{
    while (digitalRead(BUTTON_PIN) == LOW)
    {
        delay(10);
    }

    delay(50);
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
    // High baud rate is required for raw 32-bit audio.
    Serial.begin(921600);


    pinMode(
        BUTTON_PIN,
        INPUT_PULLUP
    );


    delay(1000);


    Wire.begin(
        OLED_SDA,
        OLED_SCL
    );

    display.begin();


    setupMicrophone();


    showText(
        "Press to record"
    );


    Serial.println();
    Serial.println("==============================");
    Serial.println(" NAYAK INMP441 RECORDER");
    Serial.println("==============================");
    Serial.println("Board: DOIT ESP32 DEVKIT V1");
    Serial.println("Mic: INMP441");
    Serial.println("Sample rate: 16000 Hz");
    Serial.println("Format: RAW INT32");
    Serial.println("Recording: BUTTON CONTROLLED");
    Serial.println("==============================");
    Serial.println();
    Serial.println("Press button to START.");
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
    // -------------------------------------------------
    // IDLE
    // -------------------------------------------------

    if (digitalRead(BUTTON_PIN) == LOW)
    {
        delay(50);


        // Confirm actual button press.
        if (digitalRead(BUTTON_PIN) == LOW)
        {
            // -------------------------------------------------
            // START RECORDING
            // -------------------------------------------------

            showText(
                "Listening..."
            );


            delay(100);


            recordRawAudio();


            // -------------------------------------------------
            // RECORDING FINISHED
            // -------------------------------------------------

            showText(
                "Audio sent"
            );


            // Make sure the button is released before
            // returning to idle.
            waitForButtonRelease();


            delay(500);


            showText(
                "Press to record"
            );


            Serial.println();
            Serial.println(
                "Ready for next recording."
            );
        }
    }
}
