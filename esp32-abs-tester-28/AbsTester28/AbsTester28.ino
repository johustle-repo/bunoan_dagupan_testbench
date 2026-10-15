#include <Arduino.h>
#include <SoftwareSerial.h>
#include <WiFi.h>
#include <WebServer.h>
#include "AbsDisplay.h"
#include "DashboardPage.h"

// ABS tester: ESP32 + 2.8" ILI9341 (Adafruit libraries), fed by the Arduino
// Nano running arduino-nano/NanoAbsTester. The Nano sends "A1" frames:
//   key, brake, D7 frequency x100, speed x100, REF1 / REF2 mV, current mA, battery mV
// Nano D6 -> 10k/20k divider -> ESP32 GPIO17, Nano D5 - GPIO16 (unused),
// common GND, 19200 baud 8N1.
// Wi-Fi hotspot with the dashboard at http://192.168.4.1/ and the readings
// as JSON at /api/live.
const char* const AP_SSID = "ABS-Tester";
const char* const AP_PASSWORD = "ABSdemo2026";  // At least 8 characters.
SoftwareSerial nano(17, 16);  // RX, TX (TX unused).
WebServer server(80);
char frame[80];
size_t frameLength = 0;
bool discardFrame = false;
bool havePacket = false;
uint32_t lastPacketMs = 0, badFrames = 0, lastSerialReportMs = 0;
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
  constexpr uint8_t FIELDS = 8;
  constexpr unsigned long maximum[FIELDS] = {1, 1, 100000000UL, 20000, 5500, 5500, 10500, 5500};
  unsigned long values[FIELDS];
  char* p = text + 3;
  for (uint8_t i = 0; i < FIELDS; ++i) {
    if (*p < '0' || *p > '9') return false;
    values[i] = strtoul(p, &end, 10);
    if (values[i] > maximum[i]) return false;
    if (i < FIELDS - 1) {
      if (*end != ',') return false;
      p = end + 1;
    } else if (*end) return false;
  }
  latest.keyOn = values[0] == 1;
  latest.brake = values[1] == 1;
  latest.frequencyHz = values[2] / 100.0f;
  latest.speedKmh = values[3] / 100.0f;
  latest.ref1V = values[4] / 1000.0f;
  latest.ref2V = values[5] / 1000.0f;
  latest.currentA = values[6] / 1000.0f;
  latest.batteryV = values[7] / 1000.0f;
  lastPacketMs = millis();
  havePacket = true;
  return true;
}

// Same values the LCD shows; DC LOG (0-100 %) and RPM (0-15,000) follow the D7 frequency.
String liveJson() {
  const bool fresh = havePacket && uint32_t(millis() - lastPacketMs) <= 3000;
  const float percent = constrain(latest.frequencyHz / FREQUENCY_MAX_HZ * 100, 0.0f, 100.0f);
  String json;
  json.reserve(320);
  json = F("{\"link\":\"");
  json += !havePacket ? F("waiting") : !fresh ? F("stale") : F("live");
  json += F("\",\"key\":"); json += latest.keyOn ? F("true") : F("false");
  json += F(",\"brake\":"); json += latest.brake ? F("true") : F("false");
  json += F(",\"speed_kmh\":"); json += String(latest.speedKmh, 1);
  json += F(",\"frequency_hz\":"); json += String(latest.frequencyHz, 1);
  json += F(",\"dc_log_percent\":"); json += String(percent, 1);
  json += F(",\"rpm\":"); json += String(percent / 100 * RPM_MAX, 0);
  json += F(",\"ref1_v\":"); json += String(latest.ref1V, 3);
  json += F(",\"ref2_v\":"); json += String(latest.ref2V, 3);
  json += F(",\"current_a\":"); json += String(latest.currentA, 3);
  json += F(",\"battery_v\":"); json += String(latest.batteryV, 3);
  json += F(",\"bad_frames\":"); json += badFrames;
  json += F(",\"uptime_ms\":"); json += millis();
  json += '}';
  return json;
}

void setup() {
  Serial.begin(115200);
  absDisplay::begin();
  nano.begin(19200, SWSERIAL_8N1, 17, 16, false, 256);
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
  Serial.println(F("ABS tester (2.8\") ready. Nano link on GPIO17, 19200 baud."));
  Serial.print(F("Wi-Fi: ")); Serial.print(AP_SSID); Serial.print(F(" / ")); Serial.print(AP_PASSWORD);
  Serial.print(F(" | dashboard: http://")); Serial.print(WiFi.softAPIP()); Serial.println('/');
}

void loop() {
  while (nano.available()) {
    const char c = nano.read();
    if (c == '\r') continue;
    if (c == '\n') {
      frame[frameLength] = '\0';
      if (!discardFrame && frameLength && parseFrame(frame)) {
        if (uint32_t(millis() - lastSerialReportMs) >= 1000) {
          lastSerialReportMs = millis();
          Serial.print(F("# KEY ")); Serial.print(latest.keyOn ? F("ON") : F("OFF"));
          Serial.print(F(" | BRAKE ")); Serial.print(latest.brake ? F("ON") : F("OFF"));
          Serial.print(F(" | SPEED ")); Serial.print(latest.speedKmh, 1);
          Serial.print(F(" km/h | REF1 ")); Serial.print(latest.ref1V, 2);
          Serial.print(F(" V | REF2 ")); Serial.print(latest.ref2V, 2);
          Serial.print(F(" V | CURRENT ")); Serial.print(latest.currentA, 2);
          Serial.print(F(" A | BATTERY ")); Serial.print(latest.batteryV, 2);
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
  AbsData d = latest;
  const bool fresh = havePacket && uint32_t(millis() - lastPacketMs) <= 3000;
  d.link = !havePacket ? AbsData::WAITING : !fresh ? AbsData::STALE : AbsData::LIVE;
  absDisplay::update(d);
  server.handleClient();
  yield();
}
