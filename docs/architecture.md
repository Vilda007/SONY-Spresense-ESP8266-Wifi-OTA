# Architektura

## Cíl

Dat desce SONY Spresense (bez vestavěného WiFi) možnost komunikovat po síti a aktualizovat firmware na dálku, přičemž **WiFi hesla a citlivý config neopustí fyzické zařízení** (nejsou v repozitáři ani ve firmware D1 mini).

## Blokové schéma

```
                         HTTP GET/POST
   ┌──────────────┐  ◄──────────────────►  ┌──────────────────┐
   │  WiFi AP /   │                        │  LOLIN D1 mini   │
   │  server      │                        │  (ESP8266)        │
   │  (+OTA host) │                        │  WiFi STA+client  │
   └──────────────┘                        │  + relay          │
                                           └─────────┬────────┘
                                                     │ Serial (UART0, 115200)
                                                     │ rámcový protokol
                                                     ▼
   ┌─────────────────────────────────────────────────────────────┐
   │  SONY Spresense (CXD5602)                                    │
   │   • Serial  (UART1) ──► CP210x ── COM6 (konzole/log)         │
   │   • Serial2 (UART2)  ◄──► D1 mini (PIN_D01 TX / PIN_D00 RX)  │
   │   • SD karta: config.json (tajemství) + package.bin (OTA)    │
   │   • fwup_client (v libapps.a) → A/B dual-bank swap v SBL     │
   └─────────────────────────────────────────────────────────────┘
```

## Datové toky

1. **Boot**: Spresense načte `config.json` z SD → pošle `CONFIG` rámec (ssid/pass/url) D1 mince přes Serial2. D1 mini se připojí k WiFi. Žádný hardcoded secret nikde ve firmware.
2. **Telemetrie (DATA_UP)**: Spresense sbírá sensor data → `DATA_UP` rámec → D1 mini HTTP POST na `server_url` → HTTP odpověď → `DATA_DOWN` rámec zpět na Spresense.
3. **Příkaz ze serveru (DATA_DOWN)**: server může poslat příkaz v HTTP odpovědi, D1 mini ho relayne na Spresense.
4. **OTA (fáze 5)**: D1 mini zkontroluje manifest na `ota_manifest_url` → při nové verzi stáhne `package.bin` z `ota_url` → po `OTA_CHUNK` streampuje na Spresense přes Serial2 → Spresense zavolá `fwup->download(FW_APP,...)` + `fwup->update()` → reboot → SBL atomicky prohodí A/B banky. Při neplatném image SBL vrátí předchozí verzi (recovery).

## Proč D1 mini jako relay (a ne WiFi shield addon)

- Spresense WiFi addony (iS110B) existují, ale D1 mini je levnější, běžně dostupný a programovatelný přes Arduino CLI nezávisle na Spresense.
- Relay architektura odděluje WiFi stack (na ESP8266) od aplikace (na Spresense) — Spresense FW zůstává jednoduchý, síťová logika je v D1 mini.
- D1 mini lze použít i jako WiFi→sériový most pro cokoli jiného.

## Proč config na SD kartě Spresense

- **Jediný zdroj tajemství** — SSID, heslo, server URL, OTA URL na jednom místě.
- SD karta je **fyzicky snímatelná** — tajemství odchází s kartou, ne s deskou.
- `config.json` je v `.gitignore`; v repu je jen `config.example.json` bez reálných hodnot.
- D1 mini FW nemá žádné tajemství — dostane je až za běhu v `CONFIG` rámci.

## Související

- Wiring: [wiring.md](wiring.md)
- Protokol Serial2: [../protocol.md](../protocol.md)
- OTA mechanika: [ota-mechanics.md](ota-mechanics.md)
- Řešení problémů: [troubleshooting.md](troubleshooting.md)