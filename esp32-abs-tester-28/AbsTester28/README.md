# ABS tester: ESP32 + 2.8" ILI9341

Shows the ABS tester screen for the Nano sender (`arduino-nano/NanoAbsTester`). Libraries: Adafruit GFX Library, Adafruit ILI9341, Adafruit BusIO. The Nano link uses the ESP32's hardware UART2 (no SoftwareSerial library). With the Mega 2560 Pro sender (`arduino-mega/MegaAbsTester`, link Mega TX1 (D18) → 1 kΩ → GPIO16), the screen, dashboard and serial messages say MEGA. The ESP32 tells the two apart by the frame length (the Mega adds the A5 page pot fields, 19 fields; the Nano sends 17) and labels the pots to match: Nano REAR A0 → D9 / FRONT A1 → D10 / DECLOG A2 → D3, Mega REAR A1 → D44 / FRONT A0 → D3 / DECLOG A2 → D11. Before the first frame the screen shows WAITING FOR DATA.

**Design:** everything is drawn anti-aliased (smooth edges on text, panels, the speedometer arc and needles) by `AaGraphics.h`, using the Barlow Semi Condensed typeface converted to 4-bit bitmaps in `AaFonts.h` (SIL Open Font License). Keep all the files in the sketch folder: `AbsTester28.ino`, `AbsDisplay.h`, `AaGraphics.h`, `AaFonts.h`, `DashboardPage.h` and `FreqInput.h`. To change font sizes, edit the `FONTS` list in `tools/make_aa_font.py` and regenerate: `python tools/make_aa_font.py <folder with BarlowSemiCondensed-*.ttf> esp32-abs-tester-28/AbsTester28/AaFonts.h`.

- **Power-up:** an ABS TESTER splash with a loading bar, then WAITING FOR DATA / NO LINK until data arrives. There is no starter button any more (Nano D8 is the front sensor). When live data arrives the switch-on self-test sweeps the page 1 needle and speed bar up and back with all three warning lights lit (bulb check, 1.6 s).
- **Pages:** **swipe left** on the touchscreen for page 2, **swipe right** for page 1 (a quick sideways stroke of about 1/6 of the screen width). The footer shows the page dots, with the ABS and TCS lights either side on page 1 only (pages 2 and 3 have no lights); REF1 and BAT are in the header. Page 1 has the FRONT speedometer with the REAR speed bar below. Each swipe is logged on the USB serial monitor (`# SWIPE left (raw dx …, dy …) -> page 2`); if left and right come out swapped, set `SWIPE_FLIP` to `true` in AbsDisplay.h.
- **Top row (both pages):** **REF1** and **BAT** volts, with two small warning lights between them, **ABS** and **TCS** (amber when lit).
- **Page 1 (speeds):** one full-width **FRONT speedometer** (D8, green needle; `--` with older Nano firmware), 0–250 km/h (numbers every 50, red from 210, lit arc cyan → amber → red) with the digital speed on the centre plate, and the **REAR speed bar** below it (bar and value in km/h). The LCD shows no pin names or raw Hz; the serial log and dashboard still do.
- **Page 2 (SENSOR):** a **SPEED SENSOR** gauge for the **Nano's D6** input (sent in the 21-field Nano frame) while live Nano data carries it, otherwise the ESP32's own **GPIO34** frequency input: **0–2000 Hz = 0–200 km/h** (10 Hz per km/h, numbers every 50, red from 180, km/h readout in the centre), and the **SPEED** bar below it in km/h (with the GPIO34 Hz underneath). **No pulses for 2.5 s = NO SIGNAL:** the centre plate shows `NO SIGNAL` / `--` with a red edge, the needle drops to 0 and the bar reads `NO SIGNAL ON GPIO34` in red. JSON: `input_kmh` (null with no signal) and `input_source` (`nano_d6` / `gpio34`); serial: `SENSOR NANO D6 …` / `SENSOR GPIO34 … Hz = … km/h` or `NO SIGNAL`.
- **Page 3 (DECLOG):** one **0–100 Hz DECLOG gauge** for the sender's D2 frequency (numbers every 25, Hz readout in the centre), and the **DECLOG** bar below it: 0–100 Hz shown as **0–100 %** (with the Hz underneath). Pages 2 and 3 work without the sender (DECLOG then shows 0 and NO LINK).
- **Needles** ease toward each new reading (the Nano measures D7, D8 and D2 and sends a frame every 100 ms, GPIO34 is measured every 100 ms), so they move smoothly. The pot generator settings are no longer on the LCD (still in the JSON and the USB serial line).
- **Warning lights** (worked out on the ESP32 from the Nano's speeds, AbsDisplay.h):

  | Light | Meaning |
  |---|---|
  | **ABS** | Start-up check from the **rear** speed: **on** at switch-on (when live data arrives); **off** once the rear speed goes above 10 km/h; after that **on** again below 10 km/h and **off** at 0 km/h (and above 10). Losing the Nano link counts as switching off, so the next start begins with ABS on. |
  | **TCS** | **Blinking** (2 Hz) while the rear and front speeds differ (to the whole km/h); **off** when they are equal. |

  Both light for the switch-on bulb check. The Nano's own D12 / D13 LEDs keep their old rules. SENSOR is only on the web dashboard now.

## Wi-Fi hotspot and dashboard
- Hotspot **ABS-Tester**, password **ABSdemo2026** (change `AP_SSID` / `AP_PASSWORD` in AbsTester28.ino).
- Dashboard: **http://192.168.4.1/** on a phone or laptop joined to the hotspot. Everything on the three LCD pages on one screen: REF1 / BAT in the header with the link pill; the FRONT 0–250 km/h speedometer (green) with the REAR speed bar below it; the SPEED SENSOR (0–200 km/h, red from 180, **NO SPEED SENSOR READING** with a red card edge when no pulses arrive) and the DECLOG gauge (0–100 Hz with the 0–100 % bar); a footer with the round ABS / TCS lights, KEY ON, the LCD page chip (`LCD 2/3 SENSOR · LOCKED`, amber while selecting) and the generator Hz (ENCODER REAR / FRONT with the adjusted wheel in amber and ADJUST on its gauge or bar, POT DECLOG; all POT with older sender firmware). Without live sender data a banner shows WAITING FOR DATA / NO LINK and which wire to check, and the sender's cards dim; the speed sensor keeps updating (the ESP32 measures it). Side by side on a laptop, stacked on a phone. Updates 4 times a second.
- JSON: http://192.168.4.1/api/live (`link`, `sender` ("nano", "mega" or null before the first frame), `key`, `brake`, `speed_kmh` (rear), `frequency_hz` (rear), `front`, `front_speed_kmh`, `front_frequency_hz`, `rpm`, `declog_hz`, `declog_percent`, `ref1_v`, `battery_v`, `gen_rear_hz` / `gen_front_hz` / `gen_declog_hz` (pot generator settings in whole Hz: Nano rear A0 → D9, front A1 → D10, Declog A2 → D3; Mega rear A1 → D44, front A0 → D3, Declog A2 → D11; null with older sender firmware), `abs_lamp` (with the Nano: its **D13** input, HIGH = on, LOW = off; otherwise the rear-speed switch-on check) / `tcs_lamp` / `sensor_lamp` ("off", "on", "flash", or "ok" for a green SENSOR), `bad_frames`, `uptime_ms`).
- The page is in DashboardPage.h (one HTML file, no extra libraries).

**Touch:** only used for the page swipe. The XPT2046 shares the display SPI wires and needs **T_CLK → GPIO18, T_DIN → GPIO23, T_DO → GPIO19, T_CS → GPIO33**. Only the direction of a swipe is used, so no calibration is needed. **T_IRQ is not used**: leave it unconnected or on GPIO35 (never GPIO34, the frequency input).

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
| T_IRQ | not used: unconnected or GPIO35 (not GPIO34, the frequency input) |

**Frequency input (page 2):** a 0–2000 Hz square wave on **GPIO34**, e.g. the Nano's D9 or D10 pot generator (or the Mega's D44 / D3). ESP32 pins take 3.3 V at most, so a 5 V signal needs a divider: signal → 10 kΩ → GPIO34 → 20 kΩ → GND, and a common GND. Rising edges are timed in an interrupt (`FreqInput.h`, edges closer than 100 µs ignored as noise); GPIO34 is input-only with **no internal pull-down**, so keep the divider's **20 kΩ to GND** fitted: it holds the pin low when the sensor is unplugged (a bare floating wire on GPIO34 picks up noise and can show a false speed). **Crosstalk guard:** GPIO33 (the touch panel's T_CS) switches on every touch poll; on GPIO32 (the old input pin, its neighbour) an open input picked that up as a phantom 50–100 Hz. The guard stays on GPIO34 as insurance. Edges during a touch read (and 100 µs after) now count only if they fit the rhythm of the real pulses before them, so an open input reads NO SIGNAL and a real sensor keeps all its edges. Also in the JSON as `input_hz` (and `page`) and on the USB serial line.

**Brake button:** a push button from **GPIO13 to GND** (internal pull-up, no resistor). While it is held, the REAR and FRONT speeds shown (LCD, dashboard, JSON `speed_kmh` / `front_speed_kmh`) drop by **10 km/h every 100 ms** from where they were at the press, down to 0 (250 km/h → 0 in 2.5 s), and stay at 0 while held; the FRONT needle eases down with it. Release it and the live speeds come back. Only the displayed km/h change (the measured Hz, RPM and the sender's TCS / ABS states are untouched); the ABS light follows the braked rear speed (not with the Nano, whose D13 input sets the ABS light). JSON `brake` is true for the sender's brake input or the button, `brake_button` for the button alone; the USB serial line adds `BRAKE GPIO13 rear … front … km/h` while held.

Serial link, no divider: Nano D5 (SoftwareSerial TX) → ESP32 RX2 (GPIO16), common GND, 115200 baud 8N1 (hardware UART2 on the ESP32). The ESP32 uses only the `A1` lines. The Nano's TX is 5 V and ESP32 pins are rated 3.3 V: a 1 kΩ series resistor limits the current if you want some protection.

**Page link to the Nano:** GPIO5 → Nano A6, GPIO14 → Nano A7 (3.3 V outputs, no resistor). The ESP32 drives them with the page on screen (page 1 = 5 HIGH, page 2 = 14 HIGH, page 3 = both HIGH), and the Nano runs only that page's signals: page 1 the D9 / D10 / D4 / D11 generators and D7 / D8 inputs, page 2 only the D6 sensor input (no outputs), page 3 only the D3 Declog generator and D2 input. Values of the other pages read 0 on the screen and dashboard.

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200, ESP32 core 2.0.17. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry.

## Speed encoder on the Mega or Nano (REAR / FRONT ADJUST marker)

The current Mega firmware sets the rear / front test speeds with a KY-040 on the Mega (D22 / D23 / D24, see `arduino-mega/MegaAbsTester/README.md`) and has no A5 page pot. Its frames have 20 fields: `0,0` in the page pot slots (so the pot is ignored), then the wheel the encoder adjusts (1 rear, 2 front). The Nano (`arduino-nano/NanoAbsTester`, encoder on A0 / A1 / A5) does the same with 18 fields: its 17, then the wheel. Page 1 marks that wheel in amber with **ADJUST**: an amber dot and ADJUST after the FRONT gauge title, or an amber stripe, REAR name and ADJUST in the REAR bar. Also in the JSON as `speed_encoder` (`"rear"`, `"front"`, or null for the Nano / older Mega) and on the USB serial line (`ENC REAR ->D44 … (adjusting REAR)`). Older 17- and 19-field frames still work. Page selection is the ESP32's own encoder below (or a swipe).

## Page navigation (page pot on the older Mega's A5)

Three pages: 1 SPEED (front / rear speedometers), 2 INPUT (GPIO34 gauge and INPUT bar), 3 DECLOG (Declog gauge and DECLOG bar). The Mega reads a 10 kΩ pot on A5 and splits 0–1023 into the pages: **0–341 → page 1, 342–682 → page 2, 683–1023 → page 3**. The screen jumps to the pot's page whenever that changes (and when the Mega link starts). Swipes still work (left = next page, right = previous) and hold until the pot is turned into another page. The footer shows `PAGE n/3` at the left and the page name at the right. Page changes are logged on the USB serial monitor (`# PAGE POT A5 … -> page n`). The pot reading and page arrive as two extra fields at the end of the `A1` frame; older sender firmware without them still works (swipe only).

## Rotary encoder (KY-040) page selection

| KY-040 | ESP32 |
|---|---|
| CLK | GPIO21 |
| DT | GPIO22 |
| SW | GPIO4 |
| GND | GND |
| + | **3V3** (required: the module's CLK / DT pull-ups go to +; left open they tie CLK and DT together). **Never 5V.** |

- **Press** → **SELECT**: the footer reads `SELECT n/3` in amber and the selected page's dot blinks. Turn the knob to move the selection (wraps round 1 → 2 → 3 → 1). The page shown doesn't change yet.
- **Press again** → **LOCKED**: the screen goes to the selected page and holds it. The footer reads `LOCKED n/3` in cyan. Swipes and the Mega's A5 page pot are ignored while locked, and turning does nothing.
- **Press again** → SELECT again, and so on.

Before the first press, swipes and the page pot change the page as before. While selecting, a swipe moves the selection. Encoder events are logged on the USB serial monitor (`# ENCODER press -> SELECT …`, `# ENCODER turn …`, `# ENCODER press -> LOCKED on page n`), and the JSON has `page_mode` (`free`, `select` or `locked`). If turning clockwise moves the wrong way, set `REVERSE` to `true` in `Encoder.h` (or swap CLK and DT). Pins and debounce are in `Encoder.h`. Reading: polled every loop, one step each time CLK goes LOW (DT ≠ CLK = clockwise), 50 ms button debounce. Each step and press is logged (`# ENCODER Direction: Clockwise | Position: n`, `# ENCODER Button Pressed!`), and the once-a-second line ends with `ENC CLK 1 DT 1 SW 1 pos n` (pin levels, 1 = high).
