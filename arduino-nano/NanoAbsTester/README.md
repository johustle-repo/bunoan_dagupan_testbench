# Arduino Uno / Nano ABS tester sender

Measures the two wheel-speed sensors and the Declog frequency, drives the TCS and ABS LEDs and sends the readings to the ESP32 + 2.8" screen (`esp32-abs-tester-28/AbsTester28`) as `A1` frames. Open NanoAbsTester.ino; it needs no other files or libraries. Board: **Arduino Uno**, or Arduino Nano with processor ATmega328P (Nano clones that fail with "not in sync": ATmega328P (Old Bootloader)). The pins are the same on both. USB Serial Monitor: 115200 baud.

## Uno / Nano → testbench

| Pin | Connection | Shown as |
|---|---|---|
| D7 | Speed 1 (rear) wheel sensor and RPM, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250. RPM = Hz × 15000 ÷ 1560, max 15,000 (USB only) |
| D8 | Speed 2 (front) wheel sensor, 0–5 V square wave, e.g. Mega 2560 Pro **D44** (0–2000 Hz pot generator) | Speed = Hz ÷ 6 km/h, max 250 |
| D2 | Declog, 0–100 Hz square wave, 0–5 V, e.g. Mega 2560 Pro **D2** (A9 pot generator) | DECLOG bar, 0–100 Hz |
| D13 | TCS LED (the Nano's built-in LED) | Steady: both speeds match to the whole km/h. Blinking (500 ms): they differ |
| D12 | ABS LED, through a resistor to GND | On: D7 is at least 10 % and 5 km/h slower than D8 |
| A0 | Reference 1, 0–5 V at the pin | REF1 tile, mapped to 0–30 V |
| A1 | Battery, 0–5 V at the pin | BATTERY tile, mapped to 0–30 V |
| 5V, GND | Testbench 5 V and GND (and the Mega GND when its generators drive D8 / D2) | |

A2 and A3 are not used (REF2 and current are sent as 0).

D2, D7 and D8 are sampled by Timer1 at 20 kHz (rising edges counted over 1 s windows, up to about 10 kHz per input). There is no brake input (D9 is unused). **REF1 / battery mapping:** A0 and A1 read 0–5 V at the pin, mapped to 0–30 V: volts = pin volts ÷ 5 × 30 (= pin volts × 6, the ECU testbench formula; `PIN_FULL_SCALE_V` / `INPUT_FULL_SCALE_V`). Noise filter: 20 samples with the highest and lowest dropped, then a running average (about 1 s to settle). The Serial Monitor shows the pin volts too. Never put more than 5 V on the pin; a pot or knob there needs its ends on 5V and GND (a floating pin reads random values).

USB output, once a second:

```
D7 Frequency: 1500.0 Hz
Speed 1: 250.0 km/h
RPM: 14423
D8 Frequency: 1500.0 Hz
Speed 2: 250.0 km/h
TCS: ON
ABS: OFF
Declog (D2): 50.0 Hz
REF1 (A0): 12.00 V (pin 2.00 V)
Battery (A1): 13.80 V (pin 2.30 V)
```

## Nano → ESP32

| Nano | ESP32 |
|---|---|
| D5 (TX) | RX2 (GPIO16), direct (no divider) |
| GND | GND |

SoftwareSerial on D5, 115200 baud 8N1, one-way, 4 frames a second; the USB Serial Monitor stays on the normal Serial port. Sending blocks interrupts for about 6 ms per frame; that time is left out of the frequency window. The `A1` frame carries D7 Hz, speed 1, REF1, REF2 (0), current (0), battery, D8 Hz, speed 2, RPM, TCS (1 steady / 0 blinking), ABS (1 active) and Declog Hz; the ESP32 shows them as they are. The Nano's TX is 5 V and ESP32 pins are rated 3.3 V: a 1 kΩ series resistor limits the current if you want some protection.
