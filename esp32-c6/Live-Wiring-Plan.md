# Proposed live monitor wiring

Status: planning only. Firmware currently flashed is a simulation. Verify board header labels and signal characteristics before wiring. No physical outputs are commanded.

| Simulator terminal | Function | Proposed ESP32-C6 destination | Required interface |
|---|---|---|---|
| 1 or 2 | Signal ground | GND | Confirm both simulator grounds are common and compatible with USB-powered ground |
| 4 | IAT, 1.72–3 V | GPIO0 / ADC1_CH0 | Protected scaling/buffering |
| 5 | ECT, 0.43–3 V | GPIO1 / ADC1_CH1 | Protected scaling/buffering |
| 6 | O2, 0–1.2 V | GPIO2 / ADC1_CH2 | Protected high-impedance input; confirm signal source |
| 7 | MAP, 0.78–4 V | GPIO3 / ADC1_CH3 | Protected scaling/buffering |
| 8 | TPS, 0.6–3.5 V | GPIO4 / ADC1_CH4 | Protected scaling/buffering |
| 3 | RPM frequency | GPIO5 | Pulse conditioning matched to measured voltage, output type and waveform |
| 9 | Switch 9, identity unconfirmed | GPIO6 | Input conditioning after identifying voltage and active state |
| 10 | Switch 10, identity unconfirmed | GPIO7 | Input conditioning after identifying voltage and active state |
| 11 | 3.3 V supply | Leave disconnected initially | Verify supply direction, regulation and current capacity |
| 12 | 5 V supply | Leave disconnected initially | Verify supply direction, regulation, current capacity and board power circuit |

GPIO4–7 also have external JTAG functions. This allocation assumes external JTAG is not in use; retain native USB on GPIO12/13. Confirm these GPIOs are exposed and unused on the actual development board.

## Analog interface

Design inputs for the ADC measurement range, not just the 3.3 V supply. A buffered 2:1 scaling stage would produce MAP max 2 V, TPS max 1.75 V, and ECT/IAT max 1.5 V. An unbuffered divider can load the simulator or ECU sensor circuit: resistor values and protection must be selected after examining its output impedance and intended wiring. Do not implement a divider by assumption on a shared ECU sensor line.

For a verified 2:1 stage, reconstruct simulator voltage as measured ADC voltage × 2, then apply channel calibration. Initial live display should show raw voltage until sensor curves are supplied. Two temperature endpoints are insufficient for an accurate full temperature curve. O2 voltage does not establish accurate AFR.

## Measurement checklist before wiring

1. Confirm 3.3 V and 5 V supply voltages, polarity, allowable load and regulation. Start with ESP32 powered by USB only.
2. With a high-impedance meter, record terminals 4–8 relative to signal ground at minimum, middle and maximum knob settings.
3. Check terminal 3 with an oscilloscope or suitable logic instrument: low/high voltage, waveform and frequency range. A multimeter average voltage alone cannot establish pulse amplitude.
4. Identify which of terminals 9/10 is brake. Record voltage in both switch states and identify output type/pull resistors.
5. Obtain the RPM conversion (pulses per revolution or the client's frequency-to-RPM calibration). Initially show Hz, with RPM unavailable until calibrated.
6. Fit the matched interface, verify its outputs before connecting ESP32 GPIOs, then connect and validate one channel at a time.

## Live dashboard behavior

- Explicit Monitor mode with Demo mode kept separate.
- Actual channel voltage/frequency and timestamps.
- Calibrated engineering values only where conversion is confirmed.
- Switch 9 and Switch 10 labels until identities are confirmed.
- Signal history, recording and reports identify measured data source.
- AFR, separate wheel speeds, battery current, ABS/TCS activity and actual ECU response remain unavailable unless separately sourced.

Reference: https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf
