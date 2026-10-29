# Arduino Mega + ESP32 testbench guide

The Arduino Mega 2560 Pro reads the testbench knobs and inputs, prints them on its USB serial monitor and sends them to the ESP32. The ESP32 shows them on the 4.0" touchscreen and on a Wi-Fi dashboard.

| Part | Sketch |
|---|---|
| Arduino Mega 2560 Pro (sender) | `arduino-mega/MegaTelemetry/MegaTelemetry.ino` |
| ESP32 + 4.0" ST7796S touchscreen (receiver) | `esp32-universal-v2/ReceiverV2/ReceiverV2.ino` |
| ESP32 + 4.0" ABS tester (optional receiver) | `esp32-abs-tester/AbsTester/AbsTester.ino` |

Each folder's README has the full pin tables. This guide covers how the parts work together.

## Power-up

The Mega starts with the smart key OFF: press the D11 button for KEY ON (readings start) and again for KEY OFF. With the key off the screen shows KEY OFF and only the battery is live. The separate ABS tester still runs on its own Nano (`arduino-nano/NanoAbsTester`). At power-up the ESP32 receiver plays an **ECU TESTBENCH** boot animation (about 3 s: a tachometer ring sweeps up, the title fades in and a loading bar fills), then shows the CLASSIC screen. The needle and bars sweep once when the first live data arrives.

## Wiring

| ECU testbench | Mega 2560 Pro |
|---|---|
| RPM | D7 (square wave, 0–5 V) |
| Speed | D8 (square wave, 0–5 V) |
| AIT / ECT / O2 / MAP / TPS | A0 / A1 / A2 / A3 / A4 (0–5 V) |
| ABS | D9, off LOW / on HIGH (ramps the speed to 0, like the old brake) |
| Declog | D10, off LOW / on HIGH |
| Smart key | D11, push button to GND (toggles) |
| REF 1 / REF 2 / Battery | A5 / A6 / A7, 0–30 V through a 12.5 kΩ / 2.5 kΩ divider (÷ 6) |

| Mega 2560 Pro | ESP32 |
|---|---|
| D6 (SoftwareSerial TX, 19200 baud) → 10 kΩ / 20 kΩ divider | GPIO17 |
| D5 | GPIO16 (optional, unused) |
| GND | GND (common with the simulator) |

D6 outputs 5 V: never connect it straight to an ESP32 pin.

## Formulas

The Mega and the ESP32 use the same formulas (`SensorConversion.h` in both sketches).

| Signal | Formula | ERR (fault) when |
|---|---|---|
| IAT | Lookup: 2.806 V = 15 °C, 2.300 = 25, 1.850 = 35, 1.550 = 45, 1.300 = 55 | < 0.15 V or > 4.85 V |
| ECT | Lookup: 3.564 V = 20 °C, 2.500 = 40, 1.500 = 65, 0.800 = 90, 0.300 = 110, 0.100 = 120 | < 0.10 V or > 4.85 V |
| O2 / AFR | AFR = 15.2 + (V − 0.1) × (14.0 − 15.2) / 0.8 (0.1 V = 15.2:1, 0.9 V = 14.0:1) | < 0.02 V or > 1.20 V |
| MAP | kPa = 10 + V × 90 / 4.21 (0 V = 10 kPa, 4.21 V = 100 kPa) | > 4.60 V |
| TPS | % = V × 100 / 4.25, clamped 0–100 | > 4.75 V |
| REF1 / REF2 / BATV | Pin volts × 6 | — |
| Speed | D8 Hz ÷ 6 km/h, max 250. Ramps to 0 at 100 km/h per second while ABS (D9) is on | — |
| RPM | D7 Hz × 15000 ÷ 1549 (1549 Hz = 15,000 rpm), max 15,000 | — |

Lookup values between table points are interpolated and clamped at the table ends. Each analog input is the average of 20 readings 1 ms apart. On the ESP32, a fault that lasts 2 s sets the matching check-engine DTC.

## Mega serial monitor

9600 baud, one line about every 500 ms:

```
IAT: 15.0 C | ECT: 59.4 C | O2: 0.15 V | AFR: 15.1:1 | MAP: 61 kPa | TPS Angle: 1 % | TPS: 0.0V | RPM: 3000 | SPEED: 40.0 km/h | ABS: OFF | DECLOG: OFF | REF1: 8.13 V | REF2: 15.58 V | BATV: 16.06 V
```

A sensor outside its valid range prints `ERR`.

## Mega → ESP32 link (U6 frame)

One-way SoftwareSerial, 19200 baud 8N1, from Mega D6 to ESP32 GPIO17. Each frame is a text line with an XOR checksum:

```
U6,key,ABS,IAT,ECT,O2,MAP,TPS,D7 Hz×100,D8 Hz×100,REF1 mV,REF2 mV,battery mV,declog*CS
```

The five sensors are sent as averaged ADC counts × 16; the ESP32 applies the formulas above and works out RPM from D7 and speed from D8. The receiver also accepts U3 (Uno), U4 (Nano) and U5 (older Mega firmware) frames.

## 4.0" touchscreen (ReceiverV2)

There are no tabs: **double-tap anywhere** (two taps within 0.4 s) for the next screen, CLASSIC → RACE → SENSORS. A single tap does nothing, and there is no touch-calibration screen.

- **CLASSIC:** lamps on the left (KEY, DECLOG, WINKER, CHECK, TEMP, ABS; ABS turns orange while D9 is on; TCS), a 0–15,000 rpm tachometer on a dial-face background (labels 0, 3, 6, 9, 12, 15 × 1000, red zone above 12,000) with the RPM digits, a readout grid (km/h, RPM, CURRENT (n/a on the Pro) / REFERENCE 1, REFERENCE 2, BATTERY), the DTC line, and the sensor column (AIT °C, ECT °C, O2 V, MAP kPa, TPS % with the throttle-body symbol).
- **RACE:** REF 1 and REF 2 across the top, an LED indicator strip, the curved segmented RPM bar, speed and RPM digits, and tiles for Bat, ECT, IAT, MAP, O2 V, AFR and TPS.
- **SENSORS:** per channel the pin voltage (bar with that sensor's fault limits marked), the converted value and the circuit status (OK, OUT OF SPEC, HIGH… / LOW… pending, DTC code), plus board, link, packet age, bad frames, references, frequency, RPM, battery, current and the Wi-Fi address.

## Wi-Fi dashboard

Join **ECU-Testbench** (password `ECUdemo2026`) and open http://192.168.4.1/. The Classic display matches the 4.0" CLASSIC screen: the same RPM dial, readout grid, lamps, DTC line and sensor column, with `ERR` for a faulty sensor. The Race display, Signals history, Results recording (CSV / PDF) and the sensor fault demo are on the same page. `/api/live` returns the readings as JSON.

The dashboard source is in `esp32-universal-v2/dashboard/dist`. After editing it, rebuild the embedded copy:

```
python tools/embed_dashboard.py esp32-universal-v2/dashboard/dist esp32-universal-v2/ReceiverV2/DashboardAssets.h
```

## Flashing

| Board | Arduino IDE settings | Libraries |
|---|---|---|
| Mega 2560 Pro | Arduino Mega or Mega 2560, ATmega2560 (CH340 USB driver) | — |
| ESP32 | ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200 (other settings boot-loop) | LovyanGFX 1.2.33, EspSoftwareSerial |

## Troubleshooting

| Symptom | Fix |
|---|---|
| Mega upload: "unable to open port" / "Access denied" | The Arduino IDE Serial Monitor has the port open. Close it and upload again. |
| ESP32 upload: "Wrong boot mode detected" | Hold BOOT, tap EN, release BOOT, then upload. Or hold BOOT while the output shows `Connecting...`. |
| ESP32 port disappears during upload | Replace the USB cable with a short data cable plugged straight into the PC. |
| Screen shows KEY OFF | Press the D11 smart-key button. |
| Screen shows WAITING or NO LINK | Check Mega D6 → divider → ESP32 GPIO17 and the common GND. |
| A sensor shows ERR | Its voltage is outside the range in the formula table; check the knob and its wiring. |
| REF / battery read high on USB power | The divider scaling assumes the Mega's 5 V is exactly 5.0 V; USB is often 4.6–4.9 V. |
