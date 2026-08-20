# Troubleshooting

Known issues and fixes hit while bringing this project up. Read this before pulling cables in
new ways — most of the symptoms below map to one of these root causes.

## 1. `Serial2.begin()` opens `/dev/ttyS2` but no data on D00/D01

**Symptom:** Spresense boots, prints `CONFIG sent to D1 mini` and `sent DATA_UP hello` on COM6,
but **no** `RX D1 ...` lines ever appear. Even a hardware loopback wire between Spresense D00 and
D01 (TX ↔ RX short) brings nothing back.

**Diagnostic check:** in a minimal sketch, open `/dev/ttyS2` via POSIX and `read()` for a few
seconds — returns **0 bytes**, even after `O_NONBLOCK`. `open(O_EXCL)` succeeds (UART2 is not
held by another process), `cxd56_pin_status()` shows the pin is still in GPIO mode.

**Root cause:** Arduino core 3.4.7 `HardwareSerial::begin()` opens `/dev/ttyS2` but **does NOT
switch the CXD5602 pin multiplexor** to UART2. The `pin_maps[]` table in
`cores/spresense/wiring_digital.c` maps `PIN_D00 → PIN_UART2_RXD`, `PIN_D01 → PIN_UART2_TXD`,
but those entries are never consumed at runtime — `Serial2.begin()` calls POSIX `open()` and
nothing else. The SoC pads stay in GPIO mode (input floating, output LOW), so TX never drives
and RX never listens.

**Fix:** in `setup()`, **before** `Serial2.begin()`, call the SDK pinconfig helpers explicitly:

```cpp
extern "C" {
  #include <arch/board/board_pinconfig.h>   // PINCONF_UART2_TXD, PINCONF_UART2_RXD
  #include <chip/cxd56_pinconfig.h>          // cxd56_pin_config()
}

void setup() {
  Serial.begin(115200);
  // route UART2 to D0 (RX) and D1 (TX)
  cxd56_pin_config(PINCONF_UART2_TXD);
  cxd56_pin_config(PINCONF_UART2_RXD);

  Serial2.begin(115200, SERIAL_8N1 | SERIAL_CTS | SERIAL_RTS);
  // ...
}
```

Include paths `arch/board/board_pinconfig.h` and `chip/cxd56_pinconfig.h` resolve because the
Arduino core's `platform.txt` adds `-I{build.kernel}/nuttx/arch` and
`-I{build.kernel}/nuttx/arch/chip`. Both files live in the SDK release directory.

After this fix, `RX D1 type=0x10 payload=WAIT_CONFIG` and `RX D1 type=0x3 payload=ACK:hello`
start appearing on COM6 as expected.

## 2. `WAIT_CONFIG` frames decode on the D1 side but the Spresense never sees them

**Symptom:** with the pinconfig fix in place, COM6 still shows no `RX D1` lines. The D1 mini,
read directly over its USB COM15, is emitting `WAIT_CONFIG` frames every 3 s — so the firmware
is alive. Wiring is verified (GND shared, TX wire measured at 3.3 V idle).

**Root cause (most common): the D1 mini is powered through its own USB to the PC.** The onboard
CH340 holds GPIO1 (D1 mini TX) idle-HIGH continuously while USB is connected. Any UART frame
the ESP8266 puts on TX is fought by the CH340. The Spresense RX line either sees a constant
HIGH (no start bit ever, so no bytes) or interleaved garbage that fails CRC.

**Fix:** power the D1 mini from **VIN (5 V) using an external USB charger rated for ≥1 A**, not
from the PC USB and not from the Spresense 5 V rail. Keep GND shared with the Spresense. The
CH340 powers down when VUSB is absent, GPIO1 is free for the ESP8266 UART, and frames arrive
cleanly on Spresense D00.

Re-verify by reading COM6 — you should now see `RX D1 type=0x10 payload=WAIT_CONFIG` followed
by `payload=BOOT_WAITING_CONFIG`, then `payload=IP=192.168.1.x` once CONFIG has been received
and WiFi joins.

## 3. D1 mini restarts continuously, prints `Fatal exception (0)` at 74880 baud

**Symptom:** reading the D1 mini at 74880 baud (ESP8266 boot-ROM rate) shows repeated
`Fatal exception (0): epc1=0x40100000, epc2=0, epc3=0, excvaddr=0`. Flash writes appear to
succeed but the user firmware crashes on the very first instruction after every reset.

**Root cause:** flash contents corrupted — almost always by repeated brownout during a previous
flash operation while the D1 mini was powered from a weak source (e.g. Spresense 5 V rail).

**Fix:** force the D1 mini into bootloader mode and reflash:

1. Disconnect all wires to the Spresense (TX/RX/GND).
2. Hold **GPIO0 = D3** to **GND** with a wire.
3. Press and release the **RST** button. The D1 mini boots in download mode (sketch not
   running, no crash).
4. `arduino-cli upload -p COM15 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 d1_mini_relay`
5. After upload, disconnect USB, remove the D3-GND wire, reconnect USB. Verify with
   `python tools/read_com.py COM15 6 115200` — expect `WAIT_CONFIG` frames every ~3 s.

## 4. SD config parses, CONFIG sent, but D1 mini never gets WiFi credentials

**Symptom:** COM6 shows `SD config OK: ssid=HOUSLEcz server=http://...`. `CONFIG sent to D1 mini`
is logged. But the D1 mini (read on COM15) never logs `CONFIG ssid=...` or `WiFi begin: ...`.

**Possible causes:**

- The D1 mini is not actually running the relay sketch (e.g. is in bootloader mode — see issue 3).
- The SD card is not inserted, or `config.json` has CRLF line endings that confuse some parser
  paths. (Arduino `File::read()` returns raw bytes — CRLF should be fine, but verify with a hex
  dump.)
- `config.json` exists but `wifi_ssid` is empty, so Spresense decides `configLoaded = false`
  and skips the CONFIG send.

**Diagnostic:** in `setup()`, print `wifiSsid.length()` and the full JSON string. Confirm the
file actually contains the expected fields. Use `jsonField` from `lib/relay_proto/relay_proto.h`
— it tolerates whitespace around the colon (`"k": "v"` and `"k":"v"` both work). If you write
your own JSON parser, beware that `{"ssid": "x"}` (with space) trips naive implementations.

## 5. ESP8266 HTTPClient POST on `http://192.168.x.x:8080/` times out from the D1 mini

**Symptom:** D1 mini reports `WiFi OK IP=...`, but `http.POST(...)` returns `-1` or hangs.

**Possible causes:**

- **Windows Firewall** blocked Python on the first run. Re-allow `python.exe` on private networks
  (Settings → Windows Security → Firewall → Allowed apps). Symptom: server logs are empty when
  the D1 tries to POST.
- Server is bound to `127.0.0.1` instead of `0.0.0.0`. Use `HTTPServer(("0.0.0.0", PORT), ...)`
  so external devices on the LAN can reach it.
- D1 mini and the PC are on different SSIDs or VLANs. Verify `WiFi.localIP()` and the PC LAN IP
  are on the same `/24` (e.g. both `192.168.1.x`). `tools/test_server.py` logs the source IP of
  every request — confirm it matches the D1 mini.

## 6. `arduino-cli` auto-prototype hoisting breaks compilation

**Symptom:** errors like `error: 'Parser' was not declared in this scope` or
`redeclared as different kind of entity` when the sketch uses user-defined types in function
parameters.

**Root cause:** `arduino-cli` runs a preprocessor that hoists function prototypes above type
definitions. Forward declarations help only sometimes; if a function takes `Parser &` by
reference, the hoisted prototype fails.

**Fix:** put the types **and** the functions in a shared header (`lib/relay_proto/relay_proto.h`
in this project) and `#include` it from both sketches. The auto-prototyper cannot hoist across
includes. Compile with `arduino-cli ... --library lib/relay_proto` so the library path is
visible. Sketch folder name and `.ino` filename must match (`d1_mini_relay/d1_mini_relay.ino`,
`spresense_relay/spresense_relay.ino`).

## 7. `ConfigSD` / `no wifi_ssid` even though the file is on the SD card

**Symptom:** COM6 prints `SD: config.json not found` despite the file being present.

**Possible causes:**

- The JSON parser needs whitespace-tolerant key extraction (`"k": "v"`). The default Arduino
  `String.indexOf(":")` matches strictly — this project's `jsonField()` helper in
  `lib/relay_proto/relay_proto.h` skips whitespace around the colon. Use it.
- CRLF line endings on the file. Open it with a hex editor (`python -c "open('config.json','rb').read()[:200]"`)
  and confirm it starts with `{` and not `﻿{` (UTF-8 BOM).
- The SD card is not FAT-formatted, or has a partition type the Spresense `SDHCI` driver does
  not recognise. Re-format as FAT32 (or FAT16 for ≤2 GB cards).

## See also

- [wiring.md](wiring.md) — pinout and power notes
- [architecture.md](architecture.md) — data flow and frame protocol
- [protocol.md](../protocol.md) — frame format