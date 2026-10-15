#pragma once
#include <Arduino.h>

class DebouncedButton {
 public:
  explicit DebouncedButton(uint8_t pin, bool activeLow = true)
      : pin_(pin), activeLow_(activeLow) {}
  void begin() {
    pinMode(pin_, INPUT_PULLUP);
    candidate_ = stable_ = digitalRead(pin_) == (activeLow_ ? LOW : HIGH);
    since_ = millis();
  }
  bool update(uint32_t now) {
    const bool pressed = digitalRead(pin_) == (activeLow_ ? LOW : HIGH);
    if (pressed != candidate_) { candidate_ = pressed; since_ = now; }
    if (stable_ != candidate_ && uint32_t(now - since_) >= 30) {
      stable_ = candidate_;
      return true;
    }
    return false;
  }
  bool pressed() const { return stable_; }
 private:
  uint8_t pin_;
  bool activeLow_;
  bool candidate_ = false, stable_ = false;
  uint32_t since_ = 0;
};
