#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "SensorConversion.h"
#include "SerialLink.h"
#include "Buttons.h"
#include "RpmRamp.h"

#if !defined(__AVR_ATmega328P__)
#error "Select Arduino Nano (ATmega328P)."
#endif

// Two wheel-speed sensors: D7 rear, D8 front. There is no smart-key button
// (D8 was the key): acquisition starts at power-up, like the Mega, and the
// ESP32 plays its switch-on animation.
#define D7_HIGH() ((PIND & _BV(PD7)) != 0)
#define D8_HIGH() ((PINB & _BV(PB0)) != 0)

volatile uint32_t pulseCount = 0, frontPulseCount = 0;  // D7 rear, D8 front.
volatile bool previousD7High = false, previousD8High = false;
uint32_t windowStartUs = 0;
uint32_t serialPauseUs = 0;
volatile bool acquisitionEnabled = true;
DebouncedButton brakeButton(9, false);
// Brake wiring (same as the Uno set): released LOW, pressed HIGH (active-high).
float lastFrequencyHz = 0, lastFrontFrequencyHz = 0;
uint32_t lastPublishMs = 0;
uint32_t lastRampMs = 0;
float displayedSpeed = 0, displayedFrontSpeed = 0;
float lastPublishedSpeed = 0, lastPublishedFrontSpeed = 0;

// SoftwareSerial owns PCINT2; Timer1 samples D7 and D8 at 20 kHz instead.
ISR(TIMER1_COMPA_vect) {
  const bool highNow = D7_HIGH(), frontHighNow = D8_HIGH();
  if (acquisitionEnabled && highNow && !previousD7High) pulseCount++;
  if (acquisitionEnabled && frontHighNow && !previousD8High) frontPulseCount++;
  previousD7High = highNow;
  previousD8High = frontHighNow;
}

const uint8_t sensorPins[] = {A0, A1, A2, A3, A4};
const char* const sensorNames[] = {"IAT", "ECT", "O2", "MAP", "TPS"};
const float knobZero[] = {708.3f, 747.2f, 0.0f, 78.5f, 76.0f};
const float knobMax[] = {343.4f, 61.0f, 257.8f, 889.0f, 893.0f};
constexpr float ADC_REFERENCE_V = 5.000f; // Nominal; replace with measured VCC.
// Wheel speed = Hz / 6 km/h, max 250, for D7 (rear) and D8 (front), the same
// formula as the Mega. The ESP32 works out RPM from the D7 frequency.
constexpr float HZ_PER_KMH = 6.0f;
constexpr float SPEED_MAX_KMH = 250.0f;
float rawSnapshot[5];
float voltageSnapshot[5];
float positionSnapshot[5];

// Supply references: Reference 1 = testbench 5 V on A5, Reference 2 = 3.3 V on A6.
const uint8_t referencePins[] = {A5, A6};
const char* const referenceNames[] = {"reference_1_v", "reference_2_v"};
// Internal bandgap, nominally 1.1 V (1.0-1.2 V per chip). Calibrate: measure
// the 5 V pin with a multimeter and set BANDGAP_V = 1.100 * meter / reported.
constexpr float BANDGAP_V = 1.100f;
float supplyVoltage = 0;
float referenceVoltage[2] = {};

// Reads the bandgap against AVcc. Unlike analogRead against VCC, this shows
// the actual supply, so a sagging 5 V rail is visible.
float readSupplyVoltage() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Bandgap settling after the multiplexer switch.
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 17; n++) {
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC)) {}
    if (n) sum += ADC; // First conversion after switching is discarded.
  }
  return sum ? BANDGAP_V * 1023.0f * 16 / sum : 0;
}

// Scales by the measured supply, so A5 tied to the 5 V rail reads its real value.
float readReference(uint8_t i) {
  analogRead(referencePins[i]);
  delayMicroseconds(200);
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 16; n++) sum += analogRead(referencePins[i]);
  return sum / 16.0f * supplyVoltage / 1023.0f;
}

// SoftwareSerial TX disables interrupts, so Timer1 can't sample D7 / D8
// while a frame is sent; that time is excluded from the frequency window.
void publishEspSnapshot(float hz, bool brakePressed, float speedKmh, float frontHz) {
  uint32_t pauseStart;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    acquisitionEnabled = false;
    pauseStart = micros();
  }
  sendEspSnapshot(rawSnapshot, hz, true, brakePressed, speedKmh, referenceVoltage, frontHz);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    serialPauseUs += uint32_t(micros() - pauseStart);
    previousD7High = D7_HIGH();
    previousD8High = D8_HIGH();
    acquisitionEnabled = true;
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
  brakeButton.begin();
  beginEspLink();
  analogReference(DEFAULT);
  for (uint8_t i = 0; i < 5; i++) {
    pinMode(sensorPins[i], INPUT);
    digitalWrite(sensorPins[i], LOW); // Keep internal pull-ups disabled.
  }
  pinMode(A5, INPUT); // A6 is analog-only: pinMode would index past the pin tables.
  digitalWrite(A5, LOW);
  delay(1000);
  // D7 (rear) and D8 (front) are sampled by Timer1; SoftwareSerial owns the
  // pin-change vectors. No internal pull-ups: provide a valid 0-to-VCC waveform.
  pinMode(7, INPUT);
  pinMode(8, INPUT);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    previousD7High = D7_HIGH();
    previousD8High = D8_HIGH();
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
  if (brakeButton.update(nowMs)) changed = true;
  if (uint32_t(micros() - windowStartUs) >= 1000000UL) {
    uint32_t pulses, frontPulses, elapsedUs;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      const uint32_t nowUs = micros();
      elapsedUs = uint32_t(nowUs - windowStartUs) - serialPauseUs;
      serialPauseUs = 0;
      windowStartUs = nowUs;
      pulses = pulseCount;
      frontPulses = frontPulseCount;
      pulseCount = frontPulseCount = 0;
    }
    lastFrequencyHz = elapsedUs ? pulses * 1000000.0f / elapsedUs : 0;
    lastFrontFrequencyHz = elapsedUs ? frontPulses * 1000000.0f / elapsedUs : 0;
    changed = true;
  }
  const bool brakePressed = brakeButton.pressed();
  // Each wheel speed follows its Hz / 6 and ramps to zero while the brake is pressed.
  const float elapsedS = uint32_t(nowMs - lastRampMs) / 1000.0f;
  const float targetSpeed = brakePressed ? 0 : constrain(lastFrequencyHz / HZ_PER_KMH, 0.0f, SPEED_MAX_KMH);
  const float targetFrontSpeed = brakePressed ? 0 : constrain(lastFrontFrequencyHz / HZ_PER_KMH, 0.0f, SPEED_MAX_KMH);
  displayedSpeed = stepSpeed(displayedSpeed, targetSpeed, elapsedS);
  displayedFrontSpeed = stepSpeed(displayedFrontSpeed, targetFrontSpeed, elapsedS);
  lastRampMs = nowMs;
  const bool transitioning = displayedSpeed != targetSpeed || displayedSpeed != lastPublishedSpeed ||
                             displayedFrontSpeed != targetFrontSpeed || displayedFrontSpeed != lastPublishedFrontSpeed;
  // Publish more frequently during a transition for responsive gauge updates.
  const uint16_t publishInterval = transitioning || brakePressed ? 200 : 1000;
  if (!changed && uint32_t(nowMs - lastPublishMs) < publishInterval) return;
  lastPublishMs = nowMs;
  lastPublishedSpeed = displayedSpeed;
  lastPublishedFrontSpeed = displayedFrontSpeed;
  const float frequencyHz = lastFrequencyHz, frontFrequencyHz = lastFrontFrequencyHz;
  const float speedKmh = displayedSpeed;
  Serial.print(F("{\"schema\":2,\"source\":\"nano_live\",\"smart_key\":true,\"acquisition_enabled\":true,\"brake_pressed\":"));
  Serial.print(brakePressed ? F("true") : F("false"));
  Serial.print(F(",\"uptime_ms\":"));
  Serial.print(millis());
  Serial.print(F(",\"adc_reference_v\":")); Serial.print(ADC_REFERENCE_V, 3);
  Serial.print(F(",\"voltage_reference\":\"nominal_unverified\",\"sensors\":{"));
  for (uint8_t i = 0; i < 5; i++) {
    if (i) Serial.print(',');
    printSensor(i);
  }
  supplyVoltage = readSupplyVoltage();
  for (uint8_t i = 0; i < 2; i++) referenceVoltage[i] = readReference(i);
  Serial.print(F("},\"supply_v\":")); Serial.print(supplyVoltage, 3);
  for (uint8_t i = 0; i < 2; i++) {
    Serial.print(F(",\"")); Serial.print(referenceNames[i]); Serial.print(F("\":"));
    Serial.print(referenceVoltage[i], 3);
  }
  Serial.print(F(",\"frequency_hz\":"));
  Serial.print(frequencyHz, 2);
  // engine_rpm keeps its old meaning for the dashboard: the (rear) speed in km/h.
  Serial.print(F(",\"engine_rpm\":"));
  Serial.print(speedKmh, 1);
  Serial.print(F(",\"rear_speed_kmh\":")); Serial.print(speedKmh, 1);
  Serial.print(F(",\"front_frequency_hz\":")); Serial.print(frontFrequencyHz, 2);
  Serial.print(F(",\"front_speed_kmh\":")); Serial.print(displayedFrontSpeed, 1);
  Serial.println(F(",\"speed_calibration\":\"hz_div_6\"}"));
  Serial.println(F("# --- LIVE SENSORS (voltage assumes 5.000 V reference) ---"));
  Serial.print(F("# BRAKE: "));
  Serial.println(brakePressed ? F("PRESSED - wheel speeds ramp toward zero") : F("RELEASED - wheel speeds follow D7 / D8"));
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
  Serial.print(F(" V | REF1 A5=")); Serial.print(referenceVoltage[0], 3);
  Serial.print(F(" V | REF2 A6=")); Serial.print(referenceVoltage[1], 3); Serial.println(F(" V"));
  Serial.print(F("# REAR D7 | frequency=")); Serial.print(frequencyHz, 2);
  Serial.print(F(" Hz | speed=")); Serial.print(speedKmh, 1); Serial.println(F(" km/h (Hz / 6)"));
  Serial.print(F("# FRONT D8 | frequency=")); Serial.print(frontFrequencyHz, 2);
  Serial.print(F(" Hz | speed=")); Serial.print(displayedFrontSpeed, 1); Serial.println(F(" km/h (Hz / 6)"));
  Serial.println(F("# ----------------------------------------------------"));
  publishEspSnapshot(frequencyHz, brakePressed, speedKmh, frontFrequencyHz);
}
