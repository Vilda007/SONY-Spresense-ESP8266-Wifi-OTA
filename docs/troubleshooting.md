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

## 2. D1 TX never reaches the Spresense (no `RX D1` lines) — GPIO1 is CH340-clamped

**Symptom:** with the pinconfig fix (§1) in place, COM6 still shows no `RX D1` lines. The D1
mini, read directly over its USB COM15, is emitting `WAIT_CONFIG` frames every 3 s — so the
firmware is alive. Wiring and GND are verified.

**Root cause:** D1 mini UART0 TX (**GPIO1**, the "tx" pin) is permanently tied through a series
resistor to the onboard CH340, which clamps the line in **both** power states:

- **CH340 unpowered** (D1 on a charger / on the Spresense): the CH340's ESD-protection diodes
  clamp the GPIO1 swing to ~0.7 V ↔ 1.8 V — below the Spresense D00 VIH (~2.3 V) → the
  Spresense reads idle/garbage and never sees a frame.
- **CH340 powered** (D1 on USB): the CH340 actively fights the line → corrupted frames.

So GPIO1 is unusable as the link TX regardless of how the D1 is powered. (The earlier "power
from VIN so the CH340 powers down" advice was a red herring — an unpowered CH340 clamps the line
*worse*, not better.)

**Fix (already in production `d1_mini_relay.ino`):** status/data TX moved to **`Serial1` on
GPIO2** (the "2" pin), which is not shared with the CH340 → clean rail-to-rail swing.
CONFIG/DATA_UP RX stays on UART0 RX (GPIO3). Re-flash the D1 with the current
`d1_mini_relay.ino` and wire **D1 GPIO2 → Spresense D00** (D1 GPIO3 ← D01, GND shared).

Re-verify on COM6: `RX D1 type=0x10 payload=BOOT_WAITING_CONFIG`, then `WAIT_CONFIG` every ~3 s,
then `payload=IP=192.168.1.x` once CONFIG is received and WiFi joins, then
`D1 ONLINE (CONFIG confirmed)`.

## 3. D1 mini restarts continuously / `Fatal exception (0)` — usually a transient strap, NOT corrupt flash

**Symptom:** reading the D1 mini at 74880 baud (ESP8266 boot-ROM rate) shows repeated
`Fatal exception (0): epc1=0x4010000x, epc2=0, epc3=0, excvaddr=0`, OR (read at 115200) a solid
stream of garbage that looks like a crash loop. esptool may fail to connect ("Failed to connect")
because the chip reboots too fast for auto-reset.

**Root cause (almost always): a transient GPIO2 boot-strap disturbance, NOT corrupt flash.**
GPIO2 must be HIGH at boot. The GPIO2→D00 wire, combined with power-cycling / hot-plugging, can
knock the strap → the chip crash-reboots. With the wire off (or connected before power-up and
left stable) the chip boots cleanly (boot mode (3,6), one boot log, zero exceptions) and runs the
sketch fine. **Do not assume the flash is corrupt and do not erase/reflash** — that is a red
herring hit on this project. (A 115200 read of the 74880-baud boot-ROM/RF-cal stream also looks
like a crash loop but is just misread baud; read at 74880 to see the real exception count.)

**Fix:**

1. Disconnect the GPIO2→D00 wire. Power the D1 from a solid source (≥1 A charger or PC USB).
2. Re-power (unplug/replug USB). Confirm it boots cleanly — the onboard LED gives a brief
   flicker every ~3 s (WAIT_CONFIG on Serial1/GPIO2).
3. Reconnect GPIO2→D00 **after** boot (UART TX is static-level; hot-plugging after boot is
   safe), or connect it before a clean power-up and avoid disturbing it while cycling power.
4. Only if a clean re-power with the wire off still crashes — then reflash (download mode:
   hold GPIO0/"0" pin to GND + RST, then
   `arduino-cli upload -p COM15 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 d1_mini_relay`).
   This is rare; try the strap fix first.

## 4. SD config parses, CONFIG sent, but D1 mini never gets WiFi credentials

**Symptom:** COM6 shows `SD config OK: ssid=... server=http://...`. `CONFIG sent to D1 mini`
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

## 8. D1 brownout — solid-on LED, zero `RX D1` frames (Spresense rail or weak USB)

**Symptom:** the D1 onboard blue LED is **solid on** (GPIO2 stuck LOW = the ESP8266 never reaches
`loop()`), and COM6 shows CONFIG being sent/resending but **zero** `RX D1` lines (not even
`WAIT_CONFIG`). The Spresense eventually logs
`CONFIG: no IP= from D1 after max resends; giving up (check D1-TX->Spresense-RX wire)`.

**Root cause: power, not the UART link.** The ESP8266 WiFi-TX bursts pull ~350–500 mA. If the
supply sags, the chip brownout-resets before it ever sends a frame. Two confirmed triggers:

- **Spresense 5 V or 3 V3 rail → D1** — the Spresense board's own power path to the extension
  header cannot supply the peaks (both rails brownout identically; a stiffer upstream USB port
  does not help — the limiter is the Spresense board, not the port).
- **A too-weak external USB** — weak charger, thin/high-resistance cable, or unpowered/overloaded
  hub → same solid-on-LED brownout.

**Diagnostic to separate power from the GPIO2 strap:** unplug the GPIO2→D00 wire and re-power
the D1. A brief flicker every ~3 s → it was the strap (§3); **still solid on** → it is power
(the supply cannot boot the D1 at all).

**Fix:** power the D1 from a **≥1 A USB wall charger** (short, thick cable) **or a PC USB port**
(both verified to run the full relay end-to-end). Never from the Spresense rail. Keep an explicit
GND wire to the Spresense.

## 9. Spresense "giving up" — CONFIG campaign does not auto-restart; tap RESET

**Symptom:** COM6 shows `CONFIG: no IP= from D1 after max resends; giving up …` and then stops
sending CONFIG. The D1 comes back later (e.g. after a power swap) and sends
`BOOT_WAITING_CONFIG`/`WAIT_CONFIG`, but the Spresense still sends no CONFIG — the relay never
reconnects.

**Root cause (FW design):** `spresense_relay.ino` resends CONFIG every 5 s up to a bounded max,
then sets `configGivenUp = true` and stops. The campaign **restarts only on `WiFi_FAIL` or
`WIFI_LOST` from the D1** — NOT on `WAIT_CONFIG`/`BOOT_WAITING_CONFIG` (intentional, so a missing
feedback wire cannot thrash the D1's WiFi forever). So once it has given up, the D1 booting alone
will not retrigger CONFIG.

**Fix:** **tap the Spresense RESET button** — `setup()` re-runs, sends CONFIG once and starts a
fresh resend campaign. With the D1 on solid power, expect `CONFIG sent` → `RX D1 … IP=…` →
`D1 ONLINE (CONFIG confirmed)` → `ACK:hello` flowing.

## See also

- [wiring.md](wiring.md) — pinout and power notes
- [architecture.md](architecture.md) — data flow and frame protocol
- [protocol.md](../protocol.md) — frame format