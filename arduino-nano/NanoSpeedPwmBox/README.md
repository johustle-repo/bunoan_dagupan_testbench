# Nano Speed / PWM Box

Arduino Nano (ATmega328P). Board **Arduino Nano**, processor **ATmega328P (Old Bootloader)** for the bench Nano. No libraries. USB Serial Monitor: 115200 baud.

A rotary encoder sets a speed; two outputs give the matching wheel-speed frequency (**km/h × 6 Hz**, 0–1500 Hz = 0–250 km/h), D5 gives the speed itself (Hz = km/h), two inputs are measured, D12 goes HIGH from 10 km/h, and D6 sweeps from **10 kHz down to 0 Hz in 5 s** on an encoder long press. A 4-digit TM1637 display shows one value at a time.

## Connector (4 outputs, 2 inputs)

| Nano | Dir | Function |
|---|---|---|
| **D10** | OUT | Freq Out, 0–1500 Hz square wave (speed × 6), 50 % duty, 0–5 V |
| **D9** | OUT | Freq Out, 0–1500 Hz, same frequency as D10 (in phase) |
| **D6** | OUT | **Fade:** on an encoder long press, a 50 % square wave sliding linearly from **10 kHz to 0 Hz in 5 s**, then LOW. A new long press starts it again |
| **D5** | OUT | **Speed:** D9 ÷ 6, so its Hz = km/h (0–250 Hz), 50 % duty |
| **D8** | IN | Freq In 1, 0–5 V square wave, km/h = Hz ÷ 6. **Internal pull-up on**: unconnected it reads 0 Hz (no mains hum), and open-collector Hall sensors work |
| **D7** | IN | Freq In 2, 0–5 V square wave, km/h = Hz ÷ 6 |
| GND | | Common ground with whatever drives D8 / D7 |

## Extra pins

| Part | Nano | Use |
|---|---|---|
| 10 km/h logic out | **D12** | HIGH while Freq In 1 (D8) ≥ 10 km/h (60 Hz), LOW again below 9.5 km/h |
| Rotary encoder (DFRobot EC11) | A → **A2**, B → **A1**, C (button) → **A0**, + → 5V, GND → GND | Turn: speed 0–250 km/h, 1 km/h per click (5 km/h spun fast). Short press: next display page. **Long press (1 s): start the D6 fade** |
| TM1637 display | CLK → **D4**, DIO → **D2**, VCC → 5V, GND → GND | One page at a time |
| Active buzzer | red (+) → **D13**, black (−) → GND | 30 ms click on an encoder press, 200 ms beep when D12 turns on (10 km/h), on a long press **one long beep that fades from loud to silent over the 5 s**, matching the D6 fade (an active buzzer has no volume control, so it is switched on and off 50 times a second with the on-time falling from 100 % to 0 %; it can sound slightly buzzy near the end). The onboard L LED lights with it. `BUZZER_ON = false` silences it. It may chirp briefly on reset (the bootloader blinks D13) |

Display pages (short press; the page name shows for 0.8 s): **Out** (speed set, km/h) → **In 1** (km/h) → **In 2** (km/h) → **FAdE** (D6 Hz, shown up to 9999) → back to Out. Turning the encoder jumps to **Out**; a long press jumps to **FAdE**.

Free pins: D3, D11, A3, A4, A5, A6, A7 (A6 / A7 analog only). D0 / D1 are the USB serial port.

## How the outputs are made

- **D9 / D10:** Timer1 in CTC mode toggles both pins in hardware (exact frequency, in phase).
- **D5:** the Timer1 compare interrupt toggles D5 on every 6th D9 toggle, so D5 is exactly D9 ÷ 6.
- **D6:** Timer2 in CTC mode; its interrupt toggles D6. The frequency is updated every 10 ms along the 5 s sweep, with the prescaler stepping /8 → /32 → … → /1024 as it drops. Below about 31 Hz the output stops LOW.

## Settings (top of `NanoSpeedPwmBox.ino`)

`MAX_KMH`, `HZ_PER_KMH` (6), `LOGIC_ON_KMH` / `LOGIC_OFF_KMH` (10 / 9.5), `FADE_START_HZ` (10000), `FADE_MS` (5000), `STEPS_PER_CLICK` (4), `ENC_REVERSE`, `DISP_BRIGHTNESS`.

Timers: Timer0 = millis(), Timer1 = D9 / D10 / D5, Timer2 = D6 fade, so analogWrite(), tone() and Servo can't be used.
