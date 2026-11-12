#pragma once
#include <Arduino.h>

// The ESP32 screen page, read from two wires so only that page's signals run:
//   ESP32 GPIO5 -> Nano A6 (bit 0), ESP32 GPIO14 -> Nano A7 (bit 1), common GND.
//   A6 A7: HIGH LOW = page 1, LOW HIGH = page 2, HIGH HIGH = page 3,
//   LOW LOW = no page (ESP32 off / starting / old firmware): everything runs.
// A6 / A7 are analog-only, so they are read with analogRead(): the ESP32's
// 3.3 V HIGH reads about 675, above the 400 threshold (about 2 V).
//   Page 1 SPEED:  D9 / D10 generators, D4 (/6) / D11 (/15), D7 / D8 inputs, ABS LED.
//   Page 2 SENSOR: D6 speed sensor input only (no outputs).
//   Page 3 DECLOG: D3 generator and D2 input only.
// A new page is taken once it has read the same 3 times in a row (100 ms
// apart), so a glitch on the wires can't switch the outputs.
namespace pageSel {

constexpr uint8_t PIN_BIT0 = A6, PIN_BIT1 = A7;
constexpr int THRESHOLD = 400;
constexpr uint8_t STABLE_READS = 3;
constexpr unsigned long READ_MS = 100;

uint8_t page = 0;  // 0 = all pages active, 1-3 = only that page.

inline bool speedOn()  { return page == 0 || page == 1; }
inline bool sensorOn() { return page == 0 || page == 2; }
inline bool declogOn() { return page == 0 || page == 3; }

// The ESP32's HIGH is 3.3 V (about 675). A reading above about 4.1 V (850)
// can't come from it: the wire is off (floating) or on 5V, so the page is
// ignored and everything runs.
constexpr int NOT_ESP32 = 850;

inline uint8_t readRaw() {
  const int a6 = analogRead(PIN_BIT0), a7 = analogRead(PIN_BIT1);
  if (a6 > NOT_ESP32 || a7 > NOT_ESP32) return 0;
  return uint8_t(a6 > THRESHOLD) | uint8_t(a7 > THRESHOLD) << 1;  // 0 none, 1 page 1, 2 page 2, 3 page 3.
}

// Call every loop; returns true when the page has changed.
bool update(unsigned long now) {
  static unsigned long lastMs = 0;
  static uint8_t candidate = 0, count = 0;
  if (now - lastMs < READ_MS) return false;
  lastMs = now;
  const uint8_t raw = readRaw();
  if (raw != candidate) { candidate = raw; count = 1; return false; }
  if (count < STABLE_READS) ++count;
  if (count < STABLE_READS || raw == page) return false;
  page = raw;
  Serial.print(F("# PAGE "));
  if (page) Serial.print(page); else Serial.print(F("none (all signals on)"));
  Serial.println(page == 1 ? F(": SPEED, D9/D10/D4/D11 out, D7/D8 in")
                 : page == 2 ? F(": SENSOR, D6 in only")
                 : page == 3 ? F(": DECLOG, D3 out, D2 in") : F(""));
  return true;
}

}  // namespace pageSel
