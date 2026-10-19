# Arduino Nano ABS tester sender

Measures the two wheel-speed sensors, drives the TCS and ABS LEDs and sends the readings to the ESP32 + 2.8" screen (`esp32-abs-tester-28/AbsTester28`) as `A1` frames. Open NanoAbsTester.ino; it needs no other files or libraries. Board: Arduino Nano, processor ATmega328P (clones that fail with "not in sync": ATmega328P (Old Bootloader)). USB Serial Monitor: 115200 baud.

## Nano → testbench

| Nano pin | Connection | Shown as |
|---|---|---|
| D7 | Speed 1 (rear) wheel sensor and RPM, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250. RPM = Hz × 15000 ÷ 1560, max 15,000 (USB only) |
| D8 | Speed 2 (front) wheel sensor, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250 |
| D13 | TCS LED (the Nano's built-in LED) | Steady: both speeds match to the whole km/h. Blinking (500 ms): they differ |
| D12 | ABS LED, through a resistor to GND | On: D7 is at least 10 % and 5 km/h slower than D8 |
| A0 | Reference 1 (direct, 0–5 V max) | REF1 tile, 0–5 V |
| A1 | Battery (direct, 0–5 V max) | BATTERY tile, 0–5 V |
| A2 | Reference 2 (direct, 0–5 V max) | 0–5 V (sent, not shown) |
| A3 | Current sensor output (0–5 V) | 0–10 A (sent, not shown) |
| 5V, GND | Testbench 5 V and GND | |

D7 and D8 are sampled by Timer1 at 20 kHz (rising edges counted over 1 s windows, up to about 10 kHz per input). There is no brake input (D9 is unused). **Every analog input is direct: never put more than 5 V on A0–A3.** For a real 12 V battery, use a voltage divider and change `INPUT_FULL_SCALE` in the sketch (12.5 kΩ / 2.5 kΩ divider → 30.0).

USB output, once a second:

```
D7 Frequency: 1500.0 Hz
Speed 1: 250.0 km/h
RPM: 14423
D8 Frequency: 1500.0 Hz
Speed 2: 250.0 km/h
TCS: ON
ABS: OFF
REF1 (A0): 5.00 V
Battery (A1): 4.10 V
```

## Nano → ESP32

| Nano | ESP32 |
|---|---|
| D5 (TX) | RX2 (GPIO16), direct (no divider) |
| GND | GND |

SoftwareSerial on D5, 115200 baud 8N1, one-way, 4 frames a second; the USB Serial Monitor stays on the normal Serial port. Sending blocks interrupts for about 6 ms per frame; that time is left out of the frequency window. The `A1` frame carries D7 Hz, speed 1, REF1, REF2, current, battery, D8 Hz, speed 2, RPM, TCS (1 steady / 0 blinking) and ABS (1 active); the ESP32 shows them as they are. The Nano's TX is 5 V and ESP32 pins are rated 3.3 V: a 1 kΩ series resistor limits the current if you want some protection.
