// d1_mini_relay.ino — LOLIN D1 mini (ESP8266) WiFi client + serial relay pro SONY Spresense.
//
// Linka do Spresense: Serial  (UART0, GPIO1 TX / GPIO3 RX) @115200  -> Serial2 na Spresense.
// Debug výstup:        Serial1 (TX-only, GPIO2/D4)        @115200  -> druhý USB-serial adaptér.
//                      (Debug nikdy přes Serial — to je linka do Spresense.)
//
// WiFi credentials a endpointy DOSTANE za běhu v CONFIG rámci od Spresense (viz protocol.md).
// Ve firmware NENÍ žádné hardcoded heslo. Spresense je čte z config.json na SD kartě.
//
// FQBN: esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200
// POZOR: před flashem přes USB odpojit TX/RX vodiče ke Spresense (UART0 sdílen s CH340).

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include "relay_proto.h"

// Debug přes Serial1 (TX-only na D4/GPIO2). Definice před prvním použitím.
#define DBG(x)   Serial1.print(x)
#define DBGln(x) Serial1.println(x)

// ----- Stav -----
Parser spParser;
String cfgSsid, cfgPass, cfgServerUrl, cfgOtaUrl, cfgOtaManifest;
long cfgPollMs = 30000;
bool wifiReady = false;
unsigned long lastPing = 0;

// ----- WiFi -----
void connectWifi() {
  DBG("WiFi begin: "); DBGln(cfgSsid);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    DBG("WiFi OK IP="); DBGln(WiFi.localIP().toString());
    sendFrameStr(Serial, T_STATUS, String("IP=") + WiFi.localIP().toString());
  } else {
    wifiReady = false;
    DBGln("WiFi FAIL");
    sendFrameStr(Serial, T_STATUS, "WiFi_FAIL");
  }
}

// Zpracování příchozího rámce ze Spresense
void onFrame(uint8_t type, const uint8_t *payload, size_t len) {
  String s = "";
  for (size_t i = 0; i < len; i++) s += (char)payload[i];
  switch (type) {
    case T_CONFIG: {
      cfgSsid = jsonField(s, "ssid");
      cfgPass = jsonField(s, "pass");
      cfgServerUrl = jsonField(s, "server_url");
      cfgOtaUrl = jsonField(s, "ota_url");
      cfgOtaManifest = jsonField(s, "ota_manifest_url");
      cfgPollMs = jsonFieldInt(s, "poll_ms", 30000);
      DBG("CONFIG ssid="); DBG(cfgSsid); DBG(" srv="); DBGln(cfgServerUrl);
      connectWifi();
      break;
    }
    case T_DATA_UP: {
      if (!wifiReady || cfgServerUrl.length() == 0) {
        sendFrameStr(Serial, T_DATA_DOWN, "NO_WIFI_OR_SERVER");
        break;
      }
      WiFiClient client;
      HTTPClient http;
      http.setTimeout(8000);
      if (http.begin(client, cfgServerUrl)) {
        http.addHeader("Content-Type", "application/octet-stream");
        int code = http.POST((uint8_t *)payload, len);
        String resp = (code > 0) ? http.getString() : String("HTTP_ERR_") + code;
        http.end();
        sendFrameStr(Serial, T_DATA_DOWN, resp);
        DBG("POST -> "); DBG(code); DBG(" "); DBGln(resp);
      } else {
        sendFrameStr(Serial, T_DATA_DOWN, "HTTP_BEGIN_FAIL");
      }
      break;
    }
    case T_PING:
      sendFrameStr(Serial, T_PONG, "");
      break;
    case T_OTA_AVAIL:
      // Fáze 5: nyní odmítneme, OTA pipeline se doplní později.
      sendFrame(Serial, T_OTA_ACK, (const uint8_t *)"\x00", 1); // reject
      sendFrameStr(Serial, T_STATUS, "OTA_NOT_IMPLEMENTED_YET");
      break;
    default:
      DBG("Unknown frame type=0x"); DBGln(String(type, HEX));
      break;
  }
}

void setup() {
  Serial.begin(115200);   // linka do Spresense
  Serial1.begin(115200);  // TX-only debug na D4/GPIO2
  delay(50);
  parserReset(spParser);
  DBGln("D1 mini relay boot");
  sendFrameStr(Serial, T_STATUS, "BOOT_WAITING_CONFIG");
}

void loop() {
  // 1. čti rámce ze Spresense (non-blokovací)
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (parseByte(spParser, b)) onFrame(spParser.type, spParser.buf, spParser.len);
  }

  // 2. keepalive PING každých 10 s
  if (millis() - lastPing > 10000) {
    lastPing = millis();
    if (wifiReady) sendFrameStr(Serial, T_PING, "");
  }

  // 3. udržuj WiFi (auto-reconnect řeší jádro, jen hlídáme stav)
  if (wifiReady && WiFi.status() != WL_CONNECTED) {
    wifiReady = false;
    DBGln("WiFi lost");
    sendFrameStr(Serial, T_STATUS, "WIFI_LOST");
  }
}