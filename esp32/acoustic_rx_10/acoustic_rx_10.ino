// Acoustic air-gap receiver for ESP32 + INMP441 (I2S MEMS mic)
// 17 kHz = bit 0, 18 kHz = bit 1, 19 kHz = START tone, 50 ms per bit.
// Packet: [8 preamble bits = 1][8 bit length][length bytes payload][8 bit CRC-8 (poly 0x07)]
//
// How it decodes: after the START tone and the first 18 kHz preamble edge, the sketch records
// the 17k/18k tone balance every 5 ms for the whole packet. When the packet ends it searches for
// the start offset and clock drift that give the clearest bits, decodes, and checks the CRC.
//
// Wiring:
//   INMP441 VDD -> 3V3      INMP441 GND -> GND      INMP441 L/R -> GND
//   INMP441 SCK -> GPIO32   INMP441 WS  -> GPIO33   INMP441 SD  -> GPIO27
//
// Serial monitor @115200:  'd' = dump messages.txt (stored on the ESP32), 'c' = clear it.
// For saving on a PC use pc_logger.py (it reads the MSG_HEX: lines).

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

#define HOP_STEP         240       // tone balance measured every 5 ms...
#define HOP_WIN          480       // ...over a 10 ms window
#define HOPS_PER_SYMBOL  10
#define SYMBOL_CENTER_HOP 9        // centre of bit 0, counted in hops from the sync point
#define HEADER_HOPS      162       // hops needed to read preamble + length
#define HOP_NORM_FLOOR   0.0004f
#define SILENCE_AMP      0.0003f   // 17k+18k below this = no signal
#define SILENCE_HOPS     40        // 200 ms of silence = end of packet
#define MAX_HOPS         21000

static const float TONE_FREQ[3]   = {17000.0f, 18000.0f, 19000.0f}; // ZERO, ONE, START
static const float FREQ_OFFSETS[]  = {-100.0f, 0.0f, 100.0f};       // tolerance for clock mismatch
#define N_OFFSETS 3

#define MIN_AMP          0.0005f   // min tone amplitude for START / sync
#define DOMINANCE        2.0f      // START tone must be this much stronger than the others
#define DEBUG_LEVELS     1         // 1 = extra diagnostics, 0 = quiet

#define BUF_SIZE         8192
#define MAX_PAYLOAD      255
#define MAX_BITS         (16 + MAX_PAYLOAD * 8 + 8)
#define LOG_FILE         "/messages.txt"

// ---------------- Globals ----------------
enum Tone { T_NONE = -1, T_ZERO = 0, T_ONE = 1, T_START = 2 };
enum State { STATE_WAIT, STATE_SYNC, STATE_RECEIVE };

static float hannWin[SYMBOL_SAMPLES];
static float hannHop[HOP_WIN];
static float coeffs[3][N_OFFSETS];
static float buf[BUF_SIZE];
static int   bufLen = 0;

static State   state = STATE_WAIT;
static uint8_t bits[MAX_BITS];
static int     nbits = 0;
static int     startHits = 0;
static int     syncAttempts = 0;

static int8_t  hops[MAX_HOPS];              // per-5ms score: +127 = pure 18k (bit 1), -127 = pure 17k (bit 0)
static int     nh = 0;
static int     silentHops = 0;
static int     expectedTotal = 0;           // total bits in packet, once the header is read
static int     endHop = 0;

static float   lastA0 = 0, lastA1 = 0;
static unsigned long droppedSamples = 0;

static const int OFFS[] = {0, -1, 1, -2, 2, -3, 3, -4, 4, -5, -6};
#define N_OFFS 11

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
static float goertzelAmpN(const float* x, float coeff, const float* win, int n) {
  float s1 = 0, s2 = 0;
  for (int i = 0; i < n; i++) {
    float s = x[i] * win[i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return sqrtf(p > 0 ? p : 0) * 4.0f / n;
}

static float goertzelAmp(const float* x, float coeff) {
  return goertzelAmpN(x, coeff, hannWin, SYMBOL_SAMPLES);
}

// Used for START tone / sync edge. dominance = 1.0 behaves like argmax.
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

// 17k and 18k amplitudes over a short 10 ms window
static void hopAmps(const float* x, float& a0, float& a1) {
  a0 = 0;
  a1 = 0;
  for (int o = 0; o < N_OFFSETS; o++) {
    float v0 = goertzelAmpN(x, coeffs[0][o], hannHop, HOP_WIN);
    float v1 = goertzelAmpN(x, coeffs[1][o], hannHop, HOP_WIN);
    if (v0 > a0) a0 = v0;
    if (v1 > a1) a1 = v1;
  }
}

static void dumpBits() {
#if DEBUG_LEVELS
  Serial.printf("[dbg] last hop: 17k=%.5f 18k=%.5f, hops=%d, dropped samples=%lu\n", lastA0, lastA1, nh, droppedSamples);
  Serial.printf("[dbg] bits (%d): ", nbits);
  for (int i = 0; i < nbits; i++) {
    Serial.print(bits[i] ? '1' : '0');
    if (i % 8 == 7) Serial.print(' ');
  }
  Serial.println();
#endif
}

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

// ---------------- Offline decoding of the recorded hops ----------------
// Bit b is read from the 5 hops around its (offset/drift-corrected) centre.
static int hypBit(int b, int off, float period) {
  int c = (int)lroundf(SYMBOL_CENTER_HOP + off + period * b);
  if (c - 2 < 0 || c + 2 >= nh) return -1;
  int sum = 0;
  for (int k = c - 2; k <= c + 2; k++) sum += hops[k];
  return sum > 0 ? 1 : 0;
}

// Sum of |vote| over all bits: highest when the sampling points sit in the middle of the symbols.
static float hypMetric(int off, float delta, int totalBits) {
  float period = HOPS_PER_SYMBOL * (1.0f + delta);
  long acc = 0;
  for (int b = 0; b < totalBits; b++) {
    int c = (int)lroundf(SYMBOL_CENTER_HOP + off + period * b);
    if (c - 2 < 0 || c + 2 >= nh) return -1.0f;
    int sum = 0;
    for (int k = c - 2; k <= c + 2; k++) sum += hops[k];
    acc += sum < 0 ? -sum : sum;
  }
  return (float)acc;
}

static void bestTiming(int totalBits, bool useDrift, int& bestOff, float& bestDelta) {
  float bestM = -1.0f;
  bestOff = 0;
  bestDelta = 0.0f;
  int maxD = useDrift ? 6 : 0;                       // drift search: +/-0.3 % in 0.05 % steps
  for (int di = 0; di <= maxD; di++) {
    for (int sgn = 0; sgn < (di == 0 ? 1 : 2); sgn++) {
      float delta = (sgn ? -1.0f : 1.0f) * di * 0.0005f;
      for (int oi = 0; oi < N_OFFS; oi++) {
        float m = hypMetric(OFFS[oi], delta, totalBits);
        if (m > bestM + 0.5f) { bestM = m; bestOff = OFFS[oi]; bestDelta = delta; }
      }
    }
  }
}

// Fills bits[]/nbits. Returns total packet bits, or 0 if the preamble is wrong or data is missing.
static int decodeHyp(int off, float delta, bool headerOnly) {
  float period = HOPS_PER_SYMBOL * (1.0f + delta);
  nbits = 0;
  for (int b = 0; b < 16; b++) {
    int v = hypBit(b, off, period);
    if (v < 0) return 0;
    bits[nbits++] = v;
  }
  for (int i = 0; i < 8; i++) if (bits[i] != 1) return 0;
  uint8_t plen;
  bitsToBytes(bits + 8, 8, &plen);
  int total = 16 + plen * 8 + 8;
  if (headerOnly) return total;
  for (int b = 16; b < total; b++) {
    int v = hypBit(b, off, period);
    if (v < 0) return 0;
    bits[nbits++] = v;
  }
  return total;
}

static void finalizePacket() {
  int off = 0;
  float delta = 0.0f;
  if (expectedTotal > 0) bestTiming(expectedTotal, true, off, delta);
  int total = decodeHyp(off, delta, false);
  if (total == 0 && (off != 0 || delta != 0.0f)) total = decodeHyp(0, 0.0f, false);
  if (total == 0) {
    dumpBits();
    Serial.println("\nPacket incomplete (signal ended early or header unreadable).");
    resetToWait(true);
    return;
  }
#if DEBUG_LEVELS
  Serial.printf("[dbg] timing: offset=%d hops, drift=%+.2f%%\n", off, delta * 100.0f);
#endif
  finishPacket();
}

// ---------------- State machine ----------------
static bool stepReceive() {
  if (bufLen < HOP_WIN) return false;
  float a0, a1;
  hopAmps(buf, a0, a1);
  lastA0 = a0;
  lastA1 = a1;
  if (nh < MAX_HOPS) hops[nh++] = (int8_t)lroundf(127.0f * (a1 - a0) / fmaxf(a0 + a1, HOP_NORM_FLOOR));
  silentHops = (a0 + a1 < SILENCE_AMP) ? silentHops + 1 : 0;
  consume(HOP_STEP);

  if (expectedTotal == 0 && nh >= HEADER_HOPS) {
    int off;
    float d;
    bestTiming(16, false, off, d);
    int total = decodeHyp(off, 0.0f, true);
    if (total == 0) {
      dumpBits();
      Serial.println("\nBad preamble. Dropping packet.");
      resetToWait(true);
      return true;
    }
    expectedTotal = total;
    endHop = SYMBOL_CENTER_HOP + (int)(HOPS_PER_SYMBOL * total * 1.01f) + 20;
    if (endHop > MAX_HOPS) endHop = MAX_HOPS;
  }

  if ((expectedTotal > 0 && nh >= endHop) || silentHops >= SILENCE_HOPS || nh >= MAX_HOPS) finalizePacket();
  return true;
}

// Returns true if it did work (so the caller should call again)
static bool step() {
  if (state == STATE_RECEIVE) return stepReceive();
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

  // STATE_SYNC: slide 5 ms at a time until the 18 kHz preamble becomes the strongest tone
  Tone t = classify(buf, 1.0f);
  if (t == T_ONE) {
    Serial.println("Synchronized. Receiving packet...");
    state = STATE_RECEIVE;
    nh = 0;
    silentHops = 0;
    expectedTotal = 0;
    nbits = 0;
    // buf[0] is now ~25 ms before the first preamble symbol; hop 0 starts exactly here
  } else {
    consume(SYNC_STEP);
    if (++syncAttempts > SYNC_MAX_TRIES) {
      Serial.println("\nSync timeout: Preamble not found.");
      resetToWait(true);
    }
  }
  return true;
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Receiver running...");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed (messages will not be saved on the ESP32)");

  for (int i = 0; i < SYMBOL_SAMPLES; i++)  // same as numpy.hanning
    hannWin[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (SYMBOL_SAMPLES - 1));
  for (int i = 0; i < HOP_WIN; i++)
    hannHop[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (HOP_WIN - 1));
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
