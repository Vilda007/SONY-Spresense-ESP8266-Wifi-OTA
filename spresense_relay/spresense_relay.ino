// spresense_relay.ino — SONY Spresense WiFi relay master.
//
// Reads config.json from the SD card (secrets: WiFi SSID/password, server/OTA URL) and sends it
// in a CONFIG frame to the D1 mini over Serial2 (UART2: PIN_D01 TX / PIN_D00 RX). The D1 mini then
// joins WiFi and relays HTTP. No secret is stored in this firmware.
//
// Console/log: Serial  (UART1) -> CP210x COM6 @115200
// D1 mini link: Serial2 (UART2) @115200
//
// FQBN: SPRESENSE:spresense:spresense (core 3.4.7). Flash: arduino-cli upload -p COM6
// or locally: python flash_spk.py -c COM6 build_spresense/spresense_relay.ino.spk

#include <SDHCI.h>
#include "relay_proto.h"   // shared library lib/relay_proto (compile with --library lib/relay_proto)

SDClass SD;

Parser d1Parser;
bool configLoaded = false;
String configJson;          // raw config.json content (debug only; never sent whole)
String wifiSsid, wifiPass, serverUrl, otaUrl, otaManifest;
long pollMs = 30000;

// Reads config.json from the SD card. Format: see config.example.json.
void loadConfig() {
  while (!SD.begin()) {
    Serial.println("SD: insert SD card");
    delay(1000);
  }
  File f = SD.open("config.json", FILE_READ);
  if (!f) {
    Serial.println("SD: config.json not found");
    return;
  }
  configJson = "";
  while (f.available()) configJson += (char)f.read();
  f.close();

  // config.json uses keys wifi_ssid/wifi_pass; the CONFIG frame for the D1 mini maps them to
  // ssid/pass (see d1_mini_relay jsonField).
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
    Serial.println("SD config: no wifi_ssid -> no CONFIG");
  }
}

// Send the CONFIG frame to the D1 mini (a subset of secrets). The D1 mini joins WiFi.
void sendConfigToD1() {
  String payload = "{\"ssid\":\"" + wifiSsid + "\","
                   "\"pass\":\"" + wifiPass + "\","
                   "\"server_url\":\"" + serverUrl + "\","
                   "\"ota_url\":\"" + otaUrl + "\","
                   "\"ota_manifest_url\":\"" + otaManifest + "\","
                   "\"poll_ms\":" + String(pollMs) + "}";
  sendFrameStr(Serial2, T_CONFIG, payload);
  Serial.println("CONFIG sent to D1 mini");
}

void setup() {
  Serial.begin(115200);    // COM6 log
  Serial2.begin(115200);   // D1 mini link (UART2)
  parserReset(d1Parser);
  delay(500);
  Serial.println("Spresense relay boot");
  loadConfig();
  if (configLoaded) sendConfigToD1();
}

unsigned long lastDataUp = 0;
void loop() {
  // 1. read frames from the D1 mini
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
      // T_DATA_DOWN = HTTP response from the server (phase 4 verification)
      // T_STATUS = D1 mini status (IP, WIFI_FAIL, WAIT_CONFIG, ...)
    }
  }

  // 2. periodic test DATA_UP every 5 s (MVP relay verification)
  if (configLoaded && millis() - lastDataUp > 5000) {
    lastDataUp = millis();
    sendFrameStr(Serial2, T_DATA_UP, "hello");
    Serial.println("sent DATA_UP hello");
  }
}