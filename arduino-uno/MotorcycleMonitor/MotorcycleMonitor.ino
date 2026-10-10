#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "SensorConversion.h"

#if !defined(__AVR_ATmega328P__)
#error "This sketch requires an Arduino Uno R3 with ATmega328P."
#endif

// ---- CONNECTION AND MEASUREMENT SETTINGS ----
// Client wiring: frequency signal on D7. Set to 2 if rewired to D2.
constexpr uint8_t FREQUENCY_PIN = 7;
static_assert(FREQUENCY_PIN == 2 || FREQUENCY_PIN == 7,
              "Use D2 or D7 for this sketch.");
// Matches the latest working user sketch. An open-collector source may
// need this pull-up. Verify the source accepts a pull-up to Uno VCC.
constexpr bool USE_INTERNAL_PULLUP = true;
constexpr float ADC_REFERENCE_V = 5.000f; // Replace with measured Uno VCC.
constexpr uint16_t UPDATE_MS = 1000;
constexpr uint8_t ADC_SAMPLES = 16;
const uint8_t ANALOG_PINS[5] = {A0, A1, A2, A3, A4};

// Sensor order confirmed by the user.
const char* const ANALOG_LABELS[5] = {"IAT", "ECT", "O2", "MAP", "TPS"};
const float INPUT_SCALE[5] = {1, 1, 1, 1, 1}; // 2 only for a verified 2:1 divider.

// ---- SIGNAL INTERPRETATION ----
enum SignalMode { ENGINE_SIGNAL, WHEEL_SIGNAL, WHEEL_DEMO };
constexpr SignalMode SIGNAL_MODE = ENGINE_SIGNAL;
// 0 means unknown; do not invent a count to obtain a desired display.
constexpr float ENGINE_PULSES_PER_REVOLUTION = 0.0f;
constexpr float WHEEL_PULSES_PER_REVOLUTION = 0.0f;
// Illustrative assumption for WHEEL_DEMO only; NOT a model specification.
constexpr float DEMO_PULSES_PER_WHEEL_REVOLUTION = 48.0f;

enum WheelProfile { NMAX_FRONT, NMAX_REAR, ADV160_FRONT, ADV160_REAR };
constexpr WheelProfile WHEEL_PROFILE = NMAX_REAR;
// Nominal geometry from tire sizes, not measured rolling circumference.
// Set > 0 to use an actual measured rolling circumference in metres.
constexpr float MEASURED_WHEEL_CIRCUMFERENCE_M = 0.0f;

volatile uint32_t pulseCount = 0;
volatile bool previousD7High = false;
uint32_t windowStartUs = 0;

void countPulse() { pulseCount++; }

// Uno R3 D7 uses PD7 / PCINT23, rather than attachInterrupt().
ISR(PCINT2_vect) {
  if (FREQUENCY_PIN != 7) return;
  bool high = (PIND & _BV(PD7)) != 0;
  if (high && !previousD7High) pulseCount++;
  previousD7High = high;
}

float wheelCircumference() {
  if (MEASURED_WHEEL_CIRCUMFERENCE_M > 0)
    return MEASURED_WHEEL_CIRCUMFERENCE_M;
  // C = pi * (rim diameter in mm + two sidewalls) / 1000.
  switch (WHEEL_PROFILE) {
    case NMAX_FRONT:   return PI * (13 * 25.4f + 2 * 110 * 0.70f) / 1000;
    case ADV160_FRONT:return PI * (14 * 25.4f + 2 * 110 * 0.80f) / 1000;
    default:          return PI * (13 * 25.4f + 2 * 130 * 0.70f) / 1000;
  }
}

void printProfile() {
  switch (WHEEL_PROFILE) {
    case NMAX_FRONT: Serial.print(F("NMAX front 110/70-13")); break;
    case NMAX_REAR: Serial.print(F("NMAX rear 130/70-13")); break;
    case ADV160_FRONT: Serial.print(F("ADV160 front 110/80-14")); break;
    case ADV160_REAR: Serial.print(F("ADV160 rear 130/70-13")); break;
  }
}

float readAverageRaw(uint8_t pin) {
  analogRead(pin); // Discard first conversion after changing channels.
  delayMicroseconds(20);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) sum += analogRead(pin);
  return sum / float(ADC_SAMPLES);
}

void setup() {
  Serial.begin(115200);
  analogReference(DEFAULT);
  for (uint8_t pin : ANALOG_PINS) pinMode(pin, INPUT);
  pinMode(FREQUENCY_PIN, USE_INTERNAL_PULLUP ? INPUT_PULLUP : INPUT);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    pulseCount = 0;
    windowStartUs = micros();
    if (FREQUENCY_PIN == 2) {
      attachInterrupt(digitalPinToInterrupt(2), countPulse, RISING);
    } else {
      previousD7High = (PIND & _BV(PD7)) != 0;
      PCIFR = _BV(PCIF2);
      PCMSK2 |= _BV(PCINT23);
      PCICR |= _BV(PCIE2);
    }
  }
  Serial.println(F("Arduino Uno R3 motorcycle signal monitor"));
  Serial.print(F("Frequency input: D")); Serial.println(FREQUENCY_PIN);
  Serial.println(F("A0=IAT A1=ECT A2=O2 A3=MAP A4=TPS"));
  Serial.println(F("Conversions are PROVISIONAL linear estimates from client document."));
  Serial.println(F("Out-of-range inputs are not extrapolated or clamped."));
  if (SIGNAL_MODE == WHEEL_DEMO)
    Serial.println(F("WHEEL DEMO: assumed pulses/revolution; km/h is illustrative."));
}

void loop() {
  if (uint32_t(micros() - windowStartUs) < UPDATE_MS * 1000UL) return;
  uint32_t pulses, elapsedUs;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    uint32_t now = micros();
    elapsedUs = now - windowStartUs;
    pulses = pulseCount;
    pulseCount = 0;
    windowStartUs = now;
  }
  float hz = pulses * (1000000.0f / elapsedUs);

  for (uint8_t i = 0; i < 5; i++) {
    float raw = readAverageRaw(ANALOG_PINS[i]);
    float pinV = raw * ADC_REFERENCE_V / 1023.0f;
    float signalV = pinV * INPUT_SCALE[i];
    Serial.print(ANALOG_LABELS[i]); Serial.print(F(" (A"));
    Serial.print(i); Serial.print(F("): raw="));
    Serial.print(raw, 1); Serial.print(F("  pin=")); Serial.print(pinV, 3);
    Serial.print(F(" V  signal=")); Serial.print(signalV, 3);
    Serial.print(F(" V"));
    if (i == 2) {
      Serial.println(F("  O2 voltage only; AFR unavailable"));
    } else {
      SensorConversion converted = convertSensor(i, signalV);
      if (!converted.available) {
        Serial.println(F("  OUT OF PROVISIONAL RANGE - engineering value unavailable"));
      } else {
        Serial.print(F("  estimate=")); Serial.print(converted.value, 1);
        if (i == 0 || i == 1) Serial.println(F(" C [PROVISIONAL]"));
        else if (i == 3) Serial.println(F(" kPa [PROVISIONAL]"));
        else Serial.println(F(" % [PROVISIONAL]"));
      }
    }
  }
  Serial.print(F("Frequency: ")); Serial.print(hz, 2); Serial.println(F(" Hz"));

  if (SIGNAL_MODE == ENGINE_SIGNAL) {
    if (ENGINE_PULSES_PER_REVOLUTION > 0) {
      Serial.print(F("Engine RPM: "));
      Serial.println(hz * 60.0f / ENGINE_PULSES_PER_REVOLUTION, 1);
    } else Serial.println(F("Engine RPM: unavailable - pulse mapping unknown"));
    Serial.println(F("Road speed: unavailable - input is an engine signal"));
  } else {
    float ppr = SIGNAL_MODE == WHEEL_DEMO
                  ? DEMO_PULSES_PER_WHEEL_REVOLUTION
                  : WHEEL_PULSES_PER_REVOLUTION;
    Serial.print(F("Wheel profile: ")); printProfile(); Serial.println();
    Serial.print(F("Circumference: ")); Serial.print(wheelCircumference(), 4);
    Serial.println(F(" m"));
    if (ppr > 0) {
      if (SIGNAL_MODE == WHEEL_DEMO) Serial.print(F("DEMO "));
      Serial.print(F("Wheel RPM: ")); Serial.println(hz * 60.0f / ppr, 1);
      if (SIGNAL_MODE == WHEEL_DEMO) Serial.print(F("DEMO "));
      Serial.print(F("Road speed: "));
      Serial.print(hz * wheelCircumference() * 3.6f / ppr, 2);
      Serial.println(F(" km/h"));
    } else Serial.println(F("Wheel RPM / km/h unavailable - pulse mapping unknown"));
  }
  Serial.println(F("----------------------------------------"));
}
