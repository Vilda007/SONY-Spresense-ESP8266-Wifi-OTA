#!/usr/bin/env python3
# tools/monitor_spresense.py — wait for the Spresense CP210x console to appear,
# then stream its @115200 output. Used while wiring the D1 mini relay to the
# Spresense: shows CONFIG frames sent to the D1 and status/data frames the D1
# sends back (IP=..., ACK:hello, ...).
#
# Detects the Spresense by Silicon Labs CP210x USB VID 0x10C4 (not a hardcoded
# COM number, so it works even if the OS reassigns it). Deliberately does NOT
# open the D1 mini CH340 port — opening COM15 during operation makes the CH340
# fight the D1↔Spresense UART line and corrupts frames (see docs/wiring.md).
#
# Emits one stdout line per interesting console line (relayed as Monitor events):
#   PORT_APPEAR / PORT_DISAPPEAR, and filtered serial text (CONFIG, IP=, ACK,
#   RX/TX, BOOT, status, FAIL, error, wdt, relay, ...).
import re, sys, time, serial, serial.tools.list_ports

BAUD = 115200
VID_CP210X = 0x10C4  # Silicon Labs

# Redact secret values (WiFi password, maybe SSID) from any CONFIG payload that
# appears in a console line before printing. The CONFIG frame the Spresense sends
# to the D1 carries the live WiFi password in plaintext; without this, opening
# the monitor would leak it to the terminal/log.
_SECRET_RE = re.compile(
    r'("(?:pass|wifi_pass|password|secret|token)"\s*:\s*")'
    r'([^"]*)(")', re.IGNORECASE)

def redact(txt):
    return _SECRET_RE.sub(lambda m: m.group(1) + '<redacted>' + m.group(3), txt)

def find_spresense():
    for p in serial.tools.list_ports.comports():
        if p.vid == VID_CP210X:
            return p.device
    return None

def interesting(line):
    k = ("CONFIG","IP=","ACK:","ACK ","RX ","TX ","RX_","TX_","BOOT",
         "status","FAIL","error","wdt","relay","D1","WAIT","NO_","HTTP",
         "BSSID","ssid","server","PING","PONG","DATA")
    return any(kw in line for kw in k)

def main():
    port = None
    ser = None
    buf = b""
    last = None
    # Time-window dedupe: suppress a line if it was emitted within the last
    # DEDUP_S seconds. Handles the alternating "sent DATA_UP hello" / "RX D1
    # ... hello" pair (consecutive-dedup misses those) and any recurring line,
    # while still letting genuinely new signals (IP=, ACK:hello, PORT_*) through
    # the instant they first appear.
    DEDUP_S = 15.0
    last_emit_at = {}  # line -> timestamp of last emission
    print("monitor: waiting for Spresense CP210x console (VID 10C4)...", flush=True)
    while True:
        if ser is None:
            port = find_spresense()
            if port and port != last:
                print(f"PORT_APPEAR {port}", flush=True)
                last = port
            if port:
                try:
                    ser = serial.Serial(port, BAUD, timeout=0.3)
                    ser.reset_input_buffer()
                    print(f"CONSOLE_OPEN {port} @ {BAUD}", flush=True)
                except Exception as e:
                    print(f"open_error {port}: {e}", flush=True)
                    ser = None
                    time.sleep(1)
            else:
                time.sleep(1)
            continue
        # read
        try:
            chunk = ser.read(256)
        except Exception as e:
            print(f"read_error: {e}; reopening", flush=True)
            try: ser.close()
            except: pass
            ser = None
            time.sleep(1)
            continue
        if chunk:
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                txt = line.decode("utf-8", "replace").strip("\r").strip()
                if not txt:
                    continue
                # Time-window dedupe (see DEDUP_S above): skip if this exact
                # line was emitted recently. First occurrence always passes.
                now = time.monotonic()
                ts = last_emit_at.get(txt)
                if ts is not None and (now - ts) < DEDUP_S:
                    continue
                last_emit_at[txt] = now
                out = redact(txt)
                if interesting(txt):
                    print(f"COM6> {out}", flush=True)
                else:
                    # still emit short non-garbage lines, suppress pure-noise
                    if len(txt) < 80 and not all(c in "\x00\xff " for c in txt):
                        print(f"com> {out}", flush=True)
        # detect disconnect
        if not find_spresense():
            print(f"PORT_DISAPPEAR {port}", flush=True)
            try: ser.close()
            except: pass
            ser = None
            last = None
            time.sleep(1)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("monitor stopped", flush=True)