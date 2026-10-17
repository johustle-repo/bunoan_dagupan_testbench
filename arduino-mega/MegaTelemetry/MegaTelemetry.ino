#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "SensorConversion.h"
#include "SerialLink.h"
#include "Buttons.h"
#include "RpmRamp.h"

#if !defined(__AVR_ATmega2560__)
#error "Select Arduino Mega or Mega 2560 (ATmega2560)."
#endif

// Arduino Mega 2560 sender for the universal ESP32 receiver. Same pins and
// behaviour as the Nano set (A0-A4 sensors, D7 frequency, D9 brake,
// D6 -> ESP32 GPIO17) but with no smart-key button: acquisition starts as
// soon as the Mega powers up and the ESP32 plays its power-on animation. Plus three 0-30 V inputs through voltage
// dividers: Reference 1 on A5, Reference 2 on A6 and the battery on A7, plus
// a 0-5 V current signal on A8 (0-10 A). The
// ESP32 link sends the U5 frame, so the receiver shows "Arduino Mega".
// The USB serial monitor (9600 baud) prints one line per update with the
// 5-sensor diagnostic formulas in SensorConversion.h:
//   IAT: 25.0 C | ECT: 40.0 C | O2: 0.45 V | AFR: 14.7:1 | MAP: 55 kPa | TPS Angle: 12 % | TPS: 0.5V |
//   REF1: 12.00 V | REF2: 5.00 V | BATV: 13.80 V
// On the Mega, D7 is port H bit 4.
#define D7_HIGH() ((PINH & _BV(PH4)) != 0)

volatile uint32_t pulseCount = 0;
volatile bool previousD7High = false;
uint32_t windowStartUs = 0;
uint32_t serialPauseUs = 0;
volatile bool acquisitionEnabled = true;
DebouncedButton brakeButton(9, false);
// Brake wiring (same as the Uno/Nano sets): released LOW, pressed HIGH (active-high).
float lastFrequencyHz = 0;
uint32_t lastPublishMs = 0;
uint32_t lastRampMs = 0;
float displayedSpeed = 0;
float lastPublishedSpeed = 0;

// D7 has no external interrupt on the Mega; Timer1 samples it at 20 kHz,
// as on the Uno/Nano, so the wiring stays the same.
ISR(TIMER1_COMPA_vect) {
  const bool highNow = D7_HIGH();
  if (acquisitionEnabled && highNow && !previousD7High) pulseCount++;
  previousD7High = highNow;
}

const uint8_t sensorPins[] = {A0, A1, A2, A3, A4}; // IAT, ECT, O2, MAP, TPS.
constexpr uint8_t SENSOR_SAMPLES = 20;
constexpr uint16_t PUBLISH_INTERVAL_MS = 500;
// D7 speed = Hz / 6 km/h, max 250. The ESP32 works out RPM from the same
// frequency (Hz x 15000 / 1549).
constexpr float HZ_PER_KMH = 6.0f;
constexpr float SPEED_MAX_KMH = 250.0f;
float rawSnapshot[5]; // Averaged ADC counts, sent to the ESP32.

// 0-30 V inputs, each through a divider: input -> R_TOP -> pin -> R_BOTTOM -> GND.
// 12.5k / 2.5k divides by 6, so the pin sees 0-5 V for a 0-30 V input.
// Input volts = pin volts x (12.5k + 2.5k) / 2.5k.
constexpr float VOLTAGE_DIVIDER_RATIO = 6.0f;
const uint8_t referencePins[] = {A5, A6};
constexpr uint8_t BATTERY_PIN = A7;
// Current sensor output on A8: analogRead 0-1023 (0-5 V) mapped to 0-10 A.
constexpr uint8_t CURRENT_PIN = A8;
constexpr float CURRENT_FULL_SCALE_A = 10.0f; // Current at analogRead = 1023.
float referenceVoltage[2] = {};
float batteryVoltage = 0;
float currentA = 0;

// Divider input voltage: analogRead 0-1023 (pin 0-5 V) mapped to 0-30 V.
// Averages 16 readings; the first one after switching pins is discarded.
float readDividerInput(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t n = 0; n < SENSOR_SAMPLES; n++) {
    sum += analogRead(pin);
    delay(1);
  }
  return float(sum) / SENSOR_SAMPLES * 5.0f / 1023.0f * VOLTAGE_DIVIDER_RATIO;
}

// Battery voltage and current, like the INA219 they replace.
void readBattery() {
  batteryVoltage = readDividerInput(BATTERY_PIN);
  analogRead(CURRENT_PIN);
  delayMicroseconds(200);
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 16; n++) sum += analogRead(CURRENT_PIN);
  currentA = sum / 16.0f * CURRENT_FULL_SCALE_A / 1023.0f;
}

// SoftwareSerial TX disables interrupts, so Timer1 can't sample D7 while a
// frame is sent; that time is excluded from the frequency window.
void publishEspSnapshot(float hz, bool brakePressed, float speedKmh) {
  uint32_t pauseStart;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    acquisitionEnabled = false;
    pauseStart = micros();
  }
  sendEspSnapshot(rawSnapshot, hz, true, brakePressed, speedKmh, referenceVoltage, batteryVoltage, currentA);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    serialPauseUs += uint32_t(micros() - pauseStart);
    previousD7High = D7_HIGH();
    acquisitionEnabled = true;
  }
}

// Hardware smoothing filter: 20 readings 1 ms apart. Keeps the averaged
// counts for the ESP32 frame and returns the pin voltage.
float readSensor(uint8_t i) {
  uint32_t sum = 0;
  for (uint8_t n = 0; n < SENSOR_SAMPLES; n++) {
    sum += analogRead(sensorPins[i]);
    delay(1);
  }
  rawSnapshot[i] = float(sum) / SENSOR_SAMPLES;
  return rawSnapshot[i] * 5.0f / 1023.0f;
}

// "LABEL: value unit | " or "LABEL: ERR | ".
void printReading(const __FlashStringHelper* name, const SensorReading& r, uint8_t digits,
                  const __FlashStringHelper* unit) {
  Serial.print(name);
  if (r.fault) Serial.print(F("ERR"));
  else { Serial.print(r.value, digits); Serial.print(unit); }
  Serial.print(F(" | "));
}

void setup() {
  Serial.begin(9600);
  brakeButton.begin();
  beginEspLink();
  analogReference(DEFAULT);
  for (uint8_t i = 0; i < 5; i++) {
    pinMode(sensorPins[i], INPUT);
    digitalWrite(sensorPins[i], LOW); // Keep internal pull-ups disabled.
  }
  const uint8_t dividerPins[] = {referencePins[0], referencePins[1], BATTERY_PIN, CURRENT_PIN};
  for (uint8_t pin : dividerPins) {
    pinMode(pin, INPUT);
    digitalWrite(pin, LOW);
  }
  delay(1000);
  // D7 is sampled by Timer1. No internal pull-up: provide a valid 0-to-VCC waveform.
  pinMode(7, INPUT);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    previousD7High = D7_HIGH();
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11);
    OCR1A = 99; // 16 MHz / 8 / 100 = 20 kHz.
    TCNT1 = 0;
    TIFR1 = _BV(OCF1A);
    TIMSK1 |= _BV(OCIE1A);
    windowStartUs = micros();
  }
}

void loop() {
  const uint32_t nowMs = millis();
  bool changed = false;
  if (brakeButton.update(nowMs)) changed = true;
  if (uint32_t(micros() - windowStartUs) >= 1000000UL) {
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
    changed = true;
  }
  const bool brakePressed = brakeButton.pressed();
  // Speed follows Hz / 6 and ramps to zero while the brake is pressed.
  const float targetSpeed = brakePressed ? 0 : constrain(lastFrequencyHz / HZ_PER_KMH, 0.0f, SPEED_MAX_KMH);
  displayedSpeed = stepSpeed(displayedSpeed, targetSpeed, uint32_t(nowMs - lastRampMs) / 1000.0f);
  lastRampMs = nowMs;
  const bool transitioning = displayedSpeed != targetSpeed || displayedSpeed != lastPublishedSpeed;
  // Publish every 500 ms, sooner when the brake or the frequency changes.
  if (!changed && !transitioning && uint32_t(nowMs - lastPublishMs) < PUBLISH_INTERVAL_MS) return;
  if (uint32_t(nowMs - lastPublishMs) < 200) return;
  lastPublishMs = nowMs;
  lastPublishedSpeed = displayedSpeed;
  const float frequencyHz = lastFrequencyHz;
  const float speedKmh = displayedSpeed;
  const float iatV = readSensor(0), ectV = readSensor(1), o2V = readSensor(2);
  const float mapV = readSensor(3), tpsV = readSensor(4);
  readBattery();
  for (uint8_t i = 0; i < 2; i++) referenceVoltage[i] = readDividerInput(referencePins[i]);

  const SensorReading afr = convertAfr(o2V);
  const SensorReading tps = convertTps(tpsV);
  printReading(F("IAT: "), convertIat(iatV), 1, F(" C"));
  printReading(F("ECT: "), convertEct(ectV), 1, F(" C"));
  printReading(F("O2: "), {afr.fault, o2V}, 2, F(" V"));
  printReading(F("AFR: "), afr, 1, F(":1"));
  printReading(F("MAP: "), convertMap(mapV), 0, F(" kPa"));
  Serial.print(F("TPS Angle: "));
  if (tps.fault) Serial.print(F("ERR | TPS: ERR"));
  else {
    Serial.print(tps.value, 0); Serial.print(F(" % | "));
    Serial.print(F("TPS: ")); Serial.print(tpsV, 1); Serial.print(F("V"));
  }
  Serial.print(F(" | REF1: ")); Serial.print(referenceVoltage[0], 2);
  Serial.print(F(" V | REF2: ")); Serial.print(referenceVoltage[1], 2);
  Serial.print(F(" V | BATV: ")); Serial.print(batteryVoltage, 2); Serial.println(F(" V"));
  publishEspSnapshot(frequencyHz, brakePressed, speedKmh);
}
