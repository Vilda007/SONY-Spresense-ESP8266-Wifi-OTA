// d1_heartbeat.ino — D1 mini diagnostic: heartbeat LED + WAIT_CONFIG on TX.
//
// Purpose: prove whether the D1 firmware RUNS and TRANSMITS on a given power
// source (e.g. Spresense 5 V vs a >=1 A charger). The blue LED (GPIO2) blinks
// steadily at ~2 Hz ONLY while loop() is running — so a steady blink = firmware
// alive. It also sends a relay_proto WAIT_CONFIG frame on Serial (UART0 TX =
// GPIO1) every 3 s, exactly like the production relay firmware, so the Spresense
// D00 dumper should show `AA 00 0B 10 ...` IF the D1-TX -> D00 wire + level are
// good. Boot ROM only blinks the LED ~2 times; a steady continuing blink is the
// firmware.
//
// FQBN: esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200

#include "relay_proto.h"

void setup() {
  Serial.begin(115200);          // UART0 TX on GPIO1 -> Spresense D00
  pinMode(LED_BUILTIN, OUTPUT);  // onboard blue LED on GPIO2 (active-low)
  digitalWrite(LED_BUILTIN, HIGH); // off
  delay(200);
  sendFrameStr(Serial, T_STATUS, "BOOT_WAITING_CONFIG");
}

void loop() {
  static unsigned long lastBlink = 0;
  static unsigned long lastFrame = 0;
  static bool ledOn = false;

  // 2 Hz heartbeat = loop() is running (firmware alive)
  if (millis() - lastBlink > 250) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH); // active-low
  }

  // WAIT_CONFIG every 3 s on GPIO1 (same as production firmware)
  if (millis() - lastFrame > 3000) {
    lastFrame = millis();
    sendFrameStr(Serial, T_STATUS, "WAIT_CONFIG");
  }
}