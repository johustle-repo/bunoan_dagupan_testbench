#pragma once
#include <Arduino.h>

// ABS ECU power-cycle relay on GPIO13, triggered by the Nano's knob click.
//   Nano EC11 short click -> the Nano's reset counter +1 (24th A1 frame field)
//   -> the ESP32 sees the counter change -> GPIO13 HIGH for PULSE_MS -> the
//   relay pulls in and its NC contact (in series with the ECU's +12 V feed)
//   opens -> the ECU is off for 2 s, then boots fresh (latched error cleared).
// Wiring: relay module IN -> GPIO13, VCC -> 5 V, GND -> common GND; COM + NC in
// series with the ECU feed. Use a high-level-trigger (or 3.3 V-compatible)
// module: GPIO13 HIGH = relay on.
// Only a change of +1 from the last counter fires. The first frame after
// boot, a lost link or a Nano reset (counter back to 0) just resyncs, so
// the relay never fires at power-up.
namespace ecuReset {

constexpr uint8_t PIN = 13;
constexpr uint32_t PULSE_MS = 2000;

bool synced = false;     // lastCount holds the Nano's current counter.
uint8_t lastCount = 0;
bool on = false;         // Relay on (ECU power cut).
uint32_t startMs = 0;
uint32_t pulses = 0;     // Resets done since boot (serial / JSON).

inline void begin() {
  digitalWrite(PIN, LOW);
  pinMode(PIN, OUTPUT);
  Serial.println(F("ECU reset relay ready: GPIO13 (HIGH = ECU power cut)."));
}

// Link lost: the next frame only resyncs.
inline void resync() { synced = false; }

// Every parsed frame that carries the counter.
inline void onCounter(uint8_t count) {
  if (!synced) { synced = true; lastCount = count; return; }
  if (count == lastCount) return;
  const bool step = count == uint8_t(lastCount + 1);
  lastCount = count;
  if (!step) return;  // Jump (Nano reset): resync only.
  on = true;
  startMs = millis();
  ++pulses;
  digitalWrite(PIN, HIGH);
  Serial.println(F("# ECU RESET: knob click -> GPIO13 relay ON, ECU power off for 2 s"));
}

// Call every loop.
inline void update() {
  if (on && millis() - startMs >= PULSE_MS) {
    on = false;
    digitalWrite(PIN, LOW);
    Serial.println(F("# ECU RESET: GPIO13 relay OFF, ECU power back on"));
  }
}

}  // namespace ecuReset
