#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "AbsDisplay.h"
#include "DashboardPage.h"

// ABS tester: ESP32 + 2.8" ILI9341 (Adafruit libraries), fed by the Arduino
// Nano running arduino-nano/NanoAbsTester. The Nano sends "A1" frames:
//   key, brake, D7 rear frequency x100, rear speed x100, REF1 / REF2 mV,
//   current mA, battery mV, D8 front frequency x100, front speed x100, RPM,
//   TCS (1 steady, 0 blinking) and ABS (1 active). Older Nano firmware stops
//   after the battery or the front speed.
// Speed 1 (rear), speed 2 (front), RPM, TCS and ABS are shown exactly as the
// Nano works them out (speed = Hz / 6 km/h, RPM = D7 Hz x 15000 / 1560).
// Nano D5 (SoftwareSerial TX) -> ESP32 RX2 (GPIO16), common GND,
// 115200 baud 8N1 on hardware UART2. Only lines starting with "A1," are used.
// Wi-Fi hotspot with the dashboard at http://192.168.4.1/ and the readings
// as JSON at /api/live.
const char* const AP_SSID = "ABS-Tester";
const char* const AP_PASSWORD = "ABSdemo2026";  // At least 8 characters.
constexpr uint8_t LINK_RX = 16, LINK_TX = 17;  // RX2, TX2 (TX unused).
HardwareSerial& nano = Serial2;
WebServer server(80);
char frame[80], lastBadFrame[80] = "";
size_t frameLength = 0;
bool discardFrame = false;
bool havePacket = false;
uint32_t lastPacketMs = 0, badFrames = 0, lastSerialReportMs = 0, bytesReceived = 0;
AbsData latest{};

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
  // 8 fields (oldest Nano), 10 (+ front sensor) or 13 (+ RPM, TCS, ABS).
  constexpr uint8_t MIN_FIELDS = 8, FRONT_FIELDS = 10, MAX_FIELDS = 13;
  constexpr unsigned long maximum[MAX_FIELDS] = {1, 1, 100000000UL, 25000, 5500, 5500, 10500, 5500, 100000000UL, 25000,
                                                 15000, 1, 1};
  unsigned long values[MAX_FIELDS];
  uint8_t parsed = 0;
  char* p = text + 3;
  for (uint8_t i = 0; i < MAX_FIELDS; ++i) {
    if (*p < '0' || *p > '9') return false;
    values[i] = strtoul(p, &end, 10);
    if (values[i] > maximum[i]) return false;
    parsed = i + 1;
    if (!*end && (parsed == MIN_FIELDS || parsed == FRONT_FIELDS || parsed == MAX_FIELDS)) break;
    if (*end != ',' || parsed == MAX_FIELDS) return false;
    p = end + 1;
  }
  latest.keyOn = values[0] == 1;
  latest.brake = values[1] == 1;
  latest.frequencyHz = values[2] / 100.0f;
  latest.speedKmh = values[3] / 100.0f;
  latest.ref1V = values[4] / 1000.0f;
  latest.ref2V = values[5] / 1000.0f;
  latest.currentA = values[6] / 1000.0f;
  latest.batteryV = values[7] / 1000.0f;
  latest.front = parsed >= FRONT_FIELDS;
  latest.frontHz = latest.front ? values[8] / 100.0f : 0;
  latest.frontKmh = latest.front ? values[9] / 100.0f : 0;
  latest.nanoLamps = parsed == MAX_FIELDS;
  latest.rpm = latest.nanoLamps ? values[10] : 0;
  latest.tcsSteady = latest.nanoLamps && values[11] == 1;
  latest.absActive = latest.nanoLamps && values[12] == 1;
  lastPacketMs = millis();
  havePacket = true;
  return true;
}

// Same values the LCD shows; DECLOG (0-100 %) follows the D7 frequency.
String liveJson() {
  const bool fresh = havePacket && uint32_t(millis() - lastPacketMs) <= 3000;
  const float percent = constrain(latest.frequencyHz / FREQUENCY_MAX_HZ * 100, 0.0f, 100.0f);
  String json;
  json.reserve(400);
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
  json += F(",\"declog_percent\":"); json += String(percent, 1);
  json += F(",\"ref1_v\":"); json += String(latest.ref1V, 3);
  json += F(",\"ref2_v\":"); json += String(latest.ref2V, 3);
  json += F(",\"current_a\":"); json += String(latest.currentA, 3);
  json += F(",\"battery_v\":"); json += String(latest.batteryV, 3);
  // Warning lights, the same as the LCD: "off", "on", "flash" or (SENSOR) "ok".
  const Lamps lamps = warningLamps(latest);
  json += F(",\"abs_lamp\":\""); json += fresh ? lampName(lamps.abs) : "off";
  json += F("\",\"tcs_lamp\":\""); json += fresh ? lampName(lamps.tcs) : "off";
  json += F("\",\"sensor_lamp\":\""); json += fresh ? lampName(lamps.sensor) : "off";
  json += '"';
  json += F(",\"bad_frames\":"); json += badFrames;
  json += F(",\"uptime_ms\":"); json += millis();
  json += '}';
  return json;
}

void setup() {
  Serial.begin(115200);
  absDisplay::begin();
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
      Serial.print(F(" | TCS ")); Serial.print(latest.tcsSteady ? F("ON") : F("BLINKING"));
      Serial.print(F(" | ABS ")); Serial.print(warningLamps(latest).abs == Lamp::ON ? F("ACTIVE") : F("OFF"));
      Serial.print(F(" | SPEED 2 D8 "));
      if (latest.front) {
        Serial.print(latest.frontHz, 1); Serial.print(F(" Hz = "));
        Serial.print(latest.frontKmh, 1); Serial.print(F(" km/h"));
      } else Serial.print(F("n/a (old Nano firmware)"));
      Serial.print(F(" | REF1 ")); Serial.print(latest.ref1V, 2);
      Serial.print(F(" V | BATTERY ")); Serial.print(latest.batteryV, 2);
      Serial.print(F(" V | bad frames ")); Serial.println(badFrames);
    } else {
      Serial.print(F("# NO DATA from the Nano: ")); Serial.print(bytesReceived);
      Serial.print(F(" bytes received on GPIO16, bad frames ")); Serial.print(badFrames);
      if (lastBadFrame[0]) { Serial.print(F(" | last line: ")); Serial.print(lastBadFrame); }
      Serial.println(bytesReceived ? F("") : F(" | check Nano D5 -> GPIO16 and GND"));
    }
  }
  AbsData d = latest;
  const bool fresh = havePacket && uint32_t(millis() - lastPacketMs) <= 3000;
  d.link = !havePacket ? AbsData::WAITING : !fresh ? AbsData::STALE : AbsData::LIVE;
  absDisplay::update(d);
  server.handleClient();
  yield();
}
