#pragma once
#include <Arduino.h>

// Brake button on GPIO15: a push button from GPIO15 to GND (internal
// pull-up, no resistor needed). While it is held the REAR and FRONT speeds
// shown (LCD, dashboard, JSON) drop by 10 km/h every STEP_MS from where they
// were at the press, down to 0, and stay at 0 while held. Released: back to
// the live speeds from the sender (the FRONT needle eases back up).
// Only the displayed km/h change; the measured Hz, RPM and the sender's own
// TCS / ABS states are untouched. The ABS light follows the braked rear speed.
namespace brakeButton {

constexpr uint8_t PIN = 15;  // Was GPIO13 (read LOW with nothing wired on this board).
constexpr uint32_t DEBOUNCE_MS = 30, STEP_MS = 100;
constexpr float STEP_KMH = 10;

bool held = false;               // Debounced: button pressed.
bool lastRaw = false;
uint32_t rawChangeMs = 0, lastStepMs = 0;
float rear = 0, front = 0;       // Braked speeds while held.

inline void begin() {
  pinMode(PIN, INPUT_PULLUP);
  Serial.println(F("Brake button ready: GPIO15 to GND."));
}

// Call every loop with the live speeds.
inline void update(float liveRear, float liveFront) {
  const uint32_t now = millis();
  const bool raw = digitalRead(PIN) == LOW;
  if (raw != lastRaw) { lastRaw = raw; rawChangeMs = now; }
  if (now - rawChangeMs >= DEBOUNCE_MS && raw != held) {
    held = raw;
    if (held) {
      rear = max(liveRear, 0.0f); front = max(liveFront, 0.0f);
      lastStepMs = now;
      Serial.println(F("# BRAKE pressed: speeds dropping 10 km/h steps"));
    } else Serial.println(F("# BRAKE released: live speeds"));
  }
  if (!held) return;
  // Never show more than the live speed (it may have dropped by itself).
  rear = min(rear, max(liveRear, 0.0f)); front = min(front, max(liveFront, 0.0f));
  while (now - lastStepMs >= STEP_MS) {
    lastStepMs += STEP_MS;
    rear = max(rear - STEP_KMH, 0.0f); front = max(front - STEP_KMH, 0.0f);
  }
}

inline float rearKmh(float live) { return held ? rear : live; }
inline float frontKmh(float live) { return held ? front : live; }

}  // namespace brakeButton
