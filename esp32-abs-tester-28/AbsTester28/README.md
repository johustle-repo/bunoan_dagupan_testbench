# ABS tester: ESP32 + 2.8" ILI9341

Shows the ABS tester screen for the Nano sender (`arduino-nano/NanoAbsTester`). Libraries: Adafruit GFX Library, Adafruit ILI9341, Adafruit BusIO. The Nano link uses the ESP32's hardware UART2 (no SoftwareSerial library).

**Design:** everything is drawn anti-aliased (smooth edges on text, panels, the speedometer arc and needles) by `AaGraphics.h`, using the Barlow Semi Condensed typeface converted to 4-bit bitmaps in `AaFonts.h` (SIL Open Font License). Keep all four files in the sketch folder: `AbsTester28.ino`, `AbsDisplay.h`, `AaGraphics.h`, `AaFonts.h` (plus `DashboardPage.h`). To change font sizes, edit the `FONTS` list in `tools/make_aa_font.py` and regenerate: `python tools/make_aa_font.py <folder with BarlowSemiCondensed-*.ttf> esp32-abs-tester-28/AbsTester28/AaFonts.h`.

- **Power-up:** an ABS TESTER splash with a loading bar, then WAITING FOR NANO / NO LINK until data arrives. There is no starter button any more (Nano D8 is the front sensor). When live data arrives the switch-on self-test sweeps the gauge and both bars up and back with the brake lamp and all three warning lights lit (bulb check: LAMP CHECK, TEST pill, 1.6 s).
- **Live screen:** **speedometer**, 0–250 km/h (numbers every 50, red from 210), the lit arc fading cyan → amber → red with speed. The red needle and the digital readout in the centre are the rear wheel (D7); the thin green needle is the front wheel (D8, hidden with older Nano firmware). Under it the **warning lights** and the **REAR** / **FRONT** bars with each wheel's speed in km/h (speed 1 and speed 2 exactly as the Nano works them out: Hz ÷ 6, max 250). Right tiles: **REF1**, **REAR** km/h, **FRONT** km/h (-- with older Nano firmware) and **BATTERY**. Bottom: KEY, **RPM** from the Nano (D7 Hz × 15000 ÷ 1560; LAMP CHECK during the self-test) and DECLOG % (D7, 0–1639 Hz = 0–100 %).
- **Warning lights** (thresholds in AbsDisplay.h):

  | Light | Meaning |
  |---|---|
  | **(ABS)** amber | On: D7 (rear) is at least 10 % and 5 km/h slower than D8 (front). Same rule as the Nano's D12 LED. |
  | **TCS** amber | Steady: both speeds match to the whole km/h. Flashing: they differ. Same rule as the Nano's D13 LED. |
  | **SENSOR** | Green: both wheel sensors give pulses. Amber: only one does. Off: no pulses (wheels stopped). |

## Wi-Fi hotspot and dashboard
- Hotspot **ABS-Tester**, password **ABSdemo2026** (change `AP_SSID` / `AP_PASSWORD` in AbsTester28.ino).
- Dashboard: **http://192.168.4.1/** on a phone or laptop joined to the hotspot. Same design as the LCD: gauge, ABS / TCS / SENSOR lights, REAR and FRONT bars, REF1 / REAR / FRONT / BATTERY tiles, key / brake / RPM + DECLOG footer, and the WAITING FOR NANO / NO LINK panel. Updates 4 times a second.
- JSON: http://192.168.4.1/api/live (`link`, `key`, `brake`, `speed_kmh` (rear), `frequency_hz` (rear), `front`, `front_speed_kmh`, `front_frequency_hz`, `rpm`, `declog_percent`, `ref1_v`, `ref2_v`, `current_a`, `battery_v`, `abs_lamp` / `tcs_lamp` / `sensor_lamp` ("off", "on", "flash", or "ok" for a green SENSOR), `bad_frames`, `uptime_ms`).
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

Serial link, no divider: Nano D5 (SoftwareSerial TX) → ESP32 RX2 (GPIO16), common GND, 115200 baud 8N1 (hardware UART2 on the ESP32). The ESP32 uses only the `A1` lines. The Nano's TX is 5 V and ESP32 pins are rated 3.3 V: a 1 kΩ series resistor limits the current if you want some protection.

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200, ESP32 core 2.0.17. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry.
