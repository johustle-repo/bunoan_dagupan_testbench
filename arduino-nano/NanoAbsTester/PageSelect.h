#pragma once
#include <Arduino.h>

// The page, chosen here with the page / Declog encoder (DeclogEncoder.h) and
// sent to the ESP32 in the A1 frame (28th field page, 29th nav state, 30th
// selected page), so the screen follows it. Only that page's signals run:
//   Page 1 SPEED:  D9 / D10 generators, D4 (/6) / D11 (/15), D7 / D8 inputs, ABS LED.
//   Page 2 SENSOR: D6 speed sensor input only (no outputs).
//   Page 3 DECLOG: D3 generator and D2 input only.
// Nav state (encoder):
//   FREE      power-up, page 1, nothing selected;
//   SELECTING single press: turning moves the selection (sel), the page shown stays;
//   LOCKED    double tap while selecting: the selected page is shown and its signals run;
//   ADJUST    long press: page 3, turning sets the Declog 0-100 Hz.
namespace pageSel {

enum Nav : uint8_t { FREE = 0, SELECTING = 1, LOCKED = 2, ADJUST = 3 };
constexpr uint8_t PAGE_COUNT = 3;
uint8_t page = 1;
uint8_t sel = 0;    // Selected page while SELECTING (1-3), otherwise 0.
Nav nav = FREE;

inline bool speedOn()  { return page == 1; }
inline bool sensorOn() { return page == 2; }
inline bool declogOn() { return page == 3; }

inline uint8_t wrap(int p) { return uint8_t(((p - 1) % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT + 1); }

void logPage() {
  Serial.print(F("# PAGE "));
  Serial.print(page);
  Serial.println(page == 1 ? F(": SPEED, D9/D10/D4/D11 out, D7/D8 in")
                 : page == 2 ? F(": SENSOR, D6 in only")
                             : F(": DECLOG, D3 out, D2 in"));
}

}  // namespace pageSel
