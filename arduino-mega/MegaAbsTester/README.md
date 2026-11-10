# Arduino Mega 2560 Pro ABS tester sender

A second Mega 2560 Pro in place of the Nano (`arduino-nano/NanoAbsTester`) in the ABS tester. Same pins, same readings and the same `A1` frames, plus one last field for the speed encoder's wheel (20 fields), so flash the matching ESP32 + 2.8" screen firmware (`esp32-abs-tester-28/AbsTester28`); older ESP32 firmware rejects 20-field frames. The ECU testbench Mega (`arduino-mega/MegaTelemetry`) is a separate board and stays as it is.

Open MegaAbsTester.ino (keep PotGenerator.h, SpeedEncoder.h and SupplyVoltage.h in the same folder); it needs no libraries. Board: **Arduino Mega or Mega 2560**, processor ATmega2560 (also runs on a full-size Mega 2560). The Mega 2560 Pro uses a CH340 USB chip; install the WCH CH340 driver if Windows shows no COM port. USB Serial Monitor: 115200 baud.

What changed from the Nano: the ESP32 link uses the Mega's **hardware Serial1 (TX1, D18)** instead of SoftwareSerial on D5, so the frames no longer hold interrupts off. The rear / front speeds are set by a KY-040 rotary encoder (D22 / D23 / D24) instead of pots, the Declog pot is on A2, REF1 / battery on A3 / A4, and the generator outputs move to **D44 / D3 / D11** (each on its own 16-bit timer).

## Mega → testbench

| Mega pin | Connection | Shown as |
|---|---|---|
| D7 | Speed 1 (rear) wheel sensor and RPM, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250. RPM = Hz × 15000 ÷ 1560, max 15,000 (USB only) |
| D8 | Speed 2 (front) wheel sensor, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250 |
| D2 | Declog, 0–100 Hz square wave, 0–5 V | DECLOG bar, 0–100 Hz |
| D13 | TCS LED (the built-in LED) | Steady: both speeds match to the whole km/h. Blinking (500 ms): they differ |
| D12 | ABS LED, through a resistor to GND | On: D7 is at least 10 % and 5 km/h slower than D8 |
| A3 | Reference 1, 0–5 V at the pin | REF1 tile, mapped to 0–30 V |
| A4 | Battery, 0–5 V at the pin | BATTERY tile, mapped to 0–30 V |
| D22 | Speed encoder (KY-040) CLK | Turn: speed of the selected wheel |
| D23 | Speed encoder DT | |
| D24 | Speed encoder SW (button) | Press: REAR ↔ FRONT. Hold 1 s: both 0 km/h |
| 5V, GND | Speed encoder + and GND | |
| D44 | Generator 1 output (REAR), 0–1500 Hz square wave (1500 Hz = 250 km/h, about 14,400 rpm), 0–5 V, 50 % duty (set by the encoder) | Jumper to D7 to test speed 1 / RPM |
| D3 | Generator 2 output (FRONT), 0–1500 Hz square wave (set by the encoder) | Jumper to D8 to test speed 2 |
| A2 | Declog pot (PTV09 10 kΩ) wiper: pin 1 → GND, pin 2 → A2, pin 3 → 5V | Sets D11 |
| D11 | Generator 3 output, 0–100 Hz square wave (set by A2) | Jumper to D2 to test Declog |
| 5V, GND | Testbench 5 V and GND (and the ESP32 GND) | |

**Generators** (`PotGenerator.h`): an output set to 0 Hz stops LOW; the Serial Monitor shows the three output frequencies. Each output has its own 16-bit timer (D44 Timer5, D3 Timer3, D11 Timer1), so the waves are made entirely in hardware and stay exact while the frames are sent. Declog pot reading: every 100 ms the A2 pot takes 20 `analogRead`s 50 µs apart, averages them and maps 0–1023 to 0–100 Hz in whole Hz with `map()`.

## Speed encoder (`SpeedEncoder.h`)

One KY-040 sets both test speeds: CLK → D22, DT → D23, SW → D24, + → 5V, GND → GND (internal pull-ups on all three).

- **Turn**: the selected wheel's speed changes by 1 km/h per click; spun quickly (clicks under 40 ms apart) 5 km/h per click. 0–250 km/h, and the generator runs at km/h × 6 Hz (REAR → D44, FRONT → D3).
- **Short press**: switch between **REAR** and **FRONT**.
- **Hold 1 s**: both wheels to 0 km/h.

At power-up both are 0 km/h with REAR selected. The selected wheel is the last field of the `A1` frame (1 rear, 2 front), and page 1 of the 2.8" screen marks it in amber with **ADJUST** (the FRONT gauge title, or the REAR bar). To test ABS, set FRONT to e.g. 100 km/h, then REAR at least 10 % and 5 km/h lower (e.g. 85): D12 lights.

Turning is decoded in the Timer2 sampler (20 kHz) with a quadrature state table, so contact bounce cancels out and no click is lost while the loop is busy reading REF / battery. One click = one full CLK / DT cycle, counted at the detent (both HIGH). If clockwise makes the speed go down, set `REVERSE` to `true` in `SpeedEncoder.h` (or swap CLK and DT). Each click and press is logged on the USB Serial Monitor (`# SPEED ENC REAR 85 km/h`, `# SPEED ENC press -> adjusting FRONT`, `# SPEED ENC hold -> REAR and FRONT 0 km/h`).

D2, D7 and D8 are sampled by Timer2 (compare A) at 20 kHz: every 100 ms the frequency is the rising edges counted divided by the time between the first and last of them (about ±1 Hz; up to about 10 kHz per input). With Timers 1, 2, 3 and 5 in use, analogWrite() on D2, D3, D5, D9–D13 and D44–D46, tone() and Servo can't be used. Unconnected inputs pick up mains hum (about 60 Hz); a 10 kΩ resistor to GND on an unused input stops that.

**REF1 / battery mapping:** A3 and A4 read 0–5 V at the pin, mapped to 0–30 V by `getTrueVoltage()`, which interpolates linearly between the PCB calibration points (`calPinV` / `calTrueV`, same table as the ECU testbench): pin 0.00 / 0.15 / 0.85 / 1.72 / 2.59 / 3.48 / 4.35 / 5.00 V = 0 / 1 / 5 / 10 / 15 / 20 / 25 / 30 V, clamped to 0–30 V. Noise filter: 20 samples with the highest and lowest dropped, then a running average (about 1 s to settle). Never put more than 5 V on the pin.

USB output, once a second:

```
---- Generators ----
Rear   enc -> D44 :   510.0 Hz  (85 km/h)  <- encoder
Front  enc -> D3  :   600.0 Hz  (100 km/h)
Declog A2  -> D11 :     0.0 Hz  (max 100, OFF)  noise 1 counts
--------------------
D7 Frequency: 510.0 Hz
Speed 1: 85.0 km/h
RPM: 4904
D8 Frequency: 600.0 Hz
Speed 2: 100.0 km/h
TCS: BLINKING
ABS: ACTIVE
Declog (D2): 50.0 Hz
REF1 (A3): 12.00 V (pin 2.07 V)
Battery (A4): 13.80 V (pin 2.38 V)
Speed encoder: adjusting REAR
```

## Mega → ESP32

| Mega | ESP32 |
|---|---|
| TX1 (D18) → 1 kΩ resistor → | RX2 (GPIO16) |
| GND | GND |

115200 baud 8N1, one-way, 10 frames a second. The Mega's TX is 5 V and ESP32 pins are rated 3.3 V; the 1 kΩ series resistor limits the current (for full protection use a divider instead: TX1 → 1 kΩ → GPIO16 → 2 kΩ → GND). The `A1` frame carries key (1), brake (0), D7 Hz, speed 1, REF1, REF2 (0), current (0), battery, D8 Hz, speed 2, RPM, TCS (1 steady / 0 blinking), ABS (1 active), Declog Hz, the rear / front (encoder) and Declog (A2 pot) generator outputs (D44 / D3 / D11, Hz ×100; same slots as before), `0,0` where the old A5 page pot reading and page were, and the wheel the speed encoder adjusts (1 rear, 2 front), with an XOR checksum. The ESP32's 2.8" screen still says WAITING FOR NANO until the first frame arrives; that's only the label.

## Page selection

The A5 page pot is gone; pages are chosen with the ESP32's own KY-040 (CLK GPIO21, DT GPIO22, SW GPIO4: press = select, turn = pick, press = lock) or a swipe. See `esp32-abs-tester-28/AbsTester28/README.md`.

## Measured supply for REF / battery (`SupplyVoltage.h`)

The 0–30 V inputs no longer assume the board runs on exactly 5.00 V. Before each REF / battery read the chip's internal 1.1 V bandgap is read against the 5 V rail (AVcc = 1.1 V × 1023 / counts, filtered), and the pin volts are `counts × measured supply / 1023` before they go through the PCB calibration table. On USB the rail is often 4.7–5.1 V; at 4.85 V the old fixed 5.00 V read a 12.00 V input as about 11.6 V. The measured rail is printed on the USB Serial Monitor (`Supply (5V pin, measured)` / `5V:`).

**Calibrate once per board:** the bandgap is 1.0–1.2 V from chip to chip. Measure the board's 5V pin with a multimeter, compare it with the printed supply, and set `BANDGAP_V = 1.100 × meter volts / printed volts` in `SupplyVoltage.h`. Readings outside 4.0–5.5 V are ignored (the last good value is kept).
