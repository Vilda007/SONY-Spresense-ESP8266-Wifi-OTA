# SONY Spresense + ESP8266 (D1 mini) WiFi relay s OTA

Přidání WiFi konektivity a OTA aktualizací firmwaru na vývojovou desku **SONY Spresense**, která ve standardu **nemá vestavěné WiFi/BLE/cellular** — pouze GNSS (receive-only). Jako WiFi klient a relay slouží **LOLIN D1 mini (ESP8266)**, připojený přes UART2 (`Serial2`). OTA aktualizace aplikace na Spresense řeší SDK modul `fwuputils` (A/B dual-bank swap v bootloaderu).

```
[WiFi AP / server]  ←HTTP→  [D1 mini (ESP8266)]  ←Serial2/UART2→  [Spresense]
                       WiFi client + relay        PIN_D00/D01       SD config + fwup OTA
```

## Proč takto

- Spresense nemá WiFi — přidat externí modul je nutnost.
- D1 mini je levný, běžně dostupný, programovatelný přes Arduino CLI.
- WiFi hesla a citlivý config zůstávají **mimo tento repozitář** — jediný zdroj je `config.json` na **SD kartě Spresense** (fyzicky snímatelné, gitignored). V repu je jen šablona [`config.example.json`](config.example.json).
- D1 mini firmware **neobsahuje žádné hardcoded heslo** — WiFi credentials dostane při bootu od Spresense v `CONFIG` rámci přes Serial2.

## Obsah repozitáře

| Cesta | Popis |
|---|---|
| `d1_mini/` | Sketch pro D1 mini — WiFi client + sériový relay |
| `spresense_app/` | Sketch pro Spresense — čtení SD config, Serial2, OTA klient |
| `config.example.json` | Šablona konfigurace (bez reálných hesel) |
| `protocol.md` | Specifikace rámcového protokolu Serial2 |
| `docs/architecture.md` | Blokové schéma, datové toky |
| `docs/wiring.md` | Pinout D1 mini ↔ Spresense, JP1, napájení |
| `docs/ota-mechanics.md` | `fwup_client` API, formát `package.bin`, A/B swap |
| `docs/troubleshooting.md` | Známé problémy (UART0 konflikt, CP210x glitch) |

## Hardware

- SONY Spresense: main board + extension board, bootloader v3.4.3, Arduino core 3.4.7.
- LOLIN D1 mini (ESP8266, 4 MB flash, onboard CH340).
- SD karta do extension boardu (pro `config.json` a OTA `package.bin`).

Wiring viz [`docs/wiring.md`](docs/wiring.md). Zkráceně:

| D1 mini | Spresense extension | Pozn. |
|---|---|---|
| TX (GPIO1) | RX = D00 (UART2_RXD) | D1→Spresense |
| RX (GPIO3) | TX = D01 (UART2_TXD) | Spresense→D1 |
| GND | GND | společný |
| 5V (vlastní USB) | — | napájení z vlastního USB, ne ze Spresense |

- **JP1 = 3.3V** na extension boardu.
- **Před flashem D1 mini přes USB**: odpojit TX/RX vodiče ke Spresense (UART0 sdílen s onboard CH340).

## Build & flash

Podrobnosti v [`docs/`](docs/). Vyžaduje [arduino-cli](https://github.com/arduino/arduino-cli) 1.5.1+.

**D1 mini** (COM port onboard CH340, vodiče ke Spresense odpojeny):
```bash
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
arduino-cli compile --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 d1_mini
arduino-cli upload -p COM21 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 d1_mini
```

**Spresense** (COM6 = vestavěný CP210x):
```bash
arduino-cli compile --fqbn SPRESENSE:spresense:spresense --output-dir build spresense_app
arduino-cli upload -p COM6 --fqbn SPRESENSE:spresense:spresense spresense_app
```

## Konfigurace (tajemství mimo repozitář)

1. Zkopíruj `config.example.json` jako `config.json` (lokálně, gitignored).
2. Vyplň reálné SSID/heslo/server URL.
3. Nahraj `config.json` na **SD kartu** a vlož ji do Spresense extension boardu.
4. `config.json` nikdy necommituj — je v `.gitignore`. Před každým push je v plánu kontrola stagingu.

## Stav projektu

- [x] Fáze 1 — repozitář a dokumentace
- [ ] Fáze 2 — D1 mini MVP (WiFi + relay)
- [ ] Fáze 3 — Spresense MVP (SD config + Serial2)
- [ ] Fáze 4 — end-to-end verifikace
- [ ] Fáze 5 — OTA pipeline (`fwup_client`)

## Licence

MIT — viz [`LICENSE`](LICENSE).