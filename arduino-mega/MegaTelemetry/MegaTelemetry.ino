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
// behaviour as the Nano set (A0-A4 sensors, D7 frequency, D8 smart key,
// D9 brake, D6 -> ESP32 GPIO17), plus three 0-30 V inputs through voltage
// dividers: Reference 1 on A5, Reference 2 on A6 and the battery on A7, plus
// a 0-5 V current signal on A8 (0-10 A). The
// ESP32 link sends the U5 frame, so the receiver shows "Arduino Mega".
// On the Mega, D7 is port H bit 4.
#define D7_HIGH() ((PINH & _BV(PH4)) != 0)

volatile uint32_t pulseCount = 0;
volatile bool previousD7High = false;
uint32_t windowStartUs = 0;
uint32_t serialPauseUs = 0;
volatile bool acquisitionEnabled = false;
DebouncedButton smartKeyButton(8), brakeButton(9, false);
// Brake wiring (same as the Uno/Nano sets): released LOW, pressed HIGH (active-high).
bool smartKeyOn = false;
float lastFrequencyHz = 0;
uint32_t lastPublishMs = 0;
uint32_t lastRampMs = 0;
float displayedRpm = 0;
float lastPublishedRpm = 0;

// D7 has no external interrupt on the Mega; Timer1 samples it at 20 kHz,
// as on the Uno/Nano, so the wiring stays the same.
ISR(TIMER1_COMPA_vect) {
  const bool highNow = D7_HIGH();
  if (acquisitionEnabled && highNow && !previousD7High) pulseCount++;
  previousD7High = highNow;
}

const uint8_t sensorPins[] = {A0, A1, A2, A3, A4};
const char* const sensorNames[] = {"IAT", "ECT", "O2", "MAP", "TPS"};
const float knobZero[] = {708.3f, 747.2f, 0.0f, 78.5f, 76.0f};
const float knobMax[] = {343.4f, 61.0f, 257.8f, 889.0f, 893.0f};
constexpr float ADC_REFERENCE_V = 5.000f; // Nominal; replace with measured VCC.
constexpr float FREQUENCY_MAX_HZ = 1639.0f; // Measured maximum knob frequency.
constexpr float DISPLAY_RPM_MAX = 300.0f; // User-requested display scale.
float rawSnapshot[5];
float voltageSnapshot[5];
float positionSnapshot[5];

// 0-30 V inputs, each through a divider: input -> R_TOP -> pin -> R_BOTTOM -> GND.
// 12.5k / 2.5k divides by 6, so the pin sees 0-5 V for a 0-30 V input.
// analogRead 0-1023 (0-5 V at the pin) is mapped straight to 0-30 V.
constexpr float INPUT_FULL_SCALE_V = 30.0f; // Input voltage at analogRead = 1023.
const uint8_t referencePins[] = {A5, A6};
const char* const referenceNames[] = {"reference_1_v", "reference_2_v"};
constexpr uint8_t BATTERY_PIN = A7;
// Current sensor output on A8: analogRead 0-1023 (0-5 V) mapped to 0-10 A.
constexpr uint8_t CURRENT_PIN = A8;
constexpr float CURRENT_FULL_SCALE_A = 10.0f; // Current at analogRead = 1023.
// Internal bandgap, nominally 1.1 V (1.0-1.2 V per chip). Calibrate: measure
// the 5 V pin with a multimeter and set BANDGAP_V = 1.100 * meter / reported.
constexpr float BANDGAP_V = 1.100f;
float supplyVoltage = 0;
float referenceVoltage[2] = {};
float batteryVoltage = 0;
float currentA = 0;

// Reads the bandgap against AVcc. Unlike analogRead against VCC, this shows
// the actual supply, so a sagging 5 V rail is visible.
float readSupplyVoltage() {
  // ATmega2560: bandgap is MUX5:0 = 011110 (MUX5 lives in ADCSRB).
  ADCSRB &= ~_BV(MUX5);
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Bandgap settling after the multiplexer switch.
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 17; n++) {
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC)) {}
    if (n) sum += ADC; // First conversion after switching is discarded.
  }
  return sum ? BANDGAP_V * 1023.0f * 16 / sum : 0;
}

// Divider input voltage: analogRead 0-1023 (pin 0-5 V) mapped to 0-30 V.
// Averages 16 readings; the first one after switching pins is discarded.
float readDividerInput(uint8_t pin) {
  analogRead(pin);
  delayMicroseconds(200);
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 16; n++) sum += analogRead(pin);
  return sum / 16.0f * INPUT_FULL_SCALE_V / 1023.0f;
}

// Battery voltage and current are read with the key off too, like the
// INA219 they replace.
void readBattery() {
  supplyVoltage = readSupplyVoltage();
  batteryVoltage = readDividerInput(BATTERY_PIN);
  analogRead(CURRENT_PIN);
  delayMicroseconds(200);
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 16; n++) sum += analogRead(CURRENT_PIN);
  currentA = sum / 16.0f * CURRENT_FULL_SCALE_A / 1023.0f;
}

// SoftwareSerial TX disables interrupts, so Timer1 can't sample D7 while a
// frame is sent; that time is excluded from the frequency window.
void publishEspSnapshot(float hz, bool keyOn, bool brakePressed, float mappedRpm) {
  uint32_t pauseStart;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    acquisitionEnabled = false;
    pauseStart = micros();
  }
  sendEspSnapshot(rawSnapshot, hz, keyOn, brakePressed, mappedRpm, referenceVoltage, batteryVoltage, currentA);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (keyOn) serialPauseUs += uint32_t(micros() - pauseStart);
    previousD7High = D7_HIGH();
    acquisitionEnabled = keyOn;
  }
}

void printSensor(uint8_t i) {
  analogRead(sensorPins[i]);
  delayMicroseconds(200);
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 16; n++) sum += analogRead(sensorPins[i]);
  const float raw = sum / 16.0f;
  const float voltage = raw * ADC_REFERENCE_V / 1023.0f;
  const float position = 100.0f * (raw - knobZero[i]) / (knobMax[i] - knobZero[i]);
  // Small endpoint jitter is tolerated, but out-of-range values are flagged.
  const bool inRange = position >= -1.0f && position <= 101.0f;
  rawSnapshot[i] = raw;
  voltageSnapshot[i] = voltage;
  positionSnapshot[i] = position;
  Serial.print('"'); Serial.print(sensorNames[i]); Serial.print(F("\":{\"raw\":"));
  Serial.print(raw, 2);
  Serial.print(F(",\"voltage_v\":")); Serial.print(voltage, 3);
  Serial.print(F(",\"knob_percent\":"));
  if (inRange) Serial.print(constrain(position, 0.0f, 100.0f), 1);
  else Serial.print(F("null"));
  Serial.print(F(",\"endpoint_status\":\""));
  Serial.print(inRange ? F("in_range") : F("out_of_range")); Serial.print('"');
  if (i == 4) {
    Serial.print(F(",\"throttle_percent\":"));
    if (inRange) Serial.print(constrain(position, 0.0f, 100.0f), 1);
    else Serial.print(F("null"));
    Serial.print(F(",\"calibration\":\"measured_knob_endpoints\""));
  } else if (i == 2) {
    Serial.print(F(",\"afr\":null,\"calibration\":\"voltage_only\""));
  } else {
    const SensorConversion estimate = convertSensor(i, voltage);
    Serial.print(i == 3 ? F(",\"pressure_kpa\":null,\"estimate_kpa\":")
                        : F(",\"temperature_c\":null,\"estimate_c\":"));
    if (estimate.available) Serial.print(estimate.value, 1);
    else Serial.print(F("null"));
    Serial.print(F(",\"calibration\":\"provisional_document_linear_curve\""));
  }
  Serial.print('}');
}

void setup() {
  Serial.begin(115200);
  smartKeyButton.begin();
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
  // JSON lines start with '{'; readable lines start with '#'.
  // A dashboard serial bridge should ignore lines starting with '#'.
}

void loop() {
  const uint32_t nowMs = millis();
  bool changed = false;
  if (smartKeyButton.update(nowMs) && smartKeyButton.pressed()) {
    smartKeyOn = !smartKeyOn;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      acquisitionEnabled = smartKeyOn;
      pulseCount = 0;
      serialPauseUs = 0;
      windowStartUs = micros();
      previousD7High = D7_HIGH();
    }
    lastFrequencyHz = 0;
    if (!smartKeyOn) displayedRpm = 0;
    lastRampMs = nowMs;
    changed = true;
  }
  if (brakeButton.update(nowMs)) changed = true;
  if (smartKeyOn && uint32_t(micros() - windowStartUs) >= 1000000UL) {
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
  float targetRpm = !smartKeyOn || brakePressed ? 0 : constrain(
      lastFrequencyHz * DISPLAY_RPM_MAX / FREQUENCY_MAX_HZ, 0.0f, DISPLAY_RPM_MAX);
  // Small endpoint variation should still let the full-scale knob reach 300.
  if (smartKeyOn && !brakePressed && lastFrequencyHz >= FREQUENCY_MAX_HZ * 0.99f)
    targetRpm = DISPLAY_RPM_MAX;
  displayedRpm = smartKeyOn ? stepMappedRpm(displayedRpm, targetRpm,
      uint32_t(nowMs - lastRampMs) / 1000.0f) : 0;
  lastRampMs = nowMs;
  const bool transitioning = displayedRpm != targetRpm || displayedRpm != lastPublishedRpm;
  // Publish more frequently during a transition for responsive gauge updates.
  const uint16_t publishInterval = transitioning || brakePressed ? 200 : 1000;
  if (!changed && uint32_t(nowMs - lastPublishMs) < publishInterval) return;
  lastPublishMs = nowMs;
  lastPublishedRpm = displayedRpm;
  if (!smartKeyOn) {
    for (uint8_t i = 0; i < 5; i++) rawSnapshot[i] = 0;
    referenceVoltage[0] = referenceVoltage[1] = 0;
    readBattery();
    Serial.print(F("{\"schema\":2,\"source\":\"mega_live\",\"smart_key\":false,\"acquisition_enabled\":false,\"brake_pressed\":"));
    Serial.print(brakePressed ? F("true") : F("false"));
    Serial.print(F(",\"battery_v\":")); Serial.print(batteryVoltage, 2);
    Serial.print(F(",\"current_a\":")); Serial.print(currentA, 2);
    Serial.println(F(",\"sensors\":null,\"frequency_hz\":0,\"engine_rpm\":0}"));
    Serial.println(F("# SMART KEY OFF: sensor acquisition locked. Press D8 to enable."));
    Serial.print(F("# BATTERY A7=")); Serial.print(batteryVoltage, 2);
    Serial.print(F(" V | CURRENT A8=")); Serial.print(currentA, 2); Serial.println(F(" A"));
    publishEspSnapshot(0, false, brakePressed, 0);
    return; // Only the battery is read while locked.
  }
  const float frequencyHz = lastFrequencyHz;
  const float mappedRpm = displayedRpm;
  Serial.print(F("{\"schema\":2,\"source\":\"mega_live\",\"smart_key\":true,\"acquisition_enabled\":true,\"brake_pressed\":"));
  Serial.print(brakePressed ? F("true") : F("false"));
  Serial.print(F(",\"uptime_ms\":"));
  Serial.print(millis());
  Serial.print(F(",\"adc_reference_v\":")); Serial.print(ADC_REFERENCE_V, 3);
  Serial.print(F(",\"voltage_reference\":\"nominal_unverified\",\"sensors\":{"));
  for (uint8_t i = 0; i < 5; i++) {
    if (i) Serial.print(',');
    printSensor(i);
  }
  readBattery();
  for (uint8_t i = 0; i < 2; i++) referenceVoltage[i] = readDividerInput(referencePins[i]);
  Serial.print(F("},\"supply_v\":")); Serial.print(supplyVoltage, 3);
  for (uint8_t i = 0; i < 2; i++) {
    Serial.print(F(",\"")); Serial.print(referenceNames[i]); Serial.print(F("\":"));
    Serial.print(referenceVoltage[i], 2);
  }
  Serial.print(F(",\"battery_v\":")); Serial.print(batteryVoltage, 2);
  Serial.print(F(",\"current_a\":")); Serial.print(currentA, 2);
  Serial.print(F(",\"frequency_hz\":"));
  Serial.print(frequencyHz, 2);
  Serial.print(F(",\"engine_rpm\":"));
  Serial.print(mappedRpm, 1);
  Serial.println(F(",\"rpm_calibration\":\"mapped_0_1639_hz_to_0_300\"}"));
  Serial.println(F("# --- LIVE SENSORS (voltage assumes 5.000 V reference) ---"));
  Serial.print(F("# SMART KEY ON | BRAKE: "));
  Serial.println(brakePressed ? F("PRESSED - mapped RPM ramps toward zero") : F("RELEASED - mapped RPM follows input smoothly"));
  for (uint8_t i = 0; i < 5; i++) {
    Serial.print(F("# A")); Serial.print(sensorPins[i] - A0); Serial.print(' ');
    Serial.print(sensorNames[i]);
    Serial.print(F(" | raw=")); Serial.print(rawSnapshot[i], 1);
    Serial.print(F(" | voltage=")); Serial.print(voltageSnapshot[i], 3);
    Serial.print(F(" V | knob="));
    const float position = positionSnapshot[i];
    const bool inRange = position >= -1.0f && position <= 101.0f;
    if (inRange) { Serial.print(constrain(position, 0.0f, 100.0f), 1); Serial.print('%'); }
    else Serial.print(F("OUT OF RANGE"));
    if (i == 4) {
      Serial.print(F(" | throttle="));
      if (inRange) { Serial.print(constrain(position, 0.0f, 100.0f), 1); Serial.print('%'); }
      else Serial.print(F("unavailable"));
      Serial.print(F(" (knob endpoints)"));
    } else if (i == 2) {
      Serial.print(F(" | AFR=unavailable"));
    } else {
      const SensorConversion estimate = convertSensor(i, voltageSnapshot[i]);
      Serial.print(i == 3 ? F(" | pressure estimate=") : F(" | temperature estimate="));
      if (estimate.available) {
        Serial.print(estimate.value, 1);
        Serial.print(i == 3 ? F(" kPa [PROVISIONAL]") : F(" C [PROVISIONAL]"));
      } else Serial.print(F("unavailable (outside provisional curve)"));
    }
    Serial.println();
  }
  Serial.print(F("# Supply (bandgap)=")); Serial.print(supplyVoltage, 3);
  Serial.print(F(" V | REF1 A5=")); Serial.print(referenceVoltage[0], 2);
  Serial.print(F(" V | REF2 A6=")); Serial.print(referenceVoltage[1], 2);
  Serial.print(F(" V | BATTERY A7=")); Serial.print(batteryVoltage, 2);
  Serial.print(F(" V | CURRENT A8=")); Serial.print(currentA, 2); Serial.println(F(" A"));
  Serial.print(F("# D7 | frequency=")); Serial.print(frequencyHz, 2); Serial.println(F(" Hz"));
  Serial.print(F("# Mapped RPM: ")); Serial.print(mappedRpm, 1);
  Serial.println(F(" / 300 (display scale, not calibrated engine RPM)"));
  Serial.println(F("# ----------------------------------------------------"));
  publishEspSnapshot(frequencyHz, smartKeyOn, brakePressed, mappedRpm);
}
