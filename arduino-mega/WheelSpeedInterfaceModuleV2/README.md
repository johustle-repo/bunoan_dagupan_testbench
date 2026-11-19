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
| D6  | OUT | Logic 1 for 13 s on brake event, starting 0.5 s after the trigger (`D6_DELAY_MS`). Brake triggers only count at **10 km/h or more** as shown on the displays (`EVENT_MIN_KPH`). When LOCK latches at 10 km/h, D6 alone turns on for **1 s** and then off (`LOCK_D6_PULSE_MS`, once per power-on, D7 stays off) |
| D7  | OUT | 10 pulses/s with 0.5 s interruptions, from 2 s to 10 s of D6 time. Also **on for 1 s at power-up** then off (`BOOT_D7_ON_MS`), even before arming |
| D8  | OUT | FREQ output #1 (1:1 front) |
| D9  | OUT | FREQ output #2 (1:1 rear) |
| D10 | OUT | Front FREQ ÷ 60 (RPM) |
| D11 | OUT | Front FREQ ÷ 6 (speed display, 1 Hz = 1 km/h) |
| D12 | OUT | LOCK: LOW at power-up, HIGH once front is **10 km/h or more** (as shown on the displays, `LOCK_KPH`) for 200 ms, latched until power-off |
| D13 | —   | Free (onboard LED, held LOW) |
| D24 | IN  | Condition 1 button, momentary to GND (`INPUT_PULLUP`) |
| D25 | IN  | Condition 2 button, momentary to GND (`INPUT_PULLUP`) |
| D26 | OUT | Condition 1 LED (on once button 1 was pressed) |
| D27 | OUT | Condition 2 LED (on once button 2 was pressed) |
| D20 | I/O | 0.96" OLED SDA (SSD1306 128×64 I2C) |
| D21 | OUT | 0.96" OLED SCL |
| D28 | OUT | Speed relay: clicks faster with front speed (see below) |
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
  10 km/h, the LOCK / ABS light on D12 stays off, and LOCK cannot latch. The
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

## 0.96" OLED (front + rear speed)

SSD1306 128×64 I2C module: **SDA → D20, SCL → D21, VCC → 5V, GND → GND**
(address 0x3C or 0x3D, found automatically).

```
[COND1] [COND2]    ARMED
FRONT        20 km/h
120Hz
REAR         22 km/h
132Hz
```

Top bar: the COND1 / COND2 boxes are outlined until that button is pressed,
then filled, plus `BLOCKED` / `ARMED`. Below: FRONT and REAR speed in km/h
(Hz ÷ 6) in big digits with the raw Hz. Speeds show `---` until armed. Refreshed every
200 ms (`OLED_UPDATE_MS`).

The frame is streamed to the OLED in 16-byte chunks across loop passes instead
of one blocking ~25 ms transfer, so D6/D7 timing is not disturbed. Works without
the OLED connected; it is re-detected every 2 s (serial: `# OLED found at 0x3C`).

Needs the **Adafruit SSD1306** and **Adafruit GFX** libraries.

## Speed relay (D28)

The click rate maps **linearly 0–250 km/h → 0–30 clicks/s** (front speed):

| km/h | 0 | 25 | 50 | 100 | 125 | 200 | 250 |
|------|---|----|----|-----|-----|-----|-----|
| clicks/s | 0 | 3 | 6 | 12 | 15 | 24 | 30 |

Each gap gets ±8 % random variation for a motor-like beat
(`RELAY_JITTER_PCT`, set 0 for exact timing). Each click is ON for half the
period, 12–25 ms. Silent below 1 km/h and until both arming conditions are
met. Serial shows `relay=…/s`.

Wiring: 5V relay module **IN → D28, VCC → 5V, GND → GND**. A bare relay coil
needs a transistor (e.g. 2N2222 / BC547 + 1 kΩ base resistor) and a flyback
diode across the coil. If the relay is ON at rest and clicks off, the module is
active-LOW: set `RELAY_ACTIVE_LOW = true`.

Tuning: `RELAY_FULL_KPH`, `RELAY_MAX_CLICK_HZ`, `RELAY_MIN_KPH`,
`RELAY_JITTER_PCT`, `RELAY_ON_MIN_MS` / `RELAY_ON_MAX_MS`.

## Configuration

Same CONFIG section as the Nano version, at the top of
`WheelSpeedInterfaceModuleV2.ino`:

- `BRAKE_REQUIRES_DECEL`: `false` = brake OR abrupt decel (as drawn); `true` = both
- `DECEL_DROP_KPH` / `DECEL_WINDOW_MS`: a "sudden drop" is the front speed falling 5 km/h or more below its highest value of the last 300 ms. It triggers D6/D7 when the speed **before** the drop was 10 km/h or more (`EVENT_MIN_KPH`), even if it drops to 0. The brake switch needs the current speed at 10 km/h or more. Serial prints `# EVENT: sudden drop from X to Y km/h` or `# EVENT: brake switch`
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
