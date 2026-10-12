# Arduino Uno + ESP32 dashboard

This replaces the NodeMCU receiver with the earlier ESP32 board. The Uno continues reading A0–A4, D7 frequency, D8 smart key and D9 brake; its existing firmware and U3 telemetry protocol are unchanged.

## Connection

Uno D6 TX → 5 V-to-3.3 V level converter → ESP32 GPIO17 RX. Connect both GNDs. Uno D5 → ESP32 GPIO16 may remain unused; GPIO16 is the SoftwareSerial TX pin; no commands are transmitted. No return commands are sent. Power each development board through USB. Do not move the 5 V analog sensor wires to ESP32 GPIOs.

The UART is 19200 baud, 8N1 using EspSoftwareSerial. Install EspSoftwareSerial by Dirk Kaar / Peter Lerup from Arduino Library Manager. The declaration is SoftwareSerial nodemcu(17, 16): RX first, TX second. D16/D17 aliases are not standard for ESP32 Dev Module; numeric GPIO values are used. The ESP32 input must use 3.3 V logic. A divider can use 10 kΩ from Uno D6 to GPIO17 and 20 kΩ from GPIO17 to GND, or a suitable unidirectional UART level shifter.

## Dashboard

Wi-Fi: ECU-Testbench-ESP32

Password: ECUdemo2026

Open http://192.168.4.1/ . JSON telemetry is /api/live; health is /api/health. Demo fault activation/clear is POST /api/simulate-fault; an injected fault ramps the gauge down and it recovers smoothly. TPS learning is POST /api/tps-learn with action=closed, wot or reset. Units, mobile layout and PDF report export match the latest NodeMCU version.

## Source and build

UI: ../dashboard/dist . After UI edits, run `python tools/embed_dashboard.py esp32/dashboard/dist esp32/UnoReceiver/DashboardAssets.h` from the project root. Select ESP32 Dev Module (esp32:esp32:esp32) in Arduino IDE, then upload UnoReceiver.ino to the ESP32 port. Serial logs use the USB-to-UART connector at 115200 baud.

This build targets ESP32. Other ESP32 variants need their board target and RX pin reviewed before uploading.

## SoftwareSerial receiver
Install EspSoftwareSerial by Dirk Kaar and Peter Lerup. The sketch uses SoftwareSerial nodemcu(17, 16), RX GPIO17 and TX GPIO16, at 19200 baud, with a 256-byte buffer. D5/D6 aliases are not used on ESP32 Dev Module. Uno D6 transmits to GPIO17; GPIO16 connects to Uno D5 but no return commands are currently sent. Keep the working ESP32 core 2.0.17 and board settings.

## Sensor models and diagnostics
The Uno and the U3 link are unchanged. All conversion happens on the ESP32 in SensorConversion.h, fitted to the client points in ECU TESTBENCH DISPLAY.docx:

| Sensor | Model | Client points |
|---|---|---|
| ECT | NTC thermistor with 5 V pull-up, B = 3786 K | 3.00 V = 20 °C, 0.43 V = 100 °C |
| IAT | NTC thermistor with 5 V pull-up, B = 5148 K | 3.00 V = 30 °C, 1.72 V = 50 °C |
| MAP | Linear, 25.25 kPa/V | 0.78 V = 20 kPa, 4.00 V = 101.3 kPa |
| TPS | Learned closed / wide-open endpoints, stored in NVS (defaults ADC 76 / 893) | — |
| O2 | λ = signal volts, AFR = 14.7 × λ (AFR shown for λ 0.70–1.60, the client λ/AFR table) | 0.7 / 1.0 / 1.2 V = AFR 10.3 / 14.7 / 17.6 |

Values beyond the client range are still reported and flagged with in_spec=false. A signal below 0.2 V or above 4.8 V is treated as an open or shorted circuit: the value becomes null and, after 2 s, check engine turns on with a standard DTC (IAT P0112/P0113, ECT P0117/P0118, MAP P0107/P0108, TPS P0122/P0123). O2 has no automatic circuit check because its knob reaches 0 V. Injected demo faults report P0110, P0115, P0130, P0105 or P0120. At zero speed the dashboard flashes the Honda PGM-FI blink code (MAP 1, ECT 7, TPS 8, IAT 9, O2 21). The coolant warning turns on at ECT 105 °C and off at 100 °C. Turning the smart key on runs a 2 s lamp and needle self-test.

## Throttle angle, mixture and race display
TPS percent is also shown as a throttle-plate angle, 0 % = 0° (closed) to 100 % = 90° (wide open), on a protractor symbol. THROTTLE_MAX_DEG in app.js sets the full-open angle. O2 has a lambda symbol coloured rich / stoich / lean, a 0.70–1.60 lambda scale and the client lambda/AFR table with the live row highlighted. The Race display button switches to a digital race-dash layout (lamp strip, curved segmented speed bar, header tiles); the choice is remembered per browser.
