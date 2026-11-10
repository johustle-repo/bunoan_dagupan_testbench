#pragma once
#include <Arduino.h>
#include <util/atomic.h>
#include "PotGenerator.h"

// KY-040 rotary encoder for the REAR and FRONT test speeds (in place of the
// A0 / A1 pots). It sets the D9 (rear) and D10 (front) generators, and with
// them the D4 / D11 divided outputs.
//   CLK -> A0, DT -> A1, SW -> A5, + -> 5V, GND -> GND.
//   (A0 / A1 / A5 used as digital pins; every D pin is already taken.)
// Use:
//   turn          -> the selected wheel's speed, 1 km/h per click; spun
//                    quickly (clicks under 40 ms apart) 5 km/h per click.
//                    0-250 km/h, generator Hz = km/h x 6 (250 km/h = 1500 Hz).
//   short press   -> switch the selected wheel, REAR <-> FRONT.
//   hold 1 s      -> both wheels to 0 km/h.
// Starts at 0 km/h with REAR selected. The selected wheel goes to the ESP32
// as an 18th frame field (1 rear, 2 front), which marks it ADJUST on page 1
// of the 2.8" screen.
// Rotation is decoded in the Timer2 sampler (sample(), 20 kHz) with a
// quadrature state table, so contact bounce cancels itself out and no click
// is lost while the loop is busy (REF / battery reads, SoftwareSerial frames).
// One click = one full CLK / DT cycle, counted when both are back HIGH.
// If clockwise comes out as slower, set REVERSE to true (or swap CLK and DT).
namespace speedEnc {

constexpr bool REVERSE = false;
constexpr uint8_t CLK_PIN = A0, DT_PIN = A1, SW_PIN = A5;
// A0 = port C bit 0 (CLK), A1 = port C bit 1 (DT): read straight in the ISR.
#define SPEED_ENC_AB() (PINC & (_BV(PC0) | _BV(PC1)))
constexpr int MAX_KMH = 250, HZ_PER_KMH = 6, FAST_STEP_KMH = 5;
constexpr unsigned long FAST_MS = 40, DEBOUNCE_MS = 30, HOLD_MS = 1000;

enum Wheel : uint8_t { REAR = 1, FRONT = 2 };
Wheel selected = REAR;
int kmh[2] = {0, 0};  // Rear, front, km/h.

volatile int8_t pending = 0;  // Clicks counted by the ISR, not yet applied.
volatile uint8_t lastAB = 3;
volatile int8_t quarter = 0;
volatile uint16_t pinChanges = 0;  // CLK / DT changes seen by the ISR (wiring check).
long totalClicks = 0;              // Clicks applied since power-up (wiring check).

// Called from the Timer2 sampler ISR (20 kHz).
inline void sample() {
  const uint8_t ab = SPEED_ENC_AB();
  if (ab == lastAB) return;
  pinChanges++;
  // index = old state x 4 + new state (bit 0 CLK, bit 1 DT). Clockwise:
  // 11 -> 10 -> 00 -> 01 -> 11 (CLK falls first, DT still high) = +1 each.
  static const int8_t TABLE[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
  quarter += TABLE[(lastAB << 2) | ab];
  lastAB = ab;
  if (ab == 3) {  // Detent: both HIGH.
    if (quarter >= 2 && pending < 100) pending++;
    else if (quarter <= -2 && pending > -100) pending--;
    quarter = 0;
  }
}

inline const __FlashStringHelper* nameOf(Wheel w) { return w == REAR ? F("REAR") : F("FRONT"); }

void applyWheel(Wheel w) { potgen::setWheel(w - 1, uint16_t(kmh[w - 1]) * HZ_PER_KMH); }

// After potgen::begin().
void begin() {
  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);
  pinMode(SW_PIN, INPUT_PULLUP);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { lastAB = SPEED_ENC_AB(); quarter = 0; pending = 0; }
  applyWheel(REAR);
  applyWheel(FRONT);
}

// Call every loop.
void update(unsigned long now) {
  // 1. Turning.
  static unsigned long lastClickMs = 0;
  int8_t clicks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { clicks = pending; pending = 0; }
  if (REVERSE) clicks = -clicks;
  if (clicks) {
    totalClicks += clicks;
    // Several clicks in one go (loop was busy) or clicks close together = spun quickly.
    const bool fast = clicks > 1 || clicks < -1 || now - lastClickMs < FAST_MS;
    lastClickMs = now;
    int& v = kmh[selected - 1];
    v = constrain(v + clicks * (fast ? FAST_STEP_KMH : 1), 0, MAX_KMH);
    applyWheel(selected);
    Serial.print(F("# SPEED ENC ")); Serial.print(nameOf(selected)); Serial.print(' ');
    Serial.print(v); Serial.println(F(" km/h"));
  }

  // 2. Button: short press switches the wheel, a 1 s hold zeroes both.
  static bool lastRaw = HIGH, down = false, holdDone = false;
  static unsigned long changeMs = 0, downMs = 0;
  const bool raw = digitalRead(SW_PIN);
  if (raw != lastRaw) { lastRaw = raw; changeMs = now; }
  if (now - changeMs >= DEBOUNCE_MS && (raw == LOW) != down) {
    down = raw == LOW;
    if (down) { downMs = now; holdDone = false; }
    else if (!holdDone) {
      selected = selected == REAR ? FRONT : REAR;
      Serial.print(F("# SPEED ENC press -> adjusting ")); Serial.println(nameOf(selected));
    }
  }
  if (down && !holdDone && now - downMs >= HOLD_MS) {
    holdDone = true;
    kmh[0] = kmh[1] = 0;
    applyWheel(REAR);
    applyWheel(FRONT);
    Serial.println(F("# SPEED ENC hold -> REAR and FRONT 0 km/h"));
  }
}

// Wiring check for the USB report: "ENC A0 CLK 1 A1 DT 1 A5 SW 1 | pin changes 0 | clicks 0".
// At rest CLK and DT read 1 (pull-ups); turning must raise the pin changes.
void printStatus(Print& out) {
  uint16_t changes;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { changes = pinChanges; }
  out.print(F("ENC A0 CLK ")); out.print(digitalRead(CLK_PIN));
  out.print(F(" A1 DT ")); out.print(digitalRead(DT_PIN));
  out.print(F(" A5 SW ")); out.print(digitalRead(SW_PIN));
  out.print(F(" | pin changes ")); out.print(changes);
  out.print(F(" | clicks ")); out.println(totalClicks);
}

}  // namespace speedEnc
