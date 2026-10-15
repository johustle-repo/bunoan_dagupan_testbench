# ABS tester: ESP32 + 2.8" ILI9341

Shows the ABS tester screen for the Nano sender (`arduino-nano/NanoAbsTester`), drawn with the same libraries as the old 2.8" receiver (`esp32-universal/Receiver`): Adafruit GFX Library, Adafruit ILI9341, Adafruit BusIO, plus EspSoftwareSerial.

- **Key off (starter D8 not pressed):** only the title and "PRESS START". WAITING FOR NANO / NO LINK if no data arrives.
- **Key on:** **RPM gauge**, 0–15,000 rpm (scale ×1000) from the D7 frequency, amber from 9,000, red from the 12,000 redline (`RPM_MAX`, `RPM_REDLINE` in AbsDisplay.h), with the RPM value under the hub; under it a **REAR** speed bar (0–200 km/h, coloured like the gauge) and a **DC LOG** bar (the D7 frequency as 0–100 %, 0–1639 Hz); REF1, REF2, CURRENT and BATTERY tiles; KEY, BRAKE and D7 frequency along the bottom. Holding the brake (D9) drops the speed and the REAR bar to 0; DC LOG keeps following the knob.

## Wi-Fi hotspot and dashboard
- Hotspot **ABS-Tester**, password **ABSdemo2026** (change `AP_SSID` / `AP_PASSWORD` in AbsTester28.ino).
- Dashboard: **http://192.168.4.1/** on a phone or laptop joined to the hotspot. Same design as the LCD: gauge, REAR and DC LOG bars, REF1 / REF2 / CURRENT / BATTERY tiles, key / brake / Hz footer, and the PRESS START panel with the key off. Updates 4 times a second.
- JSON: http://192.168.4.1/api/live (`link`, `key`, `brake`, `speed_kmh`, `frequency_hz`, `dc_log_percent`, `rpm`, `ref1_v`, `ref2_v`, `current_a`, `battery_v`, `bad_frames`, `uptime_ms`).
- The page is in DashboardPage.h (one HTML file, no extra libraries).

The touch controller isn't used (the testbench buttons are the controls); its T_CS is held high so it stays off the bus.

## Wiring (same as esp32-universal)

| TFT label | ESP32 |
|---|---|
| VCC, LED | 3V3 |
| GND | GND |
| CS | GPIO27 |
| RESET | GPIO26 |
| DC | GPIO25 |
| SDI (MOSI) | GPIO23 |
| SCK | GPIO18 |
| SDO (MISO) | GPIO19 |
| T_CS | GPIO33 (held high) |

Nano D6 → 10 kΩ / 20 kΩ divider → GPIO17, Nano D5 – GPIO16 (unused, one-way link), common GND.

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200, ESP32 core 2.0.17. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry.
