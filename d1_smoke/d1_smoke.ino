// d1_smoke.ino — absolutely minimal D1 mini Pro test.
//
// Does NOT include WiFi.h. Does NOT use ESP.wdt*. Just toggles the
// onboard LED and prints "ALIVE <n>" to Serial so we can confirm
// the ESP8266 boots, runs, and survives the UART0 link setup.
//
// FQBN: esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200

#define LED 2   // onboard blue LED on D1 mini (active LOW)

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);   // LED on
  delay(100);
  Serial.println("SMOKE_BOOT");
  Serial.print("ChipID=");
  Serial.println(ESP.getChipId());
  Serial.print("FlashRealSize=");
  Serial.println(ESP.getFlashChipRealSize());
  Serial.print("Heap=");
  Serial.println(ESP.getFreeHeap());
}

void loop() {
  static unsigned long t0 = 0;
  static int n = 0;
  if (millis() - t0 > 1000) {
    t0 = millis();
    n++;
    digitalWrite(LED, n & 1);
    Serial.print("ALIVE ");
    Serial.println(n);
  }
}
