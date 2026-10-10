#pragma once
#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

// Enabled at user's explicit request. Direct 5 V-to-3.3 V wiring risks damage;
// voltage conditioning remains the recommended electrical connection.
constexpr bool ESP_LINK_ENABLED = true;
// One-way, 19200 baud, 8N1: Uno D6 TX -> NodeMCU D6 (GPIO12) RX.
// Timer2 drives TX without SoftwareSerial's long interrupt blocking,
// allowing the D7 frequency interrupt to continue counting.
// Timer2 PWM on Uno D3/D11 and tone() cannot be used with this link.
static volatile uint8_t linkLength = 0;
static volatile uint8_t linkIndex = 0;
static volatile uint8_t linkBit = 0;
static char linkBuffer[96];

ISR(TIMER2_COMPA_vect) {
  if (linkBit == 0) PORTD &= ~_BV(PD6); // Start.
  else if (linkBit <= 8) {
    if ((uint8_t(linkBuffer[linkIndex]) >> (linkBit - 1)) & 1) PORTD |= _BV(PD6);
    else PORTD &= ~_BV(PD6);
  } else PORTD |= _BV(PD6); // Stop/idle.
  if (++linkBit == 10) {
    linkBit = 0;
    if (++linkIndex >= linkLength) {
      TIMSK2 &= ~_BV(OCIE2A);
      linkLength = 0;
    }
  }
}

inline void beginEspLink() {
  if (!ESP_LINK_ENABLED) return;
  digitalWrite(6, HIGH);
  pinMode(6, OUTPUT);
  pinMode(5, INPUT); // Reserved, unused; NodeMCU D5 is also left as input.
  TCCR2A = _BV(WGM21);
  TCCR2B = _BV(CS21); // 16 MHz / 8 / 104 = 19230.77 baud.
  OCR2A = 103;
  TIMSK2 &= ~_BV(OCIE2A);
}

inline void sendEspSnapshot(const float* raw, float hz, bool keyOn, bool brakePressed, float mappedRpm) {
  if (!ESP_LINK_ENABLED || linkLength) return;
  // Integers avoid AVR printf float support. ADC values in 1/16 counts;
  // frequency in 1/100 Hz. XOR checksum covers the payload before '*'.
  const int count = snprintf(linkBuffer, sizeof(linkBuffer),
    "U3,%u,%u,%u,%u,%u,%u,%u,%lu,%u", unsigned(keyOn), unsigned(brakePressed), unsigned(raw[0] * 16 + 0.5f),
    unsigned(raw[1] * 16 + 0.5f), unsigned(raw[2] * 16 + 0.5f),
    unsigned(raw[3] * 16 + 0.5f), unsigned(raw[4] * 16 + 0.5f),
    (unsigned long)(hz * 100 + 0.5f), unsigned(mappedRpm * 100 + 0.5f));
  if (count < 0 || count > int(sizeof(linkBuffer) - 5)) return;
  uint8_t checksum = 0;
  for (int i = 0; i < count; i++) checksum ^= uint8_t(linkBuffer[i]);
  snprintf(linkBuffer + count, sizeof(linkBuffer) - count, "*%02X\n", checksum);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    linkIndex = 0;
    linkBit = 0;
    linkLength = count + 4;
    TCNT2 = 0;
    TIFR2 = _BV(OCF2A);
    TIMSK2 |= _BV(OCIE2A);
  }
}
