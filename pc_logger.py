"""Reads the ESP32 serial output and appends decoded messages to messages.txt.

Usage:  python pc_logger.py COM5            (Windows)
        python pc_logger.py /dev/ttyUSB0    (Linux)
Install: pip install pyserial
Close the Arduino Serial Monitor first (only one program can use the port).
"""
import sys
import datetime
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
baud = 115200

with serial.Serial(port, baud, timeout=1) as ser:
    print(f"Listening on {port} ... (Ctrl+C to stop)")
    while True:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("MSG_HEX:"):
            try:
                text = bytes.fromhex(line[8:]).decode("utf-8")
            except (ValueError, UnicodeDecodeError):
                print("Received message is not valid UTF-8, skipped.")
                continue
            ts = datetime.datetime.now().strftime("[%Y-%m-%d %H:%M:%S]")
            print(f"{ts}\n{text}\n")
            with open("messages.txt", "a", encoding="utf-8") as f:
                f.write(f"{ts}\n{text}\n\n")
        else:
            print(line)  # show the ESP32's status lines (START detected, CRC OK, ...)
