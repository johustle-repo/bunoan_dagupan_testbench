#pragma once
#include <Arduino.h>

// Page selector pot (10k) on A5, the ESP32 2.8" screen's page navigator.
//   Pot ends -> 5V and GND, wiper -> A5.
// The 0-1023 reading is split into three pages:
//     0 -  341  page 1
//   342 -  682  page 2
//   683 - 1023  page 3
// Read every 50 ms (8 analogReads averaged). Near a boundary the page only
// changes once the reading is HYSTERESIS counts past it, so a pot left on
// the line doesn't make the screen flick between two pages.
namespace pagePot {

constexpr uint8_t PIN = A5;
constexpr uint16_t PAGE2_START = 342, PAGE3_START = 683;
constexpr uint16_t HYSTERESIS = 8;
constexpr unsigned long READ_MS = 50;

uint16_t raw = 0;  // Averaged A5 reading, 0-1023.
uint8_t page = 1;  // 1-3.

inline uint8_t pageFor(uint16_t value) {
  return value >= PAGE3_START ? 3 : value >= PAGE2_START ? 2 : 1;
}

inline uint16_t sample() {
  analogRead(PIN);  // First reading after switching pins is discarded.
  uint16_t sum = 0;
  for (uint8_t n = 0; n < 8; n++) sum += analogRead(PIN);
  return sum / 8;
}

void begin() {
  pinMode(PIN, INPUT);
  raw = sample();
  page = pageFor(raw);
}

// Call every loop.
void update(unsigned long now) {
  static unsigned long lastMs = 0;
  if (now - lastMs < READ_MS) return;
  lastMs = now;
  raw = sample();
  const uint8_t target = pageFor(raw);
  if (target == page) return;
  // Accept the new page only when the reading is clear of the boundary.
  const uint16_t boundary = target > page ? (target == 3 ? PAGE3_START : PAGE2_START)
                                          : (target == 1 ? PAGE2_START : PAGE3_START);
  const bool clear = target > page ? raw >= boundary + HYSTERESIS : raw + HYSTERESIS < boundary;
  if (clear || abs(int(target) - int(page)) > 1) page = target;
}

}  // namespace pagePot
