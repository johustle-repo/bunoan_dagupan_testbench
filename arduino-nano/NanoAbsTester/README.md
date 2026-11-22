# Arduino Uno / Nano ABS tester sender

Measures the two wheel-speed sensors and the Declog frequency, drives the TCS and ABS LEDs and sends the readings to the ESP32 + 2.8" screen (`esp32-abs-tester-28/AbsTester28`) as `A1` frames. It also has three frequency generators: rear → D9 and front → D10 (0–1500 Hz each, set together by a DFRobot Fermion EC11 rotary encoder (SEN0235) on A0 / A1 / A5, click = fresh start of page 1 at 0 km/h, double click = TCS test (rear +60 Hz for 1.5 s), hold = brake) and Declog → D3 (0–100 Hz). The Declog is set by a second encoder, the **page / Declog encoder** on A2 / D12 / A6: single press = select a page, double tap = lock it, long press = Declog adjust (turn for 0–100 Hz). Open NanoAbsTester.ino; it needs no libraries. Board: **Arduino Uno**, or Arduino Nano with processor ATmega328P (Nano clones that fail with "not in sync": ATmega328P (Old Bootloader)). The pins are the same on both. USB Serial Monitor: 115200 baud.

## Uno / Nano → testbench

| Pin | Connection | Shown as |
|---|---|---|
| D7 | Speed 1 (rear) wheel sensor and RPM, 0–5 V square wave | Speed = Hz ÷ 6 km/h, max 250 (below 10 km/h: Hz ÷ 5.4, 54–60 Hz = 10 km/h, see the low-speed band below). RPM = Hz × 15000 ÷ 1560, max 15,000 (USB only) |
| D8 | Speed 2 (front) wheel sensor, 0–5 V square wave, e.g. the Nano's own **D10** front generator | Speed = Hz ÷ 6 km/h, max 250 (same low-speed band as D7) |
| D2 | Declog, 0–100 Hz square wave, 0–5 V, e.g. the Nano's own **D3** Declog generator | DECLOG bar, 0–100 Hz |
| D6 | Speed sensor for page 2 of the ESP32 screen, 0–120 Hz square wave, 0–5 V straight in (plain INPUT, no pull-up, common GND). Checked before the reading is used: no pulses and D6 LOW = **OPEN OR SHORT SENSOR**, no pulses and D6 HIGH = **NO PULSE (STUCK HIGH)** on page 2 (23rd frame field: 0 OK, 1 open / short, 2 stuck HIGH; needs the matching ESP32 firmware), e.g. the Nano's own **D9** or **D10** | ESP32 page 2 SPEED SENSOR, Hz ÷ 6 = km/h (0–20); LOW / HIGH logic lamps (yellow / green) in the footer |
| D13 | ABS LED, through a resistor to GND (the on-board L LED lights with it). Was on D12 | On: D7 is at least 10 % and 5 km/h slower than D8 |
| A7 | ABS light input: the **real ABS ECU's lamp output** on the bench, or a test push button A7 → button → 5V. Fit **10 kΩ from A7 to GND**: A7 is analog-only with no internal pull-down, and the resistor holds it LOW the way the on-board LED held D13. Read with analogRead, HIGH above 512 (about 2.5 V). Was on D13 | **Released = ABS light on**, pressed = off. Sent as the 22nd `A1` field. No TCS LED (no free pin); TCS still shows on the ESP32 screen and dashboard |
| A2, D12, A6 | **Page / Declog encoder** (`DeclogEncoder.h`): **CLK (A) → A2, DT (B) → D12, SW → A6, plus 10 kΩ from A6 to 5V**, VCC → 5V, GND → GND | Single press = select page, double tap = lock, long press = Declog adjust (see below) |
| A3 | Reference 1, 0–5 V at the pin | REF1 tile, mapped to 0–30 V |
| A4 | Battery, 0–5 V at the pin | BATTERY tile, mapped to 0–30 V |
| 5V, GND | Testbench 5 V and GND (and the Mega / ESP32 GND when their generators drive D7 / D8 / D2) | |

REF2 and current are sent as 0. Timer2 runs the input sampler and Timer1 the D9 / D10 generators, so analogWrite(), tone() and the Servo library can't be used.

D2, D6, D7 and D8 are sampled by Timer2 (compare A) at 20 kHz (every 100 ms the frequency is the rising edges counted divided by the time between the first and last of them, about ±1 Hz; up to about 10 kHz per input). **Brake:** hold the EC11 knob button (A5) for 600 ms or more; the `A1` brake field is 1 while braking (see the speed encoder section). **REF1 / battery mapping:** A3 and A4 read 0–5 V at the pin, mapped to 0–30 V by `getTrueVoltage()`, which interpolates linearly between the PCB calibration points (`calPinV` / `calTrueV`, same table as the ECU testbench): pin 0.00 / 0.15 / 0.85 / 1.72 / 2.59 / 3.48 / 4.35 / 5.00 V = 0 / 1 / 5 / 10 / 15 / 20 / 25 / 30 V, clamped to 0–30 V. Same reading as the ECU testbench (`MegaTelemetry` `readDividerInput`): 20 readings 1 ms apart, plain average, pin volts from the measured 5 V rail. The Serial Monitor shows the pin volts too. Never put more than 5 V on the pin; a pot or knob there needs its ends on 5V and GND (a floating pin reads random values).

## Frequency generators (`PotGenerator.h`)

| Set by | Output | Range |
|---|---|---|
| Speed encoder, REAR | **D9** | 0–1500 Hz (1500 Hz = 250 km/h) |
| Speed encoder, FRONT | **D10** | 0–1500 Hz |
| Page / Declog encoder (turned on page 3) | **D3** | 0–100 Hz |
| D9 ÷ 6 (rear) | **D4** | 0–250 Hz (Hz = km/h) |
| D10 ÷ 15 (front) | **D11** | 0–100 Hz (1500 Hz → 100 Hz) |

The Declog pot is gone; the page / Declog encoder sets D3 (see its section below). Each output is a 50 % duty, 0–5 V square wave; at 0 Hz it stops LOW. Jumper **D9 → D7, D10 → D8, D3 → D2** (and D9 or D10 → **D6** for page 2) to test the tester itself. **D4** follows D9 at exactly one sixth of its frequency, the same ÷ 6 as the speed formula, so its Hz equals the wheel's km/h (100 km/h: D9 600 Hz → D4 100 Hz; 1500 Hz → 250 Hz), and **D11** follows D10 at one fifteenth (1500 Hz → 100 Hz), both 50 % duty: every 6th D9 edge / 15th D10 edge toggles them in the Timer1 interrupt, so they stay locked to the main outputs and stop LOW with them.

D9 and D10 share Timer1 but are independent: Timer1 runs free at 2 MHz and each compare channel's hardware sets or clears its pin exactly at the match, so the edges aren't moved by interrupts or the serial link (exact average frequency, steps of 0.5 µs). Declog on D3 is made by the 20 kHz Timer2 sampler (exact average frequency, up to about 50 µs of edge jitter, 0.5 % at 100 Hz).

## Speed encoder (`SpeedEncoder.h`)

One DFRobot Fermion EC11 rotary encoder (SEN0235) sets both test speeds. Every D pin is taken, so it uses the freed pot pins as digital inputs: **VCC → 5V, GND → GND, A → A0, B → A1, C (button) → A5** (internal pull-ups on all three, as well as the module's own). Decoding follows the DFRobot EC11 example: state = A << 1 | B, the `encTable` transition table, **4 transitions = one click** (one detent), run in the 20 kHz Timer2 sampler so no transition is lost while the loop is busy. If clockwise makes it slower set `REVERSE` to `true` in `SpeedEncoder.h` (or swap A and B).

- **Turn**: REAR and FRONT change together, 1 km/h per click; spun quickly (clicks under 40 ms apart) 5 km/h per click. 0–250 km/h, and the generators run at km/h × 6 Hz (REAR → D9 / D4, FRONT → D10 / D11).
- **ECU check:** each knob turn also gives a cut D9 / D10 output (no D7 / D8 return, see the no-input cut) a new 1 s try at the knob speed, and adds 1 to the turn counter (26th `A1` field) so the ESP32 lights its white CHECKING lamp for 3 s. The result per wheel is kept: pulses back while its output runs = OK, cut = no return. The 27th field is the knob setting in whole Hz (0–1500), sent even while D9 / D10 are cut, for the ESP32's blinking knob lamp. Sent as the 25th field: 0 not checked, 1 ECU OK, 2 REAR NO RETURN, 3 FRONT NO RETURN, 4 both = ECU DISCONNECTED (the ABS ECU unplugged from the testbench). Serial Monitor: `ECU: OK` / `ECU: DISCONNECTED (no D7 / D8 return)` once a second, `# ECU CHECK: knob turned -> outputs on for a 1 s try`.
- **Double click = TCS test** (page 1 only): two short clicks, the second pressed within **300 ms** of the first release. REAR = FRONT **+60 Hz (+10 km/h) for 1.5 s**, then REAR goes back to the same speed as FRONT, so TCS blinks on the ESP32 screen; double-clicking again during the pulse restarts it. A double click never also does a fresh start.
- **Single click = FRESH START of page 1**, held 30–600 ms (page 1 only); it runs **300 ms after the release**, once no second click came, as after power-up: the knob speed goes to **0 km/h**, D9 / D10 (and D4 / D11) stop at once (no ramp), a brake and any no-input cuts are cleared, and the D7 / D8 readings drop to 0 at once (instead of after the 2.5 s hold). **ECU reset:** the click also adds 1 to the reset counter (24th `A1` field); the ESP32 then switches its **GPIO13 relay** on for 2 s, which cuts the ABS ECU's power so it boots fresh with its latched error cleared (see the ESP32 README for the relay wiring). The ABS indicator keeps following A7 (the real ECU lamp). Serial Monitor: `# ECU RESET request N ...`. The button never changes the knob speed otherwise: turning carries on from the current speed.
- **Long press = BRAKE** (page 1 only), held 600 ms or more: both wheel outputs (D9 / D10, and D4 / D11 with them) drop **10 km/h every 100 ms** down to 0 (250 km/h → 0 in 2.5 s) and stay at 0 while held. Release: they climb back to the knob speed at the same rate, with no short click. Turning while braking sets the speed they climb back to. The `A1` frame’s brake field (2nd) is 1 while braking, so the ESP32 JSON `brake` reads true. Serial Monitor: `# BRAKE on` / `# BRAKE off`. Turn the knob down to bring both wheels to 0.

At power-up both are 0 km/h. The frame's 18th field (the single wheel being adjusted) is now always 0, so the 2.8" screen shows no **ADJUST** mark. REAR and FRONT always run at the same speed. Every A / B change is decoded in the 20 kHz Timer2 sampler, so invalid (bouncing) transitions cancel out and no click is lost during REF / battery reads or SoftwareSerial frames. If clockwise makes the speed go down, set `REVERSE` to `true` in `SpeedEncoder.h` (or swap A and B). Clicks and presses are logged on the USB Serial Monitor (`# Clockwise (CW) | Position: 12`, `# Button: Short Click`, `# SPEED ENC click -> FRESH START, speed 0: # SPEED ENC 0 km/h | FRONT 0 Hz | REAR 0 Hz`).

## Page / Declog encoder (`DeclogEncoder.h`, `PageSelect.h`)

A second rotary encoder with a push button replaces the ESP32 page button and the Declog pot. Wiring: **CLK (A) → A2, DT (B) → D12, SW → A6 + 10 kΩ A6 → 5V, VCC → 5V, GND → GND**. A6 is analog-only, so the button is read with analogRead (pressed below 300) and needs the pull-up. A2 / D12 use internal pull-ups.

| Gesture | Does |
|---|---|
| **Single press** | **SELECT**: turning moves the selection over pages 1–3 (ESP32 footer `SELECT n/3` in amber, the page's dot blinks). The page shown and its signals stay. A single press while selecting cancels. Acts 300 ms after the release, once no second press came. |
| **Double tap** (second press within 300 ms) | **LOCK** while selecting: the selected page is shown and its signals switch over (footer `LOCKED n/3` in cyan). Ignored when not selecting. |
| **Long press** (held 600 ms) | **DECLOG ADJUST**: jumps to page 3, and turning sets the Declog on D3, 0–100 Hz, 1 Hz per click, 5 Hz per click when spun quickly (footer `ADJUST 3/3` in amber). A long or single press leaves it; page 3 stays, locked. |
| Turn | Only in SELECT (moves the selection) or DECLOG ADJUST (sets the Hz); otherwise nothing. |

Power-up: page 1, Declog 0 Hz. Only the shown page's signals run: **page 1** D9 / D10 / D4 / D11 out, D7 / D8 in, D13 ABS LED; **page 2** D6 in only, no outputs; **page 3** D3 out, D2 in. The other inputs read 0, other outputs stop LOW, and the Declog setting is kept while off page 3. The speed knob is ignored off page 1 and keeps its speed. The page, the nav state (0 free, 1 selecting, 2 locked, 3 Declog adjust) and the selected page go to the ESP32 as the 28th–30th `A1` fields; the screen follows them.

A / B are decoded in the 20 kHz Timer2 sampler with the same transition table as the speed encoder (4 transitions = one click), so no click is lost. If clockwise turns the Declog down, set `REVERSE` to `true` in `DeclogEncoder.h` (or swap A and B). If only every second detent moves it, set `STEPS_PER_CLICK` to 2. Serial Monitor: `# DECLOG ENC single press -> SELECT ...`, `# DECLOG ENC turn -> select page 2`, `# DECLOG ENC double tap -> LOCKED page 2`, `# DECLOG ENC long press -> DECLOG ADJUST on page 3 ...`, `# DECLOG ENC turn -> Declog 12 Hz`.

USB output, once a second:

```
Page (encoder press): 3 DECLOG
D7 Frequency: 510.0 Hz
Speed 1: 85.0 km/h
RPM: 4904
D8 Frequency: 600.0 Hz
Speed 2: 100.0 km/h
TCS: BLINKING
ABS: ACTIVE
Declog (D2): 50.0 Hz
Encoder rear -> D9: 600 Hz (+0 Hz offset), front -> D10: 600 Hz | knob 100 km/h
Page / Declog encoder: A2 1 D12 1 SW A6 1023 (pressed below 300) | pos 50 | Declog 50 Hz
Declog encoder -> D3: 50 Hz
REF1 (A3): 12.00 V (pin 2.07 V)
Battery (A4): 13.80 V (pin 2.38 V)
```

## Nano → ESP32

| Nano | ESP32 |
|---|---|
| D5 (TX) | RX2 (GPIO16), direct (no divider) |
| GND | GND |

SoftwareSerial on D5, 115200 baud 8N1, one-way, 10 frames a second; the USB Serial Monitor stays on the normal Serial port. Sending blocks interrupts for about 6 ms per frame; that time is left out of the frequency window. The `A1` frame carries D7 Hz, speed 1, REF1, REF2 (0), current (0), battery, D8 Hz, speed 2, RPM, TCS (1 steady / 0 blinking), ABS (1 active), Declog Hz, the rear / front / Declog generator outputs in Hz × 100, 0,0 (the Mega's old page pot slots), the single wheel the speed encoder adjusts (always 0: the knob drives both) , the D6 speed sensor Hz × 100, the A7 ABS light input (1 = on), the D6 sensor state, the knob-click ECU reset counter, the ECU check code (0–4), the knob turn counter, the knob setting in Hz and the page 1–3, nav state and selected page from the page / Declog encoder (30 fields, so flash the matching ESP32 firmware: older ESP32 firmware rejects 30-field frames); the ESP32 shows them as they are (the generator settings in its JSON and USB serial line). The Nano's TX is 5 V and ESP32 pins are rated 3.3 V: a 1 kΩ series resistor limits the current if you want some protection.

## Measured supply for REF / battery (`SupplyVoltage.h`)

The 0–30 V inputs no longer assume the board runs on exactly 5.00 V. Before each REF / battery read the chip's internal 1.1 V bandgap is read against the 5 V rail (AVcc = 1.1 V × 1023 / counts, filtered), and the pin volts are `counts × measured supply / 1023` before they go through the PCB calibration table. On USB the rail is often 4.7–5.1 V; at 4.85 V the old fixed 5.00 V read a 12.00 V input as about 11.6 V. The measured rail is printed on the USB Serial Monitor (`Supply (5V pin, measured)` / `5V:`).

**Calibrate once per board:** the bandgap is 1.0–1.2 V from chip to chip. Measure the board's 5V pin with a multimeter, compare it with the printed supply, and set `BANDGAP_V = 1.100 × meter volts / printed volts` in `SupplyVoltage.h`. Readings outside 4.0–5.5 V are ignored (the last good value is kept).

## Low-speed band (ECU ABS lamp at 10 km/h)

The bench ABS ECU turns its warning lamp off at about **55 Hz** (measured on 2026-11-19 in 1 Hz steps: on at 54.0 Hz, off at 55.4 Hz), which is 9.2 km/h with Hz ÷ 6. To make it go off at **10 km/h**, only the 0–10 km/h band is rescaled:

| Knob | D9 / D10 output | Nano / TFT speed |
|---|---|---|
| 1–9 km/h | km/h × 5.4 (9 km/h = 49 Hz, clear of the ECU's 55 Hz) | Hz ÷ 5.4 |
| 10 km/h and up | km/h × 6 (10 km/h = 60 Hz), unchanged | Hz ÷ 6 (54–60 Hz reads 10 km/h) |

Real wheel sensors on D7 / D8 use the same reading, so below 10 km/h they read about 10 % above plain Hz ÷ 6. `LOW_BAND_KMH` and `frontHz()` are in `SpeedEncoder.h`, `hzToKmh()` in `NanoAbsTester.ino`. `FINE_TEST` in `SpeedEncoder.h` (0 = normal) turns the knob into 1 Hz steps for measuring such thresholds.

## No-input cut (per wheel)

Each wheel output is cut when its own input gives no pulses: **D7 (rear in) → D9 (rear out)** and **D8 (front in) → D10 (front out)**. While an output should be running, its input must show a rising edge at least every **1 s** (`INPUT_WAIT_MS` in `SpeedEncoder.h`), which is also the start-up window after the output starts. Otherwise that output stops LOW (and D4 / D11 with it). The other wheel keeps running. A cut output comes back **by itself as soon as its input shows pulses again** (when D7 / D8 are fed from somewhere other than D9 / D10), or on a **short knob click**, which gives it a new 1 s try (a click is also a fresh start at 0 km/h, so it applies once the knob is turned up again). **Turning the knob never restarts a cut output**; it only sets the speed it comes back at. Serial Monitor: `# INPUT CHECK: no D7 input -> D9 REAR output cut ...`, and the report line adds `D9 REAR CUT (no D7 input)` / `D10 FRONT CUT (no D8 input)` while cut. The check uses raw pulses, not the displayed Hz (which holds its last value for 2.5 s).
