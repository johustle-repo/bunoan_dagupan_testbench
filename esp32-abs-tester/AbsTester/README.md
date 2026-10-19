# ABS tester (ESP32 + 4.0" 480 x 320 touchscreen)

A separate sketch from `esp32-universal-v2`. The ESP32 receives the testbench frames (Mega U5, also Uno U3 / Nano U4) and shows one ABS tester screen. It shows only what an ABS unit depends on and no engine sensors:

| Screen item | Source |
|---|---|
| 0-250 km/h gauge, FRONT / REAR readouts and F / R bars | Two wheel-speed sensors: **rear on D7** (white needle) and **front on Nano / Mega D8** (green needle). Each **speed = Hz ÷ 6 km/h, max 250** (same formula as the Mega and esp32-universal-v2). Eases at 100 km/h per second; the brake (D9) pulls both to 0. Both frequencies are shown under the bars. Uno frames and older Nano / Mega firmware have no front sensor: the front needle is hidden and FRONT shows -- |
| BATTERY | Mega A7, 0-30 V. Green ≥ 13.2 V, orange < 11.8 V or > 15 V |
| CURRENT | Mega A8, 0-10 A |
| REF1 | Mega A5, 0-30 V |
| BRAKE ON / OFF | Brake switch D9 |
| Title corner | SELF TEST, WAITING / NO LINK / KEY OFF, then SIM (no output pin) or the output frequency |

**Switch-on animation:** the Mega has no smart-key button (D8 is unused), so every power-up plays an ABS warning-lamp splash (about 3 s): a ring sweeps round "ABS", the lamp brackets and the ABS TESTER title fade in, and a loading bar fills with the start-up steps. When live data first arrives (or the key turns on with an Uno / Nano) the switch-on self-test sweeps both needles and bars 0 → 250 → 0 km/h with the lamp lit and LAMP CHECK / SELF TEST shown (1.6 s), then live readings start. It runs again if the link drops and comes back.

**START / STOP** turns the wheel-speed outputs on and off; **RESET** turns it off. There is no Wi-Fi dashboard.

## Wheel-speed output
"SIM" means no output pin is set, so START only changes the screen. Set `ABS_OUTPUT_PIN` in AbsDisplay.h (`{rear, front}`) to free pins (GPIO4, 13, 14, 21 or 22) to get a 3.3 V, 50 % square wave per wheel at speed × 6 Hz (`ABS_HZ_PER_KMH`), so each gives back its input frequency and drops to 0 while braking. The title corner shows OUT ON while they run. Most ABS inputs need more than 3.3 V, so drive them through a transistor or driver stage.

## Wiring
Same as esp32-universal-v2: Mega D6 → 10 kΩ / 20 kΩ divider → ESP32 GPIO17, common GND, 19200 baud. The 4.0" ST7796S + XPT2046 panel uses the same pins (CS 27, DC 25, RST 26, MOSI 23, SCK 18, MISO 19, T_CS 33, T_IRQ 32). Touch calibration is shared (NVS `lgfxtouch/cal`); hold the screen while pressing EN to calibrate again.

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200, ESP32 core 2.0.17. Libraries: LovyanGFX (1.2.33), EspSoftwareSerial. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry.
