#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "SensorConversion.h"
#include "SerialLink.h"
#include "Buttons.h"
#include "RpmRamp.h"

#if !defined(__AVR_ATmega328P__)
#error "Select Arduino Uno R3 (ATmega328P)."
#endif

volatile uint32_t pulseCount = 0;
volatile bool previousD7High = false;
uint32_t windowStartUs = 0;
volatile bool acquisitionEnabled = false;
DebouncedButton smartKeyButton(8), brakeButton(9, false);
// Existing brake wiring: released LOW, pressed HIGH (active-high).
bool smartKeyOn = false;
float lastFrequencyHz = 0;
uint32_t lastPublishMs = 0;
uint32_t lastRampMs = 0;
float displayedRpm = 0;
float lastPublishedRpm = 0;

ISR(PCINT2_vect) {
  const bool highNow = (PIND & _BV(PD7)) != 0;
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
  delay(1000);
  // D7 uses a pin-change interrupt on Uno. No internal pull-up:
  // the simulator must provide a valid 0-to-VCC digital waveform.
  pinMode(7, INPUT);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    previousD7High = (PIND & _BV(PD7)) != 0;
    PCIFR = _BV(PCIF2);
    PCMSK2 |= _BV(PCINT23);
    windowStartUs = micros();
    PCICR |= _BV(PCIE2);
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
      windowStartUs = micros();
      previousD7High = (PIND & _BV(PD7)) != 0;
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
      elapsedUs = nowUs - windowStartUs;
      windowStartUs = nowUs;
      pulses = pulseCount;
      pulseCount = 0;
    }
    lastFrequencyHz = pulses * 1000000.0f / elapsedUs;
    changed = true;
  }
  const bool brakePressed = brakeButton.pressed();
  const float targetRpm = !smartKeyOn || brakePressed ? 0 : constrain(
      lastFrequencyHz * DISPLAY_RPM_MAX / FREQUENCY_MAX_HZ, 0.0f, DISPLAY_RPM_MAX);
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
    Serial.print(F("{\"schema\":2,\"source\":\"uno_live\",\"smart_key\":false,\"acquisition_enabled\":false,\"brake_pressed\":"));
    Serial.print(brakePressed ? F("true") : F("false"));
    Serial.println(F(",\"sensors\":null,\"frequency_hz\":0,\"engine_rpm\":0}"));
    Serial.println(F("# SMART KEY OFF: sensor acquisition locked. Press D8 to enable."));
    sendEspSnapshot(rawSnapshot, 0, false, brakePressed, 0);
    return; // No analogRead while locked.
  }
  const float frequencyHz = lastFrequencyHz;
  const float mappedRpm = displayedRpm;
  Serial.print(F("{\"schema\":2,\"source\":\"uno_live\",\"smart_key\":true,\"acquisition_enabled\":true,\"brake_pressed\":"));
  Serial.print(brakePressed ? F("true") : F("false"));
  Serial.print(F(",\"uptime_ms\":"));
  Serial.print(millis());
  Serial.print(F(",\"adc_reference_v\":")); Serial.print(ADC_REFERENCE_V, 3);
  Serial.print(F(",\"voltage_reference\":\"nominal_unverified\",\"sensors\":{"));
  for (uint8_t i = 0; i < 5; i++) {
    if (i) Serial.print(',');
    printSensor(i);
  }
  Serial.print(F("},\"frequency_hz\":"));
  Serial.print(frequencyHz, 2);
  Serial.print(F(",\"engine_rpm\":"));
  Serial.print(mappedRpm, 1);
  Serial.println(F(",\"rpm_calibration\":\"mapped_0_1639_hz_to_0_300\"}"));
  Serial.println(F("# --- LIVE SENSORS (voltage assumes 5.000 V reference) ---"));
  Serial.print(F("# SMART KEY ON | BRAKE: "));
  Serial.println(brakePressed ? F("PRESSED - mapped RPM ramps toward zero") : F("RELEASED - mapped RPM follows input smoothly"));
  for (uint8_t i = 0; i < 5; i++) {
    Serial.print(F("# A")); Serial.print(i); Serial.print(' ');
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
  Serial.print(F("# D7 | frequency=")); Serial.print(frequencyHz, 2); Serial.println(F(" Hz"));
  Serial.print(F("# Mapped RPM: ")); Serial.print(mappedRpm, 1);
  Serial.println(F(" / 300 (display scale, not calibrated engine RPM)"));
  Serial.println(F("# ----------------------------------------------------"));
  sendEspSnapshot(rawSnapshot, frequencyHz, smartKeyOn, brakePressed, mappedRpm);
}
