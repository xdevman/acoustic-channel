#include <driver/i2s.h>

void setup() {
  Serial.begin(9600);
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = 16000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;
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
  int32_t buf[256];
  size_t n;
  i2s_read(I2S_NUM_0, buf, sizeof(buf), &n, portMAX_DELAY);
  int32_t mx = -2147483647, mn = 2147483647;
  for (int i = 0; i < n / 4; i++) {
    int32_t s = buf[i] >> 14;
    if (s > mx) mx = s;
    if (s < mn) mn = s;
  }
  Serial.println(mx - mn);
}
