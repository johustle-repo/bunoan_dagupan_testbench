# Arduino Mega 2560 Pro telemetry (Mega + ESP32)

For the Mega 2560 Pro (also runs on a full-size Mega 2560: same ATmega2560 and pin numbers). The sensor knobs and the ESP32 link (SoftwareSerial, D6 TX → ESP32 GPIO17, D5 ↔ GPIO16) are wired as on the Nano. The differences:

- **Separate RPM and speed inputs:** D7 is the RPM frequency, RPM = Hz × 15000 ÷ 1549 (1549 Hz = 15,000 rpm, max 15,000). D8 is the speed frequency, speed = Hz ÷ 6 km/h (max 250). Timer1 samples both at 20 kHz.
- **ABS on D9:** works like the old brake: while it is on, the speed on the ESP32 ramps to 0 at 100 km/h per second, and the ABS lamp turns orange.
- **Declog from the A9 pot:** Declog is the 0–100 Hz the A9 pot sets on D2. It is sent to the ESP32 and shown as DECLOG xx Hz on the 4" screen and the dashboard; the DECLOG lamp is on above 0 Hz. D10 is no longer used.
- **Smart key follows the Mega's power:** while the Mega is on and sending, every U6 frame reports KEY ON. When the Mega is off or unplugged, the ESP32 gets no frames for 3 s and shows KEY OFF. D11 is no longer used.
- **0–30 V inputs:** Reference 1 (A5), Reference 2 (A6) and the battery (A7) each come in through a voltage divider and are reported as 0–30 V. There is no current sensor (A8 is the frequency pot).
- **Frequency generators (A8 → D44, A9 → D2):** two Bourns PTV09 10 kΩ pots each set a 50 % duty, 0–5 V square wave: A8 → D44 at 0–2000 Hz (Timer5) and A9 → D2 at 0–100 Hz (Timer3). Both are hardware timer outputs, unaffected by the serial frames. Fully down the output stops LOW. Jumper an output into D7, D8 or an ECU input to test it. Code in PotGenerator.h. (D45 can't be used for the second one: it shares Timer5 with D44.)
- **U6 frame:** key, ABS, the five sensors, D7 Hz, D8 Hz, Reference 1 and 2, battery, Declog on/off, Declog Hz ×100. The receiver shows "MEGA (U6)".
- **USB serial monitor (9600 baud):** one line per update with the 5-sensor diagnostic formulas in SensorConversion.h, `ERR` for a sensor outside its valid range:
  `IAT: 25.0 C | ECT: 40.0 C | O2: 0.45 V | AFR: 14.7:1 | MAP: 55 kPa | TPS Angle: 12 % | TPS: 0.5V | RPM: 3000 | SPEED: 40.0 km/h | ABS: OFF | DECLOG: 50.00 Hz | REF1: 12.00 V | REF2: 5.00 V | BATV: 13.80 V | A8: 512 | GEN D44: 1000.0 Hz | A9: 512 | GEN D2: 50.00 Hz`
  - A8 / A9: `analogRead()` of each pot, 0–1023 (0 V–5 V); GEN D44 / GEN D2: the frequency on each output.
  - IAT (fault > 4.85 V or < 0.15 V): lookup 2.806 V = 15 °C, 2.300 = 25, 1.850 = 35, 1.550 = 45, 1.300 = 55.
  - ECT (fault > 4.85 V or < 0.10 V): lookup 3.564 V = 20 °C, 2.500 = 40, 1.500 = 65, 0.800 = 90, 0.300 = 110, 0.100 = 120.
  - O2 / AFR (fault > 1.20 V or < 0.02 V): 0.1 V = 15.2:1, 0.9 V = 14.0:1.
  - MAP (fault > 4.60 V): 0 V = 10 kPa, 4.21 V = 100 kPa.
  - TPS (fault > 4.75 V): 0 V = 0 %, 4.25 V = 100 %.
  - REF1 (A5), REF2 (A6) and BATV (A7): pin volts × 6 (12.5 kΩ / 2.5 kΩ divider).
  - Each input is the average of 20 readings 1 ms apart. The ESP32 gets the averaged ADC counts in the U6 frame.

**Receiver:** use `esp32-universal-v2/ReceiverV2`, which accepts U3 (Uno), U4 (Nano), U5 (older Mega firmware) and U6 (this firmware). The older receivers (esp32-universal, esp32-nano) reject U6 frames and count them as bad frames.

Open MegaTelemetry.ino with Buttons.h, SensorConversion.h, SerialLink.h and PotGenerator.h in the same folder.
Board: Tools > Board > Arduino AVR Boards > Arduino Mega or Mega 2560. Processor: ATmega2560 (Mega 2560). USB Serial Monitor: 9600 baud. The Mega 2560 Pro uses a CH340 USB chip; install the WCH CH340 driver if Windows shows no COM port.

## Mega pins

| Mega 2560 Pro pin | ECU testbench signal |
|---|---|
| A0 | AIT (IAT) knob signal |
| A1 | ECT knob signal |
| A2 | O2 knob signal |
| A3 | MAP knob signal |
| A4 | TPS knob signal |
| A5 | Reference 1, 0–30 V, through the divider below |
| A6 | Reference 2, 0–30 V, through the divider below |
| A7 | Battery voltage, 0–30 V, through the divider below |
| A8 | Frequency pot (PTV09 10 kΩ) wiper: pin 1 → GND, pin 2 → A8, pin 3 → 5V |
| D44 | Frequency generator output, 0–2000 Hz square wave, 0–5 V (set by the A8 pot) |
| A9 | Second frequency pot (PTV09 10 kΩ) wiper: pin 1 → GND, pin 2 → A9, pin 3 → 5V |
| D2 | Second frequency generator output, 0–100 Hz square wave, 0–5 V (set by the A9 pot) |
| D7 | RPM frequency (0–1639 Hz, 0–5 V square wave), sampled by Timer1 at 20 kHz |
| D8 | Speed frequency (0–1500 Hz = 0–250 km/h, 0–5 V square wave), sampled by Timer1 at 20 kHz |
| D9 | ABS, off LOW / on HIGH (INPUT_PULLUP, normally-closed to GND, like the old brake) |
| D10 | Unused (Declog now comes from the A9 pot) |
| D11 | Unused (smart key now follows the Mega's power) |
| D6 | SoftwareSerial TX, 19200 baud 8N1 → divider or level shifter → ESP32 GPIO17 |
| D5 | SoftwareSerial RX ↔ ESP32 GPIO16 (unused, one-way link) |
| 5V | Sensor knob supply |
| GND | Simulator ground and ESP32 GND (common ground required) |

D9 has the internal pull-up on, so an unconnected D9 reads ABS ON (and the ESP32 then pulls the speed to 0). Keep it driven LOW (or switched to GND) when off. On the Pro the headers are double rows: D8/D10 are on the top row, D7/D9 directly below them.

## 0–30 V voltage dividers (Reference 1, Reference 2, battery)

One divider per input: **input → 12.5 kΩ → Mega pin → 2.5 kΩ → GND**. It divides by 6, so 30 V reaches the pin as 5.0 V, and draws 2 mA at 30 V. The input's ground must be connected to the Mega GND.

- **Scaling:** input volts = pin volts × 6 (`VOLTAGE_DIVIDER_RATIO`), so 0–5 V at the pin is 0–30 V. With a 12 kΩ / 2 kΩ divider (divide by 7) set `VOLTAGE_DIVIDER_RATIO = 7.0`.
- **Supply:** the mapping assumes the Mega's 5 V is exactly 5.0 V. On USB it is often 4.6–4.9 V, which makes all three readings read high by the same percentage (about 6 % at 4.7 V).
- **Protection (recommended):** a 5.1 V Zener and a 100 nF capacitor from each pin to GND, against spikes, a reversed battery and alternator noise.
- **Accuracy:** 1 % resistors give about ±2 %. About 30 mV per ADC step.

To check a divider, compare the reported value with a multimeter reading at its input.

SoftwareSerial blocks interrupts while it sends a frame (about 26 ms), so Timer1 can't sample D7/D8 during that time. The sketch leaves that time out of the 1 s frequency window, as on the Nano.

**D6 outputs 5 V.** Use a divider (10 kΩ from D6 to GPIO17, 20 kΩ from GPIO17 to GND) or a unidirectional level shifter. Never connect D6 directly to an ESP32 GPIO.
