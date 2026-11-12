#pragma once
#include <Arduino.h>
#include <util/atomic.h>
#include "PotGenerator.h"

// DFRobot Fermion EC11 rotary encoder (SEN0235) for the test speeds. One knob
// drives REAR (D9) and FRONT (D10) together, and with them the D4 / D11 outputs.
//   VCC -> 5V, GND -> GND, A -> A0, B -> A1, C (button) -> A5.
//   (A0 / A1 / A5 used as digital pins; every D pin is already taken.)
// Use:
//   turn          -> both wheels' speed together, 1 km/h per click; spun
//                    quickly (clicks under 40 ms apart) 5 km/h per click.
//                    0-250 km/h, generator Hz = km/h x 6 (250 km/h = 1500 Hz).
//   short click   -> TCS test (on release, pressed 30 ms - 600 ms):
//                    REAR = FRONT + 60 Hz (+10 km/h) for 1.5 s, then REAR goes
//                    back to the same speed as FRONT, so TCS blinks on the
//                    ESP32 screen. Clicking again during the pulse restarts it.
//   long press    -> 600 ms or more: logged only ("Button: Long Press"), no
//                    action, and no short click on release.
// The button never changes the speed: turning carries on from where it was
// (and keeps working during the pulse; REAR stays 60 Hz above FRONT).
// Rear Hz = speed x 6 (+ 60 Hz during the pulse), capped at 1500 Hz.
// Starts at 0 km/h. Turn the knob down to bring both wheels to 0.
//
// Quadrature decoding as in the DFRobot EC11 example: state = A << 1 | B,
// the encTable transition table adds +-1 per valid change, and 4 transitions
// (+4 / -4) make one click (one physical detent). It runs in the Timer2
// sampler (sample(), 20 kHz, direct port read) instead of loop(), so no
// transition is lost while the loop is busy (REF / battery reads take about
// 25 ms, SoftwareSerial frames). If clockwise comes out as slower, set
// REVERSE to true (or swap A and B).
namespace speedEnc {

constexpr bool REVERSE = false;
constexpr uint8_t PIN_A = A0, PIN_B = A1, PIN_C = A5;  // Module labels A, B, C.
// A0 = port C bit 0 (A), A1 = port C bit 1 (B): read straight in the ISR.
#define SPEED_ENC_A() ((PINC >> PC0) & 1)
#define SPEED_ENC_B() ((PINC >> PC1) & 1)
constexpr int8_t STEPS_PER_CLICK = 4;  // DFRobot EC11: 4 transitions per detent.
constexpr int MAX_KMH = 250, HZ_PER_KMH = 6, FAST_STEP_KMH = 5;
constexpr uint16_t PRESS_HZ = 60, MAX_HZ = potgen::MAX_WHEEL_HZ;  // TCS pulse: REAR + 60 Hz = + 10 km/h.
constexpr unsigned long FAST_MS = 40, CLICK_MIN_MS = 30, LONG_PRESS_MS = 600, PULSE_MS = 1500;

int kmh = 0;                // Common speed, km/h (FRONT = this, REAR = this + offset).
uint16_t rearOffsetHz = 0;  // Added to REAR: PRESS_HZ during the TCS pulse, else 0.
unsigned long pulseStartMs = 0;  // When the TCS pulse started (millis).

// Lookup table for valid quadrature transitions: index = prevState << 2 | currentState.
const int8_t encTable[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};
volatile uint8_t prevState = 3;
volatile int8_t stepAccumulator = 0;
volatile int8_t pending = 0;       // Clicks counted by the ISR, not yet applied.
volatile uint16_t pinChanges = 0;  // A / B changes seen by the ISR (wiring check).
long encoderPosition = 0;          // Clicks applied since power-up (wiring check).

// Called from the Timer2 sampler ISR (20 kHz).
inline void sample() {
  const uint8_t currentState = (SPEED_ENC_A() << 1) | SPEED_ENC_B();
  if (currentState == prevState) return;
  pinChanges++;
  stepAccumulator += encTable[(prevState << 2) | currentState];
  prevState = currentState;
  if (stepAccumulator >= STEPS_PER_CLICK) {          // Clockwise.
    if (pending < 100) pending++;
    stepAccumulator = 0;
  } else if (stepAccumulator <= -STEPS_PER_CLICK) {  // Counter-clockwise.
    if (pending > -100) pending--;
    stepAccumulator = 0;
  }
}

inline uint16_t frontHz() { return uint16_t(kmh) * HZ_PER_KMH; }
inline uint16_t rearHz() { return min(uint16_t(frontHz() + rearOffsetHz), MAX_HZ); }

bool active = true;  // false: another ESP32 page is shown, D9 / D10 (and D4 / D11) stopped LOW.

void apply() {
  potgen::setWheel(0, active ? rearHz() : 0);   // D9 rear.
  potgen::setWheel(1, active ? frontHz() : 0);  // D10 front.
}

// Page 1 on (outputs back at the knob's speed) or off (stopped; the knob's
// speed is kept for when page 1 comes back).
void setActive(bool on) {
  if (on == active) return;
  active = on;
  if (!on) rearOffsetHz = 0;  // Drop a running TCS pulse.
  apply();
}

void log() {
  Serial.print(F("# SPEED ENC ")); Serial.print(kmh); Serial.print(F(" km/h | FRONT "));
  Serial.print(frontHz()); Serial.print(F(" Hz | REAR ")); Serial.print(rearHz());
  Serial.print(F(" Hz (+")); Serial.print(rearOffsetHz); Serial.println(F(" Hz)"));
}

// After potgen::begin().
void begin() {
  // SEN0235 has onboard pull-ups; INPUT_PULLUP ensures clean idle states.
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  pinMode(PIN_C, INPUT_PULLUP);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    prevState = (SPEED_ENC_A() << 1) | SPEED_ENC_B();  // Initial state of A and B.
    stepAccumulator = 0;
    pending = 0;
  }
  apply();
  Serial.println(F("DFRobot EC11 Ready!"));
}

// Call every loop.
void update(unsigned long now) {
  // 1. Rotation: both wheels together.
  static unsigned long lastClickMs = 0;
  int8_t clicks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { clicks = pending; pending = 0; }
  if (REVERSE) clicks = -clicks;
  if (!active) clicks = 0;  // Not on page 1: turns are ignored.
  if (clicks) {
    encoderPosition += clicks;
    Serial.print(clicks > 0 ? F("# Clockwise (CW)          | Position: ") : F("# Counter-Clockwise (CCW) | Position: "));
    Serial.println(encoderPosition);
    // Several clicks in one go (loop was busy) or clicks close together = spun quickly.
    const bool fast = clicks > 1 || clicks < -1 || now - lastClickMs < FAST_MS;
    lastClickMs = now;
    kmh = constrain(kmh + clicks * (fast ? FAST_STEP_KMH : 1), 0, MAX_KMH);
    apply();
    log();
  }

  // 2. Push button (terminal C), as in the DFRobot example: short click on
  //    release (held more than 30 ms), long press after 600 ms.
  //    The speed (kmh) is never touched here.
  static bool lastButtonState = HIGH, longPressTriggered = false;
  static unsigned long pressStartTime = 0;
  const bool buttonState = digitalRead(PIN_C);
  if (buttonState == LOW && lastButtonState == HIGH) {  // Pressed down.
    pressStartTime = now;
    longPressTriggered = false;
  }
  if (buttonState == LOW && !longPressTriggered && now - pressStartTime >= LONG_PRESS_MS) {  // Held.
    longPressTriggered = true;
    Serial.println(F("# Button: Long Press"));
  }
  if (buttonState == HIGH && lastButtonState == LOW && active &&  // Released (page 1 only).
      !longPressTriggered && now - pressStartTime > CLICK_MIN_MS) {
    Serial.println(F("# Button: Short Click"));
    pulseStartMs = now;
    rearOffsetHz = PRESS_HZ;
    apply();
    Serial.print(F("# SPEED ENC click -> TCS pulse, REAR +")); Serial.print(PRESS_HZ); Serial.print(F(" Hz for 1.5 s: "));
    log();
  }
  lastButtonState = buttonState;

  // 3. End of the pulse: REAR back to the same speed as FRONT.
  if (rearOffsetHz && now - pulseStartMs >= PULSE_MS) {
    rearOffsetHz = 0;
    apply();
    Serial.print(F("# SPEED ENC pulse end -> REAR = FRONT: "));
    log();
  }
}

// Wiring check for the USB report: "EC11 A0 A 1 A1 B 1 A5 C 1 | pin changes 0 | clicks 0".
// At rest A, B and C read 1 (pull-ups); turning must raise the pin changes.
void printStatus(Print& out) {
  uint16_t changes;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { changes = pinChanges; }
  out.print(F("EC11 A0 A ")); out.print(digitalRead(PIN_A));
  out.print(F(" A1 B ")); out.print(digitalRead(PIN_B));
  out.print(F(" A5 C ")); out.print(digitalRead(PIN_C));
  out.print(F(" | pin changes ")); out.print(changes);
  out.print(F(" | clicks ")); out.println(encoderPosition);
}

}  // namespace speedEnc
