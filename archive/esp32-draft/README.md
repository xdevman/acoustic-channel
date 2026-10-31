# ESP32 draft code archive

This folder preserves the code notes transcribed in [`draft-original.txt`](draft-original.txt).
The original chat export is kept intact, including timestamps, repeated snippets,
the incomplete Python import line, and non-code photo placeholders. It is an
archive of experiments, not a comparison, recommended implementation, or
production package. The snippets are not intended to be compiled or run as one
program.

## Code inventory

| Date/time in source | Snippet | Notes |
| --- | --- | --- |
| 2026-10-23 00:16 | I2C pin-pair scanner | Tests ordered SDA/SCL pairs on GPIO 21, 22, 25, 26, 32 and 33; probes `0x3C` and `0x3D`. |
| 2026-10-23 11:18 | I2C bus scanner | Scans seven-bit addresses using SDA 25 and SCL 26. Repeated at 12:20. |
| 2026-10-23 11:25 | GPIO 25 input read | Prints the digital input value. |
| 2026-10-23 11:34 | GPIO 25/26 pull-down reads | Prints both input values. |
| 2026-10-23 11:39 | GPIO 25/26 to GPIO 27 test | Drives two outputs and samples GPIO 27 under several states. |
| 2026-10-23 12:07 | SSD1306 OLED hello | Displays “Hello ESP32” using U8g2; repeated at 12:22. |
| 2026-10-23 13:08 | I2S level measurement | Reads blocks from an I2S microphone and prints peak-to-peak sample range. |
| 2026-10-23 13:12 | Serial audio capture firmware | Responds to `R`, emits `AUD0`, then streams five seconds of mono 16 kHz PCM. |
| 2026-10-23 13:13 onward | Python serial capture/playback | The export includes an import line and its following code; it writes `record.wav` and attempts playback. |

The accompanying explanation and experiment index are in [`../../esp32/README.md`](../../esp32/README.md).

## Individual snippets

Each code occurrence from `draft.txt` is also saved separately below. Repeated
I2C scanner and OLED sketches remain separate files to preserve their original
timestamps. The explanatory Persian message and photo placeholders are not
code and remain only in the original transcript.

| Source time | File | Description |
| --- | --- | --- |
| 00:16 | [`snippets/2026-10-23_0016_i2c_pin_pair_scan.ino`](snippets/2026-10-23_0016_i2c_pin_pair_scan.ino) | I2C pin-pair scan |
| 11:18 | [`snippets/2026-10-23_1118_i2c_bus_scan.ino`](snippets/2026-10-23_1118_i2c_bus_scan.ino) | I2C bus scan |
| 11:25 | [`snippets/2026-10-23_1125_gpio25_read.ino`](snippets/2026-10-23_1125_gpio25_read.ino) | GPIO 25 input |
| 11:34 | [`snippets/2026-10-23_1134_gpio25_26_pulldown.ino`](snippets/2026-10-23_1134_gpio25_26_pulldown.ino) | GPIO 25/26 pull-down reads |
| 11:39 | [`snippets/2026-10-23_1139_gpio_loopback.ino`](snippets/2026-10-23_1139_gpio_loopback.ino) | GPIO output/input test |
| 12:07 | [`snippets/2026-10-23_1207_oled_hello.ino`](snippets/2026-10-23_1207_oled_hello.ino) | SSD1306 hello sketch |
| 12:20 | [`snippets/2026-10-23_1220_i2c_bus_scan.ino`](snippets/2026-10-23_1220_i2c_bus_scan.ino) | Repeated I2C bus scan |
| 12:22 | [`snippets/2026-10-23_1222_oled_hello.ino`](snippets/2026-10-23_1222_oled_hello.ino) | Repeated SSD1306 hello sketch |
| 13:08 | [`snippets/2026-10-23_1308_i2s_level.ino`](snippets/2026-10-23_1308_i2s_level.ino) | I2S level measurement |
| 13:12 | [`snippets/2026-10-23_1312_serial_audio_capture.ino`](snippets/2026-10-23_1312_serial_audio_capture.ino) | Serial audio capture firmware |
| 13:13 | [`snippets/2026-10-23_1313_serial_audio_capture.py`](snippets/2026-10-23_1313_serial_audio_capture.py) | Serial capture and playback utility |
