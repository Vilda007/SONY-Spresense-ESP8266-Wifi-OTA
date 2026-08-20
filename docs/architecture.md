# Architecture

## Goal

Give the SONY Spresense board (which has no built-in WiFi) the ability to communicate over the
network and to update its firmware remotely, while keeping **WiFi credentials and sensitive
config confined to the physical device** — out of the repository and out of the D1 mini firmware.

## Block diagram

```
                         HTTP GET/POST
   ┌──────────────┐  ◄──────────────────►  ┌──────────────────┐
   │  WiFi AP /   │                        │  LOLIN D1 mini   │
   │  server      │                        │  (ESP8266)        │
   │  (+OTA host) │                        │  WiFi STA+client  │
   └──────────────┘                        │  + relay          │
                                           └─────────┬────────┘
                                                     │ Serial (UART0, 115200)
                                                     │ framed protocol
                                                     ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  SONY Spresense (CXD5602)                                    │
   │   • Serial  (UART1) ──► CP210x ── COM6 (console/log)         │
   │   • Serial2 (UART2)  ◄──► D1 mini (PIN_D01 TX / PIN_D00 RX)  │
   │   • SD card: config.json (secrets) + package.bin (OTA)       │
   │   • fwup_client (in libapps.a) → A/B dual-bank swap in SBL   │
   └─────────────────────────────────────────────────────────────┘
```

## Data flows

1. **Boot**: the Spresense reads `config.json` from SD → sends a `CONFIG` frame (ssid/pass/url)
   to the D1 mini over Serial2. The D1 mini joins WiFi. No hardcoded secret exists anywhere in firmware.
2. **Telemetry (DATA_UP)**: the Spresense collects sensor data → `DATA_UP` frame → the D1 mini
   HTTP-POSTs it to `server_url` → HTTP response → `DATA_DOWN` frame back to the Spresense.
3. **Server command (DATA_DOWN)**: the server can embed a command in the HTTP response; the D1 mini
   relays it to the Spresense.
4. **OTA (phase 5)**: the D1 mini checks a manifest at `ota_manifest_url` → on a new version it
   downloads `package.bin` from `ota_url` → streams it to the Spresense in `OTA_CHUNK` frames over
   Serial2 → the Spresense calls `fwup->download(FW_APP,...)` + `fwup->update()` → reboot → the SBL
   atomically swaps the A/B banks. On an invalid image the SBL rolls back to the previous version
   (recovery).

## Why a D1 mini relay (and not a WiFi shield add-on)

- Spresense WiFi add-ons (iS110B) exist, but the D1 mini is cheaper, widely available, and
  programmable via the Arduino CLI independently of the Spresense.
- The relay architecture separates the WiFi stack (on the ESP8266) from the application (on the
  Spresense) — the Spresense firmware stays simple; networking lives in the D1 mini.
- The D1 mini can also serve as a generic WiFi↔serial bridge for anything else.

## Why config on the Spresense SD card

- **Single source of secrets** — SSID, password, server URL, OTA URL in one place.
- The SD card is **physically removable** — secrets leave with the card, not the board.
- `config.json` is in `.gitignore`; the repo ships only `config.example.json` with placeholder values.
- The D1 mini firmware holds no secret — it only receives one at runtime in a `CONFIG` frame.

## See also

- Wiring: [wiring.md](wiring.md)
- Serial2 protocol: [../protocol.md](../protocol.md)
- OTA mechanics: [ota-mechanics.md](ota-mechanics.md)
- Troubleshooting: [troubleshooting.md](troubleshooting.md)