#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Wire.h>  // Needed for LovyanGFX library detection.
#include "AbsDisplay.h"

// ABS tester on the ESP32 + 4.0" 480 x 320 touchscreen, fed by the testbench.
// Receives the same frames as esp32-universal-v2 (Uno U3, Nano U4, Mega U5)
// on GPIO17 and shows only what an ABS unit depends on: two wheel-speed
// sensors (rear on D7, front on Nano / Mega D8), battery (Mega A7), current
// (Mega A8), Reference 1 (A5) and the brake switch (D9). No Wi-Fi dashboard
// and no engine sensors.
// Mega D6 -> 10k/20k divider -> ESP32 GPIO17, common GND. 19200 baud 8N1.
SoftwareSerial testbench(17, 16);  // RX, TX (TX unused).
char frame[96];
size_t frameLength = 0;
bool discardFrame = false;
bool havePacket = false;
uint32_t lastPacketMs = 0, badFrames = 0;

bool smartKeyOn = false, brakePressed = false;
float wheelHz[2] = {};   // D7 rear, D8 front frequency.
float wheelKmh[2] = {};  // Each eases toward its Hz / 6 km/h (max 250), or 0 while braking.
bool haveFront = false;  // Nano / Mega frames with the D8 field.
float ref1V = 0;
bool haveReferences = false;
float batteryV = 0, currentA = 0;
bool haveBattery = false;
uint32_t lastSpeedMs = 0, lastSerialReportMs = 0;

// U3 (Uno): key, brake, 5 x ADC (1/16 counts), Hz x100, speed x100.
// U4 (Nano): U3 plus Reference 1 and 2 in mV.
// U5 (Mega): U4 plus battery in mV and current in mA.
// U4 and U5 end with the D8 front wheel Hz x100 (older firmware leaves it out).
// Both speeds come from the Hz fields with the shared formula, the same for
// every board; the frame's own speed field is not used.
bool parseFrame(char* text) {
  char* star = strchr(text, '*');
  if (!star || strlen(star + 1) != 2) return false;
  uint8_t checksum = 0;
  for (char* p = text; p < star; ++p) checksum ^= uint8_t(*p);
  char* end = nullptr;
  const unsigned long expected = strtoul(star + 1, &end, 16);
  if (*end || expected != checksum) return false;
  *star = '\0';
  uint8_t fieldCount, maxFields;
  if (strncmp(text, "U3,", 3) == 0) fieldCount = maxFields = 9;
  else if (strncmp(text, "U4,", 3) == 0) { fieldCount = 11; maxFields = 12; }
  else if (strncmp(text, "U5,", 3) == 0) { fieldCount = 13; maxFields = 14; }
  else return false;
  constexpr unsigned long maximum[14] = {
      1, 1, 16368, 16368, 16368, 16368, 16368, 100000000UL, 30000, 40000, 40000, 40000, 10500, 100000000UL};
  unsigned long values[14];
  uint8_t parsed = 0;
  char* p = text + 3;
  for (uint8_t i = 0; i < maxFields; ++i) {
    if (*p < '0' || *p > '9') return false;
    values[i] = strtoul(p, &end, 10);
    if (values[i] > (i == fieldCount ? 100000000UL : maximum[i])) return false;  // Front Hz.
    parsed = i + 1;
    if (!*end && parsed >= fieldCount) break;
    if (*end != ',' || parsed == maxFields) return false;
    p = end + 1;
  }
  smartKeyOn = values[0] == 1;
  brakePressed = values[1] == 1;
  haveFront = parsed > fieldCount;
  wheelHz[REAR] = smartKeyOn ? values[7] / 100.0f : 0;
  wheelHz[FRONT] = smartKeyOn && haveFront ? values[fieldCount] / 100.0f : 0;
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
          Serial.print(F(" | REAR D7 ")); Serial.print(wheelHz[REAR], 1);
          Serial.print(F(" Hz ")); Serial.print(wheelKmh[REAR], 1);
          Serial.print(F(" km/h | FRONT D8 "));
          if (haveFront) {
            Serial.print(wheelHz[FRONT], 1);
            Serial.print(F(" Hz ")); Serial.print(wheelKmh[FRONT], 1); Serial.print(F(" km/h"));
          } else Serial.print(F("n/a"));
          Serial.print(F(" | BATTERY ")); Serial.print(batteryV, 2);
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
  // Each wheel: speed = Hz / 6 km/h, max 250, eased at 100 km/h per second
  // like the universal receiver; the brake pulls both to 0.
  const uint32_t now = millis();
  const bool fresh = havePacket && uint32_t(now - lastPacketMs) <= 3000;
  const float step = 100.0f * uint32_t(now - lastSpeedMs) / 1000.0f;
  for (uint8_t w = 0; w < 2; ++w) {
    float& kmh = wheelKmh[w];
    if (!fresh || !smartKeyOn) { kmh = 0; continue; }
    const float target = brakePressed ? 0 : min(wheelHz[w] / ABS_HZ_PER_KMH, ABS_SPEED_MAX);
    kmh = kmh < target ? min(target, kmh + step) : max(target, kmh - step);
  }
  lastSpeedMs = now;

  AbsData d{};
  d.link = !havePacket ? AbsData::WAITING : !fresh ? AbsData::STALE : AbsData::LIVE;
  d.keyOn = smartKeyOn;
  d.brake = brakePressed;
  d.front = haveFront;
  for (uint8_t w = 0; w < 2; ++w) {
    d.speedKmh[w] = wheelKmh[w];
    d.frequencyHz[w] = wheelHz[w];
  }
  d.batteryAvailable = haveBattery && fresh;
  d.batteryV = batteryV;
  d.currentA = currentA;
  d.references = haveReferences;
  d.ref1V = ref1V;
  absDisplay::update(d);
  yield();
}
