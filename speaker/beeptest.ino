#include <Arduino.h>
#include "driver/i2s.h"

#define I2S_BCLK  27
#define I2S_LRC   33
#define I2S_DIN   13

#define SAMPLE_RATE 44100
#define I2S_PORT I2S_NUM_0

void setup() {
  Serial.begin(115200);

  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_PORT, &config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
}

void beep(int frequency, int duration) {
  const int samples_per_cycle = SAMPLE_RATE / frequency;
  int16_t buffer[256];

  unsigned long samples = (SAMPLE_RATE * duration) / 1000;

  for (unsigned long i = 0; i < samples; i += 256) {
    for (int j = 0; j < 256; j++) {
      unsigned long n = i + j;

      if (n >= samples)
        buffer[j] = 0;
      else {
        // Square-wave beep
        buffer[j] =
          ((n % samples_per_cycle) < samples_per_cycle / 2)
          ? 5000
          : -5000;
      }
    }

    size_t bytesWritten;
    i2s_write(
      I2S_PORT,
      buffer,
      sizeof(buffer),
      &bytesWritten,
      portMAX_DELAY
    );
  }

  // Silence
  memset(buffer, 0, sizeof(buffer));
  size_t bytesWritten;
  i2s_write(I2S_PORT, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
}

void loop() {
  beep(1000, 300);  // 1000 Hz for 300 ms
  delay(2000);
}