# Wiring — D1 mini ↔ SONY Spresense

## Přehled

D1 mini (ESP8266) komunikuje se Spresense přes **UART2** (`Serial2` na Spresense, `Serial`/UART0 na D1 mini). Obě desky mají oddělené USB-sériové převodníky, takže konzole/programovací porty se nebijí (COM6 = Spresense CP210x, COM21 = D1 mini CH340). Konflikt je jen na **UART0 D1 mini, který je sdílen s onboard CH340** (přes sériové odpory).

## Pinout

```
   D1 mini (ESP8266)                 Spresense extension board
   ┌───────────────┐                ┌────────────────────────┐
   │  TX  (GPIO1)  │──────────────► │  D00  (UART2_RXD)       │
   │  RX  (GPIO3)  │◄────────────── │  D01  (UART2_TXD)       │
   │  GND          │──────────────► │  GND                    │
   │  5V (USB)     │   (vlastní USB, ne ze Spresense)         │
   └───────────────┘                └────────────────────────┘
```

| D1 mini pin | GPIO | Směr | Spresense pin | Funkce |
|---|---|---|---|---|
| D1 (TX) | GPIO1 | D1 → Spresense | **D00** | UART2_RXD |
| D3 (RX) | GPIO3 | Spresense → D1 | **D01** | UART2_TXD |
| GND | — | společný | GND | reference |
| 5V (VIN) | — | z vlastního USB | — | napájení D1 mini |

- `Serial2` na Spresense = UART2. Z `pins_arduino.h` (variant spresense): `PIN_D01 = PIN_UART2_TXD`, `PIN_D00 = PIN_UART2_RXD`, `SERIAL_PORT_HARDWARE = Serial2`.
- `Serial` (UART1) na Spresense = CP210x konzole COM6 — nezávislé na Serial2, lze používat pro log zároveň.
- Flow control CTS/RTS (UART2) je na PIN_D27/D28 — pro relay se nevyužívá (SW flow / bez flow control, 115200 je dost rychlé).

## Jumpery a napájení

- **JP1 = 3.3V** na extension boardu. ESP8266 má 3.3V logiku; na 5V by došlo k poškození. Přepni jumper JP1 do polohy 3.3V.
- **Napájení D1 mini**: z **vlastního USB** (5V přes onboard LDO na 3.3V). **Nikdy** nenapájej D1 mini z 5V/3V3 railu Spresense — WiFi TX špičky ~300 mA způsobí brownout. Společné je jen GND.
  - Pokud musíš sdílet 5V rail: přidej bulk kondenzátor 470–1000 µF na 5V pin D1 mini a schottky diodu k oddělení propadu. Ověř, že rail během TX špiček nestoupne pod ~3.6 V.
- **3V3 injekce ze Spresense**: nedoporučuje se.

## Konflikt UART0 D1 mini (důležité)

UART0 (`Serial`, GPIO1/GPIO3) je na D1 mini trvale propojen přes odpory (~150–470 Ω) s onboard CH340. Důsledek:

- **Před programováním D1 mini přes USB (COM21)**: **odpoj TX/RX vodiče ke Spresense**. Pokud zůstanou připojené, Spresense drží linku a upload (esptool) může selhat. Pin D3/RX je kritický — CH340 i Spresense by tlačily do sebe.
- **Během normálního běhu**: neotevírej COM21 v dalším programu (Arduino Serial Monitor atd.) — CH340 by tlačil do linky, kterou sdílí se Spresense.
- Zmírňující opatření: sériový odpor 1 kΩ na každé linii TX/RX mezi D1 mini a Spresense (omezení konfliktu, plná izolace = odpojení).

## Debug výstup D1 mini

`Serial` D1 mini je rezervován pro linku do Spresense — **debug nikdy přes `Serial.print`** (injektoval by to bajty do rámce do Spresense). Možnosti:

1. **`Serial1` (TX-only na D4/GPIO2)** → druhý USB-sériový adaptér (RX na D4, společný GND). `Serial1.begin(115200); Serial1.println(...)`. GPIO2 je onboard LED a musí být při resetu HIGH (TX linka idle HIGH → v pořádku, pokud ji nic netáhne dolů).
2. **WiFi telnet debug**: `WiFiServer telnet(23);` a `print` do připojených klientů. Bez extra HW.
3. Core debug přes board option: `dbg=Serial1,lvl=HTTP_CLIENT|WIFI`.

## Konflikt při flash Spresense

Během flash Spresense přes COM6 (`arduino-cli upload`) není konflikt s D1 mini na úrovni OS portů, ale pro čistotu **odpoj TX/RX k D1 mini** i při flashi Spresense (zabraňuje zpětnému vedení signálu do ESP8266 během resetu desky).

## Baud

115200 8N1, obě strany shodné. ESP8266 UART0 je dostatečně přesný na 115200 (nepoužívat 74880 — to je boot-ROM rate).

## Související

- Architektura: [architecture.md](architecture.md)
- Řešení problémů: [troubleshooting.md](troubleshooting.md)