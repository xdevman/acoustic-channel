# Acoustic Channel — Data Exfiltration from Air-Gapped Systems

An educational **IoT security** proof of concept: two laptops with **no
network connection of any kind** between them exchange text using an
**inaudible acoustic covert channel**.

The sender encodes a message into FSK tones at **17 / 18 / 19 kHz**, above the
hearing range of most adults, and plays them through the laptop speaker. A
second laptop captures the sound with its microphone, recovers the bits with an
FFT, validates the packet and writes the decoded text to a log file.

> ⚠️ **Educational use only.** This is a course research project, tested on
> hardware owned by the team in a closed lab environment. It is not an attack
> tool and targets no real system.

📊 **Presentation:** [Google Slides](https://docs.google.com/presentation/d/131VE1lK7hx_K0YXu_2LzyrKLvbZ2a8Gl0gEQ73fd6Iw) <!-- markdownlint-disable-line MD013 -->

---

## How it works

```text
Sender laptop                      Acoustic channel                 Receiver laptop
─────────────                      ────────────────                 ───────────────
text ──► UTF-8 bytes ──► framing ──► FSK 17/18/19 kHz ──► speaker ──► mic ──► FFT peak
         (preamble · length · CRC-8)                                    detect ──► bits
                                                                        ──► CRC check
                                                                        ──► messages.txt
```

The **only** path between the two machines is sound. There is no cable, no
Wi-Fi, no Bluetooth.

### Physical layer

| Signal | Frequency | Duration | Meaning |
| --- | --- | --- | --- |
| START | 19,000 Hz | 250 ms | Wakes the receiver up |
| Bit 1 | 18,000 Hz | 50 ms | Binary `1` |
| Bit 0 | 17,000 Hz | 50 ms | Binary `0` |

- Sample rate: **48,000 Hz**
- Raw bit rate: **20 bps** (50 ms per symbol)
- Tone generation is **phase-continuous**, so there are no clicks at symbol
  boundaries
- The receiver classifies symbols by the **relative** FFT peak (±800 Hz
  tolerance) in the 14–20 kHz band, which makes it independent of playback
  volume

### Packet format

```text
┌───────────┬──────────────┬──────────────┬────────────────┬──────────┐
│  START    │  PREAMBLE    │   LENGTH     │    PAYLOAD     │  CRC-8   │
│ 250 ms    │  8 × 1 bits  │    8 bits    │   N × 8 bits   │  8 bits  │
│ 19 kHz    │  alignment   │  0–255 bytes │  UTF-8 (+ opt. │ checksum │
│           │              │              │  Fernet)       │  (0x07)  │
└───────────┴──────────────┴──────────────┴────────────────┴──────────┘
```

Framing overhead is 24 bits per packet. The receiver requires **4 consecutive
START detections** before trusting a frame, and aborts a stalled packet after a
**500 ms sync timeout**.

### Receiver state machine

`WAIT` → `SYNC` (slides forward in 5 ms steps to find the preamble edge) →
`RECEIVE` (reads one symbol at a time, verifies preamble, length and CRC, then
decodes UTF-8).

---

## Files

| File | Description |
| --- | --- |
| `sender.py` | Encodes text, builds the packet and plays the FSK audio through the speaker |
| `receiver.py` | Captures audio, demodulates with FFT, validates the packet and logs to `messages.txt` |
| `decryptor.py` | Small demo of optional **Fernet** (AES-128-CBC + HMAC-SHA256) payload encryption |
| `packages.txt` | Pinned Python dependencies |
| `messages.txt` | Timestamped log of successfully decoded messages |
| `archive/` | Local-only archive (slide sources). **Not committed** — see `.gitignore` |

---

## Requirements

- Python 3.12
- Working speaker (sender) and microphone (receiver)
- Linux, macOS or Windows with a usable audio backend

### Install

```bash
python3 -m venv .venv
source .venv/bin/activate        # Windows: .venv\Scripts\activate
pip install -r packages.txt
```

---

## Usage

Run the receiver first, on the second laptop:

```bash
python3 receiver.py
```

It prints `Waiting for signal...` and blocks, listening continuously.

Then, on the sending laptop:

```bash
python3 sender.py "Hello"
```

The message is modulated, transmitted, captured by the receiver, verified and
appended to `messages.txt`:

```text
[2026-10-17 18:18:48]
Hello
```

### A typical transmission

`"HELLO"` = 5 bytes = 40 payload bits + 24 framing bits + a 250 ms START tone
→ about **3.45 s** end to end, roughly **11.6 bps** of useful payload.

### Optional payload encryption

`decryptor.py` shows how a message can be encrypted before modulation so that
only a holder of the shared key can read it:

```python
from cryptography.fernet import Fernet

key = b'<shared out-of-band>'      # demo: hardcoded — NOT production-safe
cipher = Fernet(key)

token = cipher.encrypt(b'hello')   # base64 ciphertext → acoustic link
cipher.decrypt(token)              # b'hello'
```

Encryption protects **confidentiality** — it does not hide the fact that a
transmission is taking place.

---

## Software stack

| Package | Version | Role |
| --- | --- | --- |
| Python | 3.12 | Runtime for sender and receiver |
| numpy | 2.5.1 | Tone generation, buffering |
| sounddevice | 0.5.5 | Audio playback and capture |
| scipy | 1.18.0 | `rfft` spectral analysis |
| cryptography | 49.0.0 | Fernet payload encryption |

---

## Measured results

| Parameter | Value |
| --- | --- |
| Symbol duration | 50 ms |
| Raw bit rate | 20 bps |
| Frequency separation | 1,000 Hz |
| Detection tolerance | ±800 Hz |
| Analysis band | 14–20 kHz |
| Framing overhead | 24 bits |
| End-to-end `"HELLO"` | ~3.45 s |
| Effective goodput | ~11.6 bps |
| Decodes logged | 4 / 4 |

---

## Limitations

- CRC-8 **detects** errors but cannot correct them — a bad packet is dropped
  whole
- Low effective throughput; framing overhead is significant for short messages
- Assumes a fixed 50 ms symbol clock (no adaptive timing recovery)
- Requires the code pre-installed on both machines — not self-propagating
- Reverberant rooms degrade FFT peak detection; range, BER and noise tolerance
  are not yet formally measured
- Tested laptop-to-laptop only, never in a real air-gapped facility

## Future work

- Reed–Solomon forward error correction
- Adaptive symbol timing
- SNR / BER curves across distance and noise levels
- Power-line channel (INA219 + MOSFET load modulation)
- Multi-level FSK or OFDM, and a bidirectional link

---

## References

1. Air-gap malware — <https://en.wikipedia.org/wiki/Air-gap_malware>
2. PowerHammer — Guri et al., Ben-Gurion University (2018) —
   <https://arxiv.org/abs/1804.04014>
3. MOSQUITO — Guri et al., Ben-Gurion University (2018) —
   <https://thehackernews.com/2018/03/air-gap-computer-hacking.html>
4. DiskFiltration — Guri et al. (2016) — <https://arxiv.org/abs/1608.03431>
5. TrojPix — Shandong University (2026) — <https://thehackernews.com/2026/07/new-trojpix-attack-leaks-data-from-air.html> <!-- markdownlint-disable-line MD013 -->
6. `scipy.fft.rfft` —
   <https://docs.scipy.org/doc/scipy/reference/generated/scipy.fft.rfft.html>
7. `python-sounddevice` — <https://python-sounddevice.readthedocs.io>
8. `cryptography` (Fernet) — <https://cryptography.io/en/latest/fernet/>

---

## License & ethics

Released for educational and research purposes. All testing was conducted in a
controlled lab environment on hardware owned by the team, in accordance with
course ethics guidelines.
