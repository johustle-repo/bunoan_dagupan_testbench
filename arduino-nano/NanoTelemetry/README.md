# Arduino Nano telemetry (sample set 2: Nano + ESP32)

Same firmware as arduino-uno/DashboardTelemetry. The Nano uses the same ATmega328P and the same pin numbers, so only names changed (sketch name, board check, USB JSON source "nano_live"). The U3 link to the ESP32 is identical.

Open NanoTelemetry.ino with Buttons.h, RpmRamp.h, SensorConversion.h and SerialLink.h in the same folder.
Board: Tools > Board > Arduino AVR Boards > Arduino Nano. Processor: ATmega328P; for clone Nanos that fail to upload ("not in sync"), choose ATmega328P (Old Bootloader). USB Serial Monitor: 115200 baud.

## Nano pins

| Nano pin | Connection |
|---|---|
| A0 | IAT knob signal |
| A1 | ECT knob signal |
| A2 | O2 knob signal |
| A3 | MAP knob signal |
| A4 | TPS knob signal |
| D7 | Frequency input (0–1639 Hz, 0–5 V square wave), sampled by Timer1 at 20 kHz |
| D8 | Smart-key button, normally open to GND (INPUT_PULLUP, each press toggles) |
| D9 | Brake switch, released LOW / pressed HIGH (INPUT_PULLUP, normally-closed to GND) |
| D6 | SoftwareSerial TX, 19200 baud 8N1 → level shifter → ESP32 GPIO17 |
| D5 | SoftwareSerial RX (unused, one-way link) |
| 5V | Sensor knob supply |
| GND | Simulator ground and ESP32 GND (common ground required) |

A4/A5 are also the Nano's I2C pins; nothing else may use I2C. A6/A7 are unused.

The Nano D6 output is 5 V. Use a divider (10 kΩ from D6 to GPIO17, 20 kΩ from GPIO17 to GND) or a unidirectional level shifter. Never connect D6 directly to an ESP32 GPIO.
