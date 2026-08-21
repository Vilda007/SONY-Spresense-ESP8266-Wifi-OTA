// spresense_uart2_dumper.ino — raw byte dumper for UART2 (D1 mini link).
//
// Diagnostic: prints EVERY byte received on Serial2 (UART2: D01 TX / D00 RX) to
// the COM6 console as hex, with a millisecond timestamp and a running line so we
// can see whether ANY signal at all arrives on D00 from the D1 mini — valid
// frames, garbage, or total silence. The production spresense_relay FW only logs
// *valid* parsed frames, so it cannot distinguish "no bytes" from "noise that
// doesn't parse". This sketch can.
//
// Build:  arduino-cli compile --fqbn SPRESENSE:spresense:spresense \
//           --output-dir build/uart2_dumper spresense_uart2_dumper/spresense_uart2_dumper.ino
// Flash:  python flash_spk.py -c COM6 build/uart2_dumper/spresense_uart2_dumper.ino.spk

extern "C" {
  #include <arch/board/board_pinconfig.h>   // PINCONF_UART2_TXD, PINCONF_UART2_RXD
  #include <chip/cxd56_pinconfig.h>          // cxd56_pin_config()
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("UART2 raw dumper boot (listening on D00/RXD)");

  // Same pinconf fix as the relay FW: route UART2 to D0/D1 on the header.
  cxd56_pin_config(PINCONF_UART2_TXD);
  cxd56_pin_config(PINCONF_UART2_RXD);

  Serial2.begin(115200, SERIAL_8N1 | SERIAL_CTS | SERIAL_RTS);
  delay(200);
  Serial.println("ready: send bytes from the D1 mini on its TX (GPIO1) -> Spresense D00");
}

void loop() {
  int n = Serial2.available();
  if (n > 0) {
    Serial.print("["); Serial.print(millis()); Serial.print("] RX ");
    Serial.print(n); Serial.print("B:");
    for (int i = 0; i < n; i++) {
      uint8_t b = Serial2.read();
      Serial.print(' ');
      if (b < 0x10) Serial.print('0');
      Serial.print(b, HEX);
    }
    Serial.println();
  } else {
    // heartbeat every 3 s so we can confirm the console is live (vs. frozen).
    static unsigned long last = 0;
    if (millis() - last > 3000) {
      last = millis();
      Serial.print("["); Serial.print(last);
      Serial.println("] idle (no UART2 bytes)");
    }
  }
}