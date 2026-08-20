#!/usr/bin/env python3
# tools/read_com.py — read a COM port for N seconds and print text + hex.
# Usage: python tools/read_com.py <COMx> [seconds] [baud]
import sys, time, serial

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

port = sys.argv[1] if len(sys.argv) > 1 else "COM6"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 15.0
baud = int(sys.argv[3]) if len(sys.argv) > 3 else 115200

ser = serial.Serial(port, baud, timeout=0.2)
print(f"[reading {port} @ {baud} for {secs}s]", flush=True)
t0 = time.time()
buf = bytearray()
while time.time() - t0 < secs:
    chunk = ser.read(4096)
    if chunk:
        buf += chunk
ser.close()
text = buf.decode("ascii", "replace")
print("===== TEXT =====")
print(text)
print("===== HEX (first 256) =====")
print(" ".join(f"{b:02X}" for b in buf[:256]))