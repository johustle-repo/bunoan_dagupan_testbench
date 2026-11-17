# Wheel Speed Interface Module (WSIM) v2 — Mega 2560 Pro

Straight port of the Nano WSIM (`arduino-nano/WheelSpeedInterfaceModule`) to a
Mega 2560 Pro. Same behavior; **D2–D12 keep the same jobs as on the Nano**.
Only the display, encoder and test-generator pins moved. v2 adds **arming**
(two condition buttons, see below).

| Moved | Nano | Mega v2 | Why |
|-------|------|---------|-----|
| Display CLK / DIO | D13 / A5 | **D22 / D23** | D13 left free (onboard LED) |
| Encoder A / B / SW | A0 / A1 / A2 | **A8 / A9 / A10** | Mega A0–A7 have no pin-change interrupts |
| Generator FRONT / REAR | A3 / A4 | **D46 / D45** | Timer5 OC5A / OC5B hardware toggle |

## Pinout

| Pin | Dir | Function |
|-----|-----|----------|
| D2  | IN  | FREQ input #1 (front), INT4 |
| D3  | IN  | FREQ input #2 (rear), INT5 |
| D4  | OUT | TCS: blinks when rear is faster than front |
| D5  | IN  | Brake switch, `INPUT_PULLUP`, to GND = pressed |
| D6  | OUT | Logic 1 for 13 s on brake event, starting 0.5 s after the trigger (`D6_DELAY_MS`). Brake triggers only count at 30 km/h (180 Hz) or more (`EVENT_MIN_FREQ_HZ`). When LOCK latches at 10 km/h, D6 alone turns on for **1 s** and then off (`LOCK_D6_PULSE_MS`, once per power-on, D7 stays off) |
| D7  | OUT | 10 pulses/s with 0.5 s interruptions, from 2 s to 10 s of D6 time. Also **on for 1 s at power-up** then off (`BOOT_D7_ON_MS`), even before arming |
| D8  | OUT | FREQ output #1 (1:1 front) |
| D9  | OUT | FREQ output #2 (1:1 rear) |
| D10 | OUT | Front FREQ ÷ 60 (RPM) |
| D11 | OUT | Front FREQ ÷ 6 (speed display, 1 Hz = 1 km/h) |
| D12 | OUT | LOCK: LOW at power-up, HIGH once front reaches 60 Hz (10 km/h) for 200 ms, latched until power-off |
| D13 | —   | Free (onboard LED, held LOW) |
| D24 | IN  | Condition 1 button, momentary to GND (`INPUT_PULLUP`) |
| D25 | IN  | Condition 2 button, momentary to GND (`INPUT_PULLUP`) |
| D26 | OUT | Condition 1 LED (on once button 1 was pressed) |
| D27 | OUT | Condition 2 LED (on once button 2 was pressed) |
| D22 | OUT | 4-digit display (TM1637) CLK |
| D23 | I/O | 4-digit display (TM1637) DIO |
| A8  | IN  | Encoder A / CLK |
| A9  | IN  | Encoder B / DT |
| A10 | IN  | Encoder button / SW |
| D46 | OUT | Generated FRONT test frequency 0-1500 Hz, jumper to D2 |
| D45 | OUT | Generated REAR test frequency 0-1500 Hz, jumper to D3 |

## Arming (condition 1 and 2)

After power-up the WSIM is **blocked** until both condition buttons have been
pressed once:

- Press button 1 (D24) → condition 1 met, LED 1 (D26) on. Press button 2 (D25)
  → condition 2 met, LED 2 (D27) on. Any order. Each stays latched until
  power-off.
- While blocked: D4, D6, D7, D8–D11 and D12 all stay LOW. No D6/D7 even above
  30 km/h, the LOCK / ABS light on D12 stays off, and LOCK cannot latch. The
  display shows `----`. Speed is still measured, and the test generator still
  works.
- Once both are met, the WSIM works normally. If the brake is already held at
  that moment, release and press it again to start a D6/D7 event.

Wiring: button between the pin and GND (no resistor needed). LED: pin → 330 Ω →
LED → GND. Serial shows `ARM=00 [BLOCKED]` → `ARM=11`, plus
`# CONDITION n met` and `# ARMED: WSIM active`.

## Test generator (rotary encoder)

- **NORMAL mode** (power-up): turning changes front and rear together. 6 Hz
  (1 km/h) per click, 30 Hz per click when spun fast. 0-1500 Hz.
- **Press**: REAR-only mode. Turning changes only rear.
- **Press again**: back to NORMAL. Rear snaps back equal to front.

Fit jumpers **D46 → D2** and **D45 → D3** for bench tests. Remove them when real
wheel sensors are connected.

v2 difference: the edges are made by the Timer5 hardware (output-compare
toggle), not by software in the ISR, so the generated frequency has no
interrupt jitter.

## 4-digit display

TM1637 module: **CLK → D22, DIO → D23, VCC → 5V, GND → GND**. Shows the front
speed in whole km/h (front Hz ÷ 6), right-aligned, refreshed every 200 ms.
Shows `----` until both arming conditions are met.
Brightness: `DISP_BRIGHTNESS` (0-7).

## Configuration

Same CONFIG section as the Nano version, at the top of
`WheelSpeedInterfaceModuleV2.ino`:

- `BRAKE_REQUIRES_DECEL`: `false` = brake OR abrupt decel (as drawn); `true` = both
- `DECEL_TRIGGER_HZ_PER_S`: 130 Hz/s (~6 m/s²), above 60 Hz
- `TCS_MARGIN`: rear > front by 10 %, rear above 30 Hz, for 200 ms
- `ENC_REVERSE`: flip encoder direction

Serial monitor: 115200 baud, status line every 0.5 s (banner
`Wheel Speed Interface Module (WSIM) v2 Mega ready`).

## Hardware notes

- Outputs driving 12 V loads (lamps, relays, lock actuator) need a transistor or
  MOSFET driver. For an indicator LED: pin → 330 Ω → LED → GND.
- Wheel sensors: Hall type needs pull-up and 5 V clamp; inductive (VR) type
  needs a conditioner (e.g. LM1815) before D2/D3.

## Build / upload

```
arduino-cli compile --upload -p COMx --fqbn arduino:avr:mega:cpu=atmega2560 arduino-mega/WheelSpeedInterfaceModuleV2
```
