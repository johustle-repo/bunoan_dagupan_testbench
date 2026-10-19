# Arduino Nano telemetry (sample set 2: Nano + ESP32)

Same firmware as arduino-uno/DashboardTelemetry. The Nano uses the same ATmega328P and the same pin numbers, so only names changed (sketch name, board check, USB JSON source "nano_live"). The ESP32 link uses the U4 frame (U3 plus the two reference voltages, then the D8 front wheel frequency × 100).

**Two wheel-speed sensors, no smart key:** D7 is the rear sensor and D8 the front one (D8 was the smart-key button). Acquisition starts at power-up, like the Mega. Each speed = its Hz ÷ 6 km/h, max 250, and ramps to 0 at 100 km/h per second while the brake is pressed. The U4 speed field and the USB `engine_rpm` value are now the rear speed in km/h (it was the 0–300 mapped RPM); the USB JSON also has `rear_speed_kmh`, `front_frequency_hz` and `front_speed_kmh`. The ESP32 receivers (universal v2, ABS tester, esp32-nano, esp32-universal) accept the longer U4 frame; reflash them with this version.

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
| A5 | Reference 1: testbench 5 V output (direct, max 5 V) |
| A6 | Reference 2: testbench 3.3 V output (direct) |
| D7 | Rear wheel-speed sensor / frequency input (0–5 V square wave), sampled by Timer1 at 20 kHz |
| D8 | Front wheel-speed sensor (0–5 V square wave), sampled with D7 |
| D9 | Brake switch, released LOW / pressed HIGH (INPUT_PULLUP, normally-closed to GND) |
| D6 | SoftwareSerial TX, 19200 baud 8N1 → level shifter → ESP32 GPIO17 |
| D5 | SoftwareSerial RX (unused, one-way link) |
| 5V | Sensor knob supply |
| GND | Simulator ground and ESP32 GND (common ground required) |

A4/A5 are the Nano's I2C pins, so I2C is unavailable; the INA219 battery sensor is on the ESP32 (GPIO21 SDA, GPIO22 SCL). A6 is analog-only; A7 is unused.

Both references are scaled by the Nano's supply, measured each cycle against the internal ~1.1 V bandgap, so a sagging 5 V rail shows up instead of always reading 5.00 V. The bandgap varies 1.0–1.2 V per chip: measure the Nano 5V pin with a multimeter and set BANDGAP_V = 1.100 × meter ÷ reported supply_v. The link frame is now U4 (U3 plus Reference 1 and 2 in mV), so update the ESP32 NanoReceiver at the same time.

The Nano D6 output is 5 V. Use a divider (10 kΩ from D6 to GPIO17, 20 kΩ from GPIO17 to GND) or a unidirectional level shifter. Never connect D6 directly to an ESP32 GPIO.
