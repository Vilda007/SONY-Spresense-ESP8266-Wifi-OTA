#!/usr/bin/env python3
# tools/d1_config_via_com.py — inject a CONFIG frame to the D1 mini relay
# directly over COM15 (the on-board CH340 USB-UART), bypassing the Spresense
# UART2 link. The production d1_mini_relay firmware parses frames on Serial
# (UART0 = GPIO1 TX / GPIO3 RX), so a CONFIG sent from the PC via the CH340
# is received exactly like one from the Spresense. The D1 then joins WiFi and
# replies IP= / ACK:hello on TX, which we read back on COM15.
#
# This verifies the D1 end-to-end (RX + TX + WiFi + relay logic) WITHOUT the
# Spresense link and WITHOUT reflashing. Used to isolate a broken D1-TX ->
# Spresense-RX wire from a D1-side fault.
#
# Credentials are read from ~/.d1_test_pass (same source as gen_test_creds.py)
# and NEVER printed. Only frame types/lengths and decoded status strings
# (IP=..., ACK:hello, NO_WIFI_OR_SERVER, ...) are shown.
#
# Usage:  python tools/d1_config_via_com.py [COM15] [baud]
import sys, os, time, serial

CRED_FILE = os.path.join(os.path.expanduser("~"), ".d1_test_pass")

# relay_proto.h constants
FRAME_START = 0xAA
FRAME_END   = 0x55
T_CONFIG     = 0x01
T_DATA_UP    = 0x02
T_DATA_DOWN  = 0x03
T_STATUS     = 0x10
T_PING       = 0xFE
T_PONG       = 0xFF

TYPE_NAMES = {0x01:"CONFIG",0x02:"DATA_UP",0x03:"DATA_DOWN",0x04:"OTA_AVAIL",
              0x05:"OTA_ACK",0x06:"OTA_CHUNK",0x07:"OTA_DONE",0x10:"STATUS",
              0xFE:"PING",0xFF:"PONG"}

def crc8_update(crc, b):
    crc ^= (b & 0xFF)
    for _ in range(8):
        crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

def frame_bytes(type_byte, payload):
    crc = crc8_update(0, type_byte)
    for b in payload:
        crc = crc8_update(crc, b)
    return bytes([FRAME_START, (len(payload) >> 8) & 0xFF, len(payload) & 0xFF, type_byte]) + payload + bytes([crc, FRAME_END])

def load_creds(path):
    creds = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            kv = line.split("=", 1)
            if len(kv) == 2:
                creds[kv[0].strip().lower()] = kv[1].strip()
    return creds

def decode_frames(buf):
    """Yield (type, payload) for each complete valid frame found in buf; return remaining bytes."""
    out = []
    i = 0
    n = len(buf)
    while i < n:
        if buf[i] != FRAME_START:
            i += 1
            continue
        if i + 4 > n:
            break
        ln = (buf[i+1] << 8) | buf[i+2]
        typ = buf[i+3]
        end = i + 4 + ln + 2  # payload + crc + end
        if end > n:
            break
        payload = buf[i+4:i+4+ln]
        crc = crc8_update(0, typ)
        for b in payload:
            crc = crc8_update(crc, b)
        if buf[i+4+ln] == crc and buf[i+4+ln+1] == FRAME_END:
            out.append((typ, payload))
        i = end
    return out, buf[i:]

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM15"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    if not os.path.isfile(CRED_FILE):
        sys.exit(f"cred file not found: {CRED_FILE}")
    creds = load_creds(CRED_FILE)
    ssid = creds.get("ssid", "")
    pw   = creds.get("pass", "")
    url  = creds.get("url", "")
    if not ssid or not pw or not url:
        sys.exit(f"cred file missing ssid/pass/url (keys: {sorted(creds)})")
    # Report lengths only — never the values.
    print(f"creds: ssid_len={len(ssid)} pass_len={len(pw)} url_len={len(url)}", flush=True)

    # CONFIG payload mirrors spresense_relay sendConfigToD1().
    import json as _json
    payload = _json.dumps({"ssid":ssid,"pass":pw,"server_url":url,
                           "ota_url":"","ota_manifest_url":"","poll_ms":30000},
                          separators=(",", ":")).encode("utf-8")
    config_frame = frame_bytes(T_CONFIG, payload)
    dataup_frame = frame_bytes(T_DATA_UP, b"hello")

    print(f"opening {port} @ {baud} (this resets the D1)...", flush=True)
    s = serial.Serial(port, baud, timeout=0.3)
    try:
        s.dtr = False; s.rts = False   # avoid holding reset after open
    except Exception:
        pass
    s.reset_input_buffer()

    # Give the D1 ~1.2 s to boot after the reset pulse, then push CONFIG a few
    # times so one survives any contention with the Spresense TX on GPIO3.
    time.sleep(1.2)
    for k in range(5):
        s.write(config_frame)
        time.sleep(0.15)
    print("CONFIG sent x5; sending one DATA_UP hello ...", flush=True)
    s.write(dataup_frame)

    print("reading replies for 25 s ...", flush=True)
    t0 = time.time()
    rxbuf = b""
    saw_ip = False
    saw_ack = False
    while time.time() - t0 < 25:
        b = s.read(512)
        if b:
            rxbuf += b
            frames, rxbuf = decode_frames(rxbuf)
            for typ, pl in frames:
                name = TYPE_NAMES.get(typ, f"0x{typ:02X}")
                try:
                    txt = pl.decode("utf-8", "replace")
                except Exception:
                    txt = pl.hex()
                print(f"  RX type={name} len={len(pl)} :: {txt}", flush=True)
                if "IP=" in txt:
                    saw_ip = True
                if txt.startswith("ACK:"):
                    saw_ack = True
        # nudge with another DATA_UP every ~6 s so we can see ACK:hello / a server POST
        if int(time.time() - t0) in (6, 12, 18):
            s.write(dataup_frame)
    print(f"RESULT: saw_IP={'YES' if saw_ip else 'NO'} saw_ACK={'YES' if saw_ack else 'NO'}", flush=True)
    s.close()

if __name__ == "__main__":
    main()