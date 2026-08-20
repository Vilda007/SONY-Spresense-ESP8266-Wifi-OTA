# Wiring — D1 mini ↔ SONY Spresense

## Overview

The D1 mini (ESP8266) talks to the Spresense over **UART2** (`Serial2` on the Spresense,
`Serial`/UART0 on the D1 mini). Both boards have separate USB-serial bridges, so their
console/programming ports do not collide at the OS level (COM6 = Spresense CP210x,
COM15 = D1 mini CH340). The only conflict is on the **D1 mini's UART0, which is shared with the
onboard CH340** (through series resistors).

## Pinout

```
   D1 mini (ESP8266)                 Spresense extension board
   ┌───────────────┐                ┌────────────────────────┐
   │  TX  (GPIO1)  │──────────────► │  D00  (UART2_RXD)       │
   │  RX  (GPIO3)  │◄────────────── │  D01  (UART2_TXD)       │
   │  GND          │──────────────► │  GND                    │
   │  5V (USB)     │   (own USB, not from the Spresense)      │
   └───────────────┘                └────────────────────────┘
```

| D1 mini pin | GPIO | Direction | Spresense pin | Function |
|---|---|---|---|---|
| D1 (TX) | GPIO1 | D1 → Spresense | **D00** | UART2_RXD |
| D3 (RX) | GPIO3 | Spresense → D1 | **D01** | UART2_TXD |
| GND | — | common | GND | reference |
| 5V (VIN) | — | from own USB | — | D1 mini power |

- `Serial2` on the Spresense = UART2. From `pins_arduino.h` (spresense variant):
  `PIN_D01 = PIN_UART2_TXD`, `PIN_D00 = PIN_UART2_RXD`, `SERIAL_PORT_HARDWARE = Serial2`.
- `Serial` (UART1) on the Spresense = the CP210x console on COM6 — independent of Serial2, can be
  used for logging simultaneously.
- UART2 flow-control CTS/RTS live on PIN_D27/D28 — not used for the relay (no flow control at
  115200, plenty of headroom).

## Jumpers and power

- **JP1 = 3.3V** on the extension board. The ESP8266 is 3.3 V logic; 5 V would damage it. Set
  jumper JP1 to the 3.3 V position.
- **D1 mini power**: from its **own USB** (5 V through the onboard LDO to 3.3 V). **Never** power
  the D1 mini from the Spresense 5 V/3 V3 rail — WiFi TX bursts of ~300 mA cause a brownout. Only
  GND is shared.
  - If you must share the 5 V rail: add a bulk capacitor (470–1000 µF) at the D1 mini 5 V pin and a
    schottky diode to decouple sag. Verify the rail stays above ~3.6 V at the LDO input during TX bursts.
- **3 V3 injection from the Spresense**: not recommended.

## D1 mini UART0 conflict (important)

UART0 (`Serial`, GPIO1/GPIO3) is permanently tied on the D1 mini through ~150–470 Ω resistors to the
onboard CH340. Consequences:

- **Before programming the D1 mini over USB (COM15)**: **disconnect the TX/RX wires to the
  Spresense**. If left connected, the Spresense holds the line and the upload (esptool) can fail.
  Pin D3/RX is critical — both the CH340 and the Spresense would drive it.
- **During normal operation**: do not open COM15 in another program (Arduino Serial Monitor, etc.)
  — the CH340 would fight the line shared with the Spresense.
- Mitigation: a 1 kΩ series resistor on each TX/RX line between the D1 mini and the Spresense
  (limits contention; full isolation = disconnect).

## D1 mini debug output

The D1 mini `Serial` is reserved for the link to the Spresense — **never use `Serial.print` for
debug** (it would inject bytes into the frame stream to the Spresense). Options:

1. **`Serial1` (TX-only on D4/GPIO2)** → a second USB-serial adapter (RX on D4, common GND).
   `Serial1.begin(115200); Serial1.println(...)`. GPIO2 is the onboard LED and must be HIGH at
   reset (a TX line idles HIGH — fine as long as nothing pulls it low).
2. **WiFi telnet debug**: `WiFiServer telnet(23);` and `print` to connected clients. No extra hardware.
3. Core debug via the board option: `dbg=Serial1,lvl=HTTP_CLIENT|WIFI`.

## Conflict when flashing the Spresense

Flashing the Spresense over COM6 (`arduino-cli upload`) does not conflict with the D1 mini at the
OS-port level, but for cleanliness **disconnect the TX/RX wires to the D1 mini** as well (prevents
back-feeding the ESP8266 during a board reset).

## Why power the D1 mini from its own USB (not from the Spresense)

Powering the D1 mini from the Spresense 5 V rail (or 3.3 V) is tempting — one fewer cable — but it
fails in practice for two reasons:

1. **Brownout.** WiFi TX bursts on the ESP8266 pull ~300 mA. The Spresense 5 V regulator cannot
   sustain that and the ESP8266 brownout-resets mid-connection. Symptoms: `BOOT_WAITING_CONFIG`
   arrives, the D1 tries `WiFi.begin(...)`, the voltage dips, the chip reboots. Repeats forever.
   Verified on this project's hardware.
2. **USB mode blocks the TX line to the Spresense.** Even when the D1 mini is powered from a
   solid 5 V source **through its own USB**, the onboard CH340 holds GPIO1 (D1 mini TX) idle-HIGH
   while USB is connected. Any UART frame the ESP8266 sends on TX is fought by the CH340. Result:
   the Spresense reads garbage or zero bytes on D00, no `RX D1` lines on COM6.

   **Fix:** disconnect USB before depending on D1 TX reaching the Spresense. Power the D1 mini
   from VIN (5 V) through an external USB charger rated for ≥1 A. Keep GND shared with the
   Spresense. Then GPIO1 is not held by the CH340 and `WAIT_CONFIG` / `IP=...` frames arrive
   intact on Spresense D00.

So: power the D1 mini from a USB wall charger, not from the Spresense and not via the PC USB
that keeps COM15 open. Keep COM15 closed during normal relay operation.

## Baud

115200 8N1, matching on both sides. The ESP8266 UART0 is accurate enough at 115200 (do not use
74880 — that is the boot-ROM rate).

## See also

- Architecture: [architecture.md](architecture.md)
- Troubleshooting: [troubleshooting.md](troubleshooting.md)