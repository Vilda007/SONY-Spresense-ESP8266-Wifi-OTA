#!/usr/bin/env python3
# tools/monitor_ports.py — poll COM ports every 2s, log changes + raw bytes on COM21.
#
# Usage: python tools/monitor_ports.py [duration_s]
# Defaults to 120 s. Lists all COM ports and shows which appear/disappear.
# On COM21, prints any bytes received with timestamps.

import sys
import time
import threading
from datetime import datetime
import serial

BAUD = 115200
DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 120

def list_ports():
    import serial.tools.list_ports
    return sorted(p.device for p in serial.tools.list_ports.comports())

def reader(stop_evt, log):
    try:
        s = serial.Serial("COM21", BAUD, timeout=0.5)
    except Exception as e:
        log(f"[reader] cannot open COM21: {e}")
        return
    s.reset_input_buffer()
    log(f"[reader] COM21 open @ {BAUD}")
    while not stop_evt.is_set():
        try:
            data = s.read(64)
        except Exception as e:
            log(f"[reader] read error: {e}")
            break
        if data:
            try:
                txt = data.decode("utf-8", errors="replace")
            except Exception:
                txt = repr(data)
            log(f"[COM21 RX {len(data)}B] {txt!r}")
    s.close()
    log("[reader] COM21 closed")

stop = threading.Event()
def log(msg):
    print(f"{datetime.now().strftime('%H:%M:%S.%f')[:-3]} {msg}", flush=True)

t = threading.Thread(target=reader, args=(stop, log), daemon=True)
t.start()

t0 = time.time()
last = set()
while time.time() - t0 < DURATION and not stop.is_set():
    ports = list_ports()
    if ports != last:
        log(f"PORTS: {ports}")
        last = ports
    time.sleep(2)

stop.set()
t.join(timeout=2)
log("done")
