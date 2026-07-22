import sys
import time
import numpy as np
import sounddevice as sd
from cryptography.fernet import Fernet
# --- CONFIGURABLE CONSTANTS ---
SAMPLE_RATE = 48000
F_ZERO = 17000       # Frequency for binary 0
F_ONE = 18000        # Frequency for binary 1
F_START = 19000      # Frequency for START signal
SYMBOL_DURATION = 0.05  # 50 ms per bit
START_DURATION = 0.25   # 250 ms start tone

def crc8(data_bytes):
    """Calculates a simple CRC-8 checksum."""
    crc = 0x00
    for b in data_bytes:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ 0x07
            else:
                crc = crc << 1
            crc &= 0xFF
    return crc

def bytes_to_bits(data_bytes):
    """Converts a list of bytes into a list of bits (MSB first)."""
    bits = []
    for b in data_bytes:
        for i in range(7, -1, -1):
            bits.append((b >> i) & 1)
    return bits

def generate_fsk_audio(bits):
    """Generates a phase-continuous audio signal from the bitstream."""
    audio_frames = []
    phase = 0.0
    
    # 1. Generate START signal
    t_start = np.arange(int(START_DURATION * SAMPLE_RATE)) / SAMPLE_RATE
    wave_start = np.sin(2 * np.pi * F_START * t_start + phase)
    phase += 2 * np.pi * F_START * START_DURATION
    audio_frames.append(wave_start)
    
    # 2. Generate Data Bits
    for bit in bits:
        f = F_ONE if bit == 1 else F_ZERO
        t = np.arange(int(SYMBOL_DURATION * SAMPLE_RATE)) / SAMPLE_RATE
        wave = np.sin(2 * np.pi * f * t + phase)
        phase += 2 * np.pi * f * SYMBOL_DURATION
        audio_frames.append(wave)
        
    return np.concatenate(audio_frames)

def send_message(text):
    print(f"Preparing to send: '{text}'")
    payload = text.encode('utf-8')
    
    if len(payload) > 255:
        print("Error: Message too long (max 255 bytes).")
        return
        
    # Build Packet
    preamble_bits = [1, 1, 1, 1, 1, 1, 1, 1]
    length_bits = bytes_to_bits([len(payload)])
    payload_bits = bytes_to_bits(payload)
    crc_bits = bytes_to_bits([crc8(payload)])
    
    packet_bits = preamble_bits + length_bits + payload_bits + crc_bits
    
    # Modulate
    print("Modulating signal...")
    audio_signal = generate_fsk_audio(packet_bits)
    
    # Play
    print("Transmitting via speaker...")
    sd.play(audio_signal, samplerate=SAMPLE_RATE)
    sd.wait()
    print("Transmission complete.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print('Usage: python3 sender.py "YOUR MESSAGE"')
        sys.exit(1)
        
    message = sys.argv[1]
    send_message(message)