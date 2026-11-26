# Nano Sensor Simulator

Arduino Nano that replaces the five sensor knobs of the ECU testbench. It
outputs IAT, ECT, O2, MAP and TPS voltages into the Mega's A0–A4. One rotary
encoder sets them and a 16x2 I2C LCD shows them. Values are set in real units
and converted to volts with the same formulas as `SensorConversion.h`, so the
dashboard shows what the LCD shows.

## Wiring

| Nano | Connection |
|---|---|
| D5 | IAT → 10 kΩ → Mega **A0**, 10 µF from A0 side to GND |
| D6 | ECT → 10 kΩ → Mega **A1**, 10 µF to GND |
| D9 | O2 → 10 kΩ → Mega **A2**, **3.3 kΩ** and 10 µF to GND (0–1.24 V full scale) |
| D10 | MAP → 10 kΩ → Mega **A3**, 10 µF to GND |
| D11 | TPS → 10 kΩ → Mega **A4**, 10 µF to GND |
| A2 / A1 / A0 | Encoder CLK / DT / SW (internal pull-ups) |
| A4 / A5 | LCD SDA / SCL (backpack at 0x27; set `LCD_ADDR` to 0x3F if nothing shows) |
| GND | Common ground with the Mega |

Remove the knobs from A0–A4 first. Each output gets its own 10 kΩ + 10 µF
filter, with the capacitor's + leg on the Mega side and its − leg to GND.

## Encoder

| Action | Effect |
|---|---|
| Turn | Change the selected sensor (spin fast for 5× steps) |
| Short press | Next sensor: IAT → ECT → O2 → MAP → TPS |
| Long press (0.8 s) | Fault: OK → HIGH → LOW → OK (MAP / TPS: OK → HIGH → OK) |
| Turn while in fault | Clears the fault and goes back to the set value |

HIGH drives the output to full scale, above every ERR limit (like an open
sensor). LOW drives it to 0 V, below the IAT / ECT / O2 low limits (like a
short to ground).

## Ranges

| Sensor | Range | Step | Power-up |
|---|---|---|---|
| IAT | 15–55 °C | 1 °C | 25 °C |
| ECT | 20–120 °C | 1 °C | 90 °C |
| O2 | 0.02–1.20 V (LCD shows the AFR) | 0.01 V | 0.45 V |
| MAP | 10–100 kPa | 1 kPa | 35 kPa |
| TPS | 0–100 % | 1 % | 0 % |

## LCD

```
IAT  25°C 2.294V     selected sensor, value (or HIGH / LOW), output volts
[I] E  O! M  T       [ ] = selected, ! = sensor in fault
```

## Accuracy

The outputs scale with the Nano's 5 V rail. Measure the Nano's 5V pin and set
`NANO_VCC` in the sketch. A USB supply can be 4.7–5.1 V. IAT and ECT use
timer 0 PWM (980 Hz, about 13 mV ripple). O2, MAP and TPS run at 31 kHz.

## Build

Board: Arduino Nano (ATmega328P). Library: **LiquidCrystal I2C** (Frank de
Brabander). Serial at 115200 prints every change, for example
`# MAP 35 kPa -> 1.176 V (duty 60)`.
