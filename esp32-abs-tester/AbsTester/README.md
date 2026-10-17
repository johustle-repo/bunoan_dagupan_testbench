# ABS tester (ESP32 + 4.0" 480 x 320 touchscreen)

A separate sketch from `esp32-universal-v2`. The ESP32 receives the testbench frames (Mega U5, also Uno U3 / Nano U4) and shows one ABS tester screen. It shows only what an ABS unit depends on and no engine sensors:

| Screen item | Source |
|---|---|
| 0-200 km/h gauge and speed bar | Testbench D7 speed knob (same speed as the universal receiver; above 200 the needle stays at full scale, the digits keep counting) |
| BATTERY | Mega A7, 0-30 V. Green ≥ 13.2 V, orange < 11.8 V or > 15 V |
| CURRENT | Mega A8, 0-10 A |
| REF1 | Mega A5, 0-30 V |
| BRAKE ON / OFF | Brake switch D9 |
| Title corner | WAITING / NO LINK / KEY OFF, then SIM (no output pin) or the output frequency |

**Power-on animation:** the Mega has no smart-key button (D8 is unused), so every time the system powers up the ESP32 shows an ABS TESTER splash with a loading bar, then sweeps the needle and speed bar 0 → 200 → 0 km/h with the brake lamp lit (about 3 s) before live readings start.

**START / STOP** turns the wheel-speed output on and off; **RESET** turns it off. There is no Wi-Fi dashboard.

## Wheel-speed output
"SIM" means no output pin is set, so START only changes the screen. Set `ABS_OUTPUT_PIN` in AbsDisplay.h to a free pin (GPIO4, 13, 14, 21 or 22) to get a 3.3 V, 50 % square wave that follows the speed: about 7 Hz per km/h, from `ABS_TONE_TEETH` (48) and `ABS_WHEEL_CIRCUMFERENCE_M` (1.9 m). Most ABS inputs need more than 3.3 V, so drive them through a transistor or driver stage.

## Wiring
Same as esp32-universal-v2: Mega D6 → 10 kΩ / 20 kΩ divider → ESP32 GPIO17, common GND, 19200 baud. The 4.0" ST7796S + XPT2046 panel uses the same pins (CS 27, DC 25, RST 26, MOSI 23, SCK 18, MISO 19, T_CS 33, T_IRQ 32). Touch calibration is shared (NVS `lgfxtouch/cal`); hold the screen while pressing EN to calibrate again.

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200, ESP32 core 2.0.17. Libraries: LovyanGFX (1.2.33), EspSoftwareSerial. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry.
