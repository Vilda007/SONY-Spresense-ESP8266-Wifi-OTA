[English](README.md) | [Čeština](README.cs.md)

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
| `d1_mini_relay/` | **Produkční** sketch D1 mini — WiFi client + sériový relay (status TX na GPIO2/Serial1) |
| `spresense_relay/` | **Produkční** sketch Spresense — čtení SD config, Serial2, CONFIG-resend, OTA klient |
| `lib/relay_proto/` | Sdílená knihovna — rámcový protokol (crc8, parser, framing) |
| `config.example.json` | Šablona konfigurace (bez reálných hesel) |
| `protocol.md` | Specifikace rámcového protokolu Serial2 |
| `docs/architecture.md` | Blokové schéma, datové toky |
| `docs/wiring.md` | Pinout D1 mini ↔ Spresense, JP1, napájení, GPIO2-TX / CH340-clamp |
| `docs/troubleshooting.md` | Známé problémy (UART2 pinmux, GPIO1 CH340-clamp, brownout, giving-up) |
| `docs/troubleshooting.cs.md` | Známé problémy v češtině |
| `tools/test_server.py` | Minimální HTTP echo server pro end-to-end verifikaci |
| `tools/monitor_spresense.py` | Live COM6 monitor (auto-reconnect, redaktuje CONFIG tajemství) |
| `tools/d1_config_via_com.py` | Injekce CONFIG rámce do D1 přes COM15 (izolovaný WiFi test bez Spresense) |
| `tools/gen_test_creds.py` | Vygeneruje git-ignored `test_creds.h` z `~/.d1_test_pass` |
| `tools/monitor_ports.py` | Výpis/identifikace COM portů podle VID/PID |
| `tools/read_com.py` | Zachycení text+hex dumpu COM portu na N sekund |
| `tools/send_config.py` | Injekce CONFIG rámce do D1 mini přes USB (legacy izolovaný test) |
| `tools/flash_d1_test.ps1` | Build & flash `d1_mini_relay` s `-DTEST_DIRECT_WIFI` (izolovaný WiFi test) |
| `tools/flash_*.ps1` | Helper skripty pro flash diagnostických sketchů (`flash_d1_diag`, `flash_smoke`, `flash_wifi_test`, `flash_d1_recovery`) |
| `d1_tx_gpio2/`, `d1_heartbeat/`, `d1_diag/`, `d1_smoke/`, `d1_wifi_test/` | Diagnostické sketchy D1 mini (důkaz GPIO2 TX, heartbeat, WiFi smoke, atd.) |
| `spresense_uart2_dumper/` | Spresense UART2 RX dumper (k důkazu GPIO2-TX linky) |

> Soubory s tajemstvím (`config.json`, `**/test_creds.h`, `d1_diag/test_ssid_pass.h`, build
> artefakty, logy) jsou git-ignored — viz [`.gitignore`](.gitignore). Nikdy je necommituj.

## Hardware

- SONY Spresense: main board + extension board, bootloader v3.4.3, Arduino core 3.4.7.
- LOLIN D1 mini (ESP8266EX, 4 MB flash, onboard CH340).
- SD karta do extension boardu (pro `config.json` a OTA `package.bin`).

Wiring viz [`docs/wiring.md`](docs/wiring.md). Zkráceně:

| D1 mini (silkscreen) | GPIO | Spresense extension | Směr | Pozn. |
|---|---|---|---|---|
| **2** | GPIO2 (Serial1 TX) | RX = **D00** (UART2_RXD) | D1 → Spresense | status/data rámce |
| **rx** | GPIO3 (UART0 RX) | TX = **D01** (UART2_TXD) | Spresense → D1 | CONFIG / DATA_UP |
| GND | — | GND | společný | explicitní vodič (USB GND samotné nestačí) |
| vbus | — | — | vlastní USB | ≥1 A nabíječka nebo PC USB (NE lišta Spresense) |

- **JP1 = 3.3V** na extension boardu.
- **Proč GPIO2 pro TX?** GPIO1 (UART0 TX) je trvale spojený s onboard CH340 a je sešlápnutý na
  ~0,7–1,8 V swing v obou stavech USB (i zapnutém, i vypnutém) — pod D00 VIH Spresense, takže
  Spresense rámec nikdy nevidí. GPIO2/Serial1 není sdílený s CH340 → čistý swing. Viz
  [`docs/wiring.md`](docs/wiring.md) a [`docs/troubleshooting.cs.md`](docs/troubleshooting.cs.md) §2.
- **Napájení**: ≥1 A USB nabíječka **nebo** PC USB port (obě varianty ověřeny end-to-end). **Nikdy**
  nepovájej D1 z 5 V/3 V3 lišty Spresense — WiFi-TX špičky (~350–500 mA) způsobí brownout
  napájecí cesty desky Spresense.
- **Před flashem obou desek přes USB**: odpoj TX/RX vodiče mezi D1 a Spresense.

## Build & flash

Podrobnosti v [`docs/`](docs/). Vyžaduje [arduino-cli](https://github.com/arduino/arduino-cli) 1.5.1+.

**D1 mini** (COM port onboard CH340, vodiče ke Spresense odpojeny):
```bash
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
arduino-cli compile --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 --library lib/relay_proto --output-dir build/relay d1_mini_relay
arduino-cli upload -p COM15 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 --input-dir build/relay d1_mini_relay
```

**Spresense** (COM6 = vestavěný CP210x):
```bash
arduino-cli compile --fqbn SPRESENSE:spresense:spresense --library lib/relay_proto --output-dir build/spresense_relay spresense_relay
arduino-cli upload -p COM6 --fqbn SPRESENSE:spresense:spresense spresense_relay
# fallback (lokální nástroj, není v repu): python flash_spk.py -c COM6 build/spresense_relay/spresense_relay.ino.spk
```

## Konfigurace (tajemství mimo repozitář)

1. Zkopíruj `config.example.json` jako `config.json` (lokálně, gitignored).
2. Vyplň reálné SSID/heslo/server URL.
3. Nahraj `config.json` na **SD kartu** a vlož ji do Spresense extension boardu.
4. `config.json` nikdy necommituj — je v `.gitignore`. Před každým push je v plánu kontrola stagingu.

## Stav projektu

- [x] Fáze 1 — repozitář a dokumentace
- [x] Fáze 2 — D1 mini MVP (WiFi + relay) — build OK, flash na COM15 (ESP8266EX, 4 MB), framing/CRC ověřeno dekódováním rámce `BOOT_WAITING_CONFIG`/`WAIT_CONFIG`
- [x] Fáze 3 — Spresense MVP (SD config + Serial2) — build OK (167 KB spk); flash na COM6 přes arduino-cli (validation OK); boot, čtení SD, odeslání CONFIG/DATA_UP
- [x] **Fáze 4 — relay linka ověřena end-to-end (2026-08-21)** — provedly se tři opravy:
  (1) UART2 pinmux — `cxd56_pin_config(PINCONF_UART2_TXD/RXD)` před `Serial2.begin()` (Arduino core 3.4.7 to nekonfiguruje; viz [troubleshooting.cs.md](docs/troubleshooting.cs.md) §1);
  (2) status TX D1 přesunuta na **GPIO2/Serial1** — GPIO1/UART0-TX je trvale CH340-clampován na 0,7–1,8 V a nepoužitelný (viz §2);
  (3) Spresense **CONFIG-resend** kampaň — resenduje CONFIG co 5 s dokud nedorazí `IP=` od D1, ohraničené maximum, restart na `WiFi_FAIL`/`WIFI_LOST` (viz §9).
  Ověřeno přes **skutečnou UART linku Spresense**: `CONFIG sent` → D1 se připojí k WiFi → `RX D1 … IP=192.168.1.171` → `D1 ONLINE (CONFIG confirmed)` → `ACK:hello` round-trip (DATA_UP → HTTP POST → `test_server.py` → DATA_DOWN) + PING/PONG. Obě UART směry + WiFi + HTTP relay fungují.
  **Otevřená HW položka**: D1 musí být napájeno z ≥1 A USB nabíječky (nebo PC USB) — lišta Spresense i příliš slabé externí USB způsobí brownout ESP8266 (trvale svítící LED, nula rámců; viz [troubleshooting.cs.md](docs/troubleshooting.cs.md) §8). Po brownoutu je potřeba **resetovat Spresense**, aby se restartovala CONFIG kampaň (§9).
- [ ] Fáze 5 — OTA pipeline (`fwup_client`)

## Licence

MIT — viz [`LICENSE`](LICENSE).