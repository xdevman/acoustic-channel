#include <driver/i2s.h>

#define RATE 16000
#define SECONDS 5

void setup() {
  Serial.begin(921600);
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = 32;
  pins.ws_io_num = 33;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = 27;
  i2s_set_pin(I2S_NUM_0, &pins);
}

void loop() {
  if (Serial.available() && Serial.read() == 'R') {
    Serial.write((const uint8_t*)"AUD0", 4);
    int32_t buf[256];
    int16_t out[256];
    size_t n;
    long total = (long)RATE * SECONDS;
    while (total > 0) {
      i2s_read(I2S_NUM_0, buf, sizeof(buf), &n, portMAX_DELAY);
      int cnt = n / 4;
      if (cnt > total) cnt = total;
      for (int i = 0; i < cnt; i++) {
        int32_t s = buf[i] >> 14;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
      }
      Serial.write((uint8_t*)out, cnt * 2);
      total -= cnt;
    }
  }
}
