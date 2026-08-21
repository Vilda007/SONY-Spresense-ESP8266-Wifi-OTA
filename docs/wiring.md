# Wiring — D1 mini ↔ SONY Spresense

## Overview

The D1 mini (ESP8266) talks to the Spresense over **UART2** (`Serial2` on the Spresense).
The link is **split across two D1 mini UARTs** because of the onboard CH340 (see
"GPIO1 is CH340-clamped" below):

- **D1 → Spresense** (status/data TX) uses **`Serial1` on GPIO2** — not CH340-shared.
- **Spresense → D1** (CONFIG/DATA_UP RX) uses **`Serial` (UART0 RX) on GPIO3**.

Both boards have separate USB-serial bridges, so their console/programming ports do not
collide at the OS level (COM6 = Spresense CP210x, COM15 = D1 mini CH340).

```
   D1 mini (ESP8266)                   Spresense extension board
   ┌───────────────────┐              ┌────────────────────────┐
   │  "2" (GPIO2) TX   │────────────► │  D00  (UART2_RXD)       │  Serial1 @115200
   │  "rx" (GPIO3) RX  │◄──────────── │  D01  (UART2_TXD)       │  UART0 RX @115200
   │  GND              │────────────► │  GND                    │
   │  vbus (5V USB)    │   own ≥1 A charger or PC USB (NOT Spresense)
   └───────────────────┘              └────────────────────────┘
   (debug: "tx" GPIO1 → COM15, bench only — see "Debug output")
```

| D1 mini pin (silkscreen) | GPIO | Direction | Spresense pin | Function |
|---|---|---|---|---|
| **2** (Serial1 TX) | GPIO2 | D1 → Spresense | **D00** | UART2_RXD (status/data frames) |
| **rx** (UART0 RX) | GPIO3 | Spresense → D1 | **D01** | UART2_TXD (CONFIG / DATA_UP) |
| GND | — | common | GND | reference |
| vbus (5V) | — | own USB | — | D1 mini power (≥1 A) |

> D1 mini v4 silkscreen uses raw GPIO numbers: `tx(=GPIO1) rx(=GPIO3) 5(SCL) 4(SDA) 0(=GPIO0)
> 2(=GPIO2) gnd vbus`. The exposed non-strap, non-CH340 GPIOs are GPIO4 and GPIO5; **GPIO2 is
> the only clean TX path** (Serial1, TX-only) and it doubles as the onboard blue LED.

- `Serial2` on the Spresense = UART2. From `pins_arduino.h` (spresense variant):
  `PIN_D01 = PIN_UART2_TXD`, `PIN_D00 = PIN_UART2_RXD`, `SERIAL_PORT_HARDWARE = Serial2`.
- `Serial` (UART1) on the Spresense = the CP210x console on COM6 — independent of Serial2,
  used for logging simultaneously.
- UART2 flow-control CTS/RTS live on PIN_D27/D28 — not used for the relay (no flow control at
  115200, plenty of headroom).

## GPIO1 is CH340-clamped — that is why TX moved to GPIO2

UART0 TX (**GPIO1**, the D1 mini "tx" pin) is permanently tied through a ~150–470 Ω series
resistor to the onboard CH340. This makes GPIO1 **unusable as the link TX in both power
states**:

- **D1 on a charger / Spresense power (CH340 unpowered):** the CH340's ESD-protection diodes
  clamp the GPIO1 swing to roughly **0.7 V ↔ 1.8 V** — below the Spresense D00 VIH (~2.3 V), so
  the Spresense reads idle/garbage and never sees a frame. (Measured with a multimeter on this
  project's hardware.)
- **D1 on USB (CH340 powered):** the CH340 actively holds/fights the line, again corrupting the
  frame.

**Fix (in production FW `d1_mini_relay.ino`):** status/data TX moved to **`Serial1` on GPIO2**,
which is NOT shared with the CH340 → clean rail-to-rail swing. Validated: clean
`AA 00 0B 10 … 95 55` WAIT_CONFIG frames arrive on D00 every 3 s, rock-steady. CONFIG/DATA_UP RX
stays on **UART0 RX (GPIO3)**, which is fine — the CH340's TXD output (also on GPIO3) idles
high-Z and does not block the Spresense D01 driving the line (verified end-to-end with the D1 on
USB).

## GPIO2 is a boot-strap pin — keep the D00 load light

GPIO2 **must be HIGH at boot** (ESP8266 boot strap). It also drives the onboard LED and the D00
wire. A disturbed strap during power-cycling can cause a transient crash loop
(`Fatal exception (0)`, epc1≈0x4010000x). Rules:

- Keep the GPIO2→D00 load light (the Spresense D00 input is high-Z + weak pull-up — OK).
- **Do not hot-plug the GPIO2 wire while cycling power** — connect it, then power up, or
  connect it after the D1 has booted (UART TX is static-level; hot-plugging after boot is safe).
- If the D1 ever shows a solid-on LED (GPIO2 stuck low) right after re-wiring, re-power it
  cleanly before declaring a fault — this is almost always the strap, not corrupt flash (see
  [troubleshooting.md](troubleshooting.md) §3).

## Jumpers and power

- **JP1 = 3.3 V** on the extension board. The ESP8266 is 3.3 V logic; 5 V would damage it. Set
  jumper JP1 to the 3.3 V position. (JP1 concerns the Spresense extension I/O voltage; the D1
  is powered from its own USB, not from the extension header.)
- **D1 mini power — use a ≥1 A USB wall charger OR a PC USB port.** Both work: the relay was
  verified end-to-end on PC USB (`D1 ONLINE`, `IP=192.168.1.171`, `ACK:hello` round-trip), and
  on a ≥1 A charger. TX is on GPIO2 (not CH340-shared), so a powered CH340 no longer blocks the
  link. Use a short, thick USB cable — cheap thin cables sag under the WiFi-TX current peaks.
- **NEVER power the D1 from the Spresense 5 V or 3 V3 rail.** The ESP8266 WiFi-TX bursts pull
  ~350–500 mA; the Spresense board's own 5 V/3 V3 path to the extension header (PMIC/polyfuse/
  diode-drop) cannot supply it → brownout. Verified 2026-08-21: both 3 V3→3 V3 and 5 V→vbus
  brownout identically (onboard LED solid-on = GPIO2 stuck low, D1 never reaches `loop()`, zero
  `RX D1` frames). A stiffer upstream PC USB port did **not** help — the limiter is the Spresense
  board, not the port.
- **A too-weak external USB also brownouts.** A weak charger, a thin/high-resistance cable, or
  an unpowered/overloaded hub gives the same solid-on-LED brownout as the Spresense rail. If the
  D1 LED is solid-on and no `RX D1` frames appear, fix the supply first (see
  [troubleshooting.md](troubleshooting.md) §8).
- Only **GND** is shared between the D1 and the Spresense. The USB grounds alone are NOT a
  substitute — run an explicit GND wire.

## D1 mini debug output

The D1 mini link no longer uses UART0 TX, so **`Serial` (UART0 TX, GPIO1) is free for debug** —
but only while the D1 is on USB (readable on COM15; clamped/unreadable on a charger, and nothing
listens in production). In `d1_mini_relay.ino` the `DBG`/`DBGln` macros print to `Serial`.
Nothing on the relay path uses UART0 TX, so debug prints do not inject bytes into the frame
stream. Do **not** open COM15 during normal relay operation (opening COM15 resets the D1 via DTR
and disturbs the UART0 RX line shared with the Spresense).

## Before flashing either board over USB

- **D1 mini (COM15):** disconnect the GPIO2→D00 and GPIO3←D01 wires first. UART0 is shared with
  the onboard CH340; if the Spresense holds the RX line, esptool can fail to enter download mode.
- **Spresense (COM6):** flashing does not conflict at the OS-port level, but disconnect the
  D1 wires for cleanliness (prevents back-feeding the ESP8266 during a board reset).

Mitigation for leaving wires connected: a 1 kΩ series resistor on each TX/RX line between the D1
and the Spresense limits contention (full isolation = disconnect).

## Baud

115200 8N1, matching on both sides. The ESP8266 UART0 is accurate enough at 115200 (do not use
74880 — that is the boot-ROM rate; reading it at 115200 yields garbage that looks like a crash
loop but is not).

## See also

- Architecture: [architecture.md](architecture.md)
- Troubleshooting: [troubleshooting.md](troubleshooting.md)