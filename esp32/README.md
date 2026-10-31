# ESP32 experiments and receiver

The archived draft transcript contains notes and short sketches from bringing
up the ESP32 hardware. The snippets are experiments, not one program to upload
together.
The maintained acoustic receiver sketches are in `acoustic_rx/` and
`acoustic_rx_10/`.

The original draft export and each code occurrence are preserved in [`../archive/esp32-draft/draft-original.txt`](../archive/esp32-draft/draft-original.txt) and [`../archive/esp32-draft/snippets/`](../archive/esp32-draft/snippets/), with an inventory in [`../archive/esp32-draft/README.md`](../archive/esp32-draft/README.md). This archive is for documentation and reference; it is not a comparison or production package.

## Experiments recorded in the archived draft

| Experiment | What it checks | Notes |
| --- | --- | --- |
| I2C pin-pair scan | Tries every ordered pair from GPIO 21, 22, 25, 26, 32 and 33 as SDA/SCL, then probes addresses `0x3C` and `0x3D`. | Useful when the OLED wiring and I2C pin assignment are unknown. It repeats every three seconds. |
| I2C bus scan | Probes all seven-bit I2C addresses using SDA 25 and SCL 26. | Prints every responding address; repeat scans every two seconds. |
| GPIO 25 read | Reads GPIO 25 as a plain input. | A floating pin may fluctuate; connect it to a defined logic level through a safe circuit when testing. |
| GPIO 25/26 pull-down read | Reads GPIO 25 and 26 with internal pull-downs enabled. | Checks whether external wiring drives either pin high. |
| GPIO 25/26 to GPIO 27 loopback | Drives 25 high and 26 low, then reverses them and reads GPIO 27. It also reads GPIO 27 with pull-down and pull-up settings. | `a` and `b` should reflect the external connection to GPIO 27; `c` and `d` show the readings with the output pins released. The result depends on the jumper wiring. |
| SSD1306 OLED hello | Uses U8g2 over I2C on SDA 25/SCL 26 to show “Hello ESP32”. | Requires the U8g2 library and a compatible 128×64 SSD1306 display. |
| I2S level meter | Reads 16 kHz, 32-bit I2S samples from BCLK 32, WS 33 and data 27, then prints a block peak-to-peak measure. | This is a signal-presence check for an I2S microphone, not the acoustic packet receiver. |
| I2S serial audio capture | On serial command `R`, sends the four-byte `AUD0` marker and then five seconds of mono 16 kHz signed 16-bit PCM. | The Python capture utility is [`../rec.py`](../rec.py). The final line in the archived transcript only starts its imports; use the checked-in script rather than that incomplete fragment. |

The two I2C scanner sketches and the OLED sketch use GPIO 25/26 as their
default bus pins. Change these if the board wiring differs. GPIO tests should
be run with the board's logic voltage and pin limits in mind; do not connect a
GPIO directly to a supply voltage above its rating.

## Acoustic receiver sketches

Both receiver sketches listen for the project's 17/18/19 kHz FSK signal and
validate packets using the same preamble, length and CRC-8 format as the
Python receiver.

| Sketch | Microphone wiring | Processing approach |
| --- | --- | --- |
| [`acoustic_rx/acoustic_rx.ino`](acoustic_rx/acoustic_rx.ino) | INMP441 SCK/BCLK → GPIO 26, WS → GPIO 25, SD → GPIO 33, L/R → GND | Reads fixed 50 ms symbols and classifies them directly. |
| [`acoustic_rx_10/acoustic_rx_10.ino`](acoustic_rx_10/acoustic_rx_10.ino) | INMP441 SCK/BCLK → GPIO 32, WS → GPIO 33, SD → GPIO 27, L/R → GND | Scores short overlapping windows, then searches symbol timing and clock drift after capture. |

For either sketch, connect INMP441 VDD to 3V3 and GND to GND. Open the serial
monitor at 115200 baud. The receiver prints status and decoded text. Both
sketches emit `MSG_HEX:` lines for a host logger, and store decoded UTF-8
messages in LittleFS as `/messages.txt`. Send `d` to dump that file or `c` to
clear it. [`pc_logger.py`](../pc_logger.py) listens for `MSG_HEX:` lines and
appends decoded messages to the computer's `messages.txt`; close the Arduino
serial monitor before running it because the serial port can only be opened by
one program at a time.

`acoustic_rx_10` enables diagnostic output by default (`DEBUG_LEVELS 1`). Set
it to `0` for quieter operation. The simpler `acoustic_rx` sketch has
`DEBUG_LEVELS 0` by default.

These sketches require an ESP32 Arduino environment that provides the legacy
`driver/i2s.h` API and LittleFS. No hardware results or electrical behavior
are asserted by this document; the pin mappings and sample behavior above
describe the source code.
