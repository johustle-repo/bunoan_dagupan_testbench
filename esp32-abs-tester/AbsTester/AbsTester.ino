#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Wire.h>  // Needed for LovyanGFX library detection.
#include "AbsDisplay.h"

// ABS tester on the ESP32 + 4.0" 480 x 320 touchscreen, fed by the testbench.
// Receives the same frames as esp32-universal-v2 (Uno U3, Nano U4, Mega U5)
// on GPIO17 and shows only what an ABS unit depends on: the D7 speed knob as
// wheel speed, battery (Mega A7), current (Mega A8), Reference 1 (A5) and the
// brake switch (D9). No Wi-Fi dashboard and no engine sensors.
// Mega D6 -> 10k/20k divider -> ESP32 GPIO17, common GND. 19200 baud 8N1.
SoftwareSerial testbench(17, 16);  // RX, TX (TX unused).
char frame[96];
size_t frameLength = 0;
bool discardFrame = false;
bool havePacket = false;
uint32_t lastPacketMs = 0, badFrames = 0;

bool smartKeyOn = false, brakePressed = false;
float targetSpeed = 0;   // Testbench D7 speed, 0-300 km/h (the "mapped RPM" field).
float speedKmh = 0;      // Eased toward targetSpeed.
float ref1V = 0;
bool haveReferences = false;
float batteryV = 0, currentA = 0;
bool haveBattery = false;
uint32_t lastSpeedMs = 0, lastSerialReportMs = 0;

// U3 (Uno): key, brake, 5 x ADC (1/16 counts), Hz x100, speed x100.
// U4 (Nano): U3 plus Reference 1 and 2 in mV.
// U5 (Mega): U4 plus battery in mV and current in mA.
bool parseFrame(char* text) {
  char* star = strchr(text, '*');
  if (!star || strlen(star + 1) != 2) return false;
  uint8_t checksum = 0;
  for (char* p = text; p < star; ++p) checksum ^= uint8_t(*p);
  char* end = nullptr;
  const unsigned long expected = strtoul(star + 1, &end, 16);
  if (*end || expected != checksum) return false;
  *star = '\0';
  uint8_t fieldCount;
  if (strncmp(text, "U3,", 3) == 0) fieldCount = 9;
  else if (strncmp(text, "U4,", 3) == 0) fieldCount = 11;
  else if (strncmp(text, "U5,", 3) == 0) fieldCount = 13;
  else return false;
  constexpr unsigned long maximum[13] = {
      1, 1, 16368, 16368, 16368, 16368, 16368, 100000000UL, 30000, 40000, 40000, 40000, 10500};
  unsigned long values[13];
  char* p = text + 3;
  for (uint8_t i = 0; i < fieldCount; ++i) {
    if (*p < '0' || *p > '9') return false;
    values[i] = strtoul(p, &end, 10);
    if (values[i] > maximum[i]) return false;
    if (i < fieldCount - 1) {
      if (*end != ',') return false;
      p = end + 1;
    } else if (*end) return false;
  }
  smartKeyOn = values[0] == 1;
  brakePressed = values[1] == 1;
  targetSpeed = smartKeyOn ? values[8] / 100.0f : 0;
  haveReferences = fieldCount >= 11;
  ref1V = smartKeyOn && haveReferences ? values[9] / 1000.0f : 0;
  haveBattery = fieldCount == 13;
  batteryV = haveBattery ? values[11] / 1000.0f : 0;
  currentA = haveBattery ? values[12] / 1000.0f : 0;
  lastPacketMs = millis();
  havePacket = true;
  return true;
}

void setup() {
  Serial.begin(115200);
  absDisplay::begin();
  testbench.begin(19200, SWSERIAL_8N1, 17, 16, false, 256);
  Serial.println(F("ABS tester ready. Testbench link on GPIO17, 19200 baud."));
}

void loop() {
  while (testbench.available()) {
    const char c = testbench.read();
    if (c == '\r') continue;
    if (c == '\n') {
      frame[frameLength] = '\0';
      if (!discardFrame && frameLength && parseFrame(frame)) {
        if (uint32_t(millis() - lastSerialReportMs) >= 1000) {
          lastSerialReportMs = millis();
          Serial.print(F("# KEY ")); Serial.print(smartKeyOn ? F("ON") : F("OFF"));
          Serial.print(F(" | BRAKE ")); Serial.print(brakePressed ? F("ON") : F("OFF"));
          Serial.print(F(" | SPEED ")); Serial.print(speedKmh, 0);
          Serial.print(F(" km/h | BATTERY ")); Serial.print(batteryV, 2);
          Serial.print(F(" V | CURRENT ")); Serial.print(currentA, 2);
          Serial.print(F(" A | REF1 ")); Serial.print(ref1V, 2);
          Serial.print(F(" V | bad frames ")); Serial.println(badFrames);
        }
      } else if (discardFrame || frameLength) ++badFrames;
      frameLength = 0;
      discardFrame = false;
    } else if (!discardFrame) {
      if (frameLength < sizeof(frame) - 1) frame[frameLength++] = c;
      else discardFrame = true;
    }
  }
  // Speed eases at 100 km/h per second, like the universal receiver.
  const uint32_t now = millis();
  const bool fresh = havePacket && uint32_t(now - lastPacketMs) <= 3000;
  if (!fresh || !smartKeyOn) speedKmh = 0;
  else {
    const float step = 100.0f * uint32_t(now - lastSpeedMs) / 1000.0f;
    speedKmh = speedKmh < targetSpeed ? min(targetSpeed, speedKmh + step) : max(targetSpeed, speedKmh - step);
  }
  lastSpeedMs = now;

  AbsData d{};
  d.link = !havePacket ? AbsData::WAITING : !fresh ? AbsData::STALE : AbsData::LIVE;
  d.keyOn = smartKeyOn;
  d.brake = brakePressed;
  d.speedKmh = speedKmh;
  d.batteryAvailable = haveBattery && fresh;
  d.batteryV = batteryV;
  d.currentA = currentA;
  d.references = haveReferences;
  d.ref1V = ref1V;
  absDisplay::update(d);
  yield();
}
