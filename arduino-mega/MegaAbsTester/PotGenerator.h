#pragma once
#include <Arduino.h>
#include <util/atomic.h>
#include <math.h>

// Three frequency generators (as on the Nano, same A2 / A3 / A4 pots): a
// Bourns PTV09 10k pot sets a 50 % duty, 0-5 V square wave on a timer output.
//   GEN A2: pot on A2 -> D44 (Timer5, OC5C), 0-1600 Hz (1500 Hz = 250 km/h, 1560 Hz = 15000 rpm)
//   GEN A3: pot on A3 -> D3  (Timer3, OC3C), 0-1600 Hz
//   GEN A4: pot on A4 -> D11 (Timer1, OC1A), 0-100 Hz
//   Each pot: pin 1 -> GND, pin 2 (wiper) -> A2 / A3 / A4, pin 3 -> 5V
//   (100 nF wiper-GND optional). Turned fully down the output stops LOW.
// Each generator has its own 16-bit timer, so the three frequencies are
// independent and made entirely in hardware: no interrupts, unaffected by the
// serial frames or the 20 kHz input sampler (Timer2). The timers run in fast
// PWM mode 15 (TOP = OCRnA, double-buffered, so retuning never glitches).
// D44 and D3 use channel C, which goes low at TOP / 2. D11 is channel A,
// whose register is TOP itself, so it toggles at TOP instead (half the rate).
// D12 / D13 (the LEDs, also Timer1 pins) are driven with digitalWrite as usual.
// Jumper D44 -> D7, D3 -> D8 and D11 -> D2 to test the tester itself.

class PotGenerator {
 public:
  PotGenerator(uint8_t potPin, uint8_t outPin, float maxHz, volatile uint8_t& tccrA, volatile uint8_t& tccrB,
               volatile uint16_t& tcnt, volatile uint16_t& ocrTop, volatile uint16_t& ocrOut, uint8_t comBits,
               bool toggle)
      : potPin(potPin), outPin(outPin), maxHz(maxHz), tccrA(tccrA), tccrB(tccrB), tcnt(tcnt), ocrTop(ocrTop),
        ocrOut(ocrOut), comBits(comBits), toggle(toggle) {}

  const uint8_t potPin, outPin;
  const float maxHz;
  float outHz = 0;                     // Exact frequency on the pin (0 = stopped).
  float noiseLo = 1e9f, noiseHi = -1;  // Lowest / highest reading (ADC counts) since takeNoise().

  // Spread of the pot readings since the last call, in ADC counts (0-1023).
  // A good pot left alone stays within about 2; much more means a scratchy
  // pot or a loose lead.
  float takeNoise() {
    const float n = noiseHi >= noiseLo ? noiseHi - noiseLo : 0;
    noiseLo = 1e9f;
    noiseHi = -1;
    return n;
  }

  void begin() {
    pinMode(potPin, INPUT);
    digitalWrite(outPin, LOW);
    pinMode(outPin, OUTPUT);
    stop();
  }

  // Call every loop: reads the pot every 20 ms and smooths it.
  // Each reading takes 16 samples, sorts them and averages the middle 8, so
  // the odd bad sample from a scratchy wiper or a loose lead is thrown away.
  // The output only retunes once the pot has moved by more than DEADBAND
  // counts from where it was last applied, and the frequency is rounded to
  // whole Hz (0.1 Hz on the 0-100 Hz range). Left alone, it stays on one value.
  void update(uint32_t nowMs) {
    if (uint32_t(nowMs - lastReadMs) < READ_INTERVAL_MS) return;
    lastReadMs = nowMs;
    analogRead(potPin);  // Let the ADC settle on this channel.
    uint16_t s[16];
    for (uint8_t n = 0; n < 16; ++n) {  // Insertion sort while reading.
      const uint16_t v = analogRead(potPin);
      int8_t k = n - 1;
      while (k >= 0 && s[k] > v) { s[k + 1] = s[k]; --k; }
      s[k + 1] = v;
    }
    uint16_t sum = 0;
    for (uint8_t n = 4; n < 12; ++n) sum += s[n];
    const float counts = sum / 8.0f;
    if (counts < noiseLo) noiseLo = counts;
    if (counts > noiseHi) noiseHi = counts;
    filtered = filtered < 0 ? counts : filtered + (counts - filtered) * 0.2f;
    const bool atEnd = filtered < 4 || filtered > 1019;  // Ends snap to 0 / the maximum.
    if (appliedCounts >= 0 && fabsf(filtered - appliedCounts) < DEADBAND && !(atEnd && !appliedAtEnd)) return;
    appliedCounts = filtered;
    appliedAtEnd = atEnd;
    const float step = maxHz >= 1000 ? 1.0f : 0.1f;
    const float hz = roundf(constrain((filtered - 4) / (1019 - 4), 0.0f, 1.0f) * maxHz / step) * step;
    if (hz == setHz) return;
    setHz = hz;
    apply(hz);
  }

 private:
  static constexpr float MIN_HZ = 0.5f;
  static constexpr uint16_t READ_INTERVAL_MS = 20;
  static constexpr float DEADBAND = 2.5f;  // ADC counts.
  volatile uint8_t &tccrA, &tccrB;
  volatile uint16_t &tcnt, &ocrTop, &ocrOut;
  const uint8_t comBits;     // Output mode bits: COMnC1 (PWM) or COMnA0 (toggle).
  const bool toggle;         // Channel A: toggles at TOP, so one period = 2 timer cycles.
  float filtered = -1;       // Smoothed ADC counts.
  float appliedCounts = -1;  // Filtered counts when the output was last retuned.
  bool appliedAtEnd = false;
  float setHz = 0;           // Frequency the pot asks for.
  uint32_t lastReadMs = 0;

  // Timer off and the pin back to PORT (LOW).
  void stop() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      tccrA = 0;
      tccrB = 0;
    }
    digitalWrite(outPin, LOW);
    outHz = 0;
  }

  // The smallest prescaler that fits gives the finest steps (at 1600 Hz
  // about 0.2 Hz, at 100 Hz about 0.01 Hz).
  void apply(float hz) {
    if (hz < MIN_HZ) { if (tccrB) stop(); return; }
    static const uint16_t DIV[] = {1, 8, 64, 256, 1024};
    static const uint8_t CS[] = {1, 2, 3, 4, 5};  // CSn2:0 for each divider.
    const float cyclesPerPeriod = toggle ? 2.0f : 1.0f;
    uint8_t k = 0;
    uint32_t top = 0;
    for (; k < 5; ++k) {
      top = lroundf(F_CPU / (float(DIV[k]) * cyclesPerPeriod * hz)) - 1;
      if (top <= 65535) break;
    }
    if (k == 5) { k = 4; top = 65535; }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      if ((tccrB & 0x07) != CS[k]) tcnt = 0;  // New prescaler: restart the period.
      tccrA = comBits | _BV(WGM51) | _BV(WGM50);  // WGMn1:0 (same bit positions on every 16-bit timer).
      tccrB = _BV(WGM53) | _BV(WGM52) | CS[k];    // WGMn3:2.
      ocrTop = top;
      if (!toggle) ocrOut = top / 2;
    }
    outHz = F_CPU / (float(DIV[k]) * cyclesPerPeriod * (top + 1));
  }
};

PotGenerator genA2(A2, 44, 1600, TCCR5A, TCCR5B, TCNT5, OCR5A, OCR5C, _BV(COM5C1), false);  // D44, 0-1600 Hz.
PotGenerator genA3(A3, 3, 1600, TCCR3A, TCCR3B, TCNT3, OCR3A, OCR3C, _BV(COM3C1), false);   // D3,  0-1600 Hz.
PotGenerator genA4(A4, 11, 100, TCCR1A, TCCR1B, TCNT1, OCR1A, OCR1A, _BV(COM1A0), true);    // D11, 0-100 Hz.
PotGenerator* const generators[3] = {&genA2, &genA3, &genA4};
