# Serial2 frame protocol

Bidirectional binary protocol between the D1 mini and the Spresense over UART2
(Spresense `Serial2`) / D1 mini UART0 (`Serial`). Baud **115200, 8N1**. No hardware flow control.

## Frame format

```
┌──────┬──────────┬──────────┬────────┬─────────────────┬──────┬──────┐
│ 0xAA │ len_hi   │ len_lo   │ type   │ payload[len]     │ crc8 │ 0x55 │
└──────┴──────────┴──────────┴────────┴─────────────────┴──────┴──────┘
   start   payload length BE     1 B      len bytes         1 B    end
```

- `0xAA` start marker, `0x55` end marker (resync after noise).
- `len` = number of payload bytes (16-bit big-endian), **excluding** the header, crc and end marker.
- `type` = 1 byte (see below).
- `crc8` = CRC8 (poly 0x07, init 0x00) over `type` + `payload` (NOT start/end/len).
- Maximum payload for OTA chunks: 512 B (respects XMODEM/UART buffer); other frames are short.

> Byte-stuffing: if the payload or crc contains `0xAA`/`0x55`, that is fine — the parser is
> length-driven, not marker-driven. The start marker is only for initial synchronization.

## Frame types

| Type | Code | Direction | Payload | Meaning |
|---|---|---|---|---|
| `CONFIG` | 0x01 | Spresense → D1 | JSON: `{"ssid":"...","pass":"...","server_url":"...","ota_url":"...","ota_manifest_url":"...","poll_ms":N}` | WiFi credentials + endpoints. The D1 mini runs `WiFi.begin` on receipt. |
| `DATA_UP` | 0x02 | Spresense → D1 | JSON sensor data / text | Telemetry to POST to `server_url`. |
| `DATA_DOWN` | 0x03 | D1 → Spresense | HTTP response / server command | Relay result / command for the application. |
| `OTA_AVAIL` | 0x04 | D1 → Spresense | JSON: `{"version":"x.y","size":N,"sha256":"..."}` | An update is available. Spresense replies with `OTA_ACK` (accept/reject). |
| `OTA_ACK` | 0x05 | Spresense → D1 | 1 byte: `0x01`=accept/continue, `0x00`=reject/stop | OTA stream control. |
| `OTA_CHUNK` | 0x06 | D1 → Spresense | 4-byte offset (LE) + data (rest) | A block of `package.bin`. Spresense acks each chunk with `OTA_ACK`. |
| `OTA_DONE` | 0x07 | D1 → Spresense | SHA256 (32 B) or empty | End of stream; Spresense verifies and applies it via `fwup_client`. |
| `STATUS` | 0x10 | both | text (message/state) | Informational (D1 mini IP, FW version, errors). |
| `PING` / `PONG` | 0xFE / 0xFF | both | empty | Keepalive. |

## Control rules

- **Non-blocking parser**: both sides accumulate bytes in a state machine inside `loop()`
  (WAIT_START → READ_LEN → READ_TYPE → READ_PAYLOAD → READ_CRC → READ_END). No
  `readString()`/`while(Serial.available()==0)` blocking.
- **CONFIG**: the Spresense sends it at boot (and again after a PING with no prior CONFIG). The
  D1 mini waits for CONFIG up to a timeout (5 s); on failure it emits a `STATUS` error and retries.
- **DATA_UP/OTA**: the D1 mini issues the HTTP request, waits for a response (with timeout), and
  returns the result in `DATA_DOWN`. Retry with backoff.
- **OTA stream**: D1 mini → `OTA_AVAIL` → Spresense `OTA_ACK(accept)` → D1 mini `OTA_CHUNK` →
  Spresense `OTA_ACK(continue)` … → `OTA_DONE` → Spresense verifies → `fwup->download+update` →
  reboot. On reject/timeout the D1 mini stops the stream.
- **Keepalive**: PING every 10 s; after 3 unanswered PINGs a side considers the link dead (D1 mini:
  reconnect WiFi and wait for a fresh CONFIG).

## CRC8 (reference implementation)

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
CRC is computed over `[type | payload]`.