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
| `d1_mini_relay/` | Sketch for the D1 mini — WiFi client + serial relay |
| `spresense_relay/` | Sketch for the Spresense — SD config reader, Serial2, OTA client |
| `lib/relay_proto/` | Shared library — framing protocol (crc8, parser, framing) |
| `config.example.json` | Config template (no real credentials) |
| `protocol.md` | Serial2 frame protocol specification |
| `docs/architecture.md` | Block diagram, data flows |
| `docs/wiring.md` | D1 mini ↔ Spresense pinout, JP1, power |
| `docs/ota-mechanics.md` | `fwup_client` API, `package.bin` format, A/B swap |
| `docs/troubleshooting.md` | Known issues (UART0 conflict, CP210x glitch) |

## Hardware

- SONY Spresense: main board + extension board, bootloader v3.4.3, Arduino core 3.4.7.
- LOLIN D1 mini (ESP8266, 4 MB flash, onboard CH340).
- Micro-SD card in the extension board (for `config.json` and OTA `package.bin`).

Wiring details in [`docs/wiring.md`](docs/wiring.md). In short:

| D1 mini | Spresense extension | Note |
|---|---|---|
| TX (GPIO1) | RX = D00 (UART2_RXD) | D1→Spresense |
| RX (GPIO3) | TX = D01 (UART2_TXD) | Spresense→D1 |
| GND | GND | common |
| 5V (own USB) | — | powered from its own USB, not from the Spresense |

- **JP1 = 3.3V** on the extension board.
- **Before flashing the D1 mini over USB**: disconnect the TX/RX wires to the Spresense
  (UART0 is shared with the onboard CH340).

## Build & flash

Details in [`docs/`](docs/). Requires [arduino-cli](https://github.com/arduino/arduino-cli) 1.5.1+.

**D1 mini** (onboard CH340 COM port, wires to Spresense disconnected):
```bash
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
arduino-cli compile --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 --library lib/relay_proto --output-dir build_d1 d1_mini_relay
arduino-cli upload -p COM15 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 d1_mini_relay
```

**Spresense** (COM6 = built-in CP210x):
```bash
arduino-cli compile --fqbn SPRESENSE:spresense:spresense --library lib/relay_proto --output-dir build_spresense spresense_relay
arduino-cli upload -p COM6 --fqbn SPRESENSE:spresense:spresense spresense_relay
# fallback (local tool, not in repo): python flash_spk.py -c COM6 build_spresense/spresense_relay.ino.spk
```

## Configuration (secrets kept out of the repo)

1. Copy `config.example.json` to `config.json` (locally, gitignored).
2. Fill in the real SSID/password/server URL.
3. Put `config.json` on the **SD card** and insert it into the Spresense extension board.
4. Never commit `config.json` — it is in `.gitignore`. A staging check runs before every push.

## Project status

- [x] Phase 1 — repository and documentation
- [x] Phase 2 — D1 mini MVP (WiFi + relay) — builds OK, flashed on COM15 (ESP8266EX 0x001b3fb7), framing/CRC verified by decoding the `BOOT_WAITING_CONFIG`/`WAIT_CONFIG` frames
- [x] Phase 3 — Spresense MVP (SD config + Serial2) — builds OK (166 KB spk); flashed on COM6 via arduino-cli (validation OK); boots, reads SD (`config.json not found` until the SD has one)
- [ ] Phase 4 — end-to-end verification
- [ ] Phase 5 — OTA pipeline (`fwup_client`)

## License

MIT — see [`LICENSE`](LICENSE).