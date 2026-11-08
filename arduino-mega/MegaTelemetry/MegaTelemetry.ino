#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "SensorConversion.h"
#include "SerialLink.h"
#include "Buttons.h"
#include "PotGenerator.h"
#include "SupplyVoltage.h"

#if !defined(__AVR_ATmega2560__)
#error "Select Arduino Mega or Mega 2560 (ATmega2560)."
#endif

// Arduino Mega 2560 (Pro) sender for the universal ESP32 receiver.
//   A0-A4 IAT, ECT, O2, MAP, TPS     A5/A6 Reference 1/2, A7 battery (0-30 V dividers)
//   Any of A0-A7 with nothing attached shows ERR (pull-up test, inputOpen()).
//   D7 RPM frequency                 D8 speed frequency (jumper from the D44 generator)
//   D9 ABS (the old brake input)     D10, D11 unused
//   D6 -> ESP32 GPIO17 (U6 frames)
//   A8 PTV09 10k pot -> D44 0-2000 Hz, A9 second pot -> D2 0-100 Hz (PotGenerator.h)
//   Declog = the D2 frequency (A9 pot), sent to the ESP32 in Hz; lamp on above 0 Hz.
// Smart key follows the Mega's power: every frame reports KEY ON, and the
// ESP32 shows KEY OFF when frames stop arriving (Mega off or unplugged).
// The USB serial monitor (9600 baud) prints one line per update with the
// 5-sensor diagnostic formulas in SensorConversion.h:
//   IAT: 25.0 C | ECT: 40.0 C | O2: 0.45 V | AFR: 14.7:1 | MAP: 55 kPa | TPS Angle: 12 % | TPS: 0.5V |
//   RPM: 3000 | SPEED: 40.0 km/h | ABS: OFF | DECLOG: 50.00 Hz | REF1: 12.00 V | REF2: 5.00 V | BATV: 13.80 V |
//   A8: 512 | GEN D44: 1000.0 Hz | A9: 512 | GEN D2: 50.00 Hz
// On the Mega, D7 is port H bit 4 and D8 is port H bit 5.
constexpr uint8_t RPM_BIT = _BV(PH4), SPEED_BIT = _BV(PH5);

volatile uint32_t rpmPulses = 0, speedPulses = 0;
volatile uint8_t previousPortH = 0;
uint32_t windowStartUs = 0;
uint32_t serialPauseUs = 0;
volatile bool acquisitionEnabled = true;
constexpr bool smartKeyOn = true;  // Mega powered = key on.
// ABS wiring (same as the old brake): off LOW, on HIGH (active-high).
DebouncedButton absButton(9, false);
float rpmFrequencyHz = 0, speedFrequencyHz = 0;
uint32_t lastPublishMs = 0;

// D7 and D8 have no external interrupt on the Mega; Timer1 samples both at
// 20 kHz, as on the Uno/Nano.
ISR(TIMER1_COMPA_vect) {
  const uint8_t portH = PINH;
  const uint8_t rising = portH & ~previousPortH;
  if (acquisitionEnabled) {
    if (rising & RPM_BIT) rpmPulses++;
    if (rising & SPEED_BIT) speedPulses++;
  }
  previousPortH = portH;
}

const uint8_t sensorPins[] = {A0, A1, A2, A3, A4}; // IAT, ECT, O2, MAP, TPS.
constexpr uint8_t SENSOR_SAMPLES = 20;
constexpr uint16_t PUBLISH_INTERVAL_MS = 500;
// RPM = D7 Hz x 15000 / 1549; speed = D8 Hz / 6 km/h, max 250 (1500 Hz). The ESP32 works
// both out from the frequencies in the frame; these are for the USB output.
constexpr float RPM_PER_HZ = 15000.0f / 1549, RPM_MAX = 15000;
constexpr float HZ_PER_KMH = 6.0f;
constexpr float SPEED_MAX_KMH = 250.0f;
float rawSnapshot[5]; // Averaged ADC counts, sent to the ESP32.

// 0-30 V inputs from the PCB (0-5 V outputs) on A5/A6 (REF1/REF2) and A7 (battery).
const uint8_t referencePins[] = {A5, A6};
constexpr uint8_t BATTERY_PIN = A7;
float referenceVoltage[2] = {};
float batteryVoltage = 0;
// Pin volts use the measured 5 V rail (SupplyVoltage.h, supply::volts), not
// an assumed 5.00 V. The IAT / ECT / O2 / MAP / TPS sensors stay on the plain
// 5 V scale: they are fed from the same rail, so they are ratiometric and a
// sagging rail moves the sensor and the ADC together.

// Calibration table measured on the PCB: pin volts (x) -> true input volts (y).
constexpr uint8_t CAL_POINTS = 8;
const float calPinV[CAL_POINTS] = {0.00f, 0.15f, 0.85f, 1.72f, 2.59f, 3.48f, 4.35f, 5.00f};
const float calTrueV[CAL_POINTS] = {0.00f, 1.00f, 5.00f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f};

// Linear interpolation between the calibration points, clamped to 0-30 V.
float getTrueVoltage(float x) {
  if (x <= calPinV[0]) return calTrueV[0];
  if (x >= calPinV[CAL_POINTS - 1]) return calTrueV[CAL_POINTS - 1];
  for (uint8_t i = 0; i < CAL_POINTS - 1; i++) {
    if (x <= calPinV[i + 1]) {
      return calTrueV[i] + (x - calPinV[i]) * (calTrueV[i + 1] - calTrueV[i]) /
                               (calPinV[i + 1] - calPinV[i]);
    }
  }
  return 0.0f;
}

// No-sensor check: with the internal pull-up (about 35k) switched on, an empty
// pin rises to 5 V, while a connected sensor or PCB output stays near its own
// voltage. Every reading of 1015 counts (4.96 V) or more = nothing attached.
// The pull-up is on for about 1 ms, after the pin has been read.
constexpr uint16_t OPEN_PIN_COUNTS = 1015;
bool inputOpen(uint8_t pin) {
  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(500);
  analogRead(pin);  // Discard: the ADC is switching channels.
  bool open = true;
  for (uint8_t n = 0; n < 4 && open; n++) open = analogRead(pin) >= OPEN_PIN_COUNTS;
  pinMode(pin, INPUT);  // Pull-up off again.
  return open;
}

// Divider input voltage: averages 20 readings 1 ms apart, converts to pin
// volts, then maps through the calibration table to 0-30 V. NAN = no input
// attached (printed and shown as ERR).
float readDividerInput(uint8_t pin) {
  analogRead(pin);  // Discard: the ADC is switching channels (or back from the bandgap).
  uint32_t sum = 0;
  for (uint8_t n = 0; n < SENSOR_SAMPLES; n++) {
    sum += analogRead(pin);
    delay(1);
  }
  if (inputOpen(pin)) return NAN;
  return getTrueVoltage(float(sum) / SENSOR_SAMPLES * (supply::volts / 1023.0f));
}

// SoftwareSerial TX disables interrupts, so Timer1 can't sample D7/D8 while a
// frame is sent; that time is excluded from the frequency window.
void publishEspSnapshot() {
  uint32_t pauseStart;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    acquisitionEnabled = false;
    pauseStart = micros();
  }
  sendEspSnapshot(rawSnapshot, rpmFrequencyHz, speedFrequencyHz, smartKeyOn, absButton.pressed(),
                  gen2.outHz, referenceVoltage, batteryVoltage);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    serialPauseUs += uint32_t(micros() - pauseStart);
    previousPortH = PINH;
    acquisitionEnabled = smartKeyOn;
  }
}

// Hardware smoothing filter: 20 readings 1 ms apart. Keeps the averaged
// counts for the ESP32 frame and returns the pin voltage. With no sensor
// attached it reports 1023 counts (5 V), which every sensor formula here and
// on the ESP32 treats as a fault, so it shows ERR.
float readSensor(uint8_t i) {
  uint32_t sum = 0;
  for (uint8_t n = 0; n < SENSOR_SAMPLES; n++) {
    sum += analogRead(sensorPins[i]);
    delay(1);
  }
  rawSnapshot[i] = inputOpen(sensorPins[i]) ? 1023.0f : float(sum) / SENSOR_SAMPLES;
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

// 0-30 V input: "12.00" or "ERR" with no input attached.
void printVolts(float v) {
  if (isnan(v)) Serial.print(F("ERR"));
  else { Serial.print(v, 2); Serial.print(F(" V")); }
}

void setup() {
  Serial.begin(9600);
  absButton.begin();
  beginEspLink();
  gen1.begin();
  gen2.begin();
  analogReference(DEFAULT);
  for (uint8_t i = 0; i < 5; i++) {
    pinMode(sensorPins[i], INPUT);
    digitalWrite(sensorPins[i], LOW); // Keep internal pull-ups disabled.
  }
  const uint8_t dividerPins[] = {referencePins[0], referencePins[1], BATTERY_PIN};
  for (uint8_t pin : dividerPins) {
    pinMode(pin, INPUT);
    digitalWrite(pin, LOW);
  }
  delay(1000);
  // D7/D8 are sampled by Timer1. No internal pull-up: provide a valid 0-to-VCC waveform.
  pinMode(7, INPUT);
  pinMode(8, INPUT);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    previousPortH = PINH;
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11);
    OCR1A = 99; // 16 MHz / 8 / 100 = 20 kHz.
    TCNT1 = 0;
    TIFR1 = _BV(OCF1A);
    TIMSK1 |= _BV(OCIE1A);
    windowStartUs = micros();
  }
  Serial.println(F("# SMART KEY ON: Mega powered, sending live data."));
}

void loop() {
  const uint32_t nowMs = millis();
  gen1.update(nowMs);
  gen2.update(nowMs);
  bool changed = false;
  if (absButton.update(nowMs)) changed = true;
  if (uint32_t(micros() - windowStartUs) >= 1000000UL) {
    uint32_t rpmCount, speedCount, elapsedUs;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      const uint32_t nowUs = micros();
      elapsedUs = uint32_t(nowUs - windowStartUs) - serialPauseUs;
      serialPauseUs = 0;
      windowStartUs = nowUs;
      rpmCount = rpmPulses;
      speedCount = speedPulses;
      rpmPulses = speedPulses = 0;
    }
    rpmFrequencyHz = elapsedUs ? rpmCount * 1000000.0f / elapsedUs : 0;
    speedFrequencyHz = elapsedUs ? speedCount * 1000000.0f / elapsedUs : 0;
    changed = true;
  }
  // Publish every 500 ms, sooner when a switch or the frequencies change.
  if (!changed && uint32_t(nowMs - lastPublishMs) < PUBLISH_INTERVAL_MS) return;
  if (uint32_t(nowMs - lastPublishMs) < 200) return;
  lastPublishMs = nowMs;
  supply::read();  // Measured 5 V rail for REF1 / REF2 / battery (about 4 ms).
  batteryVoltage = readDividerInput(BATTERY_PIN);
  const float iatV = readSensor(0), ectV = readSensor(1), o2V = readSensor(2);
  const float mapV = readSensor(3), tpsV = readSensor(4);
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
  Serial.print(F(" | RPM: ")); Serial.print(min(rpmFrequencyHz * RPM_PER_HZ, RPM_MAX), 0);
  Serial.print(F(" | SPEED: ")); Serial.print(min(speedFrequencyHz / HZ_PER_KMH, SPEED_MAX_KMH), 1);
  Serial.print(F(" km/h | ABS: ")); Serial.print(absButton.pressed() ? F("ON") : F("OFF"));
  Serial.print(F(" | DECLOG: ")); Serial.print(gen2.outHz, 2); Serial.print(F(" Hz"));
  Serial.print(F(" | REF1: ")); printVolts(referenceVoltage[0]);
  Serial.print(F(" | REF2: ")); printVolts(referenceVoltage[1]);
  Serial.print(F(" | BATV: ")); printVolts(batteryVoltage);
  Serial.print(F(" | 5V: ")); Serial.print(supply::volts, 3); Serial.print(F(" V"));
  Serial.print(F(" | A8: ")); Serial.print(analogRead(A8));  // Plain analogRead, 0-1023.
  Serial.print(F(" | GEN D44: ")); Serial.print(gen1.outHz, 1);
  Serial.print(F(" Hz | A9: ")); Serial.print(analogRead(A9));
  Serial.print(F(" | GEN D2: ")); Serial.print(gen2.outHz, 2); Serial.println(F(" Hz"));
  publishEspSnapshot();
}
