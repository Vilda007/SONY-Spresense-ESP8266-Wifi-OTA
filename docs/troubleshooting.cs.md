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

## 2. D1 TX nikdy nedorazí na Spresense (žádné `RX D1`) — GPIO1 je sešlápnutá CH340

**Příznak:** po pinconfig fixi (§1) COM6 stále neukazuje `RX D1` řádky. D1 mini, čtená přímo
přes USB COM15, emituje `WAIT_CONFIG` co 3 s — firmware tedy žije. Zapojení a GND jsou ověřené.

**Kořenová příčina:** D1 mini UART0 TX (**GPIO1**, pin "tx") je trvale spojený přes sériový
rezistor s onboard CH340, který linku sešlápne v **obou** stavech napájení:

- **CH340 bez napájení** (D1 na nabíječce / na Spresense): ESD diody CH340 sešlapnou swing
  GPIO1 na ~0,7 V ↔ 1,8 V — pod D00 VIH Spresense (~2,3 V) → Spresense čte idle/garbage a rámec
  nikdy nepřijde.
- **CH340 pod napájením** (D1 na USB): CH340 aktivně přetahuje linku → poškozené rámce.

GPIO1 tedy není jako linkový TX použitelná bez ohledu na to, jak je D1 napájená. (Dřívější rada
"napájet z VIN, ať se CH340 vypne" byl red herring — bez napájení CH340 linku sešlápne
*horší*, ne lepší.)

**Oprava (už v produkčním `d1_mini_relay.ino`):** status/data TX přesunuta na **`Serial1` na
GPIO2** (pin "2"), který není sdílený s CH340 → čistý rail-to-rail swing. CONFIG/DATA_UP RX
zůstává na UART0 RX (GPIO3). Přeflashuj D1 aktuálním `d1_mini_relay.ino` a zapoj
**D1 GPIO2 → Spresense D00** (D1 GPIO3 ← D01, GND společný).

Ověření na COM6: `RX D1 type=0x10 payload=BOOT_WAITING_CONFIG`, pak `WAIT_CONFIG` co ~3 s, pak
`payload=IP=192.168.1.x` po obdržení CONFIG a připojení k WiFi, pak
`D1 ONLINE (CONFIG confirmed)`.

## 3. D1 mini se neustále restartuje / `Fatal exception (0)` — většinou transientní strap, NE poškozená flash

**Příznak:** čtení D1 mini na 74880 baud (ESP8266 boot-ROM rate) opakovaně ukazuje
`Fatal exception (0): epc1=0x4010000x, epc2=0, epc3=0, excvaddr=0`, nebo (čteno na 115200)
solidní proud garbage, který vypadá jako crash-loop. esptool může selhat připojit
("Failed to connect"), protože čip se rebootoje příliš rychle na auto-reset.

**Kořenová příčina (téměř vždy): transientní narušení GPIO2 boot-strapu, NE poškozená flash.**
GPIO2 musí být při bootu HIGH. Vodič GPIO2→D00 v kombinaci s power-cyclingem / hot-plugem může
strap narušit → čip crash-rebootuje. Bez vodiče (nebo připojeného před power-upem a ponechaného
stabilního) čip naběhne čistě (boot mode (3,6), jeden boot log, nula exceptions) a sketch běží
v pohodě. **Nepředpokládej poškozenou flash a nemaž/přeflashuj** — to byl na tomhle projektu red
herring. (115200 čtení 74880-baud boot-ROM/RF-cal streamu taky vypadá jako crash-loop, ale je to
jen špatný baud; čti na 74880, abys viděl reálný počet exceptions.)

**Oprava:**

1. Odpoj vodič GPIO2→D00. Napájej D1 ze solidního zdroje (≥1 A nabíječka nebo PC USB).
2. Re-power (odpoj/připoj USB). Ověř čistý boot — onboard LED krátce blikne co ~3 s (WAIT_CONFIG
   na Serial1/GPIO2).
3. Připoj GPIO2→D00 **až po bootu** (UART TX je static-level; hot-plug po bootu je bezpečný),
   nebo ho připoj před čistým power-upem a nenarušuj ho při cyklování napájení.
4. Jen pokud čistý re-power bez vodiče stále crashuje — přeflashuj (download mód: drž GPIO0/
   pin "0" na GND + RST, pak
   `arduino-cli upload -p COM15 --fqbn esp8266:esp8266:d1_mini:xtal=80,eesz=4M1M,ip=lm2f,baud=115200 d1_mini_relay`).
   To je vzácné; nejdřív zkus strap fix.

## 4. SD config se načte, CONFIG se odešle, ale D1 mini nikdy nedostane WiFi credentials

**Příznak:** COM6 ukáže `SD config OK: ssid=... server=http://...`. `CONFIG sent to D1 mini`
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

**Příznak:** D1 mini nahlásí `WiFi OK IP=...`, ale `http.POST(...)` vrátí `-1` nebo zablokuje.

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
funkce.

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

## 8. D1 brownout — LED trvale svítí, nula `RX D1` rámců (Spresense lišta nebo slabé USB)

**Příznak:** onboard modrá LED D1 je **trvale svítí** (GPIO2 stuck LOW = ESP8266 nikdy nedosáhne
`loop()`), a COM6 ukazuje, jak se CONFIG posílá/resenduje, ale **nula** `RX D1` řádků (ani
`WAIT_CONFIG`). Spresense nakonec zaloguje
`CONFIG: no IP= from D1 after max resends; giving up (check D1-TX->Spresense-RX wire)`.

**Kořenová příčina: napájení, ne UART linka.** WiFi-TX bursty ESP8266 berou ~350–500 mA. Pokud
zdroj prosákne, čip brownout-resetuje dřív, než vůbec pošle rámec. Dva potvrzené spouštěče:

- **5 V nebo 3 V3 lišta Spresense → D1** — vlastní napájecí cesta Spresense na extension header
  nedodá tyto špičky (obě lišty brownoutují identicky; stabilnější upstream USB port nepomůže —
  limituj je samotná deska Spresense, ne port).
- **Příliš slabé externí USB** — slabá nabíječka, tenký/high-resistance kabel, nebo nep napájený/
  přetížený hub → stejný brownout s trvale svítící LED.

**Diagnostika pro rozlišení napájení vs GPIO2 strap:** odpoj vodič GPIO2→D00 a re-power D1.
Krátké bliknutí co ~3 s → byl to strap (§3); **stále trvale svítí** → je to napájení (zdroj
vůbec nedokáže D1 nabootovat).

**Oprava:** napájej D1 z **≥1 A USB nabíječky** (krátký, tlustý kabel) **nebo z PC USB port**
(obě varianty ověřeny end-to-end). Nikdy z lišty Spresense. Drž explicitní GND vodič ke Spresense.

## 9. Spresense "giving up" — CONFIG kampaň se sama nespustí znovu; dej RESET

**Příznak:** COM6 ukáže `CONFIG: no IP= from D1 after max resends; giving up …` a pak přestane
posílat CONFIG. D1 se vrátí později (např. po výměně napájení) a posílá
`BOOT_WAITING_CONFIG`/`WAIT_CONFIG`, ale Spresense stále neposílá CONFIG — relay se nikdy
nepřipojí zpět.

**Kořenová příčina (design FW):** `spresense_relay.ino` resenduje CONFIG co 5 s až do ohraničeného
maxima, pak nastaví `configGivenUp = true` a zastaví se. Kampaň se **restartuje jen na `WiFi_FAIL`
nebo `WIFI_LOST` z D1** — NE na `WAIT_CONFIG`/`BOOT_WAITING_CONFIG` (schválně, aby chybějící
feedback vodič nemohl donekonečna thrashovat WiFi D1). Jakmile tedy vzdala, samotný boot D1 znovu
CONFIG nespustí.

**Oprava:** **stiskni RESET na Spresense** — `setup()` se proběhne, pošle CONFIG jednou a začne
čerstvá resend kampaň. S D1 na solidním napájení očekávej `CONFIG sent` → `RX D1 … IP=…` →
`D1 ONLINE (CONFIG confirmed)` → `ACK:hello` teče.

## Viz také

- [wiring.md](wiring.md) — pinout a napájení
- [architecture.md](architecture.md) — datové toky a rámcový protokol
- [protocol.md](../protocol.md) — formát rámce