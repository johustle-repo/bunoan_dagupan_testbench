# Arduino Uno / Nano ABS tester sender

Measures the two wheel-speed sensors and the Declog frequency, drives the TCS and ABS LEDs and sends the readings to the ESP32 + 2.8" screen (`esp32-abs-tester-28/AbsTester28`) as `A1` frames. It also has three frequency generators: rear → D9 and front → D10 (0–1500 Hz each, set by a KY-040 speed encoder on A0 / A1 / A5) and Declog pot A2 → D3 (0–100 Hz). Open NanoAbsTester.ino; it needs no libraries. Board: **Arduino Uno**, or Arduino Nano with processor ATmega328P (Nano clones that fail with "not in sync": ATmega328P (Old Bootloader)). The pins are the same on both. USB Serial Monitor: 115200 baud.

## Uno / Nano → testbench

| Pin | Connection | Shown as |
|---|---|---|
| D7 | Speed 1 (rear) wheel sensor and RPM, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250. RPM = Hz × 15000 ÷ 1560, max 15,000 (USB only) |
| D8 | Speed 2 (front) wheel sensor, 0–5 V square wave, e.g. the Nano's own **D10** front generator | Speed = Hz ÷ 6 km/h, max 250 |
| D2 | Declog, 0–100 Hz square wave, 0–5 V, e.g. the Nano's own **D3** Declog generator | DECLOG bar, 0–100 Hz |
| D13 | TCS LED (the Nano's built-in LED) | Steady: both speeds match to the whole km/h. Blinking (500 ms): they differ |
| D12 | ABS LED, through a resistor to GND | On: D7 is at least 10 % and 5 km/h slower than D8 |
| A3 | Reference 1, 0–5 V at the pin | REF1 tile, mapped to 0–30 V |
| A4 | Battery, 0–5 V at the pin | BATTERY tile, mapped to 0–30 V |
| 5V, GND | Testbench 5 V and GND (and the Mega / ESP32 GND when their generators drive D7 / D8 / D2) | |

REF2 and current are sent as 0. Timer2 runs the input sampler and Timer1 the D9 / D10 generators, so analogWrite(), tone() and the Servo library can't be used.

D2, D7 and D8 are sampled by Timer2 (compare A) at 20 kHz (every 100 ms the frequency is the rising edges counted divided by the time between the first and last of them, about ±1 Hz; up to about 10 kHz per input). There is no brake input. **REF1 / battery mapping:** A3 and A4 read 0–5 V at the pin, mapped to 0–30 V by `getTrueVoltage()`, which interpolates linearly between the PCB calibration points (`calPinV` / `calTrueV`, same table as the ECU testbench): pin 0.00 / 0.15 / 0.85 / 1.72 / 2.59 / 3.48 / 4.35 / 5.00 V = 0 / 1 / 5 / 10 / 15 / 20 / 25 / 30 V, clamped to 0–30 V. Same reading as the ECU testbench (`MegaTelemetry` `readDividerInput`): 20 readings 1 ms apart, plain average, pin volts from the measured 5 V rail. The Serial Monitor shows the pin volts too. Never put more than 5 V on the pin; a pot or knob there needs its ends on 5V and GND (a floating pin reads random values).

## Frequency generators (`PotGenerator.h`)

| Set by | Output | Range |
|---|---|---|
| Speed encoder, REAR | **D9** | 0–1500 Hz (1500 Hz = 250 km/h) |
| Speed encoder, FRONT | **D10** | 0–1500 Hz |
| A2 Declog pot (10 kΩ) | **D3** | 0–100 Hz |
| D9 ÷ 6 (rear) | **D4** | 0–250 Hz (Hz = km/h) |
| D10 ÷ 6 (front) | **D11** | 0–250 Hz (Hz = km/h) |

Declog pot: one end → GND, wiper → A2, other end → 5V (100 nF wiper–GND optional); every 100 ms it is read (20 readings averaged) and mapped 0–1023 → 0–100 Hz in whole Hz. Each output is a 50 % duty, 0–5 V square wave; at 0 Hz it stops LOW. Jumper **D9 → D7, D10 → D8, D3 → D2** to test the tester itself. **D4 and D11** follow D9 and D10 at exactly one sixth of their frequency, the same ÷ 6 as the speed formula, so their Hz equals the wheel's km/h (100 km/h: D9 600 Hz → D4 100 Hz; 1500 Hz → 250 Hz; 50 % duty): every 6th D9 / D10 edge toggles them in the Timer1 interrupt, so they stay locked to the main outputs and stop LOW with them.

D9 and D10 share Timer1 but are independent: Timer1 runs free at 2 MHz and each compare channel's hardware sets or clears its pin exactly at the match, so the edges aren't moved by interrupts or the serial link (exact average frequency, steps of 0.5 µs). Declog on D3 is made by the 20 kHz Timer2 sampler (exact average frequency, up to about 50 µs of edge jitter, 0.5 % at 100 Hz).

## Speed encoder (`SpeedEncoder.h`)

One KY-040 sets both test speeds. Every D pin is taken, so it uses the freed pot pins as digital inputs: **CLK → A0, DT → A1, SW → A5**, + → 5V, GND → GND (internal pull-ups on all three).

- **Turn**: the selected wheel's speed changes by 1 km/h per click; spun quickly (clicks under 40 ms apart) 5 km/h per click. 0–250 km/h, and the generator runs at km/h × 6 Hz (REAR → D9 / D4, FRONT → D10 / D11).
- **Short press**: switch between **REAR** and **FRONT**.
- **Hold 1 s**: both wheels to 0 km/h.

At power-up both are 0 km/h with REAR selected. The selected wheel is sent as an 18th `A1` frame field (1 rear, 2 front), and page 1 of the 2.8" screen marks it in amber with **ADJUST**. To test ABS, set FRONT to e.g. 100 km/h, then REAR at least 10 % and 5 km/h lower (e.g. 85): D12 lights. Turning is decoded in the 20 kHz Timer2 sampler with a quadrature state table, so bounce cancels out and no click is lost during REF / battery reads or SoftwareSerial frames. If clockwise makes the speed go down, set `REVERSE` to `true` in `SpeedEncoder.h` (or swap CLK and DT). Clicks and presses are logged on the USB Serial Monitor (`# SPEED ENC REAR 85 km/h`, `# SPEED ENC press -> adjusting FRONT`).

USB output, once a second:

```
D7 Frequency: 510.0 Hz
Speed 1: 85.0 km/h
RPM: 4904
D8 Frequency: 600.0 Hz
Speed 2: 100.0 km/h
TCS: BLINKING
ABS: ACTIVE
Declog (D2): 50.0 Hz
Encoder rear -> D9: 510 Hz (85 km/h), front -> D10: 600 Hz (100 km/h), adjusting REAR
Declog pot A2 -> D3: 50 Hz
REF1 (A3): 12.00 V (pin 2.07 V)
Battery (A4): 13.80 V (pin 2.38 V)
```

## Nano → ESP32

| Nano | ESP32 |
|---|---|
| D5 (TX) | RX2 (GPIO16), direct (no divider) |
| GND | GND |

SoftwareSerial on D5, 115200 baud 8N1, one-way, 10 frames a second; the USB Serial Monitor stays on the normal Serial port. Sending blocks interrupts for about 6 ms per frame; that time is left out of the frequency window. The `A1` frame carries D7 Hz, speed 1, REF1, REF2 (0), current (0), battery, D8 Hz, speed 2, RPM, TCS (1 steady / 0 blinking), ABS (1 active), Declog Hz, the rear / front / Declog generator outputs in Hz × 100 and the wheel the speed encoder adjusts (1 rear, 2 front; 18 fields, so flash the matching ESP32 firmware: older ESP32 firmware rejects 18-field frames); the ESP32 shows them as they are (the generator settings in its JSON and USB serial line). The Nano's TX is 5 V and ESP32 pins are rated 3.3 V: a 1 kΩ series resistor limits the current if you want some protection.

## Measured supply for REF / battery (`SupplyVoltage.h`)

The 0–30 V inputs no longer assume the board runs on exactly 5.00 V. Before each REF / battery read the chip's internal 1.1 V bandgap is read against the 5 V rail (AVcc = 1.1 V × 1023 / counts, filtered), and the pin volts are `counts × measured supply / 1023` before they go through the PCB calibration table. On USB the rail is often 4.7–5.1 V; at 4.85 V the old fixed 5.00 V read a 12.00 V input as about 11.6 V. The measured rail is printed on the USB Serial Monitor (`Supply (5V pin, measured)` / `5V:`).

**Calibrate once per board:** the bandgap is 1.0–1.2 V from chip to chip. Measure the board's 5V pin with a multimeter, compare it with the printed supply, and set `BANDGAP_V = 1.100 × meter volts / printed volts` in `SupplyVoltage.h`. Readings outside 4.0–5.5 V are ignored (the last good value is kept).
