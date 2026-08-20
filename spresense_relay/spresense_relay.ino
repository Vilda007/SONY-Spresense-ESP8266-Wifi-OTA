// spresense_relay.ino — SONY Spresense WiFi relay master.
//
// Čte config.json z SD karty (tajemství: WiFi SSID/heslo, server/OTA URL) a pošle
// ho v CONFIG rámci D1 minci přes Serial2 (UART2: PIN_D01 TX / PIN_D00 RX).
// D1 mini se pak připojí k WiFi a relayuje HTTP. Žádné tajemství v tomto FW.
//
// Konzole/log: Serial  (UART1) -> CP210x COM6 @115200
// Linka D1 mini: Serial2 (UART2) @115200
//
// FQBN: SPRESENSE:spresense:spresense (core 3.4.7). Flash: arduino-cli upload -p COM6
// nebo lokálně python flash_spk.py -c COM6 build/spresense_relay.ino.spk

#include <SDHCI.h>
#include "relay_proto.h"   // sdílená knihovna lib/relay_proto (compile s --library lib/relay_proto)

SDClass SD;

Parser d1Parser;
bool configLoaded = false;
String configJson;          // surový obsah config.json (jen pro debug, neposílá se celý)
String wifiSsid, wifiPass, serverUrl, otaUrl, otaManifest;
long pollMs = 30000;

// Načte config.json z SD. Formát viz config.example.json.
void loadConfig() {
  while (!SD.begin()) {
    Serial.println("SD: vloz SD kartu");
    delay(1000);
  }
  File f = SD.open("config.json", FILE_READ);
  if (!f) {
    Serial.println("SD: config.json nenalezen");
    return;
  }
  configJson = "";
  while (f.available()) configJson += (char)f.read();
  f.close();

  // config.json používá klíče wifi_ssid/wifi_pass; do CONFIG rámce pro D1 mini
  // mapujeme na ssid/pass (viz d1_mini_relay jsonField).
  wifiSsid = jsonField(configJson, "wifi_ssid");
  wifiPass = jsonField(configJson, "wifi_pass");
  serverUrl = jsonField(configJson, "server_url");
  otaUrl = jsonField(configJson, "ota_url");
  otaManifest = jsonField(configJson, "ota_manifest_url");
  pollMs = jsonFieldInt(configJson, "poll_interval_ms", 30000);

  configLoaded = (wifiSsid.length() > 0);
  if (configLoaded) {
    Serial.print("SD config OK: ssid="); Serial.print(wifiSsid);
    Serial.print(" server="); Serial.print(serverUrl);
    Serial.print(" poll_ms="); Serial.println(pollMs);
  } else {
    Serial.println("SD config: neni wifi_ssid -> bez CONFIG");
  }
}

// Pošle CONFIG rámec D1 minci (subset tajemství). D1 mini se připojí k WiFi.
void sendConfigToD1() {
  String payload = "{\"ssid\":\"" + wifiSsid + "\","
                   "\"pass\":\"" + wifiPass + "\","
                   "\"server_url\":\"" + serverUrl + "\","
                   "\"ota_url\":\"" + otaUrl + "\","
                   "\"ota_manifest_url\":\"" + otaManifest + "\","
                   "\"poll_ms\":" + String(pollMs) + "}";
  sendFrameStr(Serial2, T_CONFIG, payload);
  Serial.println("CONFIG odeslan D1 mini");
}

void setup() {
  Serial.begin(115200);    // COM6 log
  Serial2.begin(115200);   // linka D1 mini (UART2)
  parserReset(d1Parser);
  delay(500);
  Serial.println("Spresense relay boot");
  loadConfig();
  if (configLoaded) sendConfigToD1();
}

unsigned long lastDataUp = 0;
void loop() {
  // 1. čti rámce z D1 mini
  while (Serial2.available()) {
    uint8_t b = Serial2.read();
    if (parseByte(d1Parser, b)) {
      uint8_t t = d1Parser.type;
      size_t   l = d1Parser.len;
      const uint8_t *p = d1Parser.buf;
      String s;
      for (size_t i = 0; i < l; i++) s += (char)p[i];
      Serial.print("RX D1 type=0x"); Serial.print(t, HEX);
      Serial.print(" len="); Serial.print(l);
      Serial.print(" payload="); Serial.println(s);
      if (t == T_PING) sendFrameStr(Serial2, T_PONG, "");
      // T_DATA_DOWN = HTTP odpověď ze serveru (fáze 4 verifikace)
      // T_STATUS = stav D1 mini (IP, WIFI_FAIL, WAIT_CONFIG, ...)
    }
  }

  // 2. periodický testovací DATA_UP každých 5 s (MVP verifikace relaye)
  if (configLoaded && millis() - lastDataUp > 5000) {
    lastDataUp = millis();
    sendFrameStr(Serial2, T_DATA_UP, "hello");
    Serial.println("sent DATA_UP hello");
  }
}