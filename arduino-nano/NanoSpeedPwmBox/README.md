# Nano Speed / PWM Box

Arduino Nano (ATmega328P). Board **Arduino Nano**, processor **ATmega328P (Old Bootloader)** for the bench Nano. Library: **LiquidCrystal I2C** by Frank de Brabander (Library Manager; 1.1.4 tested). USB Serial Monitor: 115200 baud.

A rotary encoder sets a speed; two outputs give the matching wheel-speed frequency (**km/h × 6 Hz**, 0–1500 Hz = 0–250 km/h), D5 gives the speed itself (Hz = km/h), two inputs are measured, D12 goes HIGH from 10 km/h, and D6 sweeps from **10 kHz down to 0 Hz in 3 s** on an encoder long press, shown as a bar graph on **7 LEDs**. A 16×2 I2C LCD shows one page at a time.

## Connector (4 outputs, 2 inputs)

| Nano | Dir | Function |
|---|---|---|
| **D10** | OUT | Freq Out, 0–1500 Hz square wave (speed × 6), 50 % duty, 0–5 V |
| **D9** | — | **Not used.** On this board D9 is wired to A5 (LCD SCL), so the sketch leaves it as an input. Use D10 for Freq Out |
| **D6** | OUT | **Fade:** on an encoder long press, a 50 % square wave sliding linearly from **10 kHz to 0 Hz in 3 s**, then LOW. A new long press starts it again |
| **D5** | OUT | **Speed:** D10 ÷ 6, so its Hz = km/h (0–250 Hz), 50 % duty |
| **D8** | IN | Freq In 1, 0–5 V square wave, km/h = Hz ÷ 6. **Internal pull-up on**: unconnected it reads 0 Hz (no mains hum), and open-collector Hall sensors work |
| **A6** | IN | Freq In 2, 0–5 V square wave, km/h = Hz ÷ 6. A6 is an analog-only pin, so the ADC samples it 19 200 times a second: an edge counts when it rises above **3.5 V**, and it must drop below **1.5 V** before the next one. **No pull-up**: for an open-collector Hall sensor add a 10 kΩ resistor from A6 to 5V |
| GND | | Common ground with whatever drives D8 / A6 |

## Extra pins

| Part | Nano | Use |
|---|---|---|
| 10 km/h logic out | **D12** | HIGH while Freq In 1 (D8) ≥ 10 km/h (60 Hz), LOW again below 9.5 km/h |
| Rotary encoder (DFRobot EC11) | A → **A2**, B → **A1**, C (button) → **A0**, + → 5V, GND → GND | Turn: depends on the page (see below). Short press: next display page. **Long press (1 s): start the D6 fade** |
| 16×2 LCD, I2C backpack (PCF8574) | SDA → **A4**, SCL → **A5**, VCC → 5V, GND → GND | One page at a time, via the LiquidCrystal_I2C library. Address set by `LCD_ADDR` (0x27; change to 0x3F if nothing shows). Shows "Arduino Nano / Speed/PWM Box" for 1 s at start-up. If the screen is lit but blank, turn the contrast screw on the back |
| 7 fade LEDs | LED 1 → **D3**, 2 → **D11**, 3 → **A3**, 4 → **D2**, 5 → **D4**, 6 → **D7**, 7 → **D13** | Bar graph of the D6 frequency (see below) |

### Fade LEDs

Wire each LED as: Nano pin → **220–330 Ω resistor** → LED long leg (+), LED short leg (−) → GND. Put them in a row, LED 1 at the bottom, LED 7 at the top.

LEDs lit = D6 frequency ÷ 10 kHz × 7, rounded up: all 7 at 10 kHz, then one goes out about every 1430 Hz (every 0.43 s of the 3 s sweep), top first. All are off when D6 stops.

| D6 | LEDs |
|---|---|
| 10 000 Hz | ●●●●●●● |
| 7 000 Hz | ●●●●●○○ |
| 4 000 Hz | ●●●○○○○ |
| 1 000 Hz | ●○○○○○○ |
| 0 Hz | ○○○○○○○ |

LED 7 (D13) is in parallel with the Nano's onboard L LED, so both blink briefly on reset (the bootloader blinks D13).

### LCD pages

Short press: next page. A long press jumps to **FADE**. `█` = a lit LED.

```
OUT 120 km/h        IN1 D8 120.0km/h    IN2 A6 120.0km/h    FADE D6  7000 Hz
D10    720 Hz       720.0Hz  10k:ON     720.0Hz             [█████  ] RUN
```

The IN1 page also shows the D12 10 km/h output (`ON` / `--`). On the FADE page, `RUN` = the 3 s fade is running, `KNOB` = D6 is set by hand.


What turning the knob does (the page stays where it is):

| Page | Knob turns |
|---|---|
| **Out**, **In 1**, **In 2** | Speed 0–250 km/h, so D10 = km/h × 6 Hz and D5 = km/h. 1 km/h per click, 5 spun fast. On an In page you can loop an output back to the input and watch it |
| **FADE** | D6 frequency by hand, 0–10 000 Hz, 100 Hz per click, 500 Hz spun fast. The 7 LEDs follow it. Turning during a running fade stops the fade and carries on from where it was |

Free pins: A7 (analog input only). D0 / D1 are the USB serial port.

Earlier versions had Freq In 2 on D7, an active buzzer on D13 and a TM1637 display on D2 / D4. D7, D13, D2 and D4 now drive LEDs, Freq In 2 moved to A6, and the 16×2 LCD replaced the TM1637 (its I2C pins A4 / A5 used to drive LEDs 4 and 5).

## How the outputs are made

- **D10:** Timer1 in CTC mode toggles it in hardware (exact frequency). D9 (OC1A) is left off because it is wired to A5 on this board.
- **D5:** the Timer1 compare interrupt toggles D5 on every 6th D9 toggle, so D5 is exactly D9 ÷ 6.
- **D6:** Timer2 in CTC mode; its interrupt toggles D6. The frequency is updated every 10 ms along the 3 s sweep, with the prescaler stepping /8 → /32 → … → /1024 as it drops. Below about 31 Hz the output stops LOW.
- **LEDs:** updated from loop() whenever the number lit changes.

## Settings (top of `NanoSpeedPwmBox.ino`)

`MAX_KMH`, `HZ_PER_KMH` (6), `LOGIC_ON_KMH` / `LOGIC_OFF_KMH` (10 / 9.5), `FADE_START_HZ` (10000), `FADE_MS` (3000), `FADE_STEP_HZ` / `FADE_FAST_STEP_HZ` (100 / 500), `STEPS_PER_CLICK` (4), `ENC_REVERSE`, `PIN_LED` (LED order).

Timers: Timer0 = millis(), Timer1 = D9 / D10 / D5, Timer2 = D6 fade, so analogWrite(), tone() and Servo can't be used. The ADC runs free on A6, so analogRead() can't be used either.
