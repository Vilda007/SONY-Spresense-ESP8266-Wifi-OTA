// d1_mini_relay.ino — LOLIN D1 mini (ESP8266) WiFi client + serial relay for the SONY Spresense.
//
// Link to Spresense: Serial  (UART0, GPIO1 TX / GPIO3 RX) @115200  -> Serial2 on the Spresense.
// Debug output:      Serial1 (TX-only, GPIO2/D4)         @115200  -> second USB-serial adapter.
//                     (Never debug over Serial — that is the link to the Spresense.)
//
// WiFi credentials and endpoints are RECEIVED at runtime in a CONFIG frame from the Spresense
// (see protocol.md). The firmware holds NO hardcoded secret. The Spresense reads them from
// config.json on the SD card.
//
// FQBN: esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200
// WARNING: before flashing over USB, disconnect the TX/RX wires to the Spresense
//          (UART0 is shared with the onboard CH340).

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include "relay_proto.h"   // shared library lib/relay_proto (compile with --library lib/relay_proto)

// Debug over Serial1 (TX-only on D4/GPIO2). Defined before first use.
#define DBG(x)   Serial1.print(x)
#define DBGln(x) Serial1.println(x)

// ----- State -----
Parser spParser;
String cfgSsid, cfgPass, cfgServerUrl, cfgOtaUrl, cfgOtaManifest;
long cfgPollMs = 30000;
bool wifiReady = false;
bool configReceived = false;
unsigned long lastPing = 0;
unsigned long lastHeartbeat = 0;

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

// Handle an incoming frame from the Spresense
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
      configReceived = true;
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
      // Phase 5: reject for now; the OTA pipeline is added later.
      sendFrame(Serial, T_OTA_ACK, (const uint8_t *)"\x00", 1); // reject
      sendFrameStr(Serial, T_STATUS, "OTA_NOT_IMPLEMENTED_YET");
      break;
    default:
      DBG("Unknown frame type=0x"); DBGln(String(type, HEX));
      break;
  }
}

void setup() {
  Serial.begin(115200);   // link to Spresense
  Serial1.begin(115200);  // TX-only debug on D4/GPIO2
  delay(50);
  parserReset(spParser);
  DBGln("D1 mini relay boot");
  sendFrameStr(Serial, T_STATUS, "BOOT_WAITING_CONFIG");
}

void loop() {
  // 1. read frames from the Spresense (non-blocking)
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (parseByte(spParser, b)) onFrame(spParser.type, spParser.buf, spParser.len);
  }

  // 2. keepalive PING every 10 s
  if (millis() - lastPing > 10000) {
    lastPing = millis();
    if (wifiReady) sendFrameStr(Serial, T_PING, "");
  }

  // 2b. heartbeat before CONFIG — signals the app is alive and waiting for the Spresense
  if (!configReceived && millis() - lastHeartbeat > 3000) {
    lastHeartbeat = millis();
    sendFrameStr(Serial, T_STATUS, "WAIT_CONFIG");
  }

  // 3. keep WiFi up (the core handles auto-reconnect; we just watch the state)
  if (wifiReady && WiFi.status() != WL_CONNECTED) {
    wifiReady = false;
    DBGln("WiFi lost");
    sendFrameStr(Serial, T_STATUS, "WIFI_LOST");
  }
}