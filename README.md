# SONY Spresense + ESP8266 (D1 mini) WiFi relay with OTA

[English](README.md) | [Čeština](README.cs.md)

Adding WiFi connectivity and over-the-air firmware updates to the **SONY Spresense**
development board, which **has no built-in WiFi/BLE/cellular** in its standard form — only
GNSS (receive-only). A **LOLIN D1 mini (ESP8266)** acts as the WiFi client and relay, connected
via UART2 (`Serial2`). OTA updates of the Spresense application are handled by the SDK
`fwuputils` module (A/B dual-bank swap in the bootloader).

```
[WiFi AP / server]  ←HTTP→  [D1 mini (ESP8266)]  ←Serial2/UART2→  [Spresense]
                       WiFi client + relay        PIN_D00/D01       SD config + fwup OTA
```

## Why this design

- The Spresense has no WiFi — an external module is mandatory.
- The D1 mini is cheap, widely available, and programmable via the Arduino CLI.
- WiFi credentials and other sensitive config stay **out of this repository**: the only
  source is `config.json` on the **Spresense SD card** (physically removable, gitignored).
  The repo ships only the template [`config.example.json`](config.example.json).
- The D1 mini firmware contains **no hardcoded credentials** — it receives WiFi creds at
  runtime from the Spresense in a `CONFIG` frame over Serial2.

## Repository contents

| Path | Description |
|---|---|
| `d1_mini_relay/` | **Production** D1 mini sketch — WiFi client + serial relay (status TX on GPIO2/Serial1) |
| `spresense_relay/` | **Production** Spresense sketch — SD config reader, Serial2, CONFIG-resend, OTA client |
| `lib/relay_proto/` | Shared library — framing protocol (crc8, parser, framing) |
| `config.example.json` | Config template (no real credentials) |
| `protocol.md` | Serial2 frame protocol specification |
| `docs/architecture.md` | Block diagram, data flows |
| `docs/wiring.md` | D1 mini ↔ Spresense pinout, JP1, power, GPIO2-TX / CH340-clamp |
| `docs/troubleshooting.md` | Known issues (UART2 pinmux, GPIO1 CH340-clamp, brownout, give-up) |
| `docs/troubleshooting.cs.md` | To samé v češtině |
| `tools/test_server.py` | Minimal HTTP echo server for end-to-end verification |
| `tools/monitor_spresense.py` | Live COM6 monitor (auto-reconnect, redacts CONFIG secrets) |
| `tools/d1_config_via_com.py` | Inject a CONFIG frame into the D1 over COM15 (isolated WiFi test, no Spresense) |
| `tools/gen_test_creds.py` | Generate git-ignored `test_creds.h` from `~/.d1_test_pass` |
| `tools/monitor_ports.py` | List/identify COM ports by VID/PID |
| `tools/read_com.py` | Capture text+hex dump of a COM port for N seconds |
| `tools/send_config.py` | Inject a CONFIG frame into the D1 mini over its USB (legacy isolated test) |
| `tools/flash_d1_test.ps1` | Build & flash `d1_mini_relay` with `-DTEST_DIRECT_WIFI` (isolated WiFi test) |
| `tools/flash_*.ps1` | Helper scripts to flash the diagnostic sketches (`flash_d1_diag`, `flash_smoke`, `flash_wifi_test`, `flash_d1_recovery`) |
| `d1_tx_gpio2/`, `d1_heartbeat/`, `d1_diag/`, `d1_smoke/`, `d1_wifi_test/` | D1 mini diagnostic sketches (GPIO2 TX proof, heartbeat, WiFi smoke, etc.) |
| `spresense_uart2_dumper/` | Spresense UART2 RX dumper (used to prove the GPIO2-TX link) |

> Secret/credential files (`config.json`, `**/test_creds.h`, `d1_diag/test_ssid_pass.h`, build
> artifacts, logs) are git-ignored — see [`.gitignore`](.gitignore). Never commit them.

## Hardware

- SONY Spresense: main board + extension board, bootloader v3.4.3, Arduino core 3.4.7.
- LOLIN D1 mini (ESP8266EX, 4 MB flash, onboard CH340).
- Micro-SD card in the extension board (for `config.json` and OTA `package.bin`).

Wiring details in [`docs/wiring.md`](docs/wiring.md). In short:

| D1 mini (silkscreen) | GPIO | Spresense extension | Direction | Note |
|---|---|---|---|---|
| **2** | GPIO2 (Serial1 TX) | RX = **D00** (UART2_RXD) | D1 → Spresense | status/data frames |
| **rx** | GPIO3 (UART0 RX) | TX = **D01** (UART2_TXD) | Spresense → D1 | CONFIG / DATA_UP |
| GND | — | GND | common | explicit wire (USB GND alone is NOT enough) |
| vbus | — | — | own USB | ≥1 A charger or PC USB (NOT the Spresense rail) |

- **JP1 = 3.3V** on the extension board.
- **Why GPIO2 for TX?** GPIO1 (UART0 TX) is permanently tied to the onboard CH340 and is clamped
  to a ~0.7–1.8 V swing in both USB-on and USB-off states — below the Spresense D00 VIH, so the
  Spresense never sees the frame. GPIO2/Serial1 is not CH340-shared → clean swing. See
  [`docs/wiring.md`](docs/wiring.md) and [`docs/troubleshooting.md`](docs/troubleshooting.md) §2.
- **Power**: ≥1 A USB wall charger **or** a PC USB port (both verified end-to-end). **Never**
  power the D1 from the Spresense 5 V/3 V3 rail — WiFi-TX peaks (~350–500 mA) brownout the
  Spresense board's power path.
- **Before flashing either board over USB**: disconnect the D1↔Spresense TX/RX wires.

## Build & flash

Details in [`docs/`](docs/). Requires [arduino-cli](https://github.com/arduino/arduino-cli) 1.5.1+.

**D1 mini** (onboard CH340 COM port, wires to Spresense disconnected):
```bash
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
arduino-cli compile --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 --library lib/relay_proto --output-dir build/relay d1_mini_relay
arduino-cli upload -p COM15 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 --input-dir build/relay d1_mini_relay
```

**Spresense** (COM6 = built-in CP210x):
```bash
arduino-cli compile --fqbn SPRESENSE:spresense:spresense --library lib/relay_proto --output-dir build/spresense_relay spresense_relay
arduino-cli upload -p COM6 --fqbn SPRESENSE:spresense:spresense spresense_relay
# fallback (local tool, not in repo): python flash_spk.py -c COM6 build/spresense_relay/spresense_relay.ino.spk
```

## Configuration (secrets kept out of the repo)

1. Copy `config.example.json` to `config.json` (locally, gitignored).
2. Fill in the real SSID/password/server URL.
3. Put `config.json` on the **SD card** and insert it into the Spresense extension board.
4. Never commit `config.json` — it is in `.gitignore`. A staging check runs before every push.

## Project status

- [x] Phase 1 — repository and documentation
- [x] Phase 2 — D1 mini MVP (WiFi + relay) — builds OK, flashed on COM15 (ESP8266EX, 4 MB), framing/CRC verified by decoding the `BOOT_WAITING_CONFIG`/`WAIT_CONFIG` frames
- [x] Phase 3 — Spresense MVP (SD config + Serial2) — builds OK (167 KB spk); flashed on COM6 via arduino-cli (validation OK); boots, reads SD, sends CONFIG/DATA_UP
- [x] **Phase 4 — relay link verified end-to-end (2026-08-21)** — three fixes landed:
  (1) UART2 pinmux — `cxd56_pin_config(PINCONF_UART2_TXD/RXD)` before `Serial2.begin()` (Arduino core 3.4.7 does not configure it; see [troubleshooting.md](docs/troubleshooting.md) §1);
  (2) D1 status TX moved to **GPIO2/Serial1** — GPIO1/UART0-TX is permanently CH340-clamped to 0.7–1.8 V and unusable (see §2);
  (3) Spresense **CONFIG-resend** campaign — resends CONFIG every 5 s until the D1's `IP=` arrives, bounded max, restarts on `WiFi_FAIL`/`WIFI_LOST` (see §9).
  Verified over the **real Spresense UART link**: `CONFIG sent` → D1 joins WiFi → `RX D1 … IP=192.168.1.171` → `D1 ONLINE (CONFIG confirmed)` → `ACK:hello` round-trip (DATA_UP → HTTP POST → `test_server.py` → DATA_DOWN) + PING/PONG. Both UART directions + WiFi + HTTP relay all good.
  **Open hardware item**: the D1 must be powered from a ≥1 A USB charger (or PC USB) — the Spresense rail and a too-weak external USB both brownout the ESP8266 (solid-on LED, zero frames; see [troubleshooting.md](docs/troubleshooting.md) §8). After a brownout the Spresense must be **reset** to restart the CONFIG campaign (§9).
- [ ] Phase 5 — OTA pipeline (`fwup_client`)

## License

MIT — see [`LICENSE`](LICENSE).