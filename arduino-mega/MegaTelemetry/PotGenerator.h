#pragma once
#include <Arduino.h>
#include <util/atomic.h>

// Frequency generators: a Bourns PTV09 10k pot sets a 50 % duty, 0-5 V square
// wave on a timer output pin.
//   GEN1: pot on A8 -> D44, 0-2000 Hz (Timer5, OC5C)
//   GEN2: pot on A9 -> D2,  0-100 Hz  (Timer3, OC3B)
//   Each pot: pin 1 -> GND, pin 2 (wiper) -> A8 / A9, pin 3 -> 5V
//   (100 nF wiper-GND optional). Turned fully down the output stops LOW.
// The timers make the waves in hardware, so they keep running while
// SoftwareSerial sends a frame or the sensors are read. Timer3 and Timer5 are
// free in this sketch (Timer1 samples D7/D8; no Servo or tone()). D45 shares
// Timer5 with D44, so it can't carry a second, independent frequency.
// Jumper an output into D7 (RPM), D8 (speed) or an ECU input to test it.

class PotGenerator {
 public:
  PotGenerator(uint8_t potPin, uint8_t outPin, float maxHz, volatile uint8_t& tccrA, volatile uint8_t& tccrB,
               volatile uint16_t& tcnt, volatile uint16_t& ocrTop, volatile uint16_t& ocrOut, uint8_t comBits)
      : potPin(potPin), outPin(outPin), maxHz(maxHz), tccrA(tccrA), tccrB(tccrB), tcnt(tcnt), ocrTop(ocrTop),
        ocrOut(ocrOut), comBits(comBits) {}

  const uint8_t potPin, outPin;
  const float maxHz;
  float outHz = 0;  // Exact frequency on the pin (timer resolution).

  void begin() {
    pinMode(potPin, INPUT);
    digitalWrite(outPin, LOW);
    pinMode(outPin, OUTPUT);
    stop();
  }

  // Call every loop: reads the pot every 20 ms (8 samples, then smoothed) and
  // retunes the output when the setting moves by more than 0.05 % of the range.
  void update(uint32_t nowMs) {
    if (uint32_t(nowMs - lastReadMs) < READ_INTERVAL_MS) return;
    lastReadMs = nowMs;
    analogRead(potPin);  // Let the ADC settle on this channel.
    uint16_t sum = 0;
    for (uint8_t n = 0; n < 8; ++n) sum += analogRead(potPin);
    const float counts = sum / 8.0f;
    filtered = filtered < 0 ? counts : filtered + (counts - filtered) * 0.3f;
    // Ends snap to 0 and the maximum so the full range is reachable.
    const float hz = constrain((filtered - 4) / (1019 - 4), 0.0f, 1.0f) * maxHz;
    const bool end = (hz == 0 || hz == maxHz) && hz != setHz;
    if (!end && fabsf(hz - setHz) < maxHz / 2000) return;
    setHz = hz;
    apply(hz);
  }

 private:
  static constexpr float MIN_HZ = 0.5f;
  static constexpr uint16_t READ_INTERVAL_MS = 20;
  volatile uint8_t &tccrA, &tccrB;
  volatile uint16_t &tcnt, &ocrTop, &ocrOut;
  const uint8_t comBits;  // COMnx1 of the output channel (non-inverting PWM).
  float filtered = -1;    // Smoothed ADC counts.
  float setHz = 0;        // Frequency the pot asks for.
  uint32_t lastReadMs = 0;

  void stop() {
    tccrA = 0;
    tccrB = 0;
    digitalWrite(outPin, LOW);
    outHz = 0;
  }

  // Fast PWM with TOP = OCRnA (mode 15: both compare registers double-buffered,
  // so changes land at the end of a period without glitches); the output clears
  // at TOP / 2. The smallest prescaler that fits gives the finest steps.
  void apply(float hz) {
    if (hz < MIN_HZ) { if (tccrB) stop(); return; }
    static const uint16_t DIV[] = {1, 8, 64, 256, 1024};
    static const uint8_t CS[] = {1, 2, 3, 4, 5};  // CSn2:0 for each divider.
    uint8_t k = 0;
    uint32_t top = 0;
    for (; k < 5; ++k) {
      top = lroundf(F_CPU / (float(DIV[k]) * hz)) - 1;
      if (top <= 65535) break;
    }
    if (k == 5) { k = 4; top = 65535; }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      if ((tccrB & 0x07) != CS[k]) tcnt = 0;  // New prescaler: restart the period.
      tccrA = comBits | _BV(WGM51) | _BV(WGM50);  // WGMn1:0 (same bit positions on every 16-bit timer).
      tccrB = _BV(WGM53) | _BV(WGM52) | CS[k];    // WGMn3:2.
      ocrTop = top;
      ocrOut = top / 2;
    }
    outHz = F_CPU / (float(DIV[k]) * (top + 1));
  }
};

PotGenerator gen1(A8, 44, 2000, TCCR5A, TCCR5B, TCNT5, OCR5A, OCR5C, _BV(COM5C1));  // D44, 0-2000 Hz.
PotGenerator gen2(A9, 2, 100, TCCR3A, TCCR3B, TCNT3, OCR3A, OCR3B, _BV(COM3B1));    // D2, 0-100 Hz.
