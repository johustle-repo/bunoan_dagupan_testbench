#pragma once
#include <Arduino.h>

// Measured supply (AVcc) for the 0-30 V REF / battery inputs.
// analogRead() measures against the board's own 5 V rail, which on USB is
// often 4.7-5.1 V, so assuming 5.00 V puts the same error on every reading
// (4.85 V real: 12.00 V reads about 11.6 V). Here the chip's internal 1.1 V
// bandgap is read against AVcc: AVcc = BANDGAP_V x 1023 / counts. The pin
// volts are then counts x AVcc / 1023, and those go through the PCB
// calibration table as before.
//
// Calibrate once per board: the bandgap is 1.0-1.2 V from chip to chip
// (most are within a few %). Measure the 5V pin with a multimeter, compare
// with the "Supply" line on the Serial Monitor, and set
//   BANDGAP_V = 1.100 x meter volts / reported volts.
// Same file in arduino-nano/NanoAbsTester, arduino-mega/MegaAbsTester and
// arduino-mega/MegaTelemetry; works on the ATmega328P (Uno / Nano) and the
// ATmega2560 (Mega / Mega 2560 Pro).
namespace supply {

// Calibrate per board (see above). This Nano: calibrated 2026-11-08 at a real
// 25.00 V on REF1 / battery, which read 24.50 V with the nominal 1.100
// (pin 4.263 V instead of the table's 4.35 V): 1.100 x 4.35 / 4.263 = 1.1225.
constexpr float BANDGAP_V = 1.1225f;
constexpr float MIN_V = 4.0f, MAX_V = 5.5f;  // Outside this: keep the last good value.
constexpr float FALLBACK_V = 5.0f;           // Until the first good reading.
constexpr float FILTER = 0.3f;               // Running average, steadies the 1-count jitter.

float volts = FALLBACK_V;  // Filtered supply, V.
bool started = false;

// Takes about 4 ms (2 ms settling + 16 conversions). Call before reading the
// REF / battery pins; the next analogRead() switches the ADC back by itself
// (its first reading should be discarded, as the readers here do).
float read() {
#if defined(__AVR_ATmega2560__)
  ADCSRB &= ~_BV(MUX5);                                              // Channel 0b011110 = 1.1 V bandgap.
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);           // Channel 0b1110 = 1.1 V bandgap.
#endif
  delay(2);  // Bandgap settling after the multiplexer switch.
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 17; n++) {
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC)) {}
    if (n) sum += ADC;  // First conversion after switching is discarded.
  }
  const float v = sum ? BANDGAP_V * 1023.0f * 16 / sum : 0;
  if (v < MIN_V || v > MAX_V) return volts;  // Implausible: keep the last good value.
  if (!started) { volts = v; started = true; }
  else volts += FILTER * (v - volts);
  return volts;
}

}  // namespace supply
