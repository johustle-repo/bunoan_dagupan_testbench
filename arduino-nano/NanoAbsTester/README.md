# Arduino Nano ABS tester sender

The engine sensor knobs are disconnected. The Nano reads only what the ABS tester shows and sends it to the ESP32 + 2.8" screen (`esp32-abs-tester-28/AbsTester28`) as `A1` frames. Open NanoAbsTester.ino with Buttons.h and SpeedRamp.h in the same folder. Board: Arduino Nano, processor ATmega328P (clones that fail with "not in sync": ATmega328P (Old Bootloader)). USB Serial Monitor: 115200 baud.

## Nano → testbench

| Nano pin | Connection | Shown as |
|---|---|---|
| A0 | Reference 1 (direct, 0–5 V max) | 0–5 V |
| A1 | Reference 2 (direct, 0–5 V max) | 0–5 V |
| A2 | Current sensor output (0–5 V) | 0–10 A |
| A3 | Battery (direct, 0–5 V max) | 0–5 V |
| D7 | Speed knob, 0–1639 Hz 0–5 V square wave | 0–200 km/h |
| D8 | Starter button, normally open to GND | Key: each press turns the tester on / off |
| D9 | Brake switch, released LOW / pressed HIGH (same as the testbench brake) | Held: speed eases to 0; released: back to the knob |
| 5V, GND | Testbench 5 V and GND | |

**Every analog input is direct: never put more than 5 V on A0–A3.** For a real 12 V battery or a higher reference, use a voltage divider and change `INPUT_FULL_SCALE` in the sketch (12.5 kΩ / 2.5 kΩ divider → 30.0).

With the key off nothing is measured, and the screen shows only "PRESS START". The speed eases at 100 km/h per second (200 → 0 in 2 s while braking).

## Nano → ESP32

| Nano | ESP32 |
|---|---|
| D6 (TX) → 10 kΩ → | GPIO17 (RX), with 20 kΩ from GPIO17 to GND |
| D5 (RX) | GPIO16 (TX), optional: the link is one-way, nothing is sent on it |
| GND | GND |

D6 outputs 5 V: never connect it directly to the ESP32. 19200 baud 8N1, one-way.
