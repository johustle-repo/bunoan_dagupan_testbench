#pragma once
#include <Arduino.h>
#include <util/atomic.h>
#include <math.h>

// Three frequency generators, each a 50 % duty, 0-5 V square wave on a timer output:
//   Rear:   speed encoder -> D44 (Timer5, OC5C), 0-1500 Hz (1500 Hz = 250 km/h)
//   Front:  speed encoder -> D3  (Timer3, OC3C), 0-1500 Hz
//   Declog: pot on A2     -> D11 (Timer1, OC1A), 0-100 Hz
// Rear and front have no pot (NO_POT): the KY-040 speed encoder
// (SpeedEncoder.h) sets them with set(). The Declog pot (Bourns PTV09 10k):
// pin 1 -> GND, pin 2 (wiper) -> A2, pin 3 -> 5V (100 nF wiper-GND optional).
// At 0 Hz (pot fully down) the output stops LOW.
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

  static constexpr uint8_t NO_POT = 0xFF;  // Set by set() instead of a pot.
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
    if (potPin != NO_POT) pinMode(potPin, INPUT);
    digitalWrite(outPin, LOW);
    pinMode(outPin, OUTPUT);
    stop();
  }

  // Call every loop. Every 100 ms it reads the pot the simple way: 20
  // analogReads 50 us apart, averaged, then map() 0-1023 -> 0-maxHz in whole
  // Hz (A1 / A0: 0-1500 Hz, A2: 0-100 Hz). About 3 ms per pot.
  void update(uint32_t nowMs) {
    if (potPin == NO_POT) return;
    if (uint32_t(nowMs - lastReadMs) < READ_INTERVAL_MS) return;
    lastReadMs = nowMs;
    long sum = 0;
    for (uint8_t i = 0; i < NUM_SAMPLES; ++i) {
      sum += analogRead(potPin);
      delayMicroseconds(50);  // Small pause between readings for ADC stability.
    }
    const int stableVal = sum / NUM_SAMPLES;
    if (stableVal < noiseLo) noiseLo = stableVal;
    if (stableVal > noiseHi) noiseHi = stableVal;
    const long hz = map(stableVal, 0, 1023, 0, long(maxHz));
    set(hz);
  }

  // Sets the output to hz whole Hz (0 = stopped LOW), clamped to 0-maxHz.
  void set(long hz) {
    hz = constrain(hz, 0L, long(maxHz));
    if (hz == setHz) return;
    setHz = hz;
    apply(hz);
  }

 private:
  static constexpr float MIN_HZ = 0.5f;
  static constexpr uint8_t NUM_SAMPLES = 20;        // Higher = smoother, slightly slower.
  static constexpr uint16_t READ_INTERVAL_MS = 100;
  volatile uint8_t &tccrA, &tccrB;
  volatile uint16_t &tcnt, &ocrTop, &ocrOut;
  const uint8_t comBits;     // Output mode bits: COMnC1 (PWM) or COMnA0 (toggle).
  const bool toggle;         // Channel A: toggles at TOP, so one period = 2 timer cycles.
  long setHz = -1;           // Frequency the pot asks for.
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

  // The smallest prescaler that fits gives the finest steps (at 1500 Hz
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

PotGenerator genRear(PotGenerator::NO_POT, 44, 1500, TCCR5A, TCCR5B, TCNT5, OCR5A, OCR5C, _BV(COM5C1), false);  // D44, encoder.
PotGenerator genFront(PotGenerator::NO_POT, 3, 1500, TCCR3A, TCCR3B, TCNT3, OCR3A, OCR3C, _BV(COM3C1), false);   // D3,  encoder.
PotGenerator genDeclog(A2, 11, 100, TCCR1A, TCCR1B, TCNT1, OCR1A, OCR1A, _BV(COM1A0), true);    // D11, 0-100 Hz.
PotGenerator* const generators[3] = {&genRear, &genFront, &genDeclog};
