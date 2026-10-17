# Arduino Mega 2560 telemetry (Mega + ESP32)

Same firmware as arduino-nano/NanoTelemetry, ported to the ATmega2560. The sensor knobs, D7 and the ESP32 link (SoftwareSerial, D6 TX → ESP32 GPIO17, D5 ↔ GPIO16) are wired as on the Nano. The differences:

- **0–30 V inputs:** Reference 1 (A5), Reference 2 (A6) and the battery (A7) each come in through a voltage divider and are reported as 0–30 V. The battery replaces the INA219 on the ESP32.
- **Current on A8:** a 0–5 V current-sensor output, mapped 0–5 V → 0–10 A (amps = reading × 10 ÷ 1023).
- **U5 frame**: the Nano's U4 fields (U3 plus Reference 1 and 2 in mV) plus the battery in mV and the current in mA. The receiver uses it to show "Arduino Mega".
- **RPM and speed from D7:** RPM = Hz × 15000 ÷ 1549 (0 Hz = 0 rpm, 1549 Hz = 15,000 rpm, max 15,000 rpm) and speed = Hz ÷ 6 km/h (max 250 km/h). Speed goes to the ESP32 in the U5 frame (ramps to 0 at 100 km/h per second while the brake is pressed); RPM is in the USB output and the ESP32 works out RPM from the frequency in the frame.
- **USB serial monitor (9600 baud):** one line per update with the 5-sensor diagnostic formulas in SensorConversion.h, `ERR` for a sensor outside its valid range:
  `IAT: 25.0 C | ECT: 40.0 C | O2: 0.45 V | AFR: 14.7:1 | MAP: 55 kPa | TPS Angle: 12 % | TPS: 0.5V | REF1: 12.00 V | REF2: 5.00 V | BATV: 13.80 V`
  - IAT (fault > 4.85 V or < 0.15 V): lookup 2.806 V = 15 °C, 2.300 = 25, 1.850 = 35, 1.550 = 45, 1.300 = 55.
  - ECT (fault > 4.85 V or < 0.10 V): lookup 3.564 V = 20 °C, 2.500 = 40, 1.500 = 65, 0.800 = 90, 0.300 = 110, 0.100 = 120.
  - O2 / AFR (fault > 1.20 V or < 0.02 V): 0.1 V = 15.2:1, 0.9 V = 14.0:1.
  - MAP (fault > 4.60 V): 0 V = 10 kPa, 4.21 V = 100 kPa.
  - TPS (fault > 4.75 V): 0 V = 0 %, 4.25 V = 100 %.
  - REF1 (A5), REF2 (A6) and BATV (A7): pin volts × 6 (12.5 kΩ / 2.5 kΩ divider).
  - Each input is the average of 20 readings 1 ms apart. The ESP32 still gets the averaged ADC counts in the U5 frame, as before.

**Receiver:** use `esp32-universal-v2/ReceiverV2`, which accepts U3 (Uno), U4 (Nano) and U5 (Mega). The older receivers (esp32-universal, esp32-nano) reject U5 frames and count them as bad frames.

Open MegaTelemetry.ino with Buttons.h, RpmRamp.h, SensorConversion.h and SerialLink.h in the same folder.
Board: Tools > Board > Arduino AVR Boards > Arduino Mega or Mega 2560. Processor: ATmega2560 (Mega 2560). USB Serial Monitor: 9600 baud.

## Mega pins

| Mega pin | Connection |
|---|---|
| A0 | IAT knob signal |
| A1 | ECT knob signal |
| A2 | O2 knob signal |
| A3 | MAP knob signal |
| A4 | TPS knob signal |
| A5 | Reference 1, 0–30 V, through the divider below |
| A6 | Reference 2, 0–30 V, through the divider below |
| A7 | Battery voltage, 0–30 V, through the divider below |
| A8 | Current sensor output, 0–5 V = 0–10 A (direct, max 5 V) |
| D7 | Frequency input (0–1639 Hz, 0–5 V square wave), sampled by Timer1 at 20 kHz |
| D8 | Not used. There is no smart-key button: readings start at power-up and the ESP32 plays its power-on animation |
| D9 | Brake switch, released LOW / pressed HIGH (INPUT_PULLUP, normally-closed to GND) |
| D6 | SoftwareSerial TX, 19200 baud 8N1 → divider or level shifter → ESP32 GPIO17 |
| D5 | SoftwareSerial RX ↔ ESP32 GPIO16 (unused, one-way link) |
| 5V | Sensor knob supply |
| GND | Simulator ground and ESP32 GND (common ground required) |

## 0–30 V voltage dividers (Reference 1, Reference 2, battery)

One divider per input: **input → 12.5 kΩ → Mega pin → 2.5 kΩ → GND**. It divides by 6, so 30 V reaches the pin as 5.0 V, and draws 2 mA at 30 V. The input's ground must be connected to the Mega GND.

- **Scaling:** input volts = pin volts × 6 (`VOLTAGE_DIVIDER_RATIO`), so 0–5 V at the pin is 0–30 V. With a 12 kΩ / 2 kΩ divider (divide by 7) set `VOLTAGE_DIVIDER_RATIO = 7.0`.
- **Supply:** the mapping assumes the Mega's 5 V is exactly 5.0 V. On USB it is often 4.6–4.9 V, which makes all three readings read high by the same percentage (about 6 % at 4.7 V).
- **Protection (recommended):** a 5.1 V Zener and a 100 nF capacitor from each pin to GND, against spikes, a reversed battery and alternator noise.
- **Accuracy:** 1 % resistors give about ±2 %. About 30 mV per ADC step.

To check a divider, compare the reported value with a multimeter reading at its input.

SoftwareSerial blocks interrupts while it sends a frame (about 26 ms), so Timer1 can't sample D7 during that time. The sketch leaves that time out of the 1 s frequency window, as on the Nano.

**D6 outputs 5 V.** Use a divider (10 kΩ from D6 to GPIO17, 20 kΩ from GPIO17 to GND) or a unidirectional level shifter. Never connect D6 directly to an ESP32 GPIO.
