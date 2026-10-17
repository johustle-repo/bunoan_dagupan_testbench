# Universal ESP32 receiver v2 (4.0" 480 x 320 touchscreen)

The universal receiver (Arduino Uno, Nano or Mega, detected from each packet) running on the 4.0" ST7796S + XPT2046 panel instead of the 2.8" ILI9341. Telemetry, the Wi-Fi dashboard, the API, DTC logic and TPS learning are the same as `esp32-universal/Receiver`. The INA219 is no longer used: the battery voltage comes from the Mega (A7, 0–30 V divider).

Sender sketches: `Arduino-Uno-Testbench-Firmware/DashboardTelemetry` (Uno, U3 frames), `arduino-nano/NanoTelemetry` (Nano, U4) or `arduino-mega/MegaTelemetry` (Mega 2560, U5: the U4 fields plus battery voltage and current; Reference 1/2 and the battery are 0–30 V divider inputs). /api/live reports `"board": "uno" | "nano" | "mega"` and the source `<board>_via_esp32`.

- **Speed and RPM** come from the D7 frequency for every board, as on the Mega: speed = Hz ÷ 6 km/h (max 250, ramps to 0 at 100 km/h per second with the brake or an injected fault) and RPM = Hz × 15000 ÷ 1549 (1549 Hz = 15,000 rpm, max 15,000).
- Wi-Fi: ECU-Testbench, password ECUdemo2026, http://192.168.4.1/
- /api/live, /api/health, /api/simulate-fault and /api/tps-learn work the same as in v1.

## What's new on the display
- **Three screens, no tabs:** CLASSIC, RACE and SENSORS. Double-tap anywhere on the screen to go to the next one; a single tap does nothing.
- **CLASSIC:** 0–15,000 rpm tachometer on a plain dial-face background (labels 0, 3, 6, 9, 12, 15 ×1000, red zone above 12,000) with the RPM digits in the centre, a km/h cell with the speed under the gauge, glowing lamps, a sensor column in a larger font, and a full-width DTC line.
- **RACE:** REF 1 and REF 2 across the top, an LED indicator strip (KEY, BRAKE, WINKER, CHECK, TEMP, ABS, TCS), curved 50-segment RPM bar, speed and RPM in seven-segment digits, and tiles for Bat, ECT, IAT, MAP, O2 V, AFR and TPS. The battery tile turns green when charging (≥ 13.2 V).
- **SENSORS (new):** one row per channel (IAT, ECT, O2, MAP, TPS) with a 0-5 V pin-voltage bar marking the 0.20 / 4.80 V fault thresholds, the voltage, the converted value, and a circuit status: OK, OUT OF SPEC, LOW... / HIGH... (DTC pending), the confirmed DTC code, or "INJ" for a fault injected from the dashboard. Below it: board (Uno U3 / Nano U4 / Mega U5), link state, packet age, bad frames, Reference 1/2, D7 frequency, engine RPM, battery (Mega only), speed, learned TPS endpoints, and the Wi-Fi address.
- The header shows the detected board (UNO / NANO / MEGA) next to the LIVE / KEY OFF / NO LINK pill.
- Gauges are drawn off-screen in bands and pushed to the panel, so there is no flicker and no full-screen buffer is needed (the ESP32 has no PSRAM).

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200 (`esp32:esp32:esp32:FlashFreq=40,FlashMode=dio,UploadSpeed=115200`), ESP32 core 2.0.17. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry. Close Serial Monitor before uploading.

Libraries: **LovyanGFX** (lovyan03, tested with 1.2.33) and EspSoftwareSerial (Dirk Kaar / Peter Lerup). Adafruit GFX / ILI9341 / XPT2046_Touchscreen are no longer needed.

## Uno / Nano / Mega link
| Uno, Nano or Mega | ESP32 |
|---|---|
| D6 (TX) → 10 kΩ / 20 kΩ divider or level shifter | GPIO17 (RX) |
| D5 | GPIO16 optional, unused |
| GND | GND |

19200 baud 8N1, one-way. Power each board from its own USB.

## Touchscreen (4.0" ST7796S + XPT2046)
Same wiring as ESP32_Touch_Games:

| TFT / touch label | ESP32 |
|---|---|
| VCC | 5V (VIN) |
| LED | 3V3 |
| GND | GND |
| CS | GPIO27 |
| RESET | GPIO26 |
| DC/RS | GPIO25 |
| SDI (MOSI) + T_DIN | GPIO23 |
| SCK + T_CLK | GPIO18 |
| SDO (MISO) + T_DO | GPIO19 |
| T_CS | GPIO33 |
| T_IRQ | GPIO32 (**required**: used for touch detection) |
| SD_* | not connected |

**No touch calibration:** the screen is switched with a double tap, which only needs to detect a press, so there is no calibration screen. At power-up the ESP32 plays an ECU TESTBENCH boot animation (about 3 s: a tachometer ring sweeps up, the title fades in and a loading bar fills) and then shows the CLASSIC screen. A calibration saved earlier in NVS `lgfxtouch/cal` is still applied if present.

The SPI clock is 27 MHz, the value the games sketch was tested with. Most ST7796S modules also run at 40 MHz (`TFT_SPI_HZ` in LocalDisplay.h), which makes the gauges about a third faster.

## Battery and references (from the Mega)
The INA219 has been removed (its input also stopped at 26 V). The Mega reads Reference 1 (A5), Reference 2 (A6) and the battery (A7), each through a 12.5 kΩ / 2.5 kΩ divider, as 0–30 V; see `arduino-mega/MegaTelemetry/README.md`. The battery is shown with the key off too. With an Uno or Nano the battery shows "n/a", and the Nano's references stay direct 0–5 V readings (5 V / 3.3 V checks). The current comes from Mega A8 (0–5 V = 0–10 A) and shows in the CLASSIC grid, on SENSORS and in the dashboard battery box. `/api/live` reports `battery_v` and `current_a` (both null without a Mega) and `reference_scale`; `battery_a` is gone. GPIO21/22 are now free.

## Source
UI: ../dashboard/dist (same as v1). After UI edits run `python tools/embed_dashboard.py esp32-universal-v2/dashboard/dist esp32-universal-v2/ReceiverV2/DashboardAssets.h` from the project root.
