# Arduino Uno + ESP32-C6 dashboard

This replaces the NodeMCU receiver with the earlier ESP32-C6 board. The Uno continues reading A0–A4, D7 frequency, D8 smart key and D9 brake; its existing firmware and U3 telemetry protocol are unchanged.

## Connection

Uno D6 TX → 5 V-to-3.3 V level converter → ESP32-C6 GPIO20 RX. Connect both GNDs. Uno D5 and ESP32 TX are unused. Power each development board through USB. Do not move the 5 V analog sensor wires to ESP32 GPIOs.

The UART is 19200 baud, 8N1 using hardware UART1. The ESP32 input must use 3.3 V logic. A divider can use 10 kΩ from Uno D6 to GPIO20 and 20 kΩ from GPIO20 to GND, or a suitable unidirectional UART level shifter.

## Dashboard

Wi-Fi: ECU-Testbench-C6

Password: ECUdemo2026

Open http://192.168.4.1/ . JSON telemetry is /api/live; health is /api/health. Demo fault activation/clear is POST /api/simulate-fault. Only manual injection activates check engine; the gauge ramps down and recovers smoothly. Units, mobile layout and PDF report export match the latest NodeMCU version.

## Source and build

UI: ../dashboard/dist . Embed assets with work/embed-esp32-dashboard.py from the project root after UI edits. Select ESP32C6 Dev Module (esp32:esp32:esp32c6) in Arduino IDE, then upload UnoReceiver.ino to the ESP32-C6 port. Use the COM USB connector for UART serial logs with USB CDC disabled; native USB requires the matching USB CDC setting.

This build targets ESP32-C6. Other ESP32 variants need their board target and RX pin reviewed before uploading.
