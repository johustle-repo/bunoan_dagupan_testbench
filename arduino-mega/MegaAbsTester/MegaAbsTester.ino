#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <math.h>
#include "PotGenerator.h"
#include "SupplyVoltage.h"
#include "PagePot.h"

#if !defined(__AVR_ATmega2560__)
#error "Select Arduino Mega or Mega 2560 (ATmega2560)."
#endif

// ABS tester for an Arduino Mega 2560 Pro (or full-size Mega 2560), in place
// of the Nano (arduino-nano/NanoAbsTester): same pins, same "A1" frames, so
// the ESP32 (esp32-abs-tester-28/AbsTester28) needs no change. Measures the
// two wheel-speed sensors and the Declog frequency, drives the TCS / ABS LEDs
// and sends the readings to the ESP32.
//   D7 speed 1 (rear) and RPM, D8 speed 2 (front), 0-5 V square waves
//   speed = Hz / 6 km/h, max 250; RPM = D7 Hz x 15000 / 1560, max 15000
//   D2 Declog frequency, 0-100 Hz square wave, 0-5 V
//   D13 TCS LED (built-in): steady when both speeds match (whole km/h), blinking otherwise
//   D12 ABS LED: on when D7 is at least 10 % and 5 km/h slower than D8
//   A3 Reference 1, A4 battery: 0-5 V at the pin mapped to 0-30 V through the
//   PCB calibration table (same as the ECU testbench), filtered.
//   TX1 (D18) -> 1 kOhm -> ESP32 RX2 (GPIO16), common GND, 115200 8N1, one-way
//   (hardware Serial1; the USB Serial Monitor stays on Serial).
//   PTV09 10k pots: A1 rear -> D44 0-1500 Hz, A0 front -> D3 0-1500 Hz, A2 Declog -> D11 0-100 Hz
//   square waves (PotGenerator.h, Timers 5 / 3 / 1); jumper them into
//   D7 / D8 / D2 to test.
//   A5 page pot (10k, PagePot.h): the ESP32 screen's page navigator,
//   0-341 page 1, 342-682 page 2, 683-1023 page 3; reading and page go in the frame.
// Timer2 (compare A) samples D2, D7 and D8 at 20 kHz (up to about 10 kHz per
// input). With all four timers in use, analogWrite() on D2, D3, D5, D9-D13
// and D44-D46, tone() and Servo can't be used.

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

// On the Mega, D7 is port H bit 4, D8 port H bit 5 and D2 port E bit 4.
#define D7_HIGH() ((PINH & _BV(PH4)) != 0)
#define D8_HIGH() ((PINH & _BV(PH5)) != 0)
#define D2_HIGH() ((PINE & _BV(PE4)) != 0)

HardwareSerial& esp = Serial1;  // TX1 = D18 -> ESP32 GPIO16.

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
const uint8_t inputPins[2] = {A3, A4};  // REF1 A3, battery A4.

char linkBuffer[128];

// D7: Speed 1 and RPM, D8: Speed 2, D2: Declog. Rising edges, sampled at 20 kHz:
// Timer2 runs free at 500 kHz, so each sample moves compare A on by 25 ticks (50 us).
ISR(TIMER2_COMPA_vect) {
  uint8_t next = OCR2A + 25;
  if (uint8_t(next - TCNT2 - 1) > 24) next = TCNT2 + 2;  // Held off by another interrupt: resume now.
  OCR2A = next;
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

void readVolts() {
  supply::read();  // Measured 5 V rail for the pin volts (about 4 ms).
  for (uint8_t i = 0; i < 2; i++) voltsMv[i] = unsigned(readInput(i) * 1000 + 0.5);
  ref1V = voltsMv[0] / 1000.0;
  batteryV = voltsMv[1] / 1000.0;
}

// A1 frame (same as the Nano): key (always 1), brake (none, 0), D7 Hz x100,
// speed 1 x100, REF1 mV, REF2 (unused, 0), current (unused, 0), battery mV,
// D8 Hz x100, speed 2 x100, RPM, TCS (1 steady, 0 blinking), ABS (1 active),
// D2 Declog Hz x100, the rear (A1) / front (A0) / Declog (A2) pot generator outputs (D44 / D3 / D11; same slots as before)
// in Hz x100, the A5 page pot reading (0-1023) and its page (1-3),
// then an XOR checksum of everything before '*'. The ESP32 shows
// these values as they are.
void sendFrame() {
  const unsigned int* mv = voltsMv;
  const int count = snprintf(linkBuffer, sizeof(linkBuffer), "A1,1,0,%lu,%u,%u,0,0,%u,%lu,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%u,%u",
    (unsigned long)(frequencyHz1 * 100 + 0.5), unsigned(speed1 * 100 + 0.5), mv[0], mv[1],
    (unsigned long)(frequencyHz2 * 100 + 0.5), unsigned(speed2 * 100 + 0.5),
    unsigned(rpm + 0.5), unsigned(speedsEqual), unsigned(absActive),
    (unsigned long)(declogHz * 100 + 0.5), (unsigned long)(genRear.outHz * 100 + 0.5),
    (unsigned long)(genFront.outHz * 100 + 0.5), (unsigned long)(genDeclog.outHz * 100 + 0.5),
    unsigned(pagePot::raw), unsigned(pagePot::page));
  if (count < 0 || count > int(sizeof(linkBuffer) - 5)) return;
  uint8_t checksum = 0;
  for (int i = 0; i < count; i++) checksum ^= uint8_t(linkBuffer[i]);
  snprintf(linkBuffer + count, sizeof(linkBuffer) - count, "*%02X\n", checksum);
  // Hardware UART: the bytes go out from the TX buffer by interrupt, so the
  // sampler keeps running through the frame.
  esp.write(reinterpret_cast<const uint8_t*>(linkBuffer), count + 4);
}

// Reciprocal frequency: the edges counted since the previous window's last
// edge, divided by the time between that edge and this window's last edge.
// That spans whole periods, so the reading doesn't jump by a count
// (+-10 Hz on a 100 ms window) from window to window; the error is the 50 us
// sampling step over the ~100 ms span (about +-1 Hz at 2000 Hz, 0.2 km/h).
// With no edge in a window the last value is held for up to 2.5 s (slow
// signals), then it drops to 0.
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

// One aligned line per pot, e.g.
// "Rear   A1 -> D44 :  1500.0 Hz  (max 1500)  noise 1 counts". The noise is how
// far the pot's reading moved in the last second; a pot left alone should
// show about 0-2, and "CHECK POT" flags more than 6.
void printGenerator(const char* label, PotGenerator& gen) {
  char hz[10];
  dtostrf(gen.outHz, 7, 1, hz);  // Right-aligned, 1 decimal.
  Serial.print(label);
  Serial.print(hz);
  Serial.print(F(" Hz  (max "));
  Serial.print(gen.maxHz, 0);
  Serial.print(gen.outHz > 0 ? F(")") : F(", OFF)"));
  const float noise = gen.takeNoise();
  Serial.print(F("  noise "));
  Serial.print(noise, 0);
  Serial.println(noise > 6 ? F(" counts  CHECK POT") : F(" counts"));
}

void printGenerators() {
  Serial.println(F("---- Pot generators ----"));
  printGenerator("Rear   A1 -> D44 :", genRear);
  printGenerator("Front  A0 -> D3  :", genFront);
  printGenerator("Declog A2 -> D11 :", genDeclog);
  Serial.println(F("------------------------"));
}

void setup() {
  Serial.begin(115200);  // USB Serial Monitor.
  for (PotGenerator* g : generators) g->begin();  // Timers 5, 3 and 1 stopped, outputs LOW.
  esp.begin(115200);     // ESP32 link on TX1 (D18).
  pagePot::begin();      // Page pot on A5.

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
    TIMSK2 = 0;
    TCCR2A = 0;
    TCCR2B = _BV(CS21) | _BV(CS20);
    TCNT2 = 0;
    OCR2A = TCNT2 + 25;  // 16 MHz / 32 / 25 = 20 kHz.
    TIFR2 = _BV(OCF2A);
    TIMSK2 |= _BV(OCIE2A);
    windowStartUs = micros();
  }

  lastTcsBlink = millis();
  Serial.println(F("Mega ABS tester ready. ESP32 link on TX1 (D18), 115200 baud."));
}

void loop() {
  unsigned long now = millis();

  for (PotGenerator* g : generators) g->update(now);
  pagePot::update(now);

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

    const float rawSpeed1 = frequencyHz1 / 6.0;
    const float rawSpeed2 = frequencyHz2 / 6.0;

    // Display speeds capped at 250 km/h.
    speed1 = constrain(rawSpeed1, 0.0, 250.0);
    speed2 = constrain(rawSpeed2, 0.0, 250.0);

    // RPM from D7: 0-1560 Hz maps to 0-15000 RPM.
    rpm = constrain(frequencyHz1 * 15000.0 / 1560.0, 0.0, 15000.0);

    // TCS: compare displayed speeds to the nearest whole km/h.
    speedsEqual = (round(speed1) == round(speed2));

    // Simulated ABS: the D7 wheel is at least 10 % slower than the D8
    // reference, with a minimum difference of 5 km/h.
    absActive = (rawSpeed2 > 0.0) && (rawSpeed1 <= rawSpeed2 * 0.90) && ((rawSpeed2 - rawSpeed1) >= 5.0);

    digitalWrite(ABS_LED, absActive ? HIGH : LOW);
  }

  if (now - lastVoltsMs >= VOLTS_MS || lastVoltsMs == 0) {
    lastVoltsMs = now;
    readVolts();
  }

  // USB Serial Monitor report, once a second.
  if (now - lastReportMs >= REPORT_MS) {
    lastReportMs = now;
    printGenerators();
    Serial.print(F("D7 Frequency: ")); Serial.print(frequencyHz1, 1); Serial.println(F(" Hz"));
    Serial.print(F("Speed 1: ")); Serial.print(speed1, 1); Serial.println(F(" km/h"));
    Serial.print(F("RPM: ")); Serial.println(rpm, 0);
    Serial.print(F("D8 Frequency: ")); Serial.print(frequencyHz2, 1); Serial.println(F(" Hz"));
    Serial.print(F("Speed 2: ")); Serial.print(speed2, 1); Serial.println(F(" km/h"));
    Serial.print(F("TCS: ")); Serial.println(speedsEqual ? F("ON") : F("BLINKING"));
    Serial.print(F("ABS: ")); Serial.println(absActive ? F("ACTIVE") : F("OFF"));
    Serial.print(F("Declog (D2): ")); Serial.print(declogHz, 1); Serial.println(F(" Hz"));
    Serial.print(F("REF1 (A3): ")); Serial.print(ref1V, 2);
    Serial.print(F(" V (pin ")); Serial.print(pinV[0], 2); Serial.println(F(" V)"));
    Serial.print(F("Battery (A4): ")); Serial.print(batteryV, 2);
    Serial.print(F(" V (pin ")); Serial.print(pinV[1], 2); Serial.println(F(" V)"));
    Serial.print(F("Supply (5V pin, measured): ")); Serial.print(supply::volts, 3); Serial.println(F(" V"));
    Serial.print(F("Page pot (A5): ")); Serial.print(pagePot::raw);
    Serial.print(F(" -> page ")); Serial.println(pagePot::page);
    Serial.println();
  }

  // ESP32 link, 10 times a second.
  if (now - lastPublish >= PUBLISH_MS) {
    lastPublish = now;
    sendFrame();
  }

  // TCS indicator: steady when the speeds match, blinking every 500 ms when not.
  const unsigned long ledNow = millis();
  if (speedsEqual) {
    tcsLedState = true;
    digitalWrite(TCS_LED, HIGH);
    lastTcsBlink = ledNow;
  } else if (ledNow - lastTcsBlink >= TCS_BLINK_INTERVAL) {
    lastTcsBlink = ledNow;
    tcsLedState = !tcsLedState;
    digitalWrite(TCS_LED, tcsLedState);
  }
}
