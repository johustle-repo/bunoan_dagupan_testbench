#pragma once
#include <Arduino.h>
#include <util/atomic.h>
#include "PageSelect.h"

// Page / Declog encoder: a second rotary encoder with push button (EC11 /
// KY-040 module), replacing the ESP32 page button and the Declog pot.
//   CLK (A) -> A2, DT (B) -> D12, SW -> A6 + 10 kOhm A6 -> 5V,
//   VCC -> 5V, GND -> GND.
// A6 is analog-only (no internal pull-up), so the button is read with
// analogRead() and needs the 10 kOhm pull-up (or the module's own).
//   Single press -> SELECT: turning moves the selection over pages 1-3 (the
//                   ESP32 footer reads "SELECT n/3" in amber, the dot blinks);
//                   the page shown and its signals stay. A single press while
//                   selecting cancels.
//   Double tap   -> LOCK (while selecting): the selected page is shown and
//                   its signals run ("LOCKED n/3").
//   Long press   -> (held 600 ms) from page 1 / 2 or while selecting: jumps
//                   to page 3, DECLOG ADJUST ("ADJUST 3/3"), set 0 Hz, D3 off.
//                   On page 3: D3 outputs the set Hz ONLY WHILE the button is
//                   held past 600 ms (set 78 Hz -> D3 78 Hz, ESP32 green
//                   "Output" lamp); releasing stops D3. No long press, no output.
//   Turn on page 3 -> sets the Declog, 0-100 Hz, 1 Hz per click, 5 Hz per
//                   click when spun quickly. Only the set value changes; D3
//                   only runs while a long press is held.
//   Single press in ADJUST -> LOCKED page 3 (the set value stays).
// Leaving page 3 (double tap lock on page 1 / 2) sets both the set value and
// D3 to 0. Turning does nothing outside SELECT / page 3. A single press acts 300 ms
// after its release, once no second press came (so a double tap is never
// also a single press).
// A / B are decoded in the 20 kHz Timer2 sampler with the same transition
// table as the speed encoder (4 transitions = one click), so no click is
// lost while the loop reads REF / battery or sends a frame.
namespace declogEnc {

#define DECLOG_ENC_A() ((PINC & _BV(PC2)) != 0)  // A2.
#define DECLOG_ENC_B() ((PINB & _BV(PB4)) != 0)  // D12.
constexpr uint8_t PIN_A = A2, PIN_B = 12, PIN_SW = A6;
constexpr bool REVERSE = false;        // true if clockwise turns the Declog down (or swap A and B).
constexpr int8_t STEPS_PER_CLICK = 4;  // 2 for a half-step module (only every second detent moves).
constexpr unsigned long FAST_MS = 40, DEBOUNCE_MS = 30, BUTTON_READ_MS = 5;
constexpr unsigned long DOUBLE_MS = 300, LONG_MS = 600;
constexpr int FAST_STEP_HZ = 5, MAX_HZ = 100;
constexpr int PRESSED_BELOW = 300;     // A6 reading: pressed pulls it to GND.

const int8_t encTable[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};
volatile uint8_t prevState = 3;
volatile int8_t stepAccumulator = 0;
volatile int8_t pending = 0;  // Clicks counted by the ISR, not yet applied.
long position = 0;            // Clicks since power-up (Serial Monitor).

int hz = 0;                   // Declog set value, whole Hz (turning on page 3).
int outHz = 0;                // Declog running on D3, whole Hz (hz copied in when a long press on page 3 is released).
bool armed = false;           // Long press held on page 3: D3 runs at the set value until release.
// Declog output state for the ESP32 (32nd A1 field): 0 D3 off, 2 D3 running
// (1, "release to output", is no longer sent).
inline uint8_t outState() { return outHz ? 2 : 0; }
pageSel::Nav beforeSelect = pageSel::FREE;  // Nav state a cancelled select goes back to.

// Called from the Timer2 sampler ISR (20 kHz).
inline void sample() {
  const uint8_t currentState = (DECLOG_ENC_A() << 1) | DECLOG_ENC_B();
  if (currentState == prevState) return;
  stepAccumulator += encTable[(prevState << 2) | currentState];
  prevState = currentState;
  if (stepAccumulator >= STEPS_PER_CLICK) {
    if (pending < 100) pending++;
    stepAccumulator = 0;
  } else if (stepAccumulator <= -STEPS_PER_CLICK) {
    if (pending > -100) pending--;
    stepAccumulator = 0;
  }
}

void begin() {
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  pinMode(PIN_SW, INPUT);  // Analog-only pin: external 10 kOhm pull-up to 5V.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { prevState = (DECLOG_ENC_A() << 1) | DECLOG_ENC_B(); }
  Serial.println(F("Page / Declog encoder ready: A -> A2, B -> D12, SW -> A6 (press = select, double tap = lock, long press = Declog adjust)."));
}

// ---- Button gestures (each returns true when the page changed) ----
bool onSingle() {
  using namespace pageSel;
  if (nav == SELECTING) {
    nav = beforeSelect == ADJUST ? LOCKED : beforeSelect;  // Cancel: the page shown stays.
    sel = 0;
    Serial.println(F("# DECLOG ENC single press -> select cancelled"));
  } else if (nav == ADJUST) {
    nav = LOCKED;
    Serial.println(F("# DECLOG ENC single press -> Declog adjust off (page 3 locked)"));
  } else {
    beforeSelect = nav;
    nav = SELECTING;
    sel = page;
    Serial.print(F("# DECLOG ENC single press -> SELECT, turn to pick a page (now ")); Serial.print(sel); Serial.println(')');
  }
  return false;
}

bool onDouble() {
  using namespace pageSel;
  if (nav != SELECTING) {
    Serial.println(F("# DECLOG ENC double tap ignored (press once to select first)"));
    return false;
  }
  const bool changed = sel != page;
  page = sel;
  sel = 0;
  nav = LOCKED;
  Serial.print(F("# DECLOG ENC double tap -> LOCKED page ")); Serial.println(page);
  if (changed) logPage();
  return changed;
}

bool onLong() {
  using namespace pageSel;
  if (page == 3 && nav != SELECTING) {  // On page 3: D3 outputs the set value while held.
    armed = true;
    outHz = hz;
    nav = ADJUST;
    Serial.print(F("# DECLOG ENC long press held -> D3 OUTPUT ")); Serial.print(outHz); Serial.println(F(" Hz (release stops it)"));
    return false;
  }
  const bool changed = page != 3;
  page = 3;
  sel = 0;
  nav = ADJUST;
  Serial.print(F("# DECLOG ENC long press -> DECLOG ADJUST on page 3, turn to set 0-100 Hz, long press to output (set "));
  Serial.print(hz); Serial.println(F(" Hz)"));
  if (changed) logPage();
  return changed;
}

// Call every loop. Returns true when the page changed (switch the outputs).
bool update(unsigned long now) {
  using namespace pageSel;
  bool pageChanged = false;

  // 1. Rotation.
  static unsigned long lastClickMs = 0;
  int8_t clicks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { clicks = pending; pending = 0; }
  if (REVERSE) clicks = -clicks;
  if (clicks) {
    position += clicks;
    if (nav == SELECTING) {
      sel = wrap(int(sel) + clicks);
      Serial.print(F("# DECLOG ENC turn -> select page ")); Serial.println(sel);
    } else if (page == 3) {  // ADJUST or LOCKED on page 3: set value only, D3 unchanged.
      const bool fast = clicks > 1 || clicks < -1 || now - lastClickMs < FAST_MS;
      hz = constrain(hz + clicks * (fast ? FAST_STEP_HZ : 1), 0, MAX_HZ);
      if (armed) outHz = hz;  // Turned while held: D3 follows.
      Serial.print(F("# DECLOG ENC turn -> Declog set ")); Serial.print(hz);
      Serial.print(F(" Hz (D3 ")); Serial.print(outHz); Serial.println(F(" Hz, hold long press to output)"));
    }  // Otherwise turning does nothing.
    lastClickMs = now;
  }

  // 2. Button on A6: debounced, then single / double / long.
  static unsigned long lastReadMs = 0, changedMs = 0, pressMs = 0, releaseMs = 0;
  static bool raw = false, held = false, longFired = false, waiting = false, second = false;
  if (now - lastReadMs >= BUTTON_READ_MS) {
    lastReadMs = now;
    const bool pressed = analogRead(PIN_SW) < PRESSED_BELOW;
    if (pressed != raw) { raw = pressed; changedMs = now; }
    if (raw != held && now - changedMs >= DEBOUNCE_MS) {
      held = raw;
      if (held) {                        // Press.
        pressMs = now;
        longFired = false;
        second = waiting;                // Second press of a double tap.
        waiting = false;
      } else if (!longFired) {           // Release of a short press.
        if (second) pageChanged |= onDouble();
        else { waiting = true; releaseMs = now; }
        second = false;
      } else if (armed) {                // Release of a long press on page 3: D3 stops.
        armed = false;
        outHz = 0;
        Serial.println(F("# DECLOG ENC long press released -> D3 OFF"));
      }
    }
  }
  if (held && !longFired && now - pressMs >= LONG_MS) {
    longFired = true;
    second = false;
    pageChanged |= onLong();
  }
  if (waiting && !held && now - releaseMs > DOUBLE_MS) {
    waiting = false;
    pageChanged |= onSingle();
  }

  // 3. Off page 3: set value and D3 back to 0.
  if (page != 3 && (hz || outHz || armed)) {
    hz = outHz = 0;
    armed = false;
    Serial.println(F("# DECLOG left page 3 -> set 0 Hz, D3 0 Hz"));
  }
  return pageChanged;
}

void printStatus(Print& out) {
  using namespace pageSel;
  out.print(F("Page / Declog encoder: A2 ")); out.print(digitalRead(PIN_A));
  out.print(F(" D12 ")); out.print(digitalRead(PIN_B));
  out.print(F(" SW A6 ")); out.print(analogRead(PIN_SW));
  out.print(F(" (pressed below ")); out.print(PRESSED_BELOW);
  out.print(F(") | pos ")); out.print(position);
  out.print(nav == SELECTING ? F(" | SELECTING ") : nav == LOCKED ? F(" | LOCKED") : nav == ADJUST ? F(" | DECLOG ADJUST") : F(" | free"));
  if (nav == SELECTING) out.print(sel);
  out.print(F(" | Declog set ")); out.print(hz);
  out.print(F(" Hz, out ")); out.print(outHz);
  out.println(armed ? F(" Hz (release to output)") : F(" Hz"));
}

}  // namespace declogEnc
