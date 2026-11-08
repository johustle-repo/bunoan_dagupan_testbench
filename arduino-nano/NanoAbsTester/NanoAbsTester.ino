#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <math.h>
#include <SoftwareSerial.h>
#include "PotGenerator.h"
#include "SupplyVoltage.h"

#if !defined(__AVR_ATmega328P__)
#error "Select Arduino Uno or Arduino Nano (ATmega328P)."
#endif

// ABS tester for an Arduino Uno or Nano (same pins). Measures the two
// wheel-speed sensors and the Declog frequency, drives the TCS / ABS LEDs and
// sends the readings to the ESP32 (esp32-abs-tester-28/AbsTester28) as "A1" frames.
//   D7 speed 1 (rear) and RPM, D8 speed 2 (front, e.g. Mega D44), 0-5 V square waves
//   speed = Hz / 6 km/h, max 250; RPM = D7 Hz x 15000 / 1560, max 15000
//   D2 Declog frequency, 0-100 Hz square wave, 0-5 V (e.g. Mega D2)
//   D13 TCS LED: steady when both speeds match (whole km/h), blinking otherwise
//   D12 ABS LED: on when D7 is at least 10 % and 5 km/h slower than D8
//   A3 Reference 1, A4 battery: 0-5 V at the pin mapped to 0-30 V
//   through the PCB calibration table, filtered.
//   Pot frequency generators (PotGenerator.h), 50 % duty 0-5 V square waves:
//   rear pot A0 -> D9 0-1500 Hz, front pot A1 -> D10 0-1500 Hz,
//   Declog pot A2 -> D3 0-100 Hz. Jumper D9 -> D7, D10 -> D8, D3 -> D2 to self-test.
//   Divided outputs: D4 = D9 / 10, D11 = D10 / 10 (0-150 Hz), 50 % duty.
//   D5 (TX) -> ESP32 RX2 (GPIO16), common GND, 115200 8N1, one-way
//   (SoftwareSerial; the USB Serial Monitor stays on the normal Serial port).
// SoftwareSerial owns the pin-change interrupts, so Timer2 (compare A) samples
// D2, D7 and D8 at 20 kHz instead (up to about 10 kHz per input).

const byte TCS_LED = LED_BUILTIN;  // D13
const byte ABS_LED = 12;

const unsigned long TCS_BLINK_INTERVAL = 500;
// Speeds and Declog are measured every 100 ms and sent to the ESP32 10 times
// a second, so the gauges follow the pots without lag. REF1 / battery are
// read 4 times a second (each read takes about 13 ms); the Serial Monitor
// report stays once a second.
const unsigned long MEASURE_US = 100000UL;
const unsigned long PUBLISH_MS = 100;
const unsigned long VOLTS_MS = 250;
const unsigned long REPORT_MS = 1000;

#define D7_HIGH() ((PIND & _BV(PD7)) != 0)
#define D8_HIGH() ((PINB & _BV(PB0)) != 0)
#define D2_HIGH() ((PIND & _BV(PD2)) != 0)

volatile unsigned long pulseCount1 = 0;
volatile unsigned long pulseCount2 = 0;
volatile unsigned long pulseCount3 = 0;  // D2 Declog.

volatile bool previousHigh1 = false;
volatile bool previousHigh2 = false;
volatile bool previousHigh3 = false;
volatile unsigned long lastEdgeUs[3] = {0, 0, 0};  // micros() of the latest rising edge (D7, D8, D2).

unsigned long windowStartUs = 0;
unsigned long lastTcsBlink = 0;
unsigned long lastPublish = 0, lastVoltsMs = 0, lastReportMs = 0;
unsigned int voltsMv[2] = {0, 0};  // REF1, battery, as sent to the ESP32.

bool speedsEqual = true;
bool tcsLedState = true;
bool absActive = false;

float frequencyHz1 = 0, frequencyHz2 = 0, declogHz = 0;
float ref1V = 0, batteryV = 0;
float pinV[2] = {0, 0};  // Filtered A3 / A4 pin volts, 0-5 V.
float speed1 = 0, speed2 = 0;
float rpm = 0;

// REF1 / battery: the pin reads 0-5 V, mapped to 0-30 V through the PCB
// calibration table (same as the ECU testbench): pin volts (x) -> true volts (y).
// Pin volts use the measured supply (SupplyVoltage.h), not an assumed 5.00 V.
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
// Noise filter: 20 samples with the highest and lowest dropped, then a
// running average (each new reading moves the value 20 %, about 1 s to settle).
constexpr uint8_t SAMPLES = 20;
constexpr float FILTER = 0.2f;
bool filterStarted[2] = {false, false};
const uint8_t inputPins[2] = {A3, A4};          // REF1 A3, battery A4.

SoftwareSerial esp(6, 5);  // RX D6 (unused), TX D5 -> ESP32 GPIO16.
char linkBuffer[128];

// D7: Speed 1 and RPM, D8: Speed 2, D2: Declog. Rising edges, sampled at 20 kHz:
// Timer2 runs free at 500 kHz, so each sample
// moves compare A on by 25 ticks (50 us).
ISR(TIMER2_COMPA_vect) {
  const uint8_t t2 = TCNT2;
  uint8_t next = OCR2A + 25;
  if (uint8_t(next - t2 - 1) > 24) next = t2 + 2;  // Held off (SoftwareSerial byte): resume now.
  OCR2A = next;
  potgen::declogTick(t2);  // D3 Declog generator.
  const bool high1 = D7_HIGH(), high2 = D8_HIGH(), high3 = D2_HIGH();
  const bool rise1 = high1 && !previousHigh1, rise2 = high2 && !previousHigh2, rise3 = high3 && !previousHigh3;
  if (rise1 || rise2 || rise3) {
    const unsigned long nowUs = micros();
    if (rise1) { pulseCount1++; lastEdgeUs[0] = nowUs; }
    if (rise2) { pulseCount2++; lastEdgeUs[1] = nowUs; }
    if (rise3) { pulseCount3++; lastEdgeUs[2] = nowUs; }
  }
  previousHigh1 = high1;
  previousHigh2 = high2;
  previousHigh3 = high3;
}

// Reads A3 / A4 (analogRead 0-1023 = 0 V to the measured supply), filters it and returns 0-30 V
// through the calibration table.
float readInput(uint8_t i) {
  analogRead(inputPins[i]);  // First reading after switching pins is discarded.
  delayMicroseconds(200);
  uint32_t sum = 0;
  uint16_t lowest = 1023, highest = 0;
  for (uint8_t n = 0; n < SAMPLES; n++) {
    const uint16_t raw = analogRead(inputPins[i]);
    sum += raw;
    if (raw < lowest) lowest = raw;
    if (raw > highest) highest = raw;
    delayMicroseconds(500);
  }
  const float volts = float(sum - lowest - highest) / (SAMPLES - 2) * supply::volts / 1023.0f;
  if (!filterStarted[i]) { pinV[i] = volts; filterStarted[i] = true; }
  else pinV[i] += FILTER * (volts - pinV[i]);
  return getTrueVoltage(pinV[i]);
}

// A1 frame: key (always 1), brake (none, 0), D7 Hz x100, speed 1 x100,
// REF1 mV, REF2 (unused, 0), current (unused, 0), battery mV, D8 Hz x100,
// speed 2 x100, RPM, TCS (1 steady, 0 blinking), ABS (1 active), D2 Declog
// Hz x100, the rear (A0 -> D9) / front (A1 -> D10) / Declog (A2 -> D3) pot
// generator outputs in Hz x100 (17 fields, the ESP32's GEN_FIELDS frame),
// then an XOR checksum of everything before '*'. The ESP32 shows
// these values as they are.
void readVolts() {
  supply::read();  // Measured 5 V rail for the pin volts (about 4 ms).
  for (uint8_t i = 0; i < 2; i++) voltsMv[i] = unsigned(readInput(i) * 1000 + 0.5);
  ref1V = voltsMv[0] / 1000.0;
  batteryV = voltsMv[1] / 1000.0;
}

void sendFrame() {
  const unsigned int* mv = voltsMv;
  const int count = snprintf(linkBuffer, sizeof(linkBuffer), "A1,1,0,%lu,%u,%u,0,0,%u,%lu,%u,%u,%u,%u,%lu,%lu,%lu,%lu",
    (unsigned long)(frequencyHz1 * 100 + 0.5), unsigned(speed1 * 100 + 0.5), mv[0], mv[1],
    (unsigned long)(frequencyHz2 * 100 + 0.5), unsigned(speed2 * 100 + 0.5),
    unsigned(rpm + 0.5), unsigned(speedsEqual), unsigned(absActive),
    (unsigned long)(declogHz * 100 + 0.5), (unsigned long)(potgen::outHz(0) * 100 + 0.5),
    (unsigned long)(potgen::outHz(1) * 100 + 0.5), (unsigned long)(potgen::outHz(2) * 100 + 0.5));
  if (count < 0 || count > int(sizeof(linkBuffer) - 5)) return;
  uint8_t checksum = 0;
  for (int i = 0; i < count; i++) checksum ^= uint8_t(linkBuffer[i]);
  snprintf(linkBuffer + count, sizeof(linkBuffer) - count, "*%02X\n", checksum);
  // SoftwareSerial holds interrupts off for one byte at a time (about 87 us),
  // shorter than a half period up to about 5 kHz, so the sampler (which
  // catches up after each byte) keeps counting through the frame.
  esp.write(reinterpret_cast<const uint8_t*>(linkBuffer), count + 4);
}

// Reciprocal frequency: the edges counted since the previous window's last
// edge, divided by the time between that edge and this window's last edge.
// That spans whole periods, so the reading doesn't jump by a count
// (+-10 Hz on a 100 ms window) from window to window; the error is the 50 us
// sampling step over the ~100 ms span (about +-1 Hz at 2000 Hz, 0.2 km/h). With no edge in a window the
// last value is held for up to 2.5 s (slow signals), then it drops to 0.
float measureHz(uint8_t i, unsigned long pulses, unsigned long edgeUs, unsigned long nowUs, unsigned long elapsedUs) {
  static unsigned long prevEdgeUs[3];
  static bool prevValid[3] = {false, false, false};
  static float lastHz[3] = {0, 0, 0};
  if (pulses == 0) {
    if (!prevValid[i] || nowUs - prevEdgeUs[i] > 2500000UL) { prevValid[i] = false; lastHz[i] = 0; }
    return lastHz[i];
  }
  const unsigned long spanUs = edgeUs - prevEdgeUs[i];
  lastHz[i] = prevValid[i] && spanUs ? pulses * 1000000.0f / spanUs : pulses * 1000000.0f / elapsedUs;
  prevEdgeUs[i] = edgeUs;
  prevValid[i] = true;
  return lastHz[i];
}

void setup() {
  Serial.begin(115200);  // USB Serial Monitor.
  esp.begin(115200);     // ESP32 link on D5.
  esp.stopListening();   // One-way link.

  pinMode(7, INPUT);
  pinMode(8, INPUT);
  pinMode(2, INPUT);
  for (uint8_t pin : inputPins) pinMode(pin, INPUT);

  pinMode(TCS_LED, OUTPUT);
  pinMode(ABS_LED, OUTPUT);

  digitalWrite(TCS_LED, HIGH);
  digitalWrite(ABS_LED, LOW);

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    previousHigh1 = D7_HIGH();
    previousHigh2 = D8_HIGH();
    previousHigh3 = D2_HIGH();
    // Timer2 free-running in normal mode at 500 kHz (/32) for the sampler.
    // analogWrite() on D3 / D11 and tone() can't be used; Timer1 (D9 / D10)
    // belongs to the pot generators (no Servo).
    TIMSK2 = 0;
    TCCR2A = 0;
    TCCR2B = _BV(CS21) | _BV(CS20);
    TCNT2 = 0;
    OCR2A = TCNT2 + 25;      // 16 MHz / 32 / 25 = 20 kHz.
    TIFR2 = _BV(OCF2A);
    TIMSK2 |= _BV(OCIE2A);
    windowStartUs = micros();
  }
  potgen::begin();  // Pots A0 / A1 / A2, outputs D9 / D10 / D3.

  lastTcsBlink = millis();
}

void loop() {
  unsigned long now = millis();
  potgen::update(now);  // Pots, every 100 ms.

  if (micros() - windowStartUs >= MEASURE_US) {
    unsigned long pulses1, pulses2, pulses3, elapsedUs, nowUs, edge[3];
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      nowUs = micros();
      elapsedUs = nowUs - windowStartUs;
      windowStartUs = nowUs;
      pulses1 = pulseCount1;
      pulses2 = pulseCount2;
      pulses3 = pulseCount3;
      pulseCount1 = 0;
      pulseCount2 = 0;
      pulseCount3 = 0;
      for (uint8_t i = 0; i < 3; i++) edge[i] = lastEdgeUs[i];
    }

    frequencyHz1 = measureHz(0, pulses1, edge[0], nowUs, elapsedUs);
    frequencyHz2 = measureHz(1, pulses2, edge[1], nowUs, elapsedUs);
    declogHz = measureHz(2, pulses3, edge[2], nowUs, elapsedUs);

    float rawSpeed1 = frequencyHz1 / 6.0;
    float rawSpeed2 = frequencyHz2 / 6.0;

    // Display speeds capped at 250 km/h
    speed1 = constrain(rawSpeed1, 0.0, 250.0);
    speed2 = constrain(rawSpeed2, 0.0, 250.0);

    // RPM from D7: 0–1560 Hz maps to 0–15000 RPM
    rpm = constrain(
      frequencyHz1 * 15000.0 / 1560.0,
      0.0,
      15000.0
    );

    // TCS: compare displayed speeds to nearest whole km/h
    speedsEqual = (round(speed1) == round(speed2));

    // Simulated ABS:
    // D7 wheel is at least 10% slower than D8 reference,
    // with a minimum difference of 5 km/h.
    absActive =
      (rawSpeed2 > 0.0) &&
      (rawSpeed1 <= rawSpeed2 * 0.90) &&
      ((rawSpeed2 - rawSpeed1) >= 5.0);

    digitalWrite(ABS_LED, absActive ? HIGH : LOW);
  }

  if (now - lastVoltsMs >= VOLTS_MS || lastVoltsMs == 0) {
    lastVoltsMs = now;
    readVolts();
  }

  // USB Serial Monitor report, once a second.
  if (now - lastReportMs >= REPORT_MS) {
    lastReportMs = now;

    Serial.print("D7 Frequency: ");
    Serial.print(frequencyHz1, 1);
    Serial.println(" Hz");

    Serial.print("Speed 1: ");
    Serial.print(speed1, 1);
    Serial.println(" km/h");

    Serial.print("RPM: ");
    Serial.println(rpm, 0);

    Serial.print("D8 Frequency: ");
    Serial.print(frequencyHz2, 1);
    Serial.println(" Hz");

    Serial.print("Speed 2: ");
    Serial.print(speed2, 1);
    Serial.println(" km/h");

    Serial.print("TCS: ");
    Serial.println(speedsEqual ? "ON" : "BLINKING");

    Serial.print("ABS: ");
    Serial.println(absActive ? "ACTIVE" : "OFF");

    Serial.print("Declog (D2): ");
    Serial.print(declogHz, 1);
    Serial.println(" Hz");

    Serial.print("Pot rear A0 -> D9: ");
    Serial.print(potgen::outHz(0), 0);
    Serial.print(" Hz, front A1 -> D10: ");
    Serial.print(potgen::outHz(1), 0);
    Serial.print(" Hz, Declog A2 -> D3: ");
    Serial.print(potgen::outHz(2), 0);
    Serial.println(" Hz");

    Serial.print("Divided /10: D4 (D9) ");
    Serial.print(potgen::outHz(0) / potgen::DIVIDE, 1);
    Serial.print(" Hz, D11 (D10) ");
    Serial.print(potgen::outHz(1) / potgen::DIVIDE, 1);
    Serial.println(" Hz");

    Serial.print("REF1 (A3): ");
    Serial.print(ref1V, 2);
    Serial.print(" V (pin ");
    Serial.print(pinV[0], 2);
    Serial.println(" V)");

    Serial.print("Battery (A4): ");
    Serial.print(batteryV, 2);
    Serial.print(" V (pin ");
    Serial.print(pinV[1], 2);
    Serial.println(" V)");

    Serial.print("Supply (5V pin, measured): ");
    Serial.print(supply::volts, 3);
    Serial.println(" V");

    Serial.println();
  }

  // ESP32 link, 10 times a second.
  if (now - lastPublish >= PUBLISH_MS) {
    lastPublish = now;
    sendFrame();
  }

  // TCS indicator
  unsigned long ledNow = millis();

  if (speedsEqual) {
    // Equal speeds: steady ON
    tcsLedState = true;
    digitalWrite(TCS_LED, HIGH);
    lastTcsBlink = ledNow;
  } else {
    // Different speeds: blink every 500 ms
    if (ledNow - lastTcsBlink >= TCS_BLINK_INTERVAL) {
      lastTcsBlink = ledNow;
      tcsLedState = !tcsLedState;
      digitalWrite(TCS_LED, tcsLedState);
    }
  }
}
