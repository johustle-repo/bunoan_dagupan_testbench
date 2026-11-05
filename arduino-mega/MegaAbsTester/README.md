# Arduino Mega 2560 Pro ABS tester sender

A second Mega 2560 Pro in place of the Nano (`arduino-nano/NanoAbsTester`) in the ABS tester. Same pins, same readings and the same `A1` frames, so the ESP32 + 2.8" screen (`esp32-abs-tester-28/AbsTester28`) needs no change. The ECU testbench Mega (`arduino-mega/MegaTelemetry`) is a separate board and stays as it is.

Open MegaAbsTester.ino (keep PotGenerator.h in the same folder); it needs no libraries. Board: **Arduino Mega or Mega 2560**, processor ATmega2560 (also runs on a full-size Mega 2560). The Mega 2560 Pro uses a CH340 USB chip; install the WCH CH340 driver if Windows shows no COM port. USB Serial Monitor: 115200 baud.

What changed from the Nano: the ESP32 link uses the Mega's **hardware Serial1 (TX1, D18)** instead of SoftwareSerial on D5, so the frames no longer hold interrupts off. The three pots stay on A2 / A3 / A4, but their outputs move to **D44 / D3 / D11** (each on its own 16-bit timer).

## Mega → testbench

| Mega pin | Connection | Shown as |
|---|---|---|
| D7 | Speed 1 (rear) wheel sensor and RPM, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250. RPM = Hz × 15000 ÷ 1560, max 15,000 (USB only) |
| D8 | Speed 2 (front) wheel sensor, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250 |
| D2 | Declog, 0–100 Hz square wave, 0–5 V | DECLOG bar, 0–100 Hz |
| D13 | TCS LED (the built-in LED) | Steady: both speeds match to the whole km/h. Blinking (500 ms): they differ |
| D12 | ABS LED, through a resistor to GND | On: D7 is at least 10 % and 5 km/h slower than D8 |
| A0 | Reference 1, 0–5 V at the pin | REF1 tile, mapped to 0–30 V |
| A1 | Battery, 0–5 V at the pin | BATTERY tile, mapped to 0–30 V |
| A2 | Frequency pot 1 (PTV09 10 kΩ) wiper: pin 1 → GND, pin 2 → A2, pin 3 → 5V | Sets D44 |
| D44 | Generator 1 output, 0–1600 Hz square wave (1500 Hz = 250 km/h, 1560 Hz = 15,000 rpm), 0–5 V, 50 % duty (set by A2) | Jumper to D7 to test speed 1 / RPM |
| A3 | Frequency pot 2 (PTV09 10 kΩ), wired like A2 | Sets D3 |
| D3 | Generator 2 output, 0–1600 Hz square wave (set by A3) | Jumper to D8 to test speed 2 |
| A4 | Frequency pot 3 (PTV09 10 kΩ), wired like A2 | Sets D11 |
| D11 | Generator 3 output, 0–100 Hz square wave (set by A4) | Jumper to D2 to test Declog |
| 5V, GND | Testbench 5 V and GND (and the ESP32 GND) | |

**Generators** (`PotGenerator.h`): each pot turned fully down stops its output LOW; the Serial Monitor shows the three output frequencies. Each output has its own 16-bit timer (D44 Timer5, D3 Timer3, D11 Timer1), so the waves are made entirely in hardware and stay exact while the frames are sent. Each pot reading takes 16 samples and averages the middle 8; the output only retunes once the pot moves by more than 2.5 counts, rounded to whole Hz (0.1 Hz on the 0–100 Hz range).

D2, D7 and D8 are sampled by Timer2 (compare A) at 20 kHz: every 100 ms the frequency is the rising edges counted divided by the time between the first and last of them (about ±1 Hz; up to about 10 kHz per input). With Timers 1, 2, 3 and 5 in use, analogWrite() on D2, D3, D5, D9–D13 and D44–D46, tone() and Servo can't be used. Unconnected inputs pick up mains hum (about 60 Hz); a 10 kΩ resistor to GND on an unused input stops that.

**REF1 / battery mapping:** A0 and A1 read 0–5 V at the pin, mapped to 0–30 V by `getTrueVoltage()`, which interpolates linearly between the PCB calibration points (`calPinV` / `calTrueV`, same table as the ECU testbench): pin 0.00 / 0.15 / 0.85 / 1.72 / 2.59 / 3.48 / 4.35 / 5.00 V = 0 / 1 / 5 / 10 / 15 / 20 / 25 / 30 V, clamped to 0–30 V. Noise filter: 20 samples with the highest and lowest dropped, then a running average (about 1 s to settle). Never put more than 5 V on the pin.

USB output, once a second:

```
---- Pot generators ----
Pot A2 -> D44 :  1500.0 Hz  (max 1600)  noise 1 counts
Pot A3 -> D3  :  1500.0 Hz  (max 1600)  noise 0 counts
Pot A4 -> D11 :     0.0 Hz  (max 100, OFF)  noise 1 counts
------------------------
D7 Frequency: 1500.0 Hz
Speed 1: 250.0 km/h
RPM: 14423
D8 Frequency: 1500.0 Hz
Speed 2: 250.0 km/h
TCS: ON
ABS: OFF
Declog (D2): 50.0 Hz
REF1 (A0): 12.00 V (pin 2.07 V)
Battery (A1): 13.80 V (pin 2.38 V)
```

## Mega → ESP32

| Mega | ESP32 |
|---|---|
| TX1 (D18) → 1 kΩ resistor → | RX2 (GPIO16) |
| GND | GND |

115200 baud 8N1, one-way, 10 frames a second. The Mega's TX is 5 V and ESP32 pins are rated 3.3 V; the 1 kΩ series resistor limits the current (for full protection use a divider instead: TX1 → 1 kΩ → GPIO16 → 2 kΩ → GND). The `A1` frame carries key (1), brake (0), D7 Hz, speed 1, REF1, REF2 (0), current (0), battery, D8 Hz, speed 2, RPM, TCS (1 steady / 0 blinking), ABS (1 active), Declog Hz and the A2 / A3 / A4 pot generator outputs (Hz ×100, shown as A2 / A3 / A4 in the ESP32's JSON, dashboard footer and USB serial line), with an XOR checksum. The ESP32's 2.8" screen still says WAITING FOR NANO until the first frame arrives; that's only the label.
