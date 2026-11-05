#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "AbsDisplay.h"
#include "DashboardPage.h"
#include "FreqInput.h"

// ABS tester: ESP32 + 2.8" ILI9341 (Adafruit libraries), fed by the Arduino
// Nano running arduino-nano/NanoAbsTester (or the Mega 2560 Pro running
// arduino-mega/MegaAbsTester: same frames). It sends "A1" frames:
//   key, brake, D7 rear frequency x100, rear speed x100, REF1 / REF2 mV,
//   current mA, battery mV, D8 front frequency x100, front speed x100, RPM,
//   TCS (1 steady, 0 blinking), ABS (1 active), D2 Declog Hz x100 and the
//   A2 / A3 / A4 pot generator settings in Hz x100. Older
//   firmware stops after the battery, the front speed, ABS or Declog. REF2 and
//   current are not wired (sent as 0) and are ignored.
// Speed 1 (rear), speed 2 (front), RPM, TCS and ABS are shown exactly as the
// Nano works them out (speed = Hz / 6 km/h, RPM = D7 Hz x 15000 / 1560).
// Nano D5 (SoftwareSerial TX) -> ESP32 RX2 (GPIO16), common GND,
// 115200 baud 8N1 on hardware UART2. Only lines starting with "A1," are used.
// Wi-Fi hotspot with the dashboard at http://192.168.4.1/ and the readings
// as JSON at /api/live.
// LCD pages: swipe left on the touchscreen for page 2, right for page 1 (REAR / FRONT speed);
// page 2 (the 0-2000 Hz GPIO32 input gauge and the DECLOG bar, FreqInput.h).
const char* const AP_SSID = "ABS-Tester";
const char* const AP_PASSWORD = "ABSdemo2026";  // At least 8 characters.
constexpr uint8_t LINK_RX = 16, LINK_TX = 17;  // RX2, TX2 (TX unused).
HardwareSerial& nano = Serial2;
WebServer server(80);
char frame[128], lastBadFrame[128] = "";
size_t frameLength = 0;
bool discardFrame = false;
bool havePacket = false;
uint32_t lastPacketMs = 0, badFrames = 0, lastSerialReportMs = 0, bytesReceived = 0;
AbsData latest{};
uint8_t page = 1;  // LCD page: swipe left -> 2, swipe right -> 1.
AbsLamp absLamp;   // ABS light (switch-on check from the rear speed), LCD and JSON.

bool parseFrame(char* text) {
  char* star = strchr(text, '*');
  if (!star || strlen(star + 1) != 2) return false;
  uint8_t checksum = 0;
  for (char* p = text; p < star; ++p) checksum ^= uint8_t(*p);
  char* end = nullptr;
  const unsigned long expected = strtoul(star + 1, &end, 16);
  if (*end || expected != checksum) return false;
  *star = '\0';
  if (strncmp(text, "A1,", 3) != 0) return false;
  // 8 fields (oldest Nano), 10 (+ front sensor), 13 (+ RPM, TCS, ABS), 14 (+ Declog Hz)
  // or 17 (+ the three pot generator settings).
  constexpr uint8_t MIN_FIELDS = 8, FRONT_FIELDS = 10, LAMP_FIELDS = 13, DECLOG_FIELDS = 14, MAX_FIELDS = 17;
  // REF1 and battery: up to 30 V (0-30 V dividers, as on the ECU testbench).
  constexpr unsigned long maximum[MAX_FIELDS] = {1, 1, 100000000UL, 25000, 31000, 5500, 10500, 31000, 100000000UL, 25000,
                                                 15000, 1, 1, 100000000UL, 250000, 250000, 25000};
  unsigned long values[MAX_FIELDS];
  uint8_t parsed = 0;
  char* p = text + 3;
  for (uint8_t i = 0; i < MAX_FIELDS; ++i) {
    if (*p < '0' || *p > '9') return false;
    values[i] = strtoul(p, &end, 10);
    if (values[i] > maximum[i]) return false;
    parsed = i + 1;
    if (!*end && (parsed == MIN_FIELDS || parsed == FRONT_FIELDS || parsed == LAMP_FIELDS ||
                    parsed == DECLOG_FIELDS || parsed == MAX_FIELDS)) break;
    if (*end != ',' || parsed == MAX_FIELDS) return false;
    p = end + 1;
  }
  latest.keyOn = values[0] == 1;
  latest.brake = values[1] == 1;
  latest.frequencyHz = values[2] / 100.0f;
  latest.speedKmh = values[3] / 100.0f;
  latest.ref1V = values[4] / 1000.0f;
  latest.batteryV = values[7] / 1000.0f;
  latest.front = parsed >= FRONT_FIELDS;
  latest.frontHz = latest.front ? values[8] / 100.0f : 0;
  latest.frontKmh = latest.front ? values[9] / 100.0f : 0;
  latest.nanoLamps = parsed >= LAMP_FIELDS;
  latest.rpm = latest.nanoLamps ? values[10] : 0;
  latest.tcsSteady = latest.nanoLamps && values[11] == 1;
  latest.absActive = latest.nanoLamps && values[12] == 1;
  latest.declogHz = parsed >= DECLOG_FIELDS ? values[13] / 100.0f : 0;
  latest.gens = parsed == MAX_FIELDS;
  for (uint8_t i = 0; i < 3; ++i) latest.genHz[i] = latest.gens ? values[14 + i] / 100.0f : 0;
  lastPacketMs = millis();
  havePacket = true;
  return true;
}

// Same values the LCD shows; DECLOG is the D2 frequency (0-100 Hz = 0-100 %).
String liveJson() {
  const bool fresh = havePacket && uint32_t(millis() - lastPacketMs) <= 3000;
  const float percent = constrain(latest.declogHz / DECLOG_MAX_HZ * 100, 0.0f, 100.0f);
  String json;
  json.reserve(480);
  json = F("{\"link\":\"");
  json += !havePacket ? F("waiting") : !fresh ? F("stale") : F("live");
  json += F("\",\"key\":"); json += latest.keyOn ? F("true") : F("false");
  json += F(",\"brake\":"); json += latest.brake ? F("true") : F("false");
  json += F(",\"speed_kmh\":"); json += String(latest.speedKmh, 1);
  json += F(",\"frequency_hz\":"); json += String(latest.frequencyHz, 1);
  json += F(",\"front\":"); json += latest.front ? F("true") : F("false");
  json += F(",\"front_speed_kmh\":"); json += String(latest.frontKmh, 1);
  json += F(",\"front_frequency_hz\":"); json += String(latest.frontHz, 1);
  json += F(",\"rpm\":"); json += latest.rpm;
  json += F(",\"declog_hz\":"); json += String(latest.declogHz, 1);
  json += F(",\"declog_percent\":"); json += String(percent, 1);
  json += F(",\"ref1_v\":"); json += String(latest.ref1V, 3);
  json += F(",\"battery_v\":"); json += String(latest.batteryV, 3);
  json += F(",\"input_hz\":"); json += String(freqInput::hz, 1);
  json += F(",\"page\":"); json += page;
  // Pot generator settings (null with older Nano firmware).
  const char* const GEN_KEYS[3] = {",\"gen_a2_hz\":", ",\"gen_a3_hz\":", ",\"gen_a4_hz\":"};
  for (uint8_t i = 0; i < 3; ++i) { json += GEN_KEYS[i]; json += latest.gens ? String(latest.genHz[i], 1) : String("null"); }
  // Warning lights, the same as the LCD: ABS "on" / "off", TCS "flash" / "off",
  // SENSOR (web only) "ok" / "on" / "off".
  json += F(",\"abs_lamp\":\""); json += fresh && absLamp.on ? "on" : "off";
  json += F("\",\"tcs_lamp\":\""); json += fresh && tcsBlinking(latest) ? "flash" : "off";
  json += F("\",\"sensor_lamp\":\""); json += fresh ? sensorState(latest) : "off";
  json += '"';
  json += F(",\"bad_frames\":"); json += badFrames;
  json += F(",\"uptime_ms\":"); json += millis();
  json += '}';
  return json;
}

void setup() {
  Serial.begin(115200);
  absDisplay::begin();
  freqInput::begin();
  nano.setRxBufferSize(512);
  nano.begin(115200, SERIAL_8N1, LINK_RX, LINK_TX);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  server.on("/", []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
  });
  server.on("/api/live", []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", liveJson());
  });
  server.begin();
  Serial.println(F("ABS tester (2.8\") ready. Nano link on GPIO16 (RX2), 115200 baud."));
  Serial.print(F("Wi-Fi: ")); Serial.print(AP_SSID); Serial.print(F(" / ")); Serial.print(AP_PASSWORD);
  Serial.print(F(" | dashboard: http://")); Serial.print(WiFi.softAPIP()); Serial.println('/');
}

void loop() {
  while (nano.available()) {
    const char c = nano.read();
    ++bytesReceived;
    if (c == '\r') continue;
    if (c == '\n') {
      frame[frameLength] = '\0';
      // The Nano's readable Serial Monitor lines share the wire: skip them.
      const bool isFrame = !discardFrame && strncmp(frame, "A1,", 3) == 0;
      if (isFrame) strlcpy(lastBadFrame, frame, sizeof(lastBadFrame));  // parseFrame edits frame.
      if (isFrame && parseFrame(frame)) lastBadFrame[0] = '\0';
      else if (isFrame) ++badFrames;
      frameLength = 0;
      discardFrame = false;
    } else if (!discardFrame) {
      if (frameLength < sizeof(frame) - 1) frame[frameLength++] = c;
      else discardFrame = true;
    }
  }
  // USB serial monitor (115200), once a second: the readings, or why there are none.
  if (uint32_t(millis() - lastSerialReportMs) >= 1000) {
    lastSerialReportMs = millis();
    if (havePacket && uint32_t(millis() - lastPacketMs) <= 3000) {
      Serial.print(F("# SPEED 1 D7 ")); Serial.print(latest.frequencyHz, 1);
      Serial.print(F(" Hz = ")); Serial.print(latest.speedKmh, 1);
      Serial.print(F(" km/h | RPM ")); Serial.print(latest.rpm);
      Serial.print(F(" | TCS ")); Serial.print(tcsBlinking(latest) ? F("BLINKING") : F("OFF"));
      Serial.print(F(" | ABS ")); Serial.print(absLamp.on ? F("ON") : F("OFF"));
      Serial.print(F(" | SPEED 2 D8 "));
      if (latest.front) {
        Serial.print(latest.frontHz, 1); Serial.print(F(" Hz = "));
        Serial.print(latest.frontKmh, 1); Serial.print(F(" km/h"));
      } else Serial.print(F("n/a (old Nano firmware)"));
      Serial.print(F(" | DECLOG D2 ")); Serial.print(latest.declogHz, 1);
      Serial.print(F(" Hz | REF1 ")); Serial.print(latest.ref1V, 2);
      Serial.print(F(" V | BATTERY ")); Serial.print(latest.batteryV, 2);
      if (latest.gens) {
        Serial.print(F(" V | POT A2 ")); Serial.print(latest.genHz[0], 0);
        Serial.print(F(" Hz, A3 ")); Serial.print(latest.genHz[1], 0);
        Serial.print(F(" Hz, A4 ")); Serial.print(latest.genHz[2], 1); Serial.print(F(" Hz"));
      } else Serial.print(F(" V"));
      Serial.print(F(" | INPUT GPIO32 ")); Serial.print(freqInput::hz, 1);
      Serial.print(F(" Hz | bad frames ")); Serial.println(badFrames);
    } else {
      Serial.print(F("# NO DATA from the Nano: ")); Serial.print(bytesReceived);
      Serial.print(F(" bytes received on GPIO16, bad frames ")); Serial.print(badFrames);
      if (lastBadFrame[0]) { Serial.print(F(" | last line: ")); Serial.print(lastBadFrame); }
      Serial.print(bytesReceived ? F("") : F(" | check Nano D5 -> GPIO16 and GND"));
      Serial.print(F(" | INPUT GPIO32 ")); Serial.print(freqInput::hz, 1); Serial.println(F(" Hz"));
    }
  }
  AbsData d = latest;
  const bool fresh = havePacket && uint32_t(millis() - lastPacketMs) <= 3000;
  d.link = !havePacket ? AbsData::WAITING : !fresh ? AbsData::STALE : AbsData::LIVE;
  d.inputHz = freqInput::update();
  // ABS light: follows the rear speed while live; losing the link resets it
  // (the next start begins with it on).
  if (fresh) absLamp.update(latest.speedKmh); else absLamp.reset();
  d.absLamp = absLamp.on;
  // Swipe left: page 2, swipe right: page 1.
  const int8_t s = absDisplay::swipe();
  if (s) {
    if (s < 0) page = 2; else page = 1;
    Serial.print(F("# SWIPE ")); Serial.print(s < 0 ? F("left") : F("right"));
    Serial.print(F(" (raw dx ")); Serial.print(absDisplay::lastSwipeDx); Serial.print(F(", dy "));
    Serial.print(absDisplay::lastSwipeDy); Serial.print(F(") -> page ")); Serial.println(page);
  }
  absDisplay::update(d, page);
  server.handleClient();
  yield();
}
