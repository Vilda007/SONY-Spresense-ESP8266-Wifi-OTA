# Troubleshooting (Známé problémy)

Známé problémy a opravy, na které jsme narazili při oživování projektu. Před jakýmkoli novým
zapojením si přečti tento soubor — většina příznaků dole odpovídá jedné z těchto kořenových
příčin.

[English](troubleshooting.md) | [Čeština](troubleshooting.cs.md)

## 1. `Serial2.begin()` otevře `/dev/ttyS2`, ale na D00/D01 nejsou data

**Příznak:** Spresense nabootuje, na COM6 vypíše `CONFIG sent to D1 mini` a `sent DATA_UP hello`,
ale **žádné** `RX D1 ...` řádky se nikdy neobjeví. Ani hardwarová smyčka D00↔D01 (zkrat TX↔RX)
nepřinese nic zpět.

**Diagnostika:** v minimalistickém sketchi otevři `/dev/ttyS2` přes POSIX a na pár sekund
zavolej `read()` — vrátí **0 bytů**, i s `O_NONBLOCK`. `open(O_EXCL)` projde (UART2 není držen
jiným procesem), `cxd56_pin_status()` ukáže, že pin je stále v GPIO módu.

**Kořenová příčina:** Arduino core 3.4.7 `HardwareSerial::begin()` otevře `/dev/ttyS2`, ale
**nepřepne CXD5602 pin multiplexor** na UART2. Tabulka `pin_maps[]` v
`cores/spresense/wiring_digital.c` mapuje `PIN_D00 → PIN_UART2_RXD`, `PIN_D01 → PIN_UART2_TXD`,
ale tyto záznamy nejsou nikde v runtime konzumované — `Serial2.begin()` zavolá POSIX `open()`
a nic víc. Pady SoC zůstanou v GPIO (input floating, output LOW), takže TX nikdy neřídí linku
a RX nikdy neposlouchá.

**Oprava:** v `setup()` **před** `Serial2.begin()` zavolej explicitně SDK pinconfig helper:

```cpp
extern "C" {
  #include <arch/board/board_pinconfig.h>   // PINCONF_UART2_TXD, PINCONF_UART2_RXD
  #include <chip/cxd56_pinconfig.h>          // cxd56_pin_config()
}

void setup() {
  Serial.begin(115200);
  // přesměruj UART2 na D0 (RX) a D1 (TX)
  cxd56_pin_config(PINCONF_UART2_TXD);
  cxd56_pin_config(PINCONF_UART2_RXD);

  Serial2.begin(115200, SERIAL_8N1 | SERIAL_CTS | SERIAL_RTS);
  // ...
}
```

Include cesty `arch/board/board_pinconfig.h` a `chip/cxd56_pinconfig.h` se resolvují, protože
Arduino core `platform.txt` přidává `-I{build.kernel}/nuttx/arch` a
`-I{build.kernel}/nuttx/arch/chip`. Oba soubory leží v SDK release adresáři.

Po této opravě se na COM6 začnou objevovat `RX D1 type=0x10 payload=WAIT_CONFIG` a
`RX D1 type=0x3 payload=ACK:hello` podle očekávání.

## 2. `WAIT_CONFIG` rámce se dekódují na straně D1, ale Spresense je nikdy nevidí

**Příznak:** po pinconfig fixi COM6 stále neukazuje `RX D1` řádky. D1 mini, čtená přímo přes
USB COM15, emituje `WAIT_CONFIG` co 3 s — firmware tedy žije. Zapojení je ověřené (GND
společné, TX drát měřen 3.3 V v idle).

**Kořenová příčina (nejčastější): D1 mini je napájená přes vlastní USB z PC.** Onboard CH340
drží GPIO1 (TX D1 mini) trvale v idle-HIGH, dokud je USB připojené. Každý UART rámec, který
ESP8266 pošle na TX, je přetahován CH340. Spresense RX vidí buď konstantní HIGH (žádný start
bit, tedy žádné byty), nebo interleaved garbage, které neprojdou CRC.

**Oprava:** napájej D1 mini z **VIN (5 V) přes externí USB nabíječku dimenzovanou na ≥1 A**,
ne z PC USB a ne z 5 V lišty Spresense. GND nech společný se Spresense. CH340 se odpojí,
jakmile VUSB chybí, GPIO1 je volný pro ESP8266 UART a rámce přicházejí čistě na Spresense D00.

Ověření: čtení COM6 by mělo ukázat `RX D1 type=0x10 payload=WAIT_CONFIG`, pak
`payload=BOOT_WAITING_CONFIG`, a po obdržení CONFIG a připojení k WiFi
`payload=IP=192.168.1.x`.

## 3. D1 mini se neustále restartuje, na 74880 baud vypisuje `Fatal exception (0)`

**Příznak:** čtení D1 mini na 74880 baud (ESP8266 boot-ROM rate) opakovaně ukazuje
`Fatal exception (0): epc1=0x40100000, epc2=0, epc3=0, excvaddr=0`. Flash zápisy vypadají, že
projdou, ale user firmware spadne na úplně první instrukci po každém resetu.

**Kořenová příčina:** obsah flashky je poškozený — téměř vždy opakovaným brownoutem během
předchozího flash zápisu, když byla D1 mini napájená ze slabého zdroje (např. 5 V lišta
Spresense).

**Oprava:** vynuť D1 mini do bootloader módu a přeflashuj:

1. Odpoj všechny vodiče k Spresense (TX/RX/GND).
2. Drž **GPIO0 = D3** na **GND** drátem.
3. Stiskni a pusť **RST** tlačítko. D1 mini naběhne v download módu (sketch neběží, žádný
   crash).
4. `arduino-cli upload -p COM15 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 d1_mini_relay`
5. Po uploadu odpoj USB, seber D3-GND drát, připoj USB. Ověř přes
   `python tools/read_com.py COM15 6 115200` — očekávej `WAIT_CONFIG` rámce co ~3 s.

## 4. SD config se načte, CONFIG se odešle, ale D1 mini nikdy nedostane WiFi credentials

**Příznak:** COM6 ukáže `SD config OK: ssid=HOUSLEcz server=http://...`. `CONFIG sent to D1 mini`
se zaloguje. Ale D1 mini (čtená na COM15) nikdy nezaloguje `CONFIG ssid=...` ani `WiFi begin: ...`.

**Možné příčiny:**

- D1 mini ve skutečnosti neběží relay sketch (např. je v bootloader módu — viz problém 3).
- SD karta není vložená, nebo `config.json` má CRLF konce řádků, které matou některé parsery.
  (Arduino `File::read()` vrací surové byty — CRLF by mělo být v pořádku, ale ověř hex
  dumpem.)
- `config.json` existuje, ale `wifi_ssid` je prázdné, takže Spresense nastaví
  `configLoaded = false` a přeskočí odeslání CONFIG.

**Diagnostika:** v `setup()` vypiš `wifiSsid.length()` a celý JSON string. Ověř, že soubor
opravdu obsahuje očekávané klíče. Použij `jsonField` z `lib/relay_proto/relay_proto.h` —
toleruje whitespace kolem dvojtečky (`"k": "v"` i `"k":"v"` fungují). Pokud si napíšeš
vlastní parser, pozor — `{"ssid": "x"}` (s mezerou) rozbije naivní implementace.

## 5. ESP8266 HTTPClient POST na `http://192.168.x.x:8080/` timeoutuje z D1 mini

**Příznak:** D1 mini nahlásí `WiFi OK IP=...`, ale `http.POST(...)` vrátí `-1` nebo zablokne.

**Možné příčiny:**

- **Windows Firewall** zablokoval Python při prvním spuštění. Znovu povol `python.exe` na
  privátních sítích (Nastavení → Windows Security → Firewall → Allowed apps). Příznak: logy
  serveru zůstanou prázdné, když se D1 pokusí POSTnout.
- Server běží na `127.0.0.1` místo `0.0.0.0`. Použij
  `HTTPServer(("0.0.0.0", PORT), ...)`, aby se k němu dostala i externí zařízení na LAN.
- D1 mini a PC jsou na různých SSID nebo VLAN. Ověř, že `WiFi.localIP()` a PC LAN IP jsou na
  stejném `/24` (např. obě `192.168.1.x`). `tools/test_server.py` loguje zdrojovou IP
  každého requestu — potvrď, že sedí s D1 mini.

## 6. `arduino-cli` auto-prototype hoisting rozbije kompilaci

**Příznak:** chyby jako `error: 'Parser' was not declared in this scope` nebo
`redeclared as different kind of entity`, když sketch používá user-defined typy v parametrech
funkcí.

**Kořenová příčina:** `arduino-cli` spouští preprocessor, který zdvihá prototypy funkcí nad
definice typů. Forward declarations pomůžou jen někdy; pokud funkce bere `Parser &` referencí,
zdvižený prototyp selže.

**Oprava:** dej typy **a** funkce do sdíleného headeru (v tomto projektu
`lib/relay_proto/relay_proto.h`) a `#include` ho z obou sketchů. Auto-prototyper nedokáže
zvedat přes include. Kompiluj s `arduino-cli ... --library lib/relay_proto`, aby cesta ke
knihovně byla vidět. Název sketch složky a `.ino` souboru musí sedět (`d1_mini_relay/d1_mini_relay.ino`,
`spresense_relay/spresense_relay.ino`).

## 7. `SD: config.json not found`, i když soubor na kartě je

**Příznak:** COM6 vypíše `SD: config.json not found`, přestože soubor na kartě je.

**Možné příčiny:**

- JSON parser vyžaduje whitespace-tolerantní extrakci klíčů (`"k": "v"`). Defaultní
  `String.indexOf(":")` v Arduino je striktní — `jsonField()` helper v
  `lib/relay_proto/relay_proto.h` přeskočí whitespace kolem dvojtečky. Použij ho.
- CRLF konce řádků v souboru. Otevři hex editor
  (`python -c "open('config.json','rb').read()[:200]"`) a ověř, že začíná `{`, ne `﻿{` (UTF-8 BOM).
- SD karta není FAT-formátovaná, nebo má partition type, který Spresense `SDHCI` driver
  nepozná. Přeformátuj na FAT32 (nebo FAT16 pro karty ≤2 GB).

## Viz také

- [wiring.md](wiring.md) — pinout a napájení
- [architecture.md](architecture.md) — datové toky a rámcový protokol
- [protocol.md](../protocol.md) — formát rámce