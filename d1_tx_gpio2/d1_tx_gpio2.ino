// d1_tx_gpio2.ino — D1 mini diagnostic: transmit WAIT_CONFIG on UART1 TX (GPIO2)
// instead of UART0 TX (GPIO1).
//
// WHY: On the D1 mini, UART0 TX (GPIO1) is hardwired through a series resistor
// to the onboard CH340. With the D1 powered from the Spresense (USB port empty),
// that node does not cleanly carry the ESP8266's TX toggle to a remote receiver
// (the Spresense D00 sees only idle + manual-GND, never the frame). UART1 TX on
// GPIO2 ("2" pin + onboard blue LED) is NOT shared with the CH340, so it is a
// clean transmit path.
//
// Success = the Spresense D00 dumper shows `AA 00 0B 10 57 41 49 54 5F 43 4F 4E
// 46 49 47 95 55` every 3 s.
//
// Note: GPIO2 is the onboard LED. UART1 idles HIGH (LED off); during the ~1.5 ms
// frame the LED gives a brief flicker. There is no steady 2 Hz heartbeat on the
// LED in this sketch (Serial1 owns GPIO2). Liveness = COM6 frames.
//
// Wiring for the test: D1 pin "2" (GPIO2) -> Spresense D00. Power D1 from 3V3
// (USB empty). (Production will add UART0 RX on GPIO3 for CONFIG reception.)
//
// FQBN: esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200

#include "relay_proto.h"

void setup() {
  // UART1 TX on GPIO2 — clean path, not shared with the CH340.
  Serial1.begin(115200);
  delay(200);
  sendFrameStr(Serial1, T_STATUS, "BOOT_WAITING_CONFIG");
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last > 3000) {
    last = millis();
    sendFrameStr(Serial1, T_STATUS, "WAIT_CONFIG");
  }
}