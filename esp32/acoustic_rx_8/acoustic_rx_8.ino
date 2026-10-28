// Acoustic air-gap receiver for ESP32 + INMP441 (I2S MEMS mic)
// Port of the Python receiver: 17 kHz = bit 0, 18 kHz = bit 1, 19 kHz = START tone.
// Packet: [8 preamble bits = 1][8 bit length][length bytes payload][8 bit CRC-8 (poly 0x07)]
// Each bit = 50 ms.
//
// Wiring (change pins below if you like):
//   INMP441 VDD -> 3V3      INMP441 GND -> GND
//   INMP441 L/R -> GND      (left channel)
//   INMP441 SCK -> GPIO32   INMP441 WS  -> GPIO33   INMP441 SD -> GPIO27
//
// Serial monitor @115200:  'd' = dump messages.txt, 'c' = clear it.

#include <Arduino.h>
#include <LittleFS.h>
#include <driver/i2s.h>
#include <math.h>

// ---------------- Configuration ----------------
#define I2S_BCLK_PIN   32
#define I2S_WS_PIN     33
#define I2S_DIN_PIN    27

#define SAMPLE_RATE      48000
#define SYMBOL_SAMPLES   2400      // 50 ms
#define SYNC_STEP        240       // 5 ms
#define SYNC_MAX_TRIES   100       // 500 ms

static const float TONE_FREQ[3]   = {17000.0f, 18000.0f, 19000.0f}; // ZERO, ONE, START
static const float FREQ_OFFSETS[]  = {-100.0f, 0.0f, 100.0f};       // tolerance for clock mismatch
#define N_OFFSETS 3

#define MIN_AMP          0.0005f    // min tone amplitude (fraction of full scale). TUNE with DEBUG_LEVELS
#define MIN_AMP_BIT      0.0001f   // floor for data bits (idle noise is ~0.00001)
#define DOMINANCE        2.0f      // best tone must be this many times stronger than the others
#define DEBUG_LEVELS     1         // 1 = print tone amplitudes 4x/second (for tuning MIN_AMP)

#define BUF_SIZE         8192
#define MAX_PAYLOAD      255
#define MAX_BITS         (16 + MAX_PAYLOAD * 8 + 8)
#define LOG_FILE         "/messages.txt"

// ---------------- Globals ----------------
enum Tone { T_NONE = -1, T_ZERO = 0, T_ONE = 1, T_START = 2 };
enum State { STATE_WAIT, STATE_SYNC, STATE_RECEIVE };

static float hannWin[SYMBOL_SAMPLES];
static float coeffs[3][N_OFFSETS];
static float buf[BUF_SIZE];
static int   bufLen = 0;

static State   state = STATE_WAIT;
static uint8_t bits[MAX_BITS];
static int     nbits = 0;
static int     expectedBits = 0;
static int     startHits = 0;
static int     syncAttempts = 0;
static float   lastA0 = 0, lastA1 = 0;      // last data-window amplitudes (for debugging)
static unsigned long droppedSamples = 0;    // samples discarded because processing fell behind

// ---------------- Helpers ----------------
static uint8_t crc8(const uint8_t* data, int n) {
  uint8_t crc = 0;
  for (int i = 0; i < n; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

static void bitsToBytes(const uint8_t* b, int count, uint8_t* out) {
  for (int i = 0; i < count / 8; i++) {
    uint8_t v = 0;
    for (int k = 0; k < 8; k++) v = (v << 1) | b[i * 8 + k];
    out[i] = v;
  }
}

static bool validUtf8(const uint8_t* s, int n) {
  int i = 0;
  while (i < n) {
    uint8_t c = s[i];
    int k;
    if (c < 0x80) k = 0;
    else if ((c & 0xE0) == 0xC0) k = 1;
    else if ((c & 0xF0) == 0xE0) k = 2;
    else if ((c & 0xF8) == 0xF0) k = 3;
    else return false;
    if (k && i + k >= n) return false;
    for (int j = 1; j <= k; j++) if ((s[i + j] & 0xC0) != 0x80) return false;
    i += k + 1;
  }
  return true;
}

static void consume(int n) {
  if (n >= bufLen) { bufLen = 0; return; }
  memmove(buf, buf + n, (bufLen - n) * sizeof(float));
  bufLen -= n;
}

// Goertzel with Hann window -> approximate sine amplitude (full scale = 1.0)
static float goertzelAmp(const float* x, float coeff) {
  float s1 = 0, s2 = 0;
  for (int i = 0; i < SYMBOL_SAMPLES; i++) {
    float s = x[i] * hannWin[i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return sqrtf(p > 0 ? p : 0) * 4.0f / SYMBOL_SAMPLES;
}

// dominance = 1.0 behaves like the Python argmax (used for edge sync)
static Tone classify(const float* x, float dominance) {
  float amp[3] = {0, 0, 0};
  for (int t = 0; t < 3; t++)
    for (int o = 0; o < N_OFFSETS; o++) {
      float a = goertzelAmp(x, coeffs[t][o]);
      if (a > amp[t]) amp[t] = a;
    }
  int best = 0;
  for (int t = 1; t < 3; t++) if (amp[t] > amp[best]) best = t;
  float other = 0;
  for (int t = 0; t < 3; t++) if (t != best && amp[t] > other) other = amp[t];

#if DEBUG_LEVELS
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 250) {
    lastPrint = millis();
    float acc = 0;
    for (int i = 0; i < SYMBOL_SAMPLES; i++) acc += x[i] * x[i];
    Serial.printf("[dbg] rms=%.5f  17k=%.5f 18k=%.5f 19k=%.5f\n", sqrtf(acc / SYMBOL_SAMPLES), amp[0], amp[1], amp[2]);
  }
#endif

  if (amp[best] < MIN_AMP) return T_NONE;
  if (amp[best] < dominance * other) return T_NONE;
  return (Tone)best;
}

// Data bits: just compare 17 kHz vs 18 kHz (robust to small timing misalignment)
static Tone classifyBit(const float* x) {
  float a0 = 0, a1 = 0;
  for (int o = 0; o < N_OFFSETS; o++) {
    float v0 = goertzelAmp(x, coeffs[0][o]);
    float v1 = goertzelAmp(x, coeffs[1][o]);
    if (v0 > a0) a0 = v0;
    if (v1 > a1) a1 = v1;
  }
  lastA0 = a0;
  lastA1 = a1;
  if (a0 < MIN_AMP_BIT && a1 < MIN_AMP_BIT) return T_NONE;
  return (a1 > a0) ? T_ONE : T_ZERO;
}

#if DEBUG_LEVELS
static void dumpBits() {
  Serial.printf("[dbg] last window: 17k=%.5f 18k=%.5f, dropped samples=%lu\n", lastA0, lastA1, droppedSamples);
  Serial.printf("[dbg] bits so far (%d): ", nbits);
  for (int i = 0; i < nbits; i++) {
    Serial.print(bits[i] ? '1' : '0');
    if (i % 8 == 7) Serial.print(' ');
  }
  Serial.println();
}
#else
static void dumpBits() {}
#endif

static void pullAudio() {
  static int32_t raw[256];
  size_t bytes = 0;
  while (true) {
    i2s_read(I2S_NUM_0, raw, sizeof(raw), &bytes, 0);
    if (bytes == 0) break;
    int frames = bytes / 8;                                     // interleaved L,R 32-bit words
    if (bufLen + frames > BUF_SIZE) {                           // drop oldest if we fall behind
      droppedSamples += bufLen + frames - BUF_SIZE;
      consume(bufLen + frames - BUF_SIZE);
    }
    long eL = 0, eR = 0;
    for (int f = 0; f < frames; f++) {
      long l = raw[2 * f] >> 8, r = raw[2 * f + 1] >> 8;
      eL += l < 0 ? -l : l;
      eR += r < 0 ? -r : r;
    }
    int ch = (eR > eL) ? 1 : 0;                                 // whichever slot the mic is on
    for (int f = 0; f < frames; f++) buf[bufLen++] = (float)(raw[2 * f + ch] >> 8) / 8388608.0f;  // 24-bit -> +/-1.0
  }
}

static void resetToWait(bool announce = false) {
  if (announce) Serial.println("Waiting for signal...\n");
  state = STATE_WAIT;
  startHits = 0;
}

static void timestamp(char* out, size_t sz) {
  uint32_t s = millis() / 1000;
  snprintf(out, sz, "[uptime %02u:%02u:%02u]", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
}

static void finishPacket() {
  dumpBits();
  int len = (nbits - 16 - 8) / 8;   // payload length in BYTES
  static uint8_t payload[MAX_PAYLOAD + 1];
  bitsToBytes(bits + 16, len * 8, payload);
  uint8_t rxCrc;
  bitsToBytes(bits + nbits - 8, 8, &rxCrc);
  uint8_t calc = crc8(payload, len);

  if (rxCrc == calc) {
    Serial.println("CRC OK\n");
    // Machine-readable line for the PC logger (hex avoids newline/encoding problems)
    Serial.print("MSG_HEX:");
    for (int i = 0; i < len; i++) Serial.printf("%02x", payload[i]);
    Serial.println();
    if (validUtf8(payload, len)) {
      payload[len] = 0;
      char ts[32];
      timestamp(ts, sizeof(ts));
      Serial.println("Message:");
      Serial.println((char*)payload);
      Serial.println();
      File f = LittleFS.open(LOG_FILE, "a");
      if (f) { f.printf("%s\n%s\n\n", ts, (char*)payload); f.close(); }
    } else {
      Serial.println("Decoded string is not valid UTF-8.\n");
    }
  } else {
    Serial.printf("CRC FAILED. Expected %u, got %u\n\n", calc, rxCrc);
  }
  resetToWait(true);
}

// Returns true if it did work (so the caller should call again)
static bool step() {
  if (bufLen < SYMBOL_SAMPLES) return false;

  if (state == STATE_WAIT) {
    Tone t = classify(buf, DOMINANCE);
    if (t == T_START) {
      if (++startHits >= 4) {                 // 4 hits x 25 ms = 100 ms debounce
        Serial.println("START detected");
        state = STATE_SYNC;
        startHits = 0;
        syncAttempts = 0;
      }
    } else {
      startHits = 0;
    }
    consume(SYMBOL_SAMPLES / 2);
    return true;
  }

  if (state == STATE_SYNC) {
    Tone t = classify(buf, 1.0f);             // plain argmax: crossover happens exactly at the edge
    if (t == T_ONE) {
      Serial.println("Synchronized. Receiving packet...");
      state = STATE_RECEIVE;
      nbits = 0;
      expectedBits = 16;
      consume(SYMBOL_SAMPLES / 2);            // window start now sits on the first preamble symbol
    } else {
      consume(SYNC_STEP);
      if (++syncAttempts > SYNC_MAX_TRIES) {
        Serial.println("\nSync timeout: Preamble not found.");
        resetToWait();
      }
    }
    return true;
  }

  // STATE_RECEIVE
  Tone t = classifyBit(buf);
  consume(SYMBOL_SAMPLES);
  if (t == T_ONE) bits[nbits++] = 1;
  else if (t == T_ZERO) bits[nbits++] = 0;
  else {
    dumpBits();
    Serial.println("\nSignal lost or corrupted");
    resetToWait();
    return true;
  }

  if (nbits == 8) {
    for (int i = 0; i < 8; i++) if (bits[i] != 1) {
      Serial.println("\nBad preamble. Dropping packet.");
      resetToWait();
      return true;
    }
  } else if (nbits == 16) {
    uint8_t plen;
    bitsToBytes(bits + 8, 8, &plen);
    expectedBits = 16 + plen * 8 + 8;
  } else if (nbits == expectedBits) {
    finishPacket();
  }
  return true;
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Receiver running...");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed (messages will not be saved)");

  for (int i = 0; i < SYMBOL_SAMPLES; i++)  // same as numpy.hanning
    hannWin[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (SYMBOL_SAMPLES - 1));
  for (int t = 0; t < 3; t++)
    for (int o = 0; o < N_OFFSETS; o++)
      coeffs[t][o] = 2.0f * cosf(2.0f * PI * (TONE_FREQ[t] + FREQ_OFFSETS[o]) / SAMPLE_RATE);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;     // read both slots, pick the one with signal
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 16;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK_PIN;
  pins.ws_io_num = I2S_WS_PIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = I2S_DIN_PIN;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);

#if DEBUG_LEVELS
  delay(300);
  {
    static int32_t t[256];
    size_t b = 0;
    i2s_read(I2S_NUM_0, t, sizeof(t), &b, pdMS_TO_TICKS(200));
    long mL = 0, mR = 0;
    for (int i = 0; i + 1 < (int)(b / 4); i += 2) {
      long l = t[i] >> 8, r = t[i + 1] >> 8;
      l = l < 0 ? -l : l;
      r = r < 0 ? -r : r;
      if (l > mL) mL = l;
      if (r > mR) mR = r;
    }
    Serial.printf("[dbg] I2S raw check: bytes=%u  left max=%ld  right max=%ld\n", (unsigned)b, mL, mR);
  }
#endif

  Serial.println("Waiting for signal...\n");
}

void loop() {
  pullAudio();
  while (step()) { }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'd') {
      File f = LittleFS.open(LOG_FILE, "r");
      if (!f) Serial.println("(no messages yet)");
      else { while (f.available()) Serial.write(f.read()); f.close(); }
    } else if (c == 'c') {
      LittleFS.remove(LOG_FILE);
      Serial.println("Log cleared.");
    }
  }
  delay(1);
}
