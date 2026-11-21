# Nano Speed / PWM Box

Arduino Nano (ATmega328P). Board **Arduino Nano**, processor **ATmega328P (Old Bootloader)** for the bench Nano. Libraries (Library Manager): **Adafruit SSD1306** and **Adafruit GFX Library**. USB Serial Monitor: 115200 baud.

One Nano, three sections (from the hand drawing and pin list, 2026-11-21):

1. **Fade:** an encoder long press (1 s) starts a **15 kHz PWM** on D3 that fades from **100 % to 0 % duty in 3 s**, shown on **3 LEDs**.
2. **Inputs:** Freq 1 / Freq 2 input are copied to **Freq 1 / Freq 2 output**. **D12 (60 Hz logic 1)** goes HIGH when Freq 1 input reaches 60 Hz (10 km/h) and stays HIGH (**LOCKED**) until the power is restarted or the Nano RESET button is pressed.
3. **Speed:** the **EC11 encoder** sets 0–250 km/h. **Freq 1 / Freq 2 output in pot** give km/h × 6 Hz (0–1500 Hz). A **TM1637** (4 digits) shows the speed and the fade; a 0.96" I2C **OLED**, if connected, shows the speed (Freq ÷ 6) on a 0–250 kph speedometer. The display type is **auto-detected**.

## Pins

| Nano | Dir | Function |
|---|---|---|
| **D10** | OUT | **Freq 1 output**: copy of Freq 1 input (D8), follows every edge a few µs later |
| **D9** | OUT | **Freq 2 output**: copy of Freq 2 input (D7). ⚠ On the earlier perfboard D9 was bridged to A5 (SCL); if the display stays blank, the start-up check on the Serial Monitor prints `!! pin 9 pulls SCL(A5) LOW` |
| **D8** | IN | **Freq 1 input**, 0–5 V square wave. Internal pull-up on: unconnected = 0 Hz, open-collector Hall sensors work |
| **D7** | IN | **Freq 2 input**, 0–5 V square wave. Internal pull-up on, as D8 |
| **D6** | OUT | **Freq 1 output in pot**, 0–1500 Hz square wave (encoder km/h × 6), 50 % duty, 0–5 V |
| **D5** | OUT | **Freq 2 output in pot**, the same frequency as D6, in phase |
| **D12** | OUT | **60 Hz logic 1, LOCKED**: LOW at power-up, HIGH once Freq 1 input reaches 60 Hz (10 km/h), then stays HIGH whatever the input does. Only a power restart or the Nano RESET button makes it LOW again |
| **D3** | OUT | **Fade**: 15 kHz PWM (15.04 kHz), duty **100 % → 0 % in 3 s** (linear), then LOW. **Or, knob on D3:** a 0–15000 Hz square wave, 50 % duty, set with the encoder (within ~0.4 %) |
| **A2 / A1 / A0** | IN | **EC11 encoder** A → A2, B → A1, C (button) → A0, + → 5V, GND → GND. **Short press:** switch the knob between **SPEED** and **D3**. **Turn (SPEED):** 0–250 km/h (D6 / D5 = km/h × 6 Hz), 1 km/h per click, 5 spun fast. **Turn (D3):** 0–15000 Hz, 100 Hz per click, 500 spun fast; D3 keeps it when the knob goes back to SPEED. **Long press (1 s):** start the D3 fade (again) |
| **D4 / D2** | 2-wire | **TM1637** CLK → D4, DIO → D2, VCC → 5V, GND → GND. Auto-detected |
| **A4 / A5** | I2C | **OLED** SDA → A4, SCL → A5, VCC → 5V, GND → GND (SSD1306 128×64, 0x3C or 0x3D). Auto-detected |
| GND | | Common ground with whatever drives D8 / D7 |

### Fade LEDs

| LED | 1 (bottom) | 2 | 3 (top) |
|---|---|---|---|
| Nano | **A3** | D11 | D13 |

Only 3 LEDs: D6 / D7 are Freq 1 output in pot / Freq 2 input, A1 / A2 are the encoder and D2 / D4 are the TM1637.

Wire each LED as: Nano pin → **220–330 Ω resistor** → LED long leg (+), LED short leg (−) → GND.

LEDs lit = D3 duty × 3, rounded up: all 3 at 100 %, then one goes out every 1 s of the 3 s fade, top first. All are off at 0 %. LED 3 (D13) is in parallel with the Nano's onboard L LED, so both blink briefly on reset.

### Display (auto-detected)

Each display is looked for at start-up and then every 2 s while it is missing, so it can be plugged in while the box runs; a TM1637 that stops answering is looked for again. Either one, or both at once, can be connected. The Serial Monitor prints `# TM1637 found (CLK D4, DIO D2)` / `# OLED found at 0x3C`, or `... not found`.

How: the TM1637 is sent a command and its acknowledge bit is read; the OLED is an I2C probe of 0x3C, then 0x3D.

**TM1637** (4 digits): shows `----` for 0.5 s at start-up.

| Shows | When |
|---|---|
| ` 120` | speed in km/h (= Freq output in pot ÷ 6) |
| `F 73` | `F` + fade duty % while the D3 fade runs (`F100` → `F  0`), then back to the speed |
| `15:00` | knob on D3: the D3 frequency in kHz, the colon is the point (`15:00` = 15.00 kHz, ` 2:50` = 2.50 kHz) |

**OLED** (SSD1306 128×64): speedometer 0–250 kph.

```
F73%                     [LOCK]      <- fade duty while fading / D12 locked
          ,--''''--.
 0      /     \     \    250         <- gauge 0-250 kph, ticks every 25
720Hz      120 kph                   <- Freq output in pot, speed = Hz / 6
IN1 120         IN2 120              <- Freq 1 / 2 input in km/h
```

With the knob on **D3** the same gauge shows the **D3 frequency, 0–15000 Hz** (scale ends `0` / `15k`, ticks every 1500 Hz): the big number is the D3 Hz, `D3` at the top left, and the bottom row turns white with `KNOB: D3  0-15000 Hz`. No speed is shown in D3 mode.

A 1.3" SH1106 OLED is not supported by this library (it would show a shifted picture).

## Changes from the previous version (rewire these)

| Pin | Was | Now |
|---|---|---|
| D10 | Freq Out (encoder speed) | **Freq 1 output** = copy of Freq 1 input |
| D9 | Not used (bridged to SCL) | **Freq 2 output** = copy of Freq 2 input |
| D7 | Fade LED 6 | **Freq 2 input** (that LED is removed) |
| A6 | Freq In 2 | Not used |
| D6 | Fade, 10 kHz → 0 Hz sweep | **Freq 1 output in pot** |
| D5 | Speed (D10 ÷ 6) | **Freq 2 output in pot** (same Hz as D6) |
| D3 | Fade LED 1 | **Fade**: 15 kHz, duty 100 % → 0 % (that LED is removed; 3 LEDs left: A3, D11, D13) |
| D12 | HIGH ≥ 10 km/h, LOW below 9.5 | **Latches** at 60 Hz until power restart or RESET |
| A0 / A1 / A2 | EC11 encoder | EC11 encoder (unchanged); now sets the D6 / D5 speed, long press = fade |
| A4 / A5 | 16×2 I2C LCD | 0.96" SSD1306 I2C OLED, auto-detected |
| D2 / D4 | Fade LEDs 4 / 5 | **TM1637** DIO / CLK, auto-detected |

## How it works

- **D6 / D5:** Timer1 in CTC mode with no pins; its compare interrupt toggles D6 and D5 in one write, so they are always in phase. (D5 / D6 are Timer0's PWM pins, and Timer0 runs millis(), so they can't be driven by a timer in hardware.)
- **D3:** Timer2 fast PWM, TOP 132, /8 = 15.04 kHz, 133 duty steps; the duty is updated every 5 ms. 100 % and 0 % hold the pin HIGH / LOW.
- **D10 / D9:** the D8 / D7 pin-change interrupts write the input level straight to the output.
- **Encoder:** decoded in the A2 / A1 pin-change interrupt (4 transitions per click).

## Settings (top of `NanoSpeedPwmBox.ino`)

`MAX_KMH` (250), `HZ_PER_KMH` (6), `LOCK_HZ` (59.5: 60 Hz with a little measuring margin), `FADE_MS` (3000), `FAST_STEP_KMH` (5), `STEPS_PER_CLICK` (4), `ENC_REVERSE`, `PIN_LED` (LED order).

Timers: Timer0 = millis(), Timer1 = D6 / D5, Timer2 = D3 PWM, so analogWrite(), tone() and Servo can't be used.
