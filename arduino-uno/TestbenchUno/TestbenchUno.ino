#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#if !defined(__AVR_ATmega328P__)
#error "Select Arduino Uno R3 with ATmega328P. This sketch is not for Uno R4."
#endif

// Measure the Uno's actual 5V-to-GND voltage and enter it here.
constexpr float ADC_REFERENCE_V = 5.000f;
constexpr uint8_t SENSOR_PINS[] = {A0, A1, A2, A3, A4};
constexpr uint8_t SAMPLES = 16;

// Leave D7 disconnected until pulse voltage and waveform are verified.
// Change to true only for a compatible, verified signal within 0..VCC.
constexpr bool RPM_INPUT_VERIFIED = false;
volatile uint32_t risingEdges = 0;
volatile bool previousHigh = false;
uint32_t windowStartUs = 0;

// D7 is PD7 / PCINT23 on Uno R3, not a normal attachInterrupt pin.
ISR(PCINT2_vect) {
  const bool high = (PIND & _BV(PD7)) != 0;
  if (high && !previousHigh) risingEdges++;
  previousHigh = high;
}

float readVoltage(uint8_t pin) {
  analogRead(pin); // Discard first reading after switching ADC channel.
  delayMicroseconds(20);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < SAMPLES; i++) sum += analogRead(pin);
  return (sum / float(SAMPLES)) * ADC_REFERENCE_V / 1023.0f;
}

void setup() {
  Serial.begin(115200);
  analogReference(DEFAULT);
  for (uint8_t pin : SENSOR_PINS) pinMode(pin, INPUT);

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    windowStartUs = micros();
    if (RPM_INPUT_VERIFIED) {
      pinMode(7, INPUT); // No pull-up: source electrical type is not assumed.
      previousHigh = (PIND & _BV(PD7)) != 0;
      risingEdges = 0;
      PCIFR = _BV(PCIF2);
      PCMSK2 |= _BV(PCINT23);
      PCICR |= _BV(PCIE2);
    }
  }
  Serial.println(F("UNO R3 input monitor; A0-A4 sensor order unconfirmed"));
  Serial.println(F("Voltages use configured ADC_REFERENCE_V; Hz is not RPM"));
  if (!RPM_INPUT_VERIFIED) Serial.println(F("D7 frequency disabled pending electrical verification"));
}

void loop() {
  if (uint32_t(micros() - windowStartUs) < 1000000UL) return;
  uint32_t edges, elapsedUs, now;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    now = micros();
    elapsedUs = now - windowStartUs;
    edges = risingEdges;
    risingEdges = 0;
    windowStartUs = now;
  }
  const float frequencyHz = edges * (1000000.0f / elapsedUs);
  float volts[5];
  for (uint8_t i = 0; i < 5; i++) volts[i] = readVoltage(SENSOR_PINS[i]);

  // One JSON record per approximately one second for serial monitoring
  // and later PC dashboard integration. Analog readings are sequential.
  Serial.print(F("{\"uptime_ms\":")); Serial.print(millis());
  for (uint8_t i = 0; i < 5; i++) {
    Serial.print(F(",\"A")); Serial.print(i); Serial.print(F("_V\":"));
    Serial.print(volts[i], 3);
  }
  Serial.print(F(",\"rpm_hz\":"));
  if (RPM_INPUT_VERIFIED) Serial.print(frequencyHz, 2);
  else Serial.print(F("null"));
  Serial.println('}');
}
