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
//   double click  -> TCS test (page 1 only): two short clicks, the second
//                    pressed within 300 ms of the first release. REAR = FRONT
//                    + 60 Hz (+10 km/h) for 1.5 s, then REAR goes back to the
//                    same speed as FRONT, so TCS blinks on the ESP32 screen.
//                    Double-clicking again during the pulse restarts it.
//   single click  -> FRESH START of page 1 (pressed 30 ms - 600 ms, page 1
//                    only; it runs 300 ms after the release, once no second
//                    click came), as after power-up: knob speed 0,
//                    D9 / D10 (and D4 / D11) stop at once (no ramp), brake and
//                    input cuts cleared, the D7 / D8 readings drop to 0 at once
//                    (freshStart, NanoAbsTester.ino). With the wheels at 0 the
//                    ABS ECU starts its switch-on check again (lamp on, off
//                    again at 10 km/h).
//   long press    -> BRAKE (page 1 only): held 600 ms or more, both wheel
//                    outputs (D9 / D10, and D4 / D11 with them) drop 10 km/h
//                    every 100 ms down to 0 and stay at 0 while held; a
//                    running TCS pulse is dropped. Released: they climb back
//                    to the knob speed at the same rate (no short click).
//                    The A1 frame's brake field is 1 while braking.
// The button never changes the knob speed (kmh): turning carries on from
// where it was (and keeps working during the pulse; REAR stays 60 Hz above
// FRONT). Turning while braking sets the speed the wheels climb back to.
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
// TEMPORARY fine test mode (ECU ABS lamp threshold): FINE_TEST 1 = each knob
// click moves 1 Hz (5 Hz spun fast) instead of 1 km/h (6 Hz). kmh / outKmh
// then count knob steps of UNIT_HZ. Set FINE_TEST to 0 for normal use.
#define FINE_TEST 0
constexpr int UNIT_HZ = FINE_TEST ? 1 : HZ_PER_KMH;          // Hz per knob step.
constexpr int MAX_STEPS = MAX_KMH * HZ_PER_KMH / UNIT_HZ;    // 250 km/h.
constexpr unsigned long FAST_MS = 40, CLICK_MIN_MS = 30, LONG_PRESS_MS = 600;
constexpr unsigned long DOUBLE_MS = 300;  // Second click pressed within this after the first release.
constexpr uint16_t PRESS_HZ = 60, MAX_HZ = potgen::MAX_WHEEL_HZ;  // TCS pulse: REAR + 60 Hz = + 10 km/h.
constexpr unsigned long PULSE_MS = 1500;

int kmh = 0;                // Common speed, km/h (FRONT = this, REAR = this + offset).
uint16_t rearOffsetHz = 0;  // Added to REAR: PRESS_HZ during the TCS pulse, else 0.
unsigned long pulseStartMs = 0;  // When the TCS pulse started (millis).
bool freshStart = false;    // Short click: the sketch clears the D7 / D8 readings, then resets this.

// Brake (long press): the wheel outputs run at outKmh, which follows kmh
// except while braking (ramps to 0) or recovering (ramps back to kmh).
constexpr int BRAKE_STEP_KMH = 10 * HZ_PER_KMH / UNIT_HZ;  // 10 km/h per step, in knob steps.
constexpr unsigned long BRAKE_STEP_MS = 100;
bool braking = false;     // Long press held: outputs ramping down / at 0.
bool recovering = false;  // Released: outputs ramping back up to kmh.
int outKmh = 0;           // Speed the D9 / D10 outputs run at, km/h.
unsigned long lastBrakeStepMs = 0;

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

// Low-speed band (ECU ABS lamp): below LOW_BAND_KMH the outputs run at
// 5.4 Hz per km/h instead of 6, so 9 km/h = 49 Hz stays clear of the ECU's
// ~55 Hz lamp-off point and the lamp goes off at 10 km/h (60 Hz). The Nano's
// speed reading uses the same band (hzToKmh() in NanoAbsTester.ino).
constexpr int LOW_BAND_KMH = 10;
inline uint16_t frontHz() {
  if (!FINE_TEST && outKmh < LOW_BAND_KMH) return uint16_t((outKmh * 54 + 5) / 10);  // x 5.4, rounded.
  return uint16_t(outKmh) * UNIT_HZ;
}
inline uint16_t rearHz() { return min(uint16_t(frontHz() + rearOffsetHz), MAX_HZ); }  // + TCS pulse.

bool active = true;  // false: another ESP32 page is shown, D9 / D10 (and D4 / D11) stopped LOW.

// No-input cut, per wheel: D7 (rear input) -> D9, D8 (front input) -> D10.
// While a wheel output should be running, its input must show a rising edge
// at least every INPUT_WAIT_MS (also the start-up window after the output
// starts); otherwise that output is cut (stopped LOW, D4 / D11 with it).
// A cut output comes back by itself as soon as its input shows pulses again,
// or for a new 1 s try on a short knob click. Turning the knob only sets the
// speed it comes back at; it never restarts a cut output.
constexpr unsigned long INPUT_WAIT_MS = 1000;
bool cut[2] = {false, false};                // [0] rear D9, [1] front D10.
unsigned long lastInputMs[2] = {0, 0};       // Last edge seen (or window start).

void apply() {
  potgen::setWheel(0, active && !cut[0] ? rearHz() : 0);   // D9 rear.
  potgen::setWheel(1, active && !cut[1] ? frontHz() : 0);  // D10 front.
}

// Short knob click: cut outputs get another 1 s try.
void clearCuts(unsigned long now) {
  lastInputMs[0] = lastInputMs[1] = now;  // Fresh start-up window.
  if (!cut[0] && !cut[1]) return;
  cut[0] = cut[1] = false;
  Serial.println(F("# INPUT CHECK: outputs back on, 1 s to see the inputs"));
}

// Call after every input measurement with whether D7 / D8 had a rising edge.
void checkInputs(unsigned long now, bool rearEdge, bool frontEdge) {
  const bool edge[2] = {rearEdge, frontEdge};
  const uint16_t hz[2] = {rearHz(), frontHz()};
  for (uint8_t c = 0; c < 2; c++) {
    if (cut[c]) {
      if (!edge[c]) continue;
      cut[c] = false;  // Input is back (pulses from elsewhere): output back on.
      lastInputMs[c] = now;
      apply();
      Serial.println(c ? F("# INPUT CHECK: D8 input back -> D10 FRONT output on")
                       : F("# INPUT CHECK: D7 input back -> D9 REAR output on"));
      continue;
    }
    if (!active || hz[c] == 0 || edge[c]) { lastInputMs[c] = now; continue; }  // Not running or input OK.
    if (now - lastInputMs[c] >= INPUT_WAIT_MS) {
      cut[c] = true;
      apply();
      Serial.println(c ? F("# INPUT CHECK: no D8 input -> D10 FRONT output cut (back when D8 pulses return or on a knob click)")
                       : F("# INPUT CHECK: no D7 input -> D9 REAR output cut (back when D7 pulses return or on a knob click)"));
    }
  }
}

// Page 1 on (outputs back at the knob's speed) or off (stopped; the knob's
// speed is kept for when page 1 comes back).
void setActive(bool on) {
  if (on == active) return;
  active = on;
  if (!on) rearOffsetHz = 0;  // Drop a running TCS pulse.
  braking = recovering = false;  // Drop a brake too: page 1 comes back at the knob speed.
  outKmh = kmh;
  apply();
}

void log() {
  Serial.print(F("# SPEED ENC ")); Serial.print(kmh); Serial.print(F(" km/h"));
  if (outKmh != kmh) { Serial.print(F(" (out ")); Serial.print(outKmh); Serial.print(F(" km/h)")); }
  Serial.print(F(" | FRONT "));
  Serial.print(frontHz()); Serial.print(F(" Hz | REAR ")); Serial.print(rearHz());
  Serial.print(F(" Hz (+")); Serial.print(rearOffsetHz); Serial.print(F(" Hz)"));
  if (cut[0]) Serial.print(F(" | REAR CUT (no D7)"));
  if (cut[1]) Serial.print(F(" | FRONT CUT (no D8)"));
  Serial.println();
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
    kmh = constrain(kmh + clicks * (fast ? FAST_STEP_KMH : 1), 0, MAX_STEPS);
    if (!braking && !recovering) outKmh = kmh;  // Braking: kmh is where the wheels climb back to.
    apply();
    log();
  }

  // 2. Push button (terminal C): short clicks on release (held 30-600 ms),
  //    long press after 600 ms. A short click waits DOUBLE_MS for a second
  //    one: two = double click (TCS), one = single click (fresh start).
  static bool lastButtonState = HIGH, longPressTriggered = false;
  static unsigned long pressStartTime = 0;
  static bool clickPending = false;        // One short click seen, waiting for a second.
  static unsigned long firstReleaseMs = 0;
  const bool buttonState = digitalRead(PIN_C);
  if (buttonState == LOW && lastButtonState == HIGH) {  // Pressed down.
    pressStartTime = now;
    longPressTriggered = false;
  }
  if (buttonState == LOW && !longPressTriggered && now - pressStartTime >= LONG_PRESS_MS) {  // Held.
    longPressTriggered = true;
    clickPending = false;  // Click then hold: the hold (brake) wins, no fresh start.
    Serial.println(F("# Button: Long Press"));
    if (active) {
      braking = true;
      recovering = false;
      rearOffsetHz = 0;  // No TCS pulse while braking.
      lastBrakeStepMs = now - BRAKE_STEP_MS;  // First 10 km/h step now.
      Serial.println(F("# BRAKE on: wheels dropping 10 km/h per 100 ms"));
    }
  }
  if (buttonState == HIGH && lastButtonState == LOW && braking) {  // Brake released.
    braking = false;
    recovering = outKmh != kmh;
    lastBrakeStepMs = now;
    Serial.println(F("# BRAKE off: wheels climbing back to the knob speed"));
  }
  if (buttonState == HIGH && lastButtonState == LOW && active &&  // Short click released (page 1 only).
      !longPressTriggered && now - pressStartTime > CLICK_MIN_MS) {
    if (clickPending && pressStartTime - firstReleaseMs <= DOUBLE_MS) {
      // Double click: TCS test.
      clickPending = false;
      Serial.println(F("# Button: Double Click"));
      pulseStartMs = now;
      rearOffsetHz = PRESS_HZ;
      apply();
      Serial.print(F("# SPEED ENC double click -> TCS pulse, REAR +")); Serial.print(PRESS_HZ); Serial.print(F(" Hz for 1.5 s: "));
      log();
    } else {
      clickPending = true;  // Wait for a possible second click.
      firstReleaseMs = now;
    }
  }
  if (!active) clickPending = false;
  // No second click in time (and the button is up): single click.
  if (clickPending && buttonState == HIGH && now - firstReleaseMs > DOUBLE_MS) {
    clickPending = false;
    Serial.println(F("# Button: Short Click"));
    // Fresh start of page 1, as after power-up.
    kmh = outKmh = 0;
    braking = recovering = false;
    rearOffsetHz = 0;  // Drop a running TCS pulse.
    clearCuts(now);
    freshStart = true;  // NanoAbsTester.ino zeroes the D7 / D8 readings.
    apply();
    Serial.print(F("# SPEED ENC click -> FRESH START, speed 0: "));
    log();
  }
  lastButtonState = buttonState;

  // 3. Brake ramp: 10 km/h per 100 ms toward 0 (held) or kmh (released).
  if ((braking || recovering) && now - lastBrakeStepMs >= BRAKE_STEP_MS) {
    lastBrakeStepMs = now;
    const int target = braking ? 0 : kmh;
    const int before = outKmh;
    if (outKmh > target) outKmh = max(outKmh - BRAKE_STEP_KMH, target);
    else if (outKmh < target) outKmh = min(outKmh + BRAKE_STEP_KMH, target);
    if (recovering && outKmh == kmh) recovering = false;
    if (outKmh != before) { apply(); log(); }
  }

  // 4. End of the TCS pulse: REAR back to the same speed as FRONT.
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
