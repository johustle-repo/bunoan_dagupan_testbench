#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <SoftwareSerial.h>
#include "Buttons.h"
#include "SpeedRamp.h"

#if !defined(__AVR_ATmega328P__)
#error "Select Arduino Nano (ATmega328P)."
#endif

// Arduino Nano ABS tester sender. The engine sensor knobs are disconnected;
// the Nano reads only what the ABS tester screen shows and sends it to the
// ESP32 (esp32-abs-tester-28/AbsTester28) as "A1" frames.
//   A0 Reference 1, A1 Reference 2, A2 current, A3 battery (all direct, 0-5 V)
//   D7 speed knob (frequency), D8 starter / key button, D9 brake switch
//   D6 -> ESP32 GPIO17 (SoftwareSerial 19200 8N1, through a 10k/20k divider),
//   D5 <- ESP32 GPIO16 (unused, the link is one-way)
// D8 works like a key: nothing is measured or shown until it is pressed, and
// each press toggles the tester on / off. Holding D9 eases the speed to 0;
// releasing it eases back to the knob reading.

volatile uint32_t pulseCount = 0;
volatile bool previousD7High = false;
uint32_t windowStartUs = 0;
uint32_t serialPauseUs = 0;
volatile bool acquisitionEnabled = false;
DebouncedButton starterButton(8), brakeButton(9, false);
// Brake wiring (same as the testbench brake): released LOW, pressed HIGH.
bool keyOn = false;
float lastFrequencyHz = 0;
uint32_t lastPublishMs = 0, lastRampMs = 0;
float speedKmh = 0;

constexpr float FREQUENCY_MAX_HZ = 1639.0f;  // Measured maximum knob frequency.
constexpr float SPEED_MAX_KMH = 200.0f;      // Full knob = 200 km/h, the gauge's full scale.
constexpr uint16_t PUBLISH_MS = 250;

// Analog inputs, all direct 0-5 V: analogRead 0-1023 mapped to the full scale.
enum Input : uint8_t { REF1, REF2, CURRENT, BATTERY, INPUT_COUNT };
const uint8_t inputPins[INPUT_COUNT] = {A0, A1, A2, A3};
const char* const inputNames[INPUT_COUNT] = {"REF1 A0", "REF2 A1", "CURRENT A2", "BATTERY A3"};
const char* const inputUnits[INPUT_COUNT] = {"V", "V", "A", "V"};
// Value at analogRead = 1023 (5 V at the pin). Current: 0-5 V = 0-10 A.
constexpr float INPUT_FULL_SCALE[INPUT_COUNT] = {5.0f, 5.0f, 10.0f, 5.0f};
float inputValue[INPUT_COUNT] = {};

SoftwareSerial esp(5, 6);  // RX D5 <- GPIO16 (unused), TX D6 -> ESP32 GPIO17.
char linkBuffer[64];

// SoftwareSerial owns PCINT2; Timer1 samples D7 at 20 kHz instead.
ISR(TIMER1_COMPA_vect) {
  const bool highNow = (PIND & _BV(PD7)) != 0;
  if (acquisitionEnabled && highNow && !previousD7High) pulseCount++;
  previousD7High = highNow;
}

float readInput(uint8_t i) {
  analogRead(inputPins[i]);  // First reading after switching pins is discarded.
  delayMicroseconds(200);
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 16; n++) sum += analogRead(inputPins[i]);
  return sum / 16.0f * INPUT_FULL_SCALE[i] / 1023.0f;
}

// A1 frame: key, brake, frequency x100, speed x100, REF1 / REF2 mV, current mA,
// battery mV, then an XOR checksum of everything before '*'.
void sendFrame() {
  const int count = snprintf(linkBuffer, sizeof(linkBuffer), "A1,%u,%u,%lu,%u,%u,%u,%u,%u",
    unsigned(keyOn), unsigned(brakeButton.pressed()), (unsigned long)(lastFrequencyHz * 100 + 0.5f),
    unsigned(speedKmh * 100 + 0.5f), unsigned(inputValue[REF1] * 1000 + 0.5f),
    unsigned(inputValue[REF2] * 1000 + 0.5f), unsigned(inputValue[CURRENT] * 1000 + 0.5f),
    unsigned(inputValue[BATTERY] * 1000 + 0.5f));
  if (count < 0 || count > int(sizeof(linkBuffer) - 5)) return;
  uint8_t checksum = 0;
  for (int i = 0; i < count; i++) checksum ^= uint8_t(linkBuffer[i]);
  snprintf(linkBuffer + count, sizeof(linkBuffer) - count, "*%02X\n", checksum);
  // SoftwareSerial TX blocks interrupts, so Timer1 can't sample D7 meanwhile;
  // that time is left out of the frequency window.
  uint32_t pauseStart;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    acquisitionEnabled = false;
    pauseStart = micros();
  }
  esp.write(reinterpret_cast<const uint8_t*>(linkBuffer), count + 4);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (keyOn) serialPauseUs += uint32_t(micros() - pauseStart);
    previousD7High = (PIND & _BV(PD7)) != 0;
    acquisitionEnabled = keyOn;
  }
}

void setup() {
  Serial.begin(115200);
  starterButton.begin();
  brakeButton.begin();
  esp.begin(19200);
  esp.stopListening();  // One-way link.
  analogReference(DEFAULT);
  for (uint8_t pin : inputPins) {
    pinMode(pin, INPUT);
    digitalWrite(pin, LOW);  // Keep internal pull-ups disabled.
  }
  // D7: no internal pull-up, provide a valid 0-to-5 V square wave.
  pinMode(7, INPUT);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    previousD7High = (PIND & _BV(PD7)) != 0;
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11);
    OCR1A = 99;  // 16 MHz / 8 / 100 = 20 kHz.
    TCNT1 = 0;
    TIFR1 = _BV(OCF1A);
    TIMSK1 |= _BV(OCIE1A);
    windowStartUs = micros();
  }
  Serial.println(F("# NANO ABS TESTER: press the starter (D8) to turn on."));
}

void loop() {
  const uint32_t nowMs = millis();
  bool changed = false;
  if (starterButton.update(nowMs) && starterButton.pressed()) {
    keyOn = !keyOn;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      acquisitionEnabled = keyOn;
      pulseCount = 0;
      serialPauseUs = 0;
      windowStartUs = micros();
      previousD7High = (PIND & _BV(PD7)) != 0;
    }
    lastFrequencyHz = 0;
    speedKmh = 0;
    lastRampMs = nowMs;
    changed = true;
  }
  if (brakeButton.update(nowMs)) changed = true;
  if (keyOn && uint32_t(micros() - windowStartUs) >= 1000000UL) {
    uint32_t pulses, elapsedUs;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      const uint32_t nowUs = micros();
      elapsedUs = uint32_t(nowUs - windowStartUs) - serialPauseUs;
      serialPauseUs = 0;
      windowStartUs = nowUs;
      pulses = pulseCount;
      pulseCount = 0;
    }
    lastFrequencyHz = elapsedUs ? pulses * 1000000.0f / elapsedUs : 0;
  }
  // Brake held: ease to 0. Released: ease back to the knob speed.
  const bool braking = brakeButton.pressed();
  float target = !keyOn || braking ? 0
      : constrain(lastFrequencyHz * SPEED_MAX_KMH / FREQUENCY_MAX_HZ, 0.0f, SPEED_MAX_KMH);
  if (keyOn && !braking && lastFrequencyHz >= FREQUENCY_MAX_HZ * 0.99f) target = SPEED_MAX_KMH;
  speedKmh = keyOn ? stepSpeed(speedKmh, target, uint32_t(nowMs - lastRampMs) / 1000.0f) : 0;
  lastRampMs = nowMs;

  if (!changed && uint32_t(nowMs - lastPublishMs) < PUBLISH_MS) return;
  lastPublishMs = nowMs;
  if (keyOn) for (uint8_t i = 0; i < INPUT_COUNT; i++) inputValue[i] = readInput(i);
  else for (float& v : inputValue) v = 0;  // Nothing is measured with the key off.
  sendFrame();

  // USB Serial Monitor, 115200 baud.
  if (!keyOn) { Serial.println(F("# KEY OFF: press the starter (D8) to turn on.")); return; }
  Serial.print(F("# KEY ON | BRAKE ")); Serial.print(braking ? F("ON") : F("OFF"));
  Serial.print(F(" | D7 ")); Serial.print(lastFrequencyHz, 1);
  Serial.print(F(" Hz | SPEED ")); Serial.print(speedKmh, 1); Serial.print(F(" km/h"));
  for (uint8_t i = 0; i < INPUT_COUNT; i++) {
    Serial.print(F(" | ")); Serial.print(inputNames[i]); Serial.print(' ');
    Serial.print(inputValue[i], 2); Serial.print(' '); Serial.print(inputUnits[i]);
  }
  Serial.println();
}
