# ABS tester: ESP32 + 2.8" ILI9341

Shows the ABS tester screen for the Nano sender (`arduino-nano/NanoAbsTester`). Libraries: Adafruit GFX Library, Adafruit ILI9341, Adafruit BusIO. The Nano link uses the ESP32's hardware UART2 (no SoftwareSerial library). With the Mega 2560 Pro sender (`arduino-mega/MegaAbsTester`, link Mega TX1 (D18) → 1 kΩ → GPIO16), the screen, dashboard and serial messages say MEGA, and the pots are labelled REAR (A1) / FRONT (A0) / DECLOG (A2).

**Design:** everything is drawn anti-aliased (smooth edges on text, panels, the speedometer arc and needles) by `AaGraphics.h`, using the Barlow Semi Condensed typeface converted to 4-bit bitmaps in `AaFonts.h` (SIL Open Font License). Keep all the files in the sketch folder: `AbsTester28.ino`, `AbsDisplay.h`, `AaGraphics.h`, `AaFonts.h`, `DashboardPage.h` and `FreqInput.h`. To change font sizes, edit the `FONTS` list in `tools/make_aa_font.py` and regenerate: `python tools/make_aa_font.py <folder with BarlowSemiCondensed-*.ttf> esp32-abs-tester-28/AbsTester28/AaFonts.h`.

- **Power-up:** an ABS TESTER splash with a loading bar, then WAITING FOR MEGA / NO LINK until data arrives. There is no starter button any more (Nano D8 is the front sensor). When live data arrives the switch-on self-test sweeps both page 1 needles and speed bars up and back with all three warning lights lit (bulb check, 1.6 s).
- **Pages:** **swipe left** on the touchscreen for page 2, **swipe right** for page 1 (a quick sideways stroke of about 1/6 of the screen width). The footer shows the ABS light, the two page dots and the TCS light; REF1 and BAT are in the header. Page 1 has FRONT on the left and REAR on the right. Each swipe is logged on the USB serial monitor (`# SWIPE left (raw dx …, dy …) -> page 2`); if left and right come out swapped, set `SWIPE_FLIP` to `true` in AbsDisplay.h.
- **Top row (both pages):** **REF1** and **BAT** volts, with two small warning lights between them, **ABS** and **TCS** (amber when lit).
- **Page 1 (speeds):** two **speedometers** side by side, 0–250 km/h (numbers every 50, red from 210, lit arc cyan → amber → red): **REAR** (D7, red needle) and **FRONT** (D8, green needle; `--` with older Nano firmware), each with its digital speed under the centre and its **speed bar** below.
- **Page 2 (frequency):** one **0–2000 Hz gauge** for the ESP32's own **GPIO32** frequency input (numbers every 500, Hz readout in the centre), and the **DECLOG** bar below it: the Nano's D2 frequency, 0–100 Hz shown as **0–100 %** (with the Hz underneath). Page 2 works without the Nano (DECLOG then shows `--` / NO NANO).
- **Needles** ease toward each new reading (the Nano measures D7, D8 and D2 and sends a frame every 100 ms, GPIO32 is measured every 100 ms), so they move smoothly. The pot generator settings are no longer on the LCD (still in the JSON and the USB serial line).
- **Warning lights** (worked out on the ESP32 from the Nano's speeds, AbsDisplay.h):

  | Light | Meaning |
  |---|---|
  | **ABS** | Start-up check from the **rear** speed: **on** at switch-on (when live data arrives); **off** once the rear speed goes above 10 km/h; after that **on** again below 10 km/h and **off** at 0 km/h (and above 10). Losing the Nano link counts as switching off, so the next start begins with ABS on. |
  | **TCS** | **Blinking** (2 Hz) while the rear and front speeds differ (to the whole km/h); **off** when they are equal. |

  Both light for the switch-on bulb check. The Nano's own D12 / D13 LEDs keep their old rules. SENSOR is only on the web dashboard now.

## Wi-Fi hotspot and dashboard
- Hotspot **ABS-Tester**, password **ABSdemo2026** (change `AP_SSID` / `AP_PASSWORD` in AbsTester28.ino).
- Dashboard: **http://192.168.4.1/** on a phone or laptop joined to the hotspot. Same layout as the LCD: a short ABS TESTER splash, REF1 / BAT in the header, FRONT (left, green) and REAR (right, red) speedometers each with its speed bar, the DECLOG bar, and a footer with the round ABS / TCS lights, KEY ON and the REAR / FRONT / DECLOG pot Hz (JSON `gen_rear_hz`, `gen_front_hz`, `gen_declog_hz`), plus the WAITING FOR MEGA / NO LINK panel. Works side by side on a laptop and stacked on a phone. Updates 4 times a second.
- JSON: http://192.168.4.1/api/live (`link`, `key`, `brake`, `speed_kmh` (rear), `frequency_hz` (rear), `front`, `front_speed_kmh`, `front_frequency_hz`, `rpm`, `declog_hz`, `declog_percent`, `ref1_v`, `battery_v`, `gen_rear_hz` / `gen_front_hz` / `gen_declog_hz` (pot generator settings in whole Hz: Mega rear A1 → D44, front A0 → D3, Declog A2 → D11; null with older sender firmware), `abs_lamp` / `tcs_lamp` / `sensor_lamp` ("off", "on", "flash", or "ok" for a green SENSOR), `bad_frames`, `uptime_ms`).
- The page is in DashboardPage.h (one HTML file, no extra libraries).

**Touch:** only used for the page swipe. The XPT2046 shares the display SPI wires and needs **T_CLK → GPIO18, T_DIN → GPIO23, T_DO → GPIO19, T_CS → GPIO33**. Only the direction of a swipe is used, so no calibration is needed. **T_IRQ is not used**: leave it unconnected or on GPIO35 (never GPIO32, the frequency input).

## Wiring (same as esp32-universal)

| TFT label | ESP32 |
|---|---|
| VCC, LED | 3V3 |
| GND | GND |
| CS | GPIO27 |
| RESET | GPIO26 |
| DC | GPIO25 |
| SDI (MOSI) + T_DIN | GPIO23 |
| SCK + T_CLK | GPIO18 |
| SDO (MISO) + T_DO | GPIO19 |
| T_CS | GPIO33 |
| T_IRQ | not used: unconnected or GPIO35 (not GPIO32, the frequency input) |

**Frequency input (page 2):** a 0–2000 Hz square wave on **GPIO32**, e.g. the Nano's D9 or D10 pot generator (or the Mega's D44 / D3). ESP32 pins take 3.3 V at most, so a 5 V signal needs a divider: signal → 10 kΩ → GPIO32 → 20 kΩ → GND, and a common GND. Rising edges are timed in an interrupt (`FreqInput.h`, edges closer than 100 µs ignored as noise); the internal pull-down keeps an open input at 0 Hz. Also in the JSON as `input_hz` (and `page`) and on the USB serial line.

Serial link, no divider: Nano D5 (SoftwareSerial TX) → ESP32 RX2 (GPIO16), common GND, 115200 baud 8N1 (hardware UART2 on the ESP32). The ESP32 uses only the `A1` lines. The Nano's TX is 5 V and ESP32 pins are rated 3.3 V: a 1 kΩ series resistor limits the current if you want some protection.

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200, ESP32 core 2.0.17. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry.

## Page navigation (page pot on the Mega's A5)

Three pages: 1 SPEED (front / rear speedometers), 2 INPUT (GPIO32 gauge and DECLOG), 3 (placeholder for now). The Mega reads a 10 kΩ pot on A5 and splits 0–1023 into the pages: **0–341 → page 1, 342–682 → page 2, 683–1023 → page 3**. The screen jumps to the pot's page whenever that changes (and when the Mega link starts). Swipes still work (left = next page, right = previous) and hold until the pot is turned into another page. The footer shows `PAGE n/3` at the left and the page name at the right. Page changes are logged on the USB serial monitor (`# PAGE POT A5 … -> page n`). The pot reading and page arrive as two extra fields at the end of the `A1` frame; older sender firmware without them still works (swipe only).
