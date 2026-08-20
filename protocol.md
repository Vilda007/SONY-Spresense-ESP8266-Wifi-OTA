# Rámcový protokol Serial2

Obousměrný binární protokol mezi D1 mini a Spresense přes UART2 (Spresense `Serial2`) / UART0 (D1 mini `Serial`). Baud **115200, 8N1**. Žádný hardware flow control.

## Formát rámce

```
┌──────┬──────────┬──────────┬────────┬─────────────────┬──────┬──────┐
│ 0xAA │ len_hi   │ len_lo   │ type   │ payload[len]     │ crc8 │ 0x55 │
└──────┴──────────┴──────────┴────────┴─────────────────┴──────┴──────┘
   start   payload length BE     1 B      len bytes         1 B    end
```

- `0xAA` start marker, `0x55` end marker (resync po šumu).
- `len` = počet bajtů payloadu (16-bit big-endian), **bez** hlavičky a bez crc/end.
- `type` = 1 bajt (viz níže).
- `crc8` = CRC8 (poly 0x07, init 0x00) přes `type` + `payload` (NE start/end/len).
- Maximální payload pro OTA chunk: 512 B (respektuje XMODEM/UART buffer); ostatní rámce krátké.

> Byte-stuffing: pokud payload nebo crc obsahuje `0xAA`/`0x55`, je to v pořádku — parser se řídí délkou, ne markery. Start marker slouží jen k počáteční synchronizaci.

## Typy rámců

| Typ | Kód | Směr | Payload | Význam |
|---|---|---|---|---|
| `CONFIG` | 0x01 | Spresense → D1 | JSON: `{"ssid":"...","pass":"...","server_url":"...","ota_url":"...","ota_manifest_url":"...","poll_ms":N}` | WiFi creds + endpointy. D1 mini po přijetí provede `WiFi.begin`. |
| `DATA_UP` | 0x02 | Spresense → D1 | JSON sensor data / text | Telemetrie k odeslání na `server_url` (HTTP POST). |
| `DATA_DOWN` | 0x03 | D1 → Spresense | HTTP odpověď / příkaz ze serveru | Výsledek relaye, příkaz pro aplikaci. |
| `OTA_AVAIL` | 0x04 | D1 → Spresense | JSON: `{"version":"x.y","size":N,"sha256":"..."}` | Oznámení dostupné aktualizace. Spresense odpoví `OTA_ACK` (accept/reject). |
| `OTA_ACK` | 0x05 | Spresense → D1 | 1 B: `0x01`=accept/continue, `0x00`=reject/stop | Řízení streamu OTA. |
| `OTA_CHUNK` | 0x06 | D1 → Spresense | 4 B offset (LE) + data (zbytek) | Blok `package.bin`. Spresense potvrzuje `OTA_ACK` po každém chunku. |
| `OTA_DONE` | 0x07 | D1 → Spresense | SHA256 (32 B) nebo prázdné | Konec streamu; Spresense verifikuje a aplikuje přes `fwup_client`. |
| `STATUS` | 0x10 | obě strany | text (zpráva/stav) | Informační (IP adresa D1 mini, verze FW, chyby). |
| `PING` / `PONG` | 0xFE / 0xFF | obě strany | prázdné | Keepalive. |

## Řídící pravidla

- **Non-blokovací parser**: obě strany v `loop()` akumulují bajty do stavového automatu (WAIT_START → READ_LEN → READ_TYPE → READ_PAYLOAD → READ_CRC → READ_END). Žádné `readString()`/`while(Serial.available()==0)` blokování.
- **CONFIG**: Spresense odesílá při bootu (a znovu po přijetí PING bez předchozího CONFIG). D1 mini do timeoutu (5 s) čeká na CONFIG; při neúspěchu vypíše `STATUS` chybu a zkusí znovu.
- **DATA_UP/OTA**: D1 mini pošle HTTP, čeká na odpověď (s timeoutem), výsledek v `DATA_DOWN`. Pri retry backoff.
- **OTA stream**: D1 mini → `OTA_AVAIL` → Spresense `OTA_ACK(accept)` → D1 mini `OTA_CHUNK` → Spresense `OTA_ACK(continue)` … → `OTA_DONE` → Spresense verifikuje → `fwup->download+update` → reboot. Pri reject/timeout D1 mini stream zastaví.
- **Keepalive**: PING každých 10 s; při 3 po sobě jdoucích nezodpovězených PING strana považuje linku za mrtvou (D1 mini: reconnect WiFi + čeká na nový CONFIG).

## CRC8 (referenční implementace)

```c
uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
  }
  return crc;
}
```
CRC se počítá přes `[type | payload]`.