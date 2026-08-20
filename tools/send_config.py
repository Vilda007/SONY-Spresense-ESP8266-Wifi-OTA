#!/usr/bin/env python3
# tools/send_config.py — inject a CONFIG frame (and a DATA_UP "hello") into the D1 mini
# over its USB-serial (COM), then decode and print the frames the D1 sends back.
# This tests the D1 mini + WiFi + HTTP + server path in isolation, without the Spresense.
#
# Usage:
#   python tools/send_config.py <COM> <ssid> <pass> <server_url>
#   python tools/send_config.py COM15 HOUSLEcz mysecret http://192.168.1.72:8080/
#
# NOTE: the WiFi password is passed only as a process argument and sent over the serial
#       link to the D1 mini. It is not written to any file. Keep this script out of any
#       committed log. (The script itself is generic — it contains no secret.)
import sys, time, json, serial

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

FRAME_START = 0xAA
FRAME_END  = 0x55
T_CONFIG     = 0x01
T_DATA_UP    = 0x02
T_DATA_DOWN  = 0x03
T_STATUS     = 0x10

TYPE_NAMES = {
    0x01: "CONFIG", 0x02: "DATA_UP", 0x03: "DATA_DOWN", 0x04: "OTA_AVAIL",
    0x05: "OTA_ACK", 0x06: "OTA_CHUNK", 0x07: "OTA_DONE",
    0x10: "STATUS", 0xFE: "PING", 0xFF: "PONG",
}

def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

def frame(ttype, payload=b""):
    body = bytes([ttype]) + payload
    crc = crc8(body)
    return bytes([FRAME_START, (len(payload) >> 8) & 0xFF, len(payload) & 0xFF, ttype]) + payload + bytes([crc, FRAME_END])

class P:
    def __init__(self):
        self.st = 0; self.len = 0; self.type = 0; self.idx = 0; self.buf = bytearray(); self.crc = 0
    def feed(self, b):
        if self.st == 0:
            if b == FRAME_START: self.st = 1
            return None
        if self.st == 1:
            self.len = b << 8; self.st = 2; return None
        if self.st == 2:
            self.len |= b; self.idx = 0; self.buf = bytearray(); self.st = 3; return None
        if self.st == 3:
            self.type = b; self.crc = crc8(bytes([b])); self.st = 4 if self.len else 6; return None
        if self.st == 4:
            self.buf.append(b)
            self._crc_step(b)
            self.idx += 1
            if self.idx >= self.len: self.st = 6
            return None
        if self.st == 6:
            ok = (b == self.crc); self.st = 7; return None
        if self.st == 7:
            self.st = 0
            if b != FRAME_END: return None
            return (self.type, bytes(self.buf))
        return None
    def _crc_step(self, b):
        c = self.crc ^ b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
        self.crc = c

def main():
    port = sys.argv[1]; ssid = sys.argv[2]; pw = sys.argv[3]; srv = sys.argv[4]
    cfg = json.dumps({"ssid": ssid, "pass": pw, "server_url": srv,
                      "ota_url": srv + "package.bin",
                      "ota_manifest_url": srv + "manifest.json",
                      "poll_ms": 30000}, separators=(",", ":"))
    ser = serial.Serial(port, 115200, timeout=0.2)
    print(f"[open {port}] sending CONFIG ({len(cfg)} B) -> {ssid} / {srv}", flush=True)
    ser.write(frame(T_CONFIG, cfg.encode()))
    ser.flush()
    p = P()
    t0 = time.time()
    sent_dataup = False
    while time.time() - t0 < 28:
        chunk = ser.read(4096)
        if chunk:
            for b in chunk:
                r = p.feed(b)
                if r is not None:
                    ttype, payload = r
                    name = TYPE_NAMES.get(ttype, f"0x{ttype:02X}")
                    txt = payload.decode("ascii", "replace")
                    print(f"  RX {name:9s} ({len(payload)} B): {txt}", flush=True)
        # send a DATA_UP "hello" ~8s after CONFIG (give WiFi time to connect)
        if not sent_dataup and time.time() - t0 > 8:
            ser.write(frame(T_DATA_UP, b"hello"))
            ser.flush()
            sent_dataup = True
            print("[sent DATA_UP 'hello']", flush=True)
    ser.close()
    print("[done]", flush=True)

if __name__ == "__main__":
    main()