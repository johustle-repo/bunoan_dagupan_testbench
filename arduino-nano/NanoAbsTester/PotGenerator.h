#pragma once
#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

// Three frequency generators for the Uno / Nano ABS tester, 50 % duty,
// 0-5 V square waves:
//   Rear:   speed encoder -> D9  (Timer1 OC1A), 0-1500 Hz (1500 Hz = 250 km/h)
//   Front:  speed encoder -> D10 (Timer1 OC1B), 0-1500 Hz
//   Declog: pot on A2     -> D3  (made in the 20 kHz Timer2 sampler), 0-100 Hz
//   The KY-040 speed encoder (SpeedEncoder.h) sets rear / front with
//   setWheel(). Declog pot (10 kOhm): one end -> GND, wiper -> A2, other end
//   -> 5V (100 nF wiper-GND optional). At 0 Hz the output stops LOW.
// Jumper D9 -> D7, D10 -> D8 and D3 -> D2 to test the tester itself.
//
// The ATmega328P has only one free 16-bit timer, so D9 and D10 share Timer1
// but have independent frequencies: Timer1 runs free at 2 MHz and each
// compare channel's hardware sets or clears its pin exactly at the match; the
// compare interrupt then moves that channel's match point on by the next half
// period (kept in 1/256 tick, so the average frequency is exact). Edges are
// made by the hardware, so interrupt delays (SoftwareSerial bytes, the
// sampler) don't move them. Half periods longer than the 16-bit counter
// (below about 31 Hz) are split into steps where the pin keeps its level.
// Timer1 is used, so analogWrite() on D9 / D10 and the Servo library can't be.
//
// Declog (at most 100 Hz) is a phase accumulator advanced by the Timer2
// sampler from Timer2's own count, so it keeps time through SoftwareSerial
// hold-offs: the average frequency is exact, each edge has up to about 50 us
// of jitter (0.5 % of a 100 Hz half period).

namespace potgen {

constexpr uint8_t DECLOG_POT = A2;  // A0 / A1 are the speed encoder's CLK / DT now.
constexpr uint8_t REAR_OUT = 9, FRONT_OUT = 10, DECLOG_OUT = 3;
// Divided outputs: D4 = D9 / 6, D11 = D10 / 6 (1500 Hz -> 250 Hz), 50 % duty,
// the same /6 as the speed formula, so their Hz equals the wheel's km/h
// (100 km/h: D9 600 Hz, D4 100 Hz).
// Each D9 / D10 edge (rising and falling) is counted in the Timer1 interrupt
// and every 6th toggles the divided pin, so they stay locked to the main
// outputs (exact average frequency; each edge up to about 0.1 ms late, which
// is under 5 % of a 250 Hz half period). They stop LOW with the main output.
constexpr uint8_t REAR_DIV_OUT = 4, FRONT_DIV_OUT = 11, DIVIDE = 6;
uint8_t divCount[2] = {0, 0};

// Writing 1 to a PINx bit toggles that output (ATmega328P), in one instruction.
inline void divToggle(uint8_t c) { if (c) PINB = _BV(PB3); else PIND = _BV(PD4); }
inline void divLow(uint8_t c) {
  divCount[c] = 0;
  if (c) PORTB &= ~_BV(PB3); else PORTD &= ~_BV(PD4);
}
constexpr uint16_t MAX_WHEEL_HZ = 1500, MAX_DECLOG_HZ = 100;
constexpr uint8_t NUM_SAMPLES = 20;          // Pot readings averaged.
constexpr uint16_t READ_INTERVAL_MS = 100;
constexpr uint32_t T1_HZ = 2000000UL;        // Timer1 clock, 16 MHz / 8.
constexpr uint32_t T2_HZ = 500000UL;         // Timer2 clock (sampler), 16 MHz / 32.
constexpr uint16_t MAX_STEP = 60000;         // Longest single compare step, ticks.

// ---- Timer1 wheel channels (index 0 = A / D9 rear, 1 = B / D10 front) ----
struct Channel {
  volatile uint32_t halfQ8 = 0;  // Half period in 1/256 Timer1 ticks (0 = stopped).
  uint32_t fracQ8 = 0;           // Fractional ticks carried to the next half period.
  uint32_t remaining = 0;        // Ticks left until the next edge.
  bool level = false;            // Pin level now.
  bool edgeArmed = false;        // The programmed match is an edge.
};
Channel chan[2];

inline volatile uint16_t& ocr(uint8_t c) { return c ? OCR1B : OCR1A; }

// COM bits for channel c: true = set pin at match, false = clear it.
inline void setAction(uint8_t c, bool high) {
  const uint8_t shift = c ? 4 : 6;  // COM1B1:0 = bits 5:4, COM1A1:0 = bits 7:6.
  TCCR1A = (TCCR1A & ~(3 << shift)) | ((high ? 3 : 2) << shift);
}

// Program the next compare step: the full remainder if it fits, otherwise a
// part of it with the pin held at its level.
inline void schedule(uint8_t c) {
  Channel& ch = chan[c];
  uint32_t step = ch.remaining;
  if (step > MAX_STEP) step = step > 2UL * MAX_STEP ? MAX_STEP : step / 2;
  ch.remaining -= step;
  ch.edgeArmed = ch.remaining == 0;
  setAction(c, ch.edgeArmed ? !ch.level : ch.level);
  ocr(c) += uint16_t(step);
}

inline void onMatch(uint8_t c) {
  Channel& ch = chan[c];
  if (ch.edgeArmed) {
    ch.level = !ch.level;
    if (++divCount[c] >= DIVIDE) { divCount[c] = 0; divToggle(c); }  // D4 / D11.
    const uint32_t half = ch.halfQ8;
    if (!half) {  // Stopped: let the pin finish LOW and turn the channel off.
      if (!ch.level) { TIMSK1 &= c ? ~_BV(OCIE1B) : ~_BV(OCIE1A); divLow(c); return; }
      ch.fracQ8 = 256UL * 100;  // Fallback: short last high step (50 us), then LOW.
    } else {
      ch.fracQ8 += half;
    }
    ch.remaining = ch.fracQ8 >> 8;
    ch.fracQ8 &= 0xFF;
  }
  schedule(c);
}

// Starts, retunes or stops channel c. Retuning takes effect from the next edge.
void setWheelHz(uint8_t c, uint16_t hz) {
  const uint32_t half = hz ? (T1_HZ * 128UL + hz / 2) / hz : 0;  // T1_HZ * 256 / (2 hz).
  const uint8_t ie = c ? _BV(OCIE1B) : _BV(OCIE1A);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    chan[c].halfQ8 = half;
    if (!half && (TIMSK1 & ie) && !chan[c].level) {
      // Stopping while LOW: cancel the coming rising edge now. (While HIGH the
      // half period finishes and the interrupt stops it at the falling edge.)
      setAction(c, false);
      TIMSK1 &= ~ie;
      divLow(c);
    } else if (half && !(TIMSK1 & ie)) {
      divLow(c);  // The divided output starts LOW with the count at 0.
      // Start LOW: force the pin low, then the first rising edge in 50 us.
      Channel& ch = chan[c];
      setAction(c, false);
      TCCR1C = c ? _BV(FOC1B) : _BV(FOC1A);
      ch.level = false;
      ch.fracQ8 = 0;
      ch.remaining = 0;
      ch.edgeArmed = true;
      setAction(c, true);
      ocr(c) = TCNT1 + 100;
      TIFR1 = c ? _BV(OCF1B) : _BV(OCF1A);
      TIMSK1 |= ie;
    }
  }
}

// ---- Declog phase accumulator, run from the Timer2 sampler ----
volatile uint32_t declogInc = 0;  // Phase step per Timer2 tick (2^32 = one period).
uint32_t declogPhase = 0;
uint8_t declogLastT2 = 0;

// Call from the Timer2 compare A interrupt (TCNT2 is read once by the caller).
inline void declogTick(uint8_t t2) {
  const uint8_t dt = t2 - declogLastT2;
  declogLastT2 = t2;
  const uint32_t inc = declogInc;
  if (!inc) {
    if (PORTD & _BV(PD3)) PORTD &= ~_BV(PD3);
    declogPhase = 0;
    return;
  }
  declogPhase += inc * dt;
  if (declogPhase & 0x80000000UL) PORTD |= _BV(PD3);
  else PORTD &= ~_BV(PD3);
}

void setDeclogHz(uint16_t hz) {
  const uint32_t inc = uint32_t((4294967296.0 / T2_HZ) * hz + 0.5);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { declogInc = inc; }
}

// ---- Settings ----
uint16_t setHz[3] = {0xFFFF, 0xFFFF, 0xFFFF};  // Rear, front, Declog, whole Hz.
uint32_t lastReadMs = 0;

// Frequency each output is set to (Hz), for the frame and the Serial Monitor.
float outHz(uint8_t i) { return setHz[i] == 0xFFFF ? 0 : setHz[i]; }

// Rear (c = 0, D9) or front (c = 1, D10) wheel output, 0-1500 Hz (0 = stopped LOW).
void setWheel(uint8_t c, uint16_t hz) {
  if (hz > MAX_WHEEL_HZ) hz = MAX_WHEEL_HZ;
  if (hz == setHz[c]) return;
  setHz[c] = hz;
  setWheelHz(c, hz);
}

void begin() {
  pinMode(DECLOG_POT, INPUT);
  digitalWrite(REAR_OUT, LOW);
  digitalWrite(FRONT_OUT, LOW);
  digitalWrite(DECLOG_OUT, LOW);
  digitalWrite(REAR_DIV_OUT, LOW);
  digitalWrite(FRONT_DIV_OUT, LOW);
  pinMode(REAR_OUT, OUTPUT);
  pinMode(FRONT_OUT, OUTPUT);
  pinMode(DECLOG_OUT, OUTPUT);
  pinMode(REAR_DIV_OUT, OUTPUT);   // D4 = D9 / 6.
  pinMode(FRONT_DIV_OUT, OUTPUT);  // D11 = D10 / 6 (plain output: Timer2 runs the sampler, no PWM).
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TIMSK1 = 0;
    TCCR1A = 0;                // Normal mode, pins not connected yet.
    TCCR1B = _BV(CS11);        // 2 MHz, free running.
    TCNT1 = 0;
    declogLastT2 = TCNT2;
  }
}

// Call every loop: every 100 ms the Declog pot is read (20 analogReads 50 us
// apart, averaged, about 3 ms) and mapped 0-1023 -> 0-100 Hz in whole Hz.
void update(uint32_t nowMs) {
  if (uint32_t(nowMs - lastReadMs) < READ_INTERVAL_MS) return;
  lastReadMs = nowMs;
  analogRead(DECLOG_POT);  // First reading after switching pins is discarded.
  uint32_t sum = 0;
  for (uint8_t n = 0; n < NUM_SAMPLES; ++n) {
    sum += analogRead(DECLOG_POT);
    delayMicroseconds(50);
  }
  const uint16_t hz = map(long(sum / NUM_SAMPLES), 0, 1023, 0, MAX_DECLOG_HZ);
  if (hz == setHz[2]) return;
  setHz[2] = hz;
  setDeclogHz(hz);
}

}  // namespace potgen

ISR(TIMER1_COMPA_vect) { potgen::onMatch(0); }  // D9 rear.
ISR(TIMER1_COMPB_vect) { potgen::onMatch(1); }  // D10 front.
