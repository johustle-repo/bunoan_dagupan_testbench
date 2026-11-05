#pragma once
#include <Arduino.h>

// Frequency input for page 2: a 0-2000 Hz square wave on GPIO32 (e.g. the
// Nano's D9 or D10 pot generator). ESP32 pins take 3.3 V at most, so a 5 V
// signal goes through a divider: signal -> 10 kOhm -> GPIO32 -> 20 kOhm -> GND
// (3.3 V high), with a common GND. The internal pull-down keeps an open
// input at 0 Hz.
// Each rising edge is timestamped in an interrupt (edges closer than 100 us
// are ignored as noise, so up to about 10 kHz). Ten times a second the
// frequency is the edges since the previous window's last edge divided by the
// time between that edge and this window's last edge (whole periods, so no
// +-1 count jitter). With no edge in a window the last value is held for up to
// 2.5 s (slow signals), then it drops to 0.
namespace freqInput {

constexpr uint8_t PIN = 32;
constexpr uint32_t WINDOW_MS = 100, MIN_EDGE_US = 100, HOLD_US = 2500000;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t edges = 0, lastEdgeUs = 0;
float hz = 0;
uint32_t windowStartMs = 0, prevEdgeUs = 0;
bool prevValid = false;

void IRAM_ATTR onEdge() {
  const uint32_t now = micros();
  portENTER_CRITICAL_ISR(&mux);
  if (now - lastEdgeUs >= MIN_EDGE_US) {
    lastEdgeUs = now;
    edges = edges + 1;
  }
  portEXIT_CRITICAL_ISR(&mux);
}

void begin() {
  pinMode(PIN, INPUT_PULLDOWN);
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
