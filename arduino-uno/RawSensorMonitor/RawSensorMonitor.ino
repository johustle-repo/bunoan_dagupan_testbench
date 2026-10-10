#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#if !defined(__AVR_ATmega328P__)
#error "Select Arduino Uno R3 (ATmega328P)."
#endif

volatile uint32_t pulseCount = 0;
volatile bool previousD7High = false;
uint32_t windowStartUs = 0;

ISR(PCINT2_vect) {
  const bool highNow = (PIND & _BV(PD7)) != 0;
  if (highNow && !previousD7High) pulseCount++;
  previousD7High = highNow;
}

const uint8_t sensorPins[] = {A0, A1, A2, A3, A4};
const char* const sensorNames[] = {"IAT", "ECT", "O2", "MAP", "TPS"};

void setup() {
  Serial.begin(115200);
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
  Serial.println(F("Uno raw sensor monitor: ADC counts 0-1023"));
  Serial.println(F("A0=IAT A1=ECT A2=O2 A3=MAP A4=TPS"));
  Serial.println(F("D7=frequency in Hz (rising edges)"));
}

void loop() {
  if ((uint32_t)(micros() - windowStartUs) < 1000000UL) return;
  uint32_t pulses, elapsedUs;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    const uint32_t nowUs = micros();
    elapsedUs = nowUs - windowStartUs;
    windowStartUs = nowUs;
    pulses = pulseCount;
    pulseCount = 0;
  }
  const float frequencyHz = pulses * 1000000.0f / elapsedUs;
  for (uint8_t i = 0; i < 5; i++) {
    analogRead(sensorPins[i]); // Discard first conversion after channel change.
    delayMicroseconds(200);
    const int raw = analogRead(sensorPins[i]);
    Serial.print('A');
    Serial.print(i);
    Serial.print(' ');
    Serial.print(sensorNames[i]);
    Serial.print(F(": raw="));
    Serial.println(raw);
  }
  Serial.print(F("D7: frequency="));
  Serial.print(frequencyHz, 2);
  Serial.println(F(" Hz"));
  Serial.println(F("------------------------"));
}
