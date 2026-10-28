import serial, wave, sys, time, platform, os

PORT = "/dev/ttyUSB0"
RATE, SECONDS = 16000, 5

ser = serial.Serial(PORT, 921600, timeout=3)
time.sleep(2)
ser.reset_input_buffer()
print("Speak now...")
ser.write(b"R")

hdr = b""
while hdr != b"AUD0":
    b = ser.read(1)
    if not b:
        sys.exit("No response from ESP32")
    hdr = (hdr + b)[-4:]

need = RATE * SECONDS * 2
data = bytearray()
while len(data) < need:
    chunk = ser.read(need - len(data))
    if not chunk:
        break
    data += chunk
ser.close()

with wave.open("record.wav", "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(bytes(data))
print("Saved record.wav,", len(data), "bytes")

if platform.system() == "Windows":
    import winsound
    winsound.PlaySound("record.wav", winsound.SND_FILENAME)
elif platform.system() == "Darwin":
    os.system("afplay record.wav")
else:
    os.system("aplay record.wav")