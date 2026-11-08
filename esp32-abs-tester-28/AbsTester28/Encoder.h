#pragma once
#include <Arduino.h>

// KY-040 rotary encoder for page selection, wired straight to the ESP32:
//   CLK -> GPIO21, DT -> GPIO22, SW -> GPIO4 (not GPIO2: strapping pin),
//   + -> 3V3 (never 5V), GND -> GND. Internal pull-ups on all three.
// Use (AbsTester28.ino):
//   press        -> SELECT: the footer reads "SELECT n/3" in amber, turning
//                   moves the blinking dot (the page shown stays put);
//   press again  -> LOCK: the screen goes to the selected page and holds it
//                   (swipes and the Mega's A5 page pot are ignored);
//   press again  -> SELECT again, and so on.
// Turning while not selecting does nothing.
// Reading (the tested stand-alone encoder sketch): polled every loop. Each
// time CLK changes to LOW is one step; DT != CLK then means clockwise,
// otherwise counter-clockwise. The button is debounced for 50 ms and acts on
// the press. If the direction comes out reversed, set REVERSE to true.
namespace encoder {

const int pinA = 21;   // CLK / channel A.
const int pinB = 22;   // DT / channel B.
const int pinSW = 4;   // Button.
constexpr bool REVERSE = false;
const int debounceDelay = 50;  // Button debounce, ms.

int counter = 0;  // Position, for the serial log.
int lastStateA;
int8_t steps = 0;  // Steps since the last takeSteps().

bool buttonState = HIGH;      // Settled button state.
bool lastButtonState = HIGH;  // Previous raw reading.
unsigned long lastDebounceTime = 0;
bool pressEvent = false;      // A press not yet taken by pressed().

void begin() {
  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  pinMode(pinSW, INPUT_PULLUP);
  lastStateA = digitalRead(pinA);
  Serial.println(F("Encoder ready: CLK GPIO21, DT GPIO22, SW GPIO4."));
}

// Call every loop.
void poll() {
  // 1. Rotation.
  const int currentStateA = digitalRead(pinA);
  if (currentStateA != lastStateA && currentStateA == LOW) {
    const bool clockwise = (digitalRead(pinB) != currentStateA) != REVERSE;
    if (clockwise) { counter++; if (steps < 10) steps++; }
    else { counter--; if (steps > -10) steps--; }
    Serial.print(clockwise ? F("# ENCODER Direction: Clockwise | Position: ")
                           : F("# ENCODER Direction: Counter-Clockwise | Position: "));
    Serial.println(counter);
  }
  lastStateA = currentStateA;

  // 2. Button.
  const int currentButtonReading = digitalRead(pinSW);
  if (currentButtonReading != lastButtonState) lastDebounceTime = millis();
  if (millis() - lastDebounceTime > (unsigned long)debounceDelay && currentButtonReading != buttonState) {
    buttonState = currentButtonReading;
    if (buttonState == LOW) {
      Serial.println(F("# ENCODER Button Pressed!"));
      pressEvent = true;
    }
  }
  lastButtonState = currentButtonReading;
}

// Steps turned since the last call (+ clockwise, - counter-clockwise).
int8_t takeSteps() {
  const int8_t s = steps;
  steps = 0;
  return s;
}

// Throws away any turning so far (e.g. when selecting starts).
void clearSteps() { steps = 0; }

// True once per button press.
bool pressed() {
  const bool p = pressEvent;
  pressEvent = false;
  return p;
}

// Pin levels for the USB serial line (1 = high).
void printStatus(Print& out) {
  out.print(F("ENC CLK ")); out.print(digitalRead(pinA));
  out.print(F(" DT ")); out.print(digitalRead(pinB));
  out.print(F(" SW ")); out.print(digitalRead(pinSW));
  out.print(F(" pos ")); out.print(counter);
}

}  // namespace encoder
