#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <math.h>
#include <SoftwareSerial.h>
#include "PotGenerator.h"
#include "SupplyVoltage.h"
#include "SpeedEncoder.h"
#include "PageSelect.h"
#include "DeclogEncoder.h"

#if !defined(__AVR_ATmega328P__)
#error "Select Arduino Uno or Arduino Nano (ATmega328P)."
#endif

// ABS tester for an Arduino Uno or Nano (same pins). Measures the two
// wheel-speed sensors and the Declog frequency, drives the TCS / ABS LEDs and
// sends the readings to the ESP32 (esp32-abs-tester-28/AbsTester28) as "A1" frames.
//   D7 speed 1 (rear) and RPM, D8 speed 2 (front, e.g. Mega D44), 0-5 V square waves
//   speed = Hz / 6 km/h, max 250; RPM = D7 Hz x 15000 / 1560, max 15000
//   D2 Declog frequency, 0-100 Hz square wave, 0-5 V (e.g. Mega D2)
//   D6 speed sensor for page 2 of the ESP32 screen, 0-120 Hz (Hz / 6 =
//   km/h, 0-20), 0-5 V straight in (plain INPUT, no pull-up), common GND. Sent as
//   the 21st frame field; the ESP32 uses it instead of its GPIO34. With no
//   pulses the D6 level is the 23rd field: LOW = "OPEN OR SHORT SENSOR",
//   HIGH = "NO PULSE (STUCK HIGH)" on the ESP32 page 2.
//   A7 ABS light button for the ESP32 screen: A7 -> push button -> 5V, plus
//   10 kOhm A7 -> GND (A7 is analog-only, read with analogRead). The ABS
//   light is ON while the button is released (A7 LOW) and OFF while it is
//   pressed (A7 HIGH). Sent as the 22nd frame field (1 = light on). (No TCS
//   LED: no free pin. TCS is still worked out here and shown on the ESP32
//   screen and dashboard.)
//   D13 ABS LED, active low (5V -> resistor -> LED -> D13): follows the A7
//   ABS lamp, D13 LOW = LED on while A7 is LOW (the onboard L LED shows the
//   opposite). The simulated ABS (D7 at least 10 % and 5 km/h
//   slower than D8) is still sent to the ESP32
//   A3 Reference 1, A4 battery: 0-5 V at the pin mapped to 0-30 V
//   through the PCB calibration table (same formula as the ECU testbench).
//   Frequency generators (PotGenerator.h), 50 % duty 0-5 V square waves:
//   rear -> D9 0-1500 Hz and front -> D10 0-1500 Hz set by the DFRobot EC11 (SEN0235) speed
//   encoder (SpeedEncoder.h: A -> A0, B -> A1, C -> A5; turn = both wheels
//   together, click = TCS test, double click = reset of page 1 at 0 km/h + ECU reset, hold = brake), and
//   Declog -> D3 0-100 Hz set by the page / Declog encoder (DeclogEncoder.h:
//   A -> A2, B -> D12, SW -> A6; turn on page 3 = set 0-100 Hz, long press = output it).
//   Jumper D9 -> D7, D10 -> D8, D3 -> D2 to self-test.
//   Divided outputs: D4 = D9 / 6 (0-250 Hz, Hz = km/h), D11 = D10 / 15 (0-100 Hz), 50 % duty.
//   D5 (TX) -> ESP32 RX2 (GPIO16), common GND, 115200 8N1, one-way
//   (SoftwareSerial; the USB Serial Monitor stays on the normal Serial port).
//   The page is chosen here (encoder press, PageSelect.h) and sent to the
//   ESP32 as the 28th frame field; the ESP32 screen follows it. Only that
//   page's signals run, the rest are off / read 0:
//     page 1 SPEED:  D9 / D10 out, D4 (/6) / D11 (/15) out, D7 / D8 in, D13 ABS LED
//     page 2 SENSOR: D6 in only, no outputs
//     page 3 DECLOG: D3 out, D2 in
// SoftwareSerial owns the pin-change interrupts, so Timer2 (compare A) samples
// D2, D6, D7 and D8 at 20 kHz instead (up to about 10 kHz per input).

const byte ABS_IN = A7;  // ABS light button to 5V, 10 kOhm to GND: released (LOW) = light on, pressed (HIGH) = off.
const int ABS_PRESSED_ABOVE = 512;  // A7 reading (analog-only pin).
const byte ABS_LED = 13;

// Speeds and Declog are measured every 100 ms and sent to the ESP32 10 times
// a second, so the gauges follow the pots without lag. REF1 / battery are
// read 4 times a second (each read takes about 25 ms); the Serial Monitor
// report stays once a second.
const unsigned long MEASURE_US = 100000UL;
const unsigned long PUBLISH_MS = 100;
const unsigned long VOLTS_MS = 250;
const unsigned long REPORT_MS = 1000;

#define D7_HIGH() ((PIND & _BV(PD7)) != 0)
#define D8_HIGH() ((PINB & _BV(PB0)) != 0)
#define D2_HIGH() ((PIND & _BV(PD2)) != 0)
#define D6_HIGH() ((PIND & _BV(PD6)) != 0)

volatile unsigned long pulseCount1 = 0;
volatile unsigned long pulseCount2 = 0;
volatile unsigned long pulseCount3 = 0;  // D2 Declog.
volatile unsigned long pulseCount4 = 0;  // D6 speed sensor (ESP32 page 2).

volatile bool previousHigh1 = false;
volatile bool previousHigh2 = false;
volatile bool previousHigh3 = false;
volatile bool previousHigh4 = false;
volatile unsigned long lastEdgeUs[4] = {0, 0, 0, 0};  // micros() of the latest rising edge (D7, D8, D2, D6).

unsigned long windowStartUs = 0;
unsigned long lastPublish = 0, lastVoltsMs = 0, lastReportMs = 0;
unsigned int voltsMv[2] = {0, 0};  // REF1, battery, as sent to the ESP32.

bool speedsEqual = true;
bool absInputOn = true;  // A7 ABS light button (on until pressed), read every 100 ms.
// ECU reset counter: +1 on every knob double click (reset), sent as the 24th
// A1 field. The ESP32 fires its GPIO13 relay on each +1 step, which cuts the
// ABS ECU's power for 2 s so it boots fresh (latched error cleared).
uint8_t ecuResetCount = 0;
bool absActive = false;

float frequencyHz1 = 0, frequencyHz2 = 0, declogHz = 0, sensorHz = 0;  // sensorHz: D6.
// D6 sensor check, only while D6 gives no pulses: 0 = pulses (reading OK),
// 1 = stuck LOW (open or short sensor), 2 = stuck HIGH (no pulse).
enum : uint8_t { SENSOR_OK = 0, SENSOR_OPEN_SHORT = 1, SENSOR_STUCK_HIGH = 2 };
uint8_t sensorState = SENSOR_OPEN_SHORT;
float ref1V = 0, batteryV = 0;
float pinV[2] = {0, 0};  // A3 / A4 pin volts (20-reading average), 0-5 V.
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
// Same formula as the ECU testbench (arduino-mega/MegaTelemetry,
// readDividerInput): 20 readings, plain average; here spread over one 60 Hz
// cycle and smoothed (smoothed() below) against mains hum.
constexpr uint8_t SAMPLES = 20;
constexpr unsigned long MAINS_CYCLE_US = 16667;  // One 60 Hz mains cycle.
const uint8_t inputPins[2] = {A3, A4};          // REF1 A3, battery A4.

SoftwareSerial esp(6, 5);  // TX D5 -> ESP32 GPIO16. RX D6 is never listened on (stopListening), so D6 is the speed sensor input.
char linkBuffer[144];  // Longest 32-field frame (all fields at their maximum) is about 133 bytes.

// D7: Speed 1 and RPM, D8: Speed 2, D2: Declog, D6: page 2 speed sensor.
// Rising edges, sampled at 20 kHz: Timer2 runs free at 500 kHz, so each sample
// moves compare A on by 25 ticks (50 us).
ISR(TIMER2_COMPA_vect) {
  const uint8_t t2 = TCNT2;
  uint8_t next = OCR2A + 25;
  if (uint8_t(next - t2 - 1) > 24) next = t2 + 2;  // Held off (SoftwareSerial byte): resume now.
  OCR2A = next;
  potgen::declogTick(t2);  // D3 Declog generator.
  const bool high1 = D7_HIGH(), high2 = D8_HIGH(), high3 = D2_HIGH(), high4 = D6_HIGH();
  const bool rise1 = high1 && !previousHigh1, rise2 = high2 && !previousHigh2, rise3 = high3 && !previousHigh3,
             rise4 = high4 && !previousHigh4;
  if (rise1 || rise2 || rise3 || rise4) {
    const unsigned long nowUs = micros();
    if (rise1) { pulseCount1++; lastEdgeUs[0] = nowUs; }
    if (rise2) { pulseCount2++; lastEdgeUs[1] = nowUs; }
    if (rise3) { pulseCount3++; lastEdgeUs[2] = nowUs; }
    if (rise4) { pulseCount4++; lastEdgeUs[3] = nowUs; }
  }
  previousHigh1 = high1;
  previousHigh2 = high2;
  previousHigh3 = high3;
  previousHigh4 = high4;
  speedEnc::sample();
  declogEnc::sample();
}

// Reads A3 / A4 exactly like the ECU testbench: averages 20 readings 1 ms
// apart, converts to pin volts with the measured 5 V rail, then maps through
// the calibration table to 0-30 V.
float readInput(uint8_t i) {
  analogRead(inputPins[i]);  // Discard: the ADC is switching channels (or back from the bandgap).
  // 20 readings spread over exactly one 60 Hz mains cycle (16.67 ms), so the
  // hum the wires pick up averages out (1 ms apart = 20 ms left part of a
  // cycle in, and REF1 / BAT jumped about +-0.5 V).
  uint32_t sum = 0;
  const unsigned long startUs = micros();
  for (uint8_t n = 0; n < SAMPLES; n++) {
    while (micros() - startUs < n * MAINS_CYCLE_US / SAMPLES) {}
    sum += analogRead(inputPins[i]);
  }
  pinV[i] = float(sum) / SAMPLES * (supply::volts / 1023.0f);
  return getTrueVoltage(pinV[i]);
}

// Smoothing on top (4 reads a second): each new read moves the shown value
// 1/4 of the way (about 1 s to settle), so leftover noise doesn't flicker on
// the screen. A real change of more than 1.5 V jumps straight to the new
// value (no lag when the supply is switched on / off).
constexpr float SMOOTH = 0.25f, SNAP_V = 1.5f;
float smoothV[2] = {-1, -1};  // -1 = no reading yet.
float smoothed(uint8_t i, float v) {
  if (smoothV[i] < 0 || fabsf(v - smoothV[i]) > SNAP_V) smoothV[i] = v;
  else smoothV[i] += (v - smoothV[i]) * SMOOTH;
  return smoothV[i];
}

// A1 frame: key (always 1), brake (1 while the EC11 knob is held: SpeedEncoder.h), D7 Hz x100, speed 1 x100,
// REF1 mV, REF2 (unused, 0), current (unused, 0), battery mV, D8 Hz x100,
// speed 2 x100, RPM, TCS (1 steady, 0 blinking), ABS (1 active), D2 Declog
// Hz x100, the rear (D9) / front (D10) encoder and Declog (encoder -> D3)
// generator outputs in Hz x100, 0,0 (the Mega's old page pot slots), the
// single wheel the speed encoder adjusts (always 0: the knob drives both),
// the D6 speed sensor Hz x100, the A7 ABS light button (1 = light on, button released),
// the D6 sensor state (0 pulses OK, 1 open / short = stuck LOW, 2 stuck HIGH),
// the knob-click ECU reset counter (0-255, ESP32 GPIO13 relay), the ECU check code (0 not checked,
// 1 OK, 2 rear no return, 3 front no return, 4 both = ECU disconnected) and the knob turn counter
// (0-255, ESP32 white CHECKING lamp), the knob setting in whole Hz (0-1500, sent even while D9 / D10
// are cut, for the ESP32's blinking knob lamp) and the page (1-3, chosen with the page / Declog
// encoder; the ESP32 screen follows it), the encoder nav state (0 free, 1 selecting, 2 locked,
// 3 Declog adjust) and the selected page (1-3 while selecting, else 0), the Declog set value
// (0-100 Hz, turned on page 3) and the Declog output state (0 D3 off, 1 long press held =
// release to output, 2 D3 running) (32 fields, the ESP32's MAX_FIELDS frame), then
// an XOR checksum of everything before '*'. The ESP32 shows
// these values as they are.
// Wheel Hz -> km/h, Hz / 6 with a low-speed band matching the knob output
// (SpeedEncoder.h frontHz()): the ECU's ABS lamp goes off at about 55 Hz,
// which should read 10 km/h.
//   below 54 Hz: Hz / 5.4 (0-10 km/h)
//   54-60 Hz:    10 km/h
//   60 Hz up:    Hz / 6, as before
float hzToKmh(float hz) {
  if (hz < 54.0f) return hz / 5.4f;
  if (hz < 60.0f) return 10.0f;
  return hz / 6.0f;
}

void readVolts() {
  supply::read();  // Measured 5 V rail for the pin volts (about 4 ms).
  for (uint8_t i = 0; i < 2; i++) voltsMv[i] = unsigned(smoothed(i, readInput(i)) * 1000 + 0.5);
  ref1V = voltsMv[0] / 1000.0;
  batteryV = voltsMv[1] / 1000.0;
}

void sendFrame() {
  const unsigned int* mv = voltsMv;
  const int count = snprintf(linkBuffer, sizeof(linkBuffer), "A1,1,%u,%lu,%u,%u,0,0,%u,%lu,%u,%u,%u,%u,%lu,%lu,%lu,%lu,0,0,%u,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
    unsigned(speedEnc::braking),
    (unsigned long)(frequencyHz1 * 100 + 0.5), unsigned(speed1 * 100 + 0.5), mv[0], mv[1],
    (unsigned long)(frequencyHz2 * 100 + 0.5), unsigned(speed2 * 100 + 0.5),
    unsigned(rpm + 0.5), unsigned(speedsEqual), unsigned(absActive),
    (unsigned long)(declogHz * 100 + 0.5), (unsigned long)(potgen::outHz(0) * 100 + 0.5),
    (unsigned long)(potgen::outHz(1) * 100 + 0.5), (unsigned long)(potgen::outHz(2) * 100 + 0.5),
    0u, (unsigned long)(min(sensorHz, 10000.0f) * 100 + 0.5),  // ESP32 limit: 10 kHz.
    unsigned(absInputOn), unsigned(sensorState), unsigned(ecuResetCount),
    unsigned(speedEnc::ecuCode()), unsigned(speedEnc::turnCount),
    unsigned(speedEnc::frontHz()),   // Knob setting in Hz (also while the outputs are cut).
    unsigned(pageSel::page),         // Page 1-3 (the ESP32 screen follows it).
    unsigned(pageSel::nav), unsigned(pageSel::sel),  // Encoder nav (0 free, 1 select, 2 locked, 3 Declog adjust), selected page.
    unsigned(declogEnc::hz), unsigned(declogEnc::outState()));  // Declog set Hz, output state (0 off, 1 release to output, 2 running).
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
unsigned long prevEdgeUs[4];
bool prevValid[4] = {false, false, false, false};
float lastHz[4] = {0, 0, 0, 0};

// Fresh start (knob click): forget the held D7 / D8 readings so they read 0
// at once instead of after the 2.5 s hold.
void clearWheelReadings() {
  for (uint8_t i = 0; i < 2; i++) { prevValid[i] = false; lastHz[i] = 0; }
}

float measureHz(uint8_t i, unsigned long pulses, unsigned long edgeUs, unsigned long nowUs, unsigned long elapsedUs) {
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

// Switches the outputs to the page's signals.
void applyPage() {
  speedEnc::setActive(pageSel::speedOn());    // D9 / D10 / D4 / D11.
  potgen::declogEnabled = pageSel::declogOn();  // D3.
}

void setup() {
  Serial.begin(115200);  // USB Serial Monitor.
  Serial.println(F("# NANO ABS TESTER start (a reset sets both speeds to 0)"));
  esp.begin(115200);     // ESP32 link on D5.
  esp.stopListening();   // One-way link: D6 (its RX) is never read by SoftwareSerial.
  pinMode(6, INPUT);  // D6 speed sensor (page 2), no pull-up: LOW with no pulses = open or short sensor.

  pinMode(7, INPUT);
  pinMode(8, INPUT);
  pinMode(2, INPUT);
  for (uint8_t pin : inputPins) pinMode(pin, INPUT);

  pinMode(ABS_IN, INPUT);  // A7: analog-only, external 10 kOhm to GND.
  pinMode(ABS_LED, OUTPUT);  // D13, with the onboard L LED.

  digitalWrite(ABS_LED, LOW);

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    previousHigh1 = D7_HIGH();
    previousHigh2 = D8_HIGH();
    previousHigh3 = D2_HIGH();
    previousHigh4 = D6_HIGH();
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
  potgen::begin();    // Outputs D9 / D10 / D3.
  speedEnc::begin();  // Speed encoder on A0 / A1 / A5, both wheels at 0 km/h.
  declogEnc::begin(); // Page / Declog encoder on A2 / D12 / A6, Declog 0 Hz.
  applyPage();        // Power-up: page 1.

}

void loop() {
  unsigned long now = millis();
  if (declogEnc::update(now)) {  // Encoder lock / long press changed the page: switch the outputs.
    applyPage();
  }
  potgen::update(declogEnc::outHz);  // D3 Declog: the set value after a long press (0 off page 3).
  speedEnc::update(now);  // Speed encoder turns and button.

  if (micros() - windowStartUs >= MEASURE_US) {
    unsigned long pulses1, pulses2, pulses3, pulses4, elapsedUs, nowUs, edge[4];
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      nowUs = micros();
      elapsedUs = nowUs - windowStartUs;
      windowStartUs = nowUs;
      pulses1 = pulseCount1;
      pulses2 = pulseCount2;
      pulses3 = pulseCount3;
      pulses4 = pulseCount4;
      pulseCount1 = 0;
      pulseCount2 = 0;
      pulseCount3 = 0;
      pulseCount4 = 0;
      for (uint8_t i = 0; i < 4; i++) edge[i] = lastEdgeUs[i];
    }

    if (speedEnc::freshStart) {  // Knob click: D7 / D8 start from 0, as after power-up.
      speedEnc::freshStart = false;
      clearWheelReadings();
      pulses1 = pulses2 = 0;     // Edges from before the click don't count.
      ++ecuResetCount;           // ESP32 GPIO13 relay: ECU power off 2 s.
      Serial.print(F("# ECU RESET request "));
      Serial.print(ecuResetCount);
      Serial.println(F(" -> ESP32 GPIO13 relay cuts the ABS ECU power for 2 s"));
    }
    // Return-loop gate: D7 / D8 only count while their own output (D9 / D10)
    // is running. With the output stopped (knob 0, cut, brake at 0, not page 1)
    // a floating D7 / D8 wire picks up mains hum (a steady ~60 Hz on D8,
    // 0-10 Hz on D7), which showed as phantom speed / TCS on the screen and
    // brought a cut output straight back (ECU never shown as disconnected).
    for (uint8_t i = 0; i < 2; i++) {
      if (potgen::outHz(i) == 0) {
        prevValid[i] = false;
        lastHz[i] = 0;
        if (i == 0) pulses1 = 0; else pulses2 = 0;
      }
    }
    frequencyHz1 = measureHz(0, pulses1, edge[0], nowUs, elapsedUs);
    frequencyHz2 = measureHz(1, pulses2, edge[1], nowUs, elapsedUs);
    declogHz = measureHz(2, pulses3, edge[2], nowUs, elapsedUs);
    sensorHz = measureHz(3, pulses4, edge[3], nowUs, elapsedUs);
    // Inputs of the pages not on the ESP32 screen read 0.
    if (!pageSel::speedOn()) frequencyHz1 = frequencyHz2 = 0;
    // No-input cut: D9 stops without D7 pulses, D10 without D8 (SpeedEncoder.h).
    speedEnc::checkInputs(now, pulses1 > 0, pulses2 > 0);
    if (!pageSel::declogOn()) declogHz = 0;
    if (!pageSel::sensorOn()) sensorHz = 0;
    // D6 check before using the reading: no pulses -> look at the level.
    sensorState = sensorHz > 0 ? SENSOR_OK : D6_HIGH() ? SENSOR_STUCK_HIGH : SENSOR_OPEN_SHORT;
    absInputOn = analogRead(ABS_IN) <= ABS_PRESSED_ABOVE;  // A7 button: released = ABS light on, pressed = off.

    float rawSpeed1 = hzToKmh(frequencyHz1);
    float rawSpeed2 = hzToKmh(frequencyHz2);

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

    digitalWrite(ABS_LED, absInputOn ? LOW : HIGH);  // D13 active-low LED (5V -> resistor -> LED -> D13) mirrors the A7 ABS lamp.
  }

  if (now - lastVoltsMs >= VOLTS_MS || lastVoltsMs == 0) {
    lastVoltsMs = now;
    readVolts();
  }

  // USB Serial Monitor report, once a second.
  if (now - lastReportMs >= REPORT_MS) {
    lastReportMs = now;

    Serial.print("Page (encoder press): ");
    Serial.println(pageSel::page == 1 ? "1 SPEED" : pageSel::page == 2 ? "2 SENSOR" : "3 DECLOG");

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

    Serial.print("ABS light button (A7 ");
    Serial.print(analogRead(ABS_IN));
    Serial.print("): ");
    Serial.println(absInputOn ? "released = light ON" : "PRESSED = light OFF");

    Serial.print("Declog (D2): ");
    Serial.print(declogHz, 1);
    Serial.println(" Hz");

    Serial.print("Speed sensor (D6, ESP32 page 2): ");
    if (sensorState != SENSOR_OK) {
      Serial.println(sensorState == SENSOR_OPEN_SHORT ? "Open or Short Sensor (D6 stuck LOW)" : "No Pulse (D6 stuck HIGH)");
    } else {
      Serial.print(sensorHz, 1);
      Serial.print(" Hz = ");
      Serial.print(min(sensorHz / 6.0f, 20.0f), 1);  // ESP32 page 2: 0-120 Hz = 0-20 km/h.
      Serial.println(" km/h");
    }

    Serial.print("Encoder rear -> D9: ");
    Serial.print(potgen::outHz(0), 0);
    Serial.print(" Hz, front -> D10: ");
    Serial.print(potgen::outHz(1), 0);
    Serial.print(" Hz | knob ");
    Serial.print(speedEnc::kmh * speedEnc::UNIT_HZ / float(speedEnc::HZ_PER_KMH), 1);  // Knob setting (km/h).
    Serial.print(" km/h");
    if (speedEnc::cut[0]) Serial.print(" | D9 REAR CUT (no D7 input)");
    if (speedEnc::cut[1]) Serial.print(" | D10 FRONT CUT (no D8 input)");
    Serial.println();
    Serial.print(F("ECU: "));
    switch (speedEnc::ecuCode()) {
      case 1: Serial.println(F("OK")); break;
      case 2: Serial.println(F("REAR NO RETURN (D9 -> D7)")); break;
      case 3: Serial.println(F("FRONT NO RETURN (D10 -> D8)")); break;
      case 4: Serial.println(F("DISCONNECTED (no D7 / D8 return)")); break;
      default: Serial.println(F("NOT CHECKED (turn the knob)"));
    }
    speedEnc::printStatus(Serial);
    declogEnc::printStatus(Serial);
    Serial.print("Declog encoder -> D3: ");
    Serial.print(potgen::outHz(2), 0);
    Serial.println(pageSel::declogOn() ? " Hz" : " Hz (off: not page 3)");

    Serial.print("Divided: D4 (D9 /"); Serial.print(potgen::DIVIDE[0]); Serial.print(") ");
    Serial.print(potgen::outHz(0) / potgen::DIVIDE[0], 1);
    Serial.print(" Hz, D11 (D10 /"); Serial.print(potgen::DIVIDE[1]); Serial.print(") ");
    Serial.print(potgen::outHz(1) / potgen::DIVIDE[1], 1);
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
}
