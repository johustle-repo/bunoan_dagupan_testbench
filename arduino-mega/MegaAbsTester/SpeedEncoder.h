#pragma once
#include <Arduino.h>
#include <util/atomic.h>
#include "PotGenerator.h"

// KY-040 rotary encoder for the REAR and FRONT test speeds (in place of the
// A1 / A0 pots). It sets the D44 (rear) and D3 (front) generators.
//   CLK -> D22, DT -> D23, SW -> D24, + -> 5V, GND -> GND.
// Use:
//   turn          -> the selected wheel's speed, 1 km/h per click; spun
//                    quickly (clicks under 40 ms apart) 5 km/h per click.
//                    0-250 km/h, generator Hz = km/h x 6 (250 km/h = 1500 Hz).
//   short press   -> switch the selected wheel, REAR <-> FRONT.
//   hold 1 s      -> both wheels to 0 km/h.
// Starts at 0 km/h with REAR selected. The selected wheel goes to the ESP32
// in the A1 frame (last field: 1 rear, 2 front), which marks it ADJUST on
// page 1 of the 2.8" screen.
// Rotation is decoded in the Timer2 sampler (sample(), 20 kHz) with a
// quadrature state table, so contact bounce cancels itself out and no click
// is lost while the loop is busy (REF / battery reads, frames). One click =
// one full CLK / DT cycle, counted when both are back HIGH at the detent.
// If clockwise comes out as slower, set REVERSE to true (or swap CLK and DT).
namespace speedEnc {

constexpr bool REVERSE = false;
constexpr uint8_t CLK_PIN = 22, DT_PIN = 23, SW_PIN = 24;
// D22 = port A bit 0 (CLK), D23 = port A bit 1 (DT): read straight in the ISR.
#define SPEED_ENC_AB() (PINA & (_BV(PA0) | _BV(PA1)))
constexpr int MAX_KMH = 250, HZ_PER_KMH = 6, FAST_STEP_KMH = 5;
constexpr unsigned long FAST_MS = 40, DEBOUNCE_MS = 30, HOLD_MS = 1000;

enum Wheel : uint8_t { REAR = 1, FRONT = 2 };
Wheel selected = REAR;
int kmh[2] = {0, 0};  // Rear, front, km/h.

volatile int8_t pending = 0;  // Clicks counted by the ISR, not yet applied.
volatile uint8_t lastAB = 3;
volatile int8_t quarter = 0;

// Called from the Timer2 sampler ISR (20 kHz).
inline void sample() {
  const uint8_t ab = SPEED_ENC_AB();
  if (ab == lastAB) return;
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

inline PotGenerator& genFor(Wheel w) { return w == REAR ? genRear : genFront; }
inline const __FlashStringHelper* nameOf(Wheel w) { return w == REAR ? F("REAR") : F("FRONT"); }

void applyWheel(Wheel w) { genFor(w).set(long(kmh[w - 1]) * HZ_PER_KMH); }

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

}  // namespace speedEnc
