# Wheel Speed Interface Module (WSIM)

Arduino Nano add-on module that sits between the wheel-speed sensors and the
car's original modules. It passes the front/rear signals through 1:1 and adds
extra functions from the same signals. Built from the hand-drawn "NANO" sketch.
Not part of the ABS tester or the ECU testbench.

## Pinout

| Pin | Dir | Function |
|-----|-----|----------|
| D2  | IN  | FREQ input #1 (front), INT0 |
| D3  | IN  | FREQ input #2 (rear), INT1 |
| D4  | OUT | TCS: blinks when rear is faster than front |
| D5  | IN  | Brake switch, `INPUT_PULLUP`, to GND = pressed |
| D6  | OUT | Logic 1 for 13 s on brake event, starting 0.5 s after the trigger (`D6_DELAY_MS`). D6 / D7 only respond at 30 km/h (180 Hz) or more (`EVENT_MIN_FREQ_HZ`) |
| D7  | OUT | 10 pulses/s with 0.5 s interruptions, from 2 s to 10 s of D6 time |
| D8  | OUT | FREQ output #1 (1:1 front) |
| D9  | OUT | FREQ output #2 (1:1 rear) |
| D10 | OUT | Front FREQ ÷ 60 (RPM) |
| D11 | OUT | Front FREQ ÷ 6 (speed display, 1 Hz = 1 km/h) |
| D12 | OUT | LOCK: LOW at power-up, HIGH once front reaches 60 Hz (10 km/h) for 200 ms, latched until power-off |
| D13 | OUT | 4-digit display (TM1637) CLK. Onboard LED flickers dimly; no heartbeat |
| A0  | IN  | Encoder A / CLK |
| A1  | IN  | Encoder B / DT |
| A2  | IN  | Encoder button / SW |
| A3  | OUT | Generated FRONT test frequency 0-1500 Hz, jumper to D2 |
| A4  | OUT | Generated REAR test frequency 0-1500 Hz, jumper to D3 |
| A5  | I/O | 4-digit display (TM1637) DIO |

## Test generator (rotary encoder)

- **NORMAL mode** (power-up): turning changes front and rear together. 6 Hz
  (1 km/h) per click, 30 Hz per click when spun fast. 0-1500 Hz.
- **Press**: REAR-only mode. Turning changes only rear.
- **Press again**: back to NORMAL. Rear snaps back equal to front.

Fit jumpers A3 → D2 and A4 → D3 for bench tests. Remove them when real wheel
sensors are connected.

## 4-digit display

TM1637 module: **CLK → D13, DIO → A5, VCC → 5V, GND → GND**. Shows the front
speed in whole km/h (front Hz ÷ 6), right-aligned, refreshed every 200 ms.
Brightness: `DISP_BRIGHTNESS` (0-7). LOCK is shown on D12 and in serial `lock=`.

## Configuration

Thresholds that the drawing does not specify are assumptions, set in the
CONFIG section at the top of `WheelSpeedInterfaceModule.ino`:

- `BRAKE_REQUIRES_DECEL`: `false` = brake OR abrupt decel (as drawn); `true` = both
- `DECEL_TRIGGER_HZ_PER_S`: 130 Hz/s (~6 m/s²), above 60 Hz
- `TCS_MARGIN`: rear > front by 10 %, rear above 30 Hz, for 200 ms
- `ENC_REVERSE`: flip encoder direction

Serial monitor: 115200 baud, status line every 0.5 s.

## Hardware notes

- Outputs driving 12 V loads (lamps, relays, lock actuator) need a transistor or
  MOSFET driver. For an indicator LED: pin → 330 Ω → LED → GND.
- Wheel sensors: Hall type needs pull-up and 5 V clamp; inductive (VR) type
  needs a conditioner (e.g. LM1815) before D2/D3.
- Clone Nano upload failing: select *ATmega328P (Old Bootloader)*.

## Build / upload

```
arduino-cli compile --upload -p COM5 --fqbn arduino:avr:nano arduino-nano/WheelSpeedInterfaceModule
```
