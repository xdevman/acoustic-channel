import time
import queue
import datetime
import numpy as np
import sounddevice as sd
from scipy.fft import rfft, rfftfreq

# --- CONFIGURABLE CONSTANTS ---
SAMPLE_RATE = 48000
F_ZERO = 17000
F_ONE = 18000
F_START = 19000
FREQ_TOLERANCE = 800

SYMBOL_DURATION = 0.05  # 50 ms per bit
SAMPLES_PER_SYMBOL = int(SAMPLE_RATE * SYMBOL_DURATION) # 2400 samples
SYNC_STEP = 240 # 5 ms sliding window for precise edge detection

# State Machine
STATE_WAIT = 0
STATE_SYNC = 1
STATE_RECEIVE = 2

def crc8(data_bytes):
    """Calculates standard CRC-8 checksum to verify data integrity."""
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

def bits_to_bytes(bits):
    """Packs a list of bits back into a bytearray."""
    b_array = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for bit in bits[i:i+8]:
            byte = (byte << 1) | bit
        b_array.append(byte)
    return b_array

def get_dominant_frequency(audio_chunk):
    """
    Uses FFT to find the strongest frequency in the 14kHz - 20kHz band.
    Relies entirely on relative peak detection to ignore volume/gain.
    """
    window = audio_chunk * np.hanning(len(audio_chunk))
    spectrum = np.abs(rfft(window))
    frequencies = rfftfreq(len(audio_chunk), 1/SAMPLE_RATE)
    
    # Mask to only look at our high-frequency band
    mask = (frequencies > 14000) & (frequencies < 20000)
    
    if not np.any(mask):
        return 0
        
    peak_idx = np.argmax(spectrum[mask])
    peak_freq = frequencies[mask][peak_idx]
    
    return peak_freq

def run_receiver():
    audio_queue = queue.Queue()
    
    def callback(indata, frames, time, status):
        """Pushes raw audio samples to the processing queue."""
        audio_queue.put(indata[:, 0].copy())

    print("Receiver running...")
    print("Waiting for signal...\n")
    
    buffer = np.zeros(0, dtype=np.float32)
    state = STATE_WAIT
    bits = []
    expected_bits = 0
    
    # Squelch and Timeout Counters
    start_hits = 0 
    sync_attempts = 0
    
    stream = sd.InputStream(samplerate=SAMPLE_RATE, channels=1, callback=callback)
    
    with stream:
        while True:
            while not audio_queue.empty():
                buffer = np.concatenate((buffer, audio_queue.get()))
                
            if state == STATE_WAIT:
                # Fast scan: Overlap windows by 50% to find the 19kHz START tone
                step = SAMPLES_PER_SYMBOL // 2 
                if len(buffer) >= SAMPLES_PER_SYMBOL:
                    chunk = buffer[:SAMPLES_PER_SYMBOL]
                    freq = get_dominant_frequency(chunk)
                    
                    if abs(freq - F_START) < FREQ_TOLERANCE:
                        start_hits += 1
                        # DEBOUNCE: Require 4 consecutive hits (100ms) to confirm a real START tone
                        if start_hits >= 4:
                            print("START detected")
                            state = STATE_SYNC
                            start_hits = 0
                            sync_attempts = 0
                    else:
                        # Reset counter if the 19kHz tone is broken by noise
                        start_hits = 0
                        
                    buffer = buffer[step:]
                    
            elif state == STATE_SYNC:
                # Precision scan: Slide forward 5ms at a time to find the exact moment
                # the 19kHz tone drops and the 18kHz Preamble begins.
                if len(buffer) >= SAMPLES_PER_SYMBOL:
                    chunk = buffer[:SAMPLES_PER_SYMBOL]
                    freq = get_dominant_frequency(chunk)
                    
                    if abs(freq - F_ONE) < FREQ_TOLERANCE:
                        print("Synchronized. Receiving packet...")
                        state = STATE_RECEIVE
                        bits = []
                        expected_bits = 16 # Preamble (8) + Length (8)
                        
                        # Advance buffer by half a symbol (25ms) so the NEXT window
                        # aligns perfectly with the center of the 18kHz preamble bit.
                        buffer = buffer[SAMPLES_PER_SYMBOL // 2:]
                    else:
                        buffer = buffer[SYNC_STEP:] 
                        sync_attempts += 1
                        
                        # TIMEOUT: If we scan for 500ms and never find the 18kHz preamble, abort.
                        if sync_attempts > 100: 
                            print("\nSync timeout: Preamble not found.")
                            print("Waiting for signal...\n")
                            state = STATE_WAIT
                            start_hits = 0
                        
            elif state == STATE_RECEIVE:
                # Read exactly one full symbol duration
                if len(buffer) >= SAMPLES_PER_SYMBOL:
                    chunk = buffer[:SAMPLES_PER_SYMBOL]
                    freq = get_dominant_frequency(chunk)
                    
                    # Advance buffer by exactly 1 symbol duration
                    buffer = buffer[SAMPLES_PER_SYMBOL:]
                    
                    # Demodulate based on proximity to target frequencies
                    if abs(freq - F_ONE) < FREQ_TOLERANCE:
                        bits.append(1)
                    elif abs(freq - F_ZERO) < FREQ_TOLERANCE:
                        bits.append(0)
                    else:
                        print(f"\nSignal lost or corrupted (Detected: {freq:.0f} Hz)")
                        print("Waiting for signal...\n")
                        state = STATE_WAIT
                        start_hits = 0
                        continue
                        
                    # Protocol Decoding
                    if len(bits) == 8:
                        if bits != [1, 1, 1, 1, 1, 1, 1, 1]:
                            print("\nBad preamble. Dropping packet.")
                            print("Waiting for signal...\n")
                            state = STATE_WAIT
                            start_hits = 0
                            continue
                            
                    elif len(bits) == 16:
                        # Dynamically calculate total expected packet size based on header length
                        payload_length = bits_to_bytes(bits[8:16])[0]
                        expected_bits = 16 + (payload_length * 8) + 8 # + CRC
                        
                    elif len(bits) == expected_bits:
                        # Full packet received
                        payload_bytes = bits_to_bytes(bits[16:-8])
                        received_crc = bits_to_bytes(bits[-8:])[0]
                        calc_crc = crc8(payload_bytes)
                        
                        if received_crc == calc_crc:
                            print("CRC OK\n")
                            try:
                                message_text = payload_bytes.decode('utf-8')
                                timestamp = datetime.datetime.now().strftime("[%Y-%m-%d %H:%M:%S]")
                                output = f"{timestamp}\n{message_text}\n"
                                
                                print("Message:")
                                print(message_text + "\n")
                                
                                # Log to file
                                with open("messages.txt", "a") as f:
                                    f.write(output + "\n")
                            except UnicodeDecodeError:
                                print("Decoded string is not valid UTF-8.\n")
                        else:
                            print(f"CRC FAILED. Expected {calc_crc}, got {received_crc}\n")
                            
                        print("Waiting for signal...\n")
                        state = STATE_WAIT
                        start_hits = 0

            else:
                # Prevent CPU spin if buffer is too small
                time.sleep(0.01)

if __name__ == "__main__":
    run_receiver()