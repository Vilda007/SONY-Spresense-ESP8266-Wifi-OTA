// relay_proto.h — společný rámcový protokol Serial2 (viz protocol.md).
// Zde v headeru, aby Arduino auto-prototyp generátor nehoistoval prototypy nad
// definice typů (rozbilo by to kompilaci). Includuje se nahoře .ino souboru.

#ifndef RELAY_PROTO_H
#define RELAY_PROTO_H
#include <Arduino.h>

#define FRAME_START 0xAA
#define FRAME_END   0x55

#define T_CONFIG     0x01
#define T_DATA_UP    0x02
#define T_DATA_DOWN  0x03
#define T_OTA_AVAIL  0x04
#define T_OTA_ACK    0x05
#define T_OTA_CHUNK  0x06
#define T_OTA_DONE   0x07
#define T_STATUS     0x10
#define T_PING       0xFE
#define T_PONG       0xFF

#define MAX_PAYLOAD 600

enum ParseState { PS_START, PS_LEN_HI, PS_LEN_LO, PS_TYPE, PS_PAYLOAD, PS_CRC, PS_END };

struct Parser {
  ParseState st;
  uint16_t len;
  uint8_t  type;
  uint8_t  buf[MAX_PAYLOAD];
  size_t   idx;
  uint8_t  crc;
  bool     overflow;
};

inline uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
  }
  return crc;
}

inline uint8_t crc8_with_seed(const uint8_t *data, size_t len, uint8_t seed) {
  uint8_t crc = seed;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
  }
  return crc;
}

inline void parserReset(Parser &p) {
  p.st = PS_START; p.len = 0; p.type = 0; p.idx = 0; p.crc = 0; p.overflow = false;
}

// Vrací true, když je k dispozici kompletní platný rámec; volající čte p.type/p.buf/p.len.
inline bool parseByte(Parser &p, uint8_t b) {
  switch (p.st) {
    case PS_START:
      if (b == FRAME_START) p.st = PS_LEN_HI;
      return false;
    case PS_LEN_HI:
      p.len = (uint16_t)b << 8; p.st = PS_LEN_LO; return false;
    case PS_LEN_LO:
      p.len |= b;
      p.overflow = (p.len > MAX_PAYLOAD);
      p.idx = 0; p.st = PS_TYPE; return false;
    case PS_TYPE:
      p.type = b; p.crc = crc8(&b, 1); p.st = (p.len == 0) ? PS_CRC : PS_PAYLOAD; return false;
    case PS_PAYLOAD:
      if (p.overflow) { p.st = PS_START; return false; }
      p.buf[p.idx++] = b;
      p.crc = crc8_with_seed(&b, 1, p.crc);
      if (p.idx >= p.len) p.st = PS_CRC;
      return false;
    case PS_CRC:
      if (b != p.crc) { p.st = PS_START; return false; }
      p.st = PS_END; return false;
    case PS_END:
      p.st = PS_START;
      if (b != FRAME_END) return false;
      return true;
  }
  return false;
}

// Odeslání rámce: 0xAA | len_hi | len_lo | type | payload | crc | 0x55
inline void sendFrame(HardwareSerial &port, uint8_t type, const uint8_t *payload, size_t len) {
  if (len > 65535) len = 65535;
  uint8_t hdr[4] = { FRAME_START, (uint8_t)(len >> 8), (uint8_t)(len & 0xFF), type };
  port.write(hdr, 4);
  if (len) port.write(payload, len);
  uint8_t crc = crc8(&type, 1);
  if (len) crc = crc8_with_seed(payload, len, crc);
  uint8_t tail[2] = { crc, FRAME_END };
  port.write(tail, 2);
}

inline void sendFrameStr(HardwareSerial &port, uint8_t type, const String &s) {
  sendFrame(port, type, (const uint8_t *)s.c_str(), s.length());
}

// Minimální JSON field extractor (ssid/pass/url z CONFIG payload).
inline String jsonField(const String &json, const char *key) {
  String pat = String("\"") + key + "\":\"";
  int i = json.indexOf(pat);
  if (i < 0) return "";
  i += pat.length();
  int j = json.indexOf('"', i);
  if (j < 0) return "";
  return json.substring(i, j);
}
inline long jsonFieldInt(const String &json, const char *key, long def) {
  String pat = String("\"") + key + "\":";
  int i = json.indexOf(pat);
  if (i < 0) return def;
  i += pat.length();
  return json.substring(i).toInt();
}

#endif // RELAY_PROTO_H