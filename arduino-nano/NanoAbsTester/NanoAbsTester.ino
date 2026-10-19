#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <math.h>
#include <SoftwareSerial.h>

#if !defined(__AVR_ATmega328P__)
#error "Select Arduino Nano (ATmega328P)."
#endif

// Arduino Nano ABS tester. Measures the two wheel-speed sensors, drives the
// TCS / ABS LEDs and sends the readings to the ESP32
// (esp32-abs-tester-28/AbsTester28) as "A1" frames.
//   D7 speed 1 (rear) and RPM, D8 speed 2 (front), 0-5 V square waves
//   speed = Hz / 6 km/h, max 250; RPM = D7 Hz x 15000 / 1560, max 15000
//   D13 TCS LED: steady when both speeds match (whole km/h), blinking otherwise
//   D12 ABS LED: on when D7 is at least 10 % and 5 km/h slower than D8
//   A0 Reference 1, A1 battery, A2 Reference 2, A3 current (direct, 0-5 V)
//   D5 (TX) -> ESP32 RX2 (GPIO16), common GND, 115200 8N1, one-way
//   (SoftwareSerial; the USB Serial Monitor stays on the normal Serial port).
// SoftwareSerial owns the pin-change interrupts, so Timer1 samples D7 and D8
// at 20 kHz instead (up to about 10 kHz per input).

const byte TCS_LED = LED_BUILTIN;  // D13
const byte ABS_LED = 12;

const unsigned long TCS_BLINK_INTERVAL = 500;
const unsigned long PUBLISH_MS = 250;

#define D7_HIGH() ((PIND & _BV(PD7)) != 0)
#define D8_HIGH() ((PINB & _BV(PB0)) != 0)

volatile unsigned long pulseCount1 = 0;
volatile unsigned long pulseCount2 = 0;

volatile bool previousHigh1 = false;
volatile bool previousHigh2 = false;
volatile bool acquisitionEnabled = true;

unsigned long windowStartUs = 0;
unsigned long serialPauseUs = 0;
unsigned long lastTcsBlink = 0;
unsigned long lastPublish = 0;

bool speedsEqual = true;
bool tcsLedState = true;
bool absActive = false;

float frequencyHz1 = 0, frequencyHz2 = 0;
float ref1V = 0, batteryV = 0;
float speed1 = 0, speed2 = 0;
float rpm = 0;

// Analog inputs, all direct 0-5 V: analogRead 0-1023 mapped to the full scale.
// Frame order: REF1, REF2, current, battery.
const uint8_t inputPins[4] = {A0, A2, A3, A1};    // REF1 A0, REF2 A2, current A3, battery A1.
const float INPUT_FULL_SCALE[4] = {5.0, 5.0, 10.0, 5.0};  // Current: 0-5 V = 0-10 A.

SoftwareSerial esp(6, 5);  // RX D6 (unused), TX D5 -> ESP32 GPIO16.
char linkBuffer[80];

// D7: Speed 1 and RPM, D8: Speed 2. Rising edges, sampled at 20 kHz.
ISR(TIMER1_COMPA_vect) {
  const bool high1 = D7_HIGH(), high2 = D8_HIGH();
  if (acquisitionEnabled && high1 && !previousHigh1) pulseCount1++;
  if (acquisitionEnabled && high2 && !previousHigh2) pulseCount2++;
  previousHigh1 = high1;
  previousHigh2 = high2;
}

float readInput(uint8_t i) {
  analogRead(inputPins[i]);  // First reading after switching pins is discarded.
  delayMicroseconds(200);
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 16; n++) sum += analogRead(inputPins[i]);
  return sum / 16.0 * INPUT_FULL_SCALE[i] / 1023.0;
}

// A1 frame: key (always 1), brake (none, 0), D7 Hz x100, speed 1 x100,
// REF1 / REF2 mV, current mA, battery mV, D8 Hz x100, speed 2 x100, RPM,
// TCS (1 steady, 0 blinking), ABS (1 active), then an XOR checksum of
// everything before '*'. The ESP32 shows these values as they are.
void sendFrame() {
  unsigned int mv[4];
  for (uint8_t i = 0; i < 4; i++) mv[i] = unsigned(readInput(i) * 1000 + 0.5);
  ref1V = mv[0] / 1000.0;
  batteryV = mv[3] / 1000.0;
  const int count = snprintf(linkBuffer, sizeof(linkBuffer), "A1,1,0,%lu,%u,%u,%u,%u,%u,%lu,%u,%u,%u,%u",
    (unsigned long)(frequencyHz1 * 100 + 0.5), unsigned(speed1 * 100 + 0.5), mv[0], mv[1], mv[2], mv[3],
    (unsigned long)(frequencyHz2 * 100 + 0.5), unsigned(speed2 * 100 + 0.5),
    unsigned(rpm + 0.5), unsigned(speedsEqual), unsigned(absActive));
  if (count < 0 || count > int(sizeof(linkBuffer) - 5)) return;
  uint8_t checksum = 0;
  for (int i = 0; i < count; i++) checksum ^= uint8_t(linkBuffer[i]);
  snprintf(linkBuffer + count, sizeof(linkBuffer) - count, "*%02X\n", checksum);
  // SoftwareSerial TX blocks interrupts (about 6 ms per frame), so Timer1
  // can't sample D7 / D8 meanwhile; that time is left out of the window.
  unsigned long pauseStart;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    acquisitionEnabled = false;
    pauseStart = micros();
  }
  esp.write(reinterpret_cast<const uint8_t*>(linkBuffer), count + 4);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    serialPauseUs += micros() - pauseStart;
    previousHigh1 = D7_HIGH();
    previousHigh2 = D8_HIGH();
    acquisitionEnabled = true;
  }
}

void setup() {
  Serial.begin(115200);  // USB Serial Monitor.
  esp.begin(115200);     // ESP32 link on D5.
  esp.stopListening();   // One-way link.

  pinMode(7, INPUT);
  pinMode(8, INPUT);
  for (uint8_t pin : inputPins) pinMode(pin, INPUT);

  pinMode(TCS_LED, OUTPUT);
  pinMode(ABS_LED, OUTPUT);

  digitalWrite(TCS_LED, HIGH);
  digitalWrite(ABS_LED, LOW);

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    previousHigh1 = D7_HIGH();
    previousHigh2 = D8_HIGH();
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11);
    OCR1A = 99;  // 16 MHz / 8 / 100 = 20 kHz.
    TCNT1 = 0;
    TIFR1 = _BV(OCF1A);
    TIMSK1 |= _BV(OCIE1A);
    windowStartUs = micros();
  }

  lastTcsBlink = millis();
}

void loop() {
  unsigned long now = millis();

  if (micros() - windowStartUs >= 1000000UL) {
    unsigned long pulses1, pulses2, elapsedUs;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      const unsigned long nowUs = micros();
      elapsedUs = nowUs - windowStartUs - serialPauseUs;
      serialPauseUs = 0;
      windowStartUs = nowUs;
      pulses1 = pulseCount1;
      pulses2 = pulseCount2;
      pulseCount1 = 0;
      pulseCount2 = 0;
    }

    frequencyHz1 = elapsedUs ? pulses1 * 1000000.0 / elapsedUs : 0;
    frequencyHz2 = elapsedUs ? pulses2 * 1000000.0 / elapsedUs : 0;

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

    Serial.print("REF1 (A0): ");
    Serial.print(ref1V, 2);
    Serial.println(" V");

    Serial.print("Battery (A1): ");
    Serial.print(batteryV, 2);
    Serial.println(" V");

    Serial.println();
  }

  // ESP32 link, 4 times a second.
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
