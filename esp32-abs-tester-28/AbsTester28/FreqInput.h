#pragma once
#include <Arduino.h>

// Frequency input for page 2: a 0-2000 Hz square wave on GPIO34 (e.g. the
// Nano's D9 or D10 pot generator). ESP32 pins take 3.3 V at most, so a 5 V
// signal goes through a divider: signal -> 10 kOhm -> GPIO34 -> 20 kOhm -> GND
// (3.3 V high), with a common GND. GPIO34 is input-only with no internal
// pull-down, so the divider's 20 kOhm to GND must stay fitted: it holds the
// pin low when the sensor is unplugged (a bare floating wire picks up noise).
// Each rising edge is timestamped in an interrupt (edges closer than 100 us
// are ignored as noise, so up to about 10 kHz). Ten times a second the
// frequency is the edges since the previous window's last edge divided by the
// time between that edge and this window's last edge (whole periods, so no
// +-1 count jitter). With no edge in a window the last value is held for up to
// 2.5 s (slow signals), then it drops to 0.
namespace freqInput {

constexpr uint8_t PIN = 34;  // Input-only pin, no internal pull-up / pull-down.
constexpr uint32_t WINDOW_MS = 100, MIN_EDGE_US = 100, HOLD_US = 2500000;

// Crosstalk guard: GPIO33 (the touch panel's T_CS) switches twice every touch
// poll (20 ms); on GPIO32, its neighbour, an open input picked that up as a
// phantom 50-100 Hz. GPIO34 sits further away, but the guard stays as
// insurance (it costs a real signal nothing). The touch read is wrapped in
// guardBegin() / guardEnd(); an edge during it or up to GUARD_US after it
// counts only if it fits the rhythm of the real pulses just before it
// (within +-30 % of the previous period, and that previous edge was outside
// a guard). Phantom edges only ever come inside a guard, so they never count
// and an open input reads NO SIGNAL; a real signal keeps all its edges.
constexpr uint32_t GUARD_US = 100, RHYTHM_RESET_US = 1000000;
volatile bool guardBusy = false;
volatile uint32_t guardEndUs = 0;
inline void guardBegin() { guardBusy = true; }
inline void guardEnd() { guardEndUs = micros(); guardBusy = false; }

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t edges = 0, lastEdgeUs = 0;
uint32_t lastPeriodUs = 0;     // Period before the latest counted edge (0 = unknown).
bool lastEdgeGuarded = true;   // The latest counted edge was inside a guard.
float hz = 0;
uint32_t windowStartMs = 0, prevEdgeUs = 0;
bool prevValid = false;

void IRAM_ATTR onEdge() {
  const uint32_t now = micros();
  const bool guarded = guardBusy || now - guardEndUs < GUARD_US;
  portENTER_CRITICAL_ISR(&mux);
  const uint32_t period = now - lastEdgeUs;
  if (period >= MIN_EDGE_US) {
    const uint32_t known = period > RHYTHM_RESET_US ? 0 : lastPeriodUs;  // Long gap: rhythm unknown.
    const bool fits = known && !lastEdgeGuarded && period * 10 >= known * 7 && period * 10 <= known * 13;
    if (!guarded || fits) {
      lastPeriodUs = period > RHYTHM_RESET_US ? 0 : period;
      lastEdgeGuarded = guarded;
      lastEdgeUs = now;
      edges = edges + 1;
    }
  }
  portEXIT_CRITICAL_ISR(&mux);
}

void begin() {
  pinMode(PIN, INPUT);  // GPIO34 has no internal pull-down: the divider's 20 kOhm holds it low.
  attachInterrupt(digitalPinToInterrupt(PIN), onEdge, RISING);
  windowStartMs = millis();
}

// Call every loop; returns the frequency in Hz.
float update() {
  const uint32_t nowMs = millis();
  if (nowMs - windowStartMs < WINDOW_MS) return hz;
  const uint32_t elapsedUs = (nowMs - windowStartMs) * 1000;
  windowStartMs = nowMs;
  portENTER_CRITICAL(&mux);
  const uint32_t n = edges, edgeUs = lastEdgeUs;
  edges = 0;
  portEXIT_CRITICAL(&mux);
  if (n == 0) {
    if (!prevValid || micros() - prevEdgeUs > HOLD_US) { prevValid = false; hz = 0; }
    return hz;
  }
  const uint32_t spanUs = edgeUs - prevEdgeUs;
  hz = prevValid && spanUs ? n * 1e6f / spanUs : n * 1e6f / elapsedUs;
  prevEdgeUs = edgeUs;
  prevValid = true;
  return hz;
}

}  // namespace freqInput
