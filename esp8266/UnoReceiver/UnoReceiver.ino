#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "SensorConversion.h"
#include "DashboardAssets.h"

// NodeMCU D6 = GPIO12, receiving from level-shifted Uno D6.
// RX only: NodeMCU D5 remains unused/input, not a second transmitter.
SoftwareSerial unoSerial;
ESP8266WebServer server(80);
const char* const names[] = {"IAT", "ECT", "O2", "MAP", "TPS"};
const float zeroRaw[] = {708.3f, 747.2f, 0, 78.5f, 76};
const float maxRaw[] = {343.4f, 61, 257.8f, 889, 893};
char frame[96];
size_t frameLength = 0;
bool discardFrame = false;
bool havePacket = false;
uint32_t lastPacketMs = 0;
uint32_t badFrames = 0;
float rawValues[5] = {};
float frequencyHz = 0;
bool smartKeyOn = false, brakePressed = false;
float mappedRpm = 0;
uint32_t sampleSequence = 0;
uint32_t lastSerialReportMs = 0;
uint8_t injectedFault = 0;
float faultLimitedSpeed = 0;
uint32_t lastSpeedMs = 0;

bool parseFrame(char* text) {
  char* star = strchr(text, '*');
  if (!star || strlen(star + 1) != 2) return false;
  uint8_t checksum = 0;
  for (char* p = text; p < star; ++p) checksum ^= uint8_t(*p);
  char* end = nullptr;
  const unsigned long expected = strtoul(star + 1, &end, 16);
  if (*end || expected != checksum) return false;
  *star = '\0';
  if (strncmp(text, "U3,", 3) != 0) return false;
  unsigned long values[9];
  char* p = text + 3;
  for (uint8_t i = 0; i < 9; ++i) {
    if (*p < '0' || *p > '9') return false;
    values[i] = strtoul(p, &end, 10);
    if (i < 8) {
      const unsigned long maximum = i < 2 ? 1UL : i < 7 ? 16368UL : 100000000UL;
      if (values[i] > maximum || *end != ',') return false;
      p = end + 1;
    } else if (*end || values[i] > 30000UL) return false;
  }
  smartKeyOn = values[0] == 1;
  brakePressed = values[1] == 1;
  for (uint8_t i = 0; i < 5; ++i) rawValues[i] = smartKeyOn ? values[i + 2] / 16.0f : 0;
  frequencyHz = smartKeyOn ? values[7] / 100.0f : 0;
  mappedRpm = smartKeyOn ? values[8] / 100.0f : 0;
  lastPacketMs = millis();
  havePacket = true;
  if (!smartKeyOn) injectedFault = 0;
  ++sampleSequence;
  return true;
}

String telemetryJson() {
  const bool stale = !havePacket || uint32_t(millis() - lastPacketMs) > 3000;
  String json;
  json.reserve(1800);
  json = F("{\"source\":\"uno_via_esp8266\",\"stale\":");
  json += stale ? F("true") : F("false");
  json += F(",\"bad_frames\":"); json += badFrames;
  if (!havePacket) { json += F(",\"sensors\":null}"); return json; }
  json += F(",\"sample_age_ms\":"); json += uint32_t(millis() - lastPacketMs);
  json += F(",\"sample_seq\":"); json += sampleSequence;
  json += F(",\"smart_key\":"); json += smartKeyOn ? F("true") : F("false");
  json += F(",\"acquisition_enabled\":"); json += smartKeyOn ? F("true") : F("false");
  json += F(",\"brake_pressed\":"); json += brakePressed ? F("true") : F("false");
  json += F(",\"check_engine\":"); json += !stale && smartKeyOn && injectedFault ? F("true") : F("false");
  json += F(",\"fault_mask\":"); json += !stale && smartKeyOn ? injectedFault : 0;
  json += F(",\"injected_fault_mask\":"); json += !stale && smartKeyOn ? injectedFault : 0;
  json += F(",\"simulated_speed_kmh\":"); json += String(faultLimitedSpeed, 1);
  if (!smartKeyOn) { json += F(",\"sensors\":null,\"frequency_hz\":0,\"engine_rpm\":0}"); return json; }
  json += F(",\"adc_reference_v\":5.000,\"voltage_reference\":\"nominal_unverified\",\"sensors\":{");
  for (uint8_t i = 0; i < 5; ++i) {
    if (i) json += ',';
    const float voltage = rawValues[i] * 5.0f / 1023;
    const float pos = (rawValues[i] - zeroRaw[i]) * 100 / (maxRaw[i] - zeroRaw[i]);
    const bool valid = pos >= -1 && pos <= 101;
    json += '"'; json += names[i]; json += F("\":{\"raw\":"); json += String(rawValues[i], 2);
    json += F(",\"voltage_v\":"); json += String(voltage, 3);
    json += F(",\"knob_percent\":"); json += valid ? String(constrain(pos, 0.0f, 100.0f), 1) : String(F("null"));
    json += F(",\"endpoint_status\":\""); json += valid ? F("in_range") : F("out_of_range"); json += '"';
    if (i == 4) {
      json += F(",\"throttle_percent\":"); json += valid ? String(constrain(pos, 0.0f, 100.0f), 1) : String(F("null"));
      json += F(",\"calibration\":\"measured_knob_endpoints\"");
    } else if (i == 2) json += F(",\"afr\":null,\"calibration\":\"voltage_only\"");
    else {
      const SensorConversion estimate = convertSensor(i, voltage);
      json += i == 3 ? F(",\"pressure_kpa\":null,\"estimate_kpa\":") : F(",\"temperature_c\":null,\"estimate_c\":");
      json += estimate.available ? String(estimate.value, 1) : String(F("null"));
      json += F(",\"calibration\":\"provisional_document_linear_curve\"");
    }
    json += '}';
  }
  json += F("},\"frequency_hz\":"); json += String(frequencyHz, 2);
  json += F(",\"engine_rpm\":"); json += String(mappedRpm, 1);
  json += F(",\"rpm_calibration\":\"mapped_0_1639_hz_to_0_300\"}");
  return json;
}

void setup() {
  Serial.begin(115200);
  pinMode(D5, INPUT);
  unoSerial.begin(19200, SWSERIAL_8N1, D6, -1, false, 128);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ECU-Testbench-8266", "ECUdemo2026");
  server.on("/api/simulate-fault", HTTP_POST, []() {
    server.sendHeader("Cache-Control", "no-store");
    const String sensor = server.arg("sensor");
    if (sensor == "clear") injectedFault = 0;
    else {
      if (!havePacket || !smartKeyOn || uint32_t(millis()-lastPacketMs)>3000) {
        server.send(409,"application/json","{\"error\":\"Enable smart key and wait for live data\"}"); return;
      }
      int channel = -1;
      for (uint8_t i=0;i<5;++i) if (sensor == names[i]) channel = i;
      if (channel < 0) { server.send(400,"application/json","{\"error\":\"Invalid sensor\"}"); return; }
      injectedFault = 1 << channel;
    }
    server.send(200,"application/json",telemetryJson());
  });
  server.on("/api/live", []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", telemetryJson());
  });
  server.on("/api/health", []() {
    String json = F("{\"uptime_ms\":"); json += millis();
    json += F(",\"free_heap\":"); json += ESP.getFreeHeap();
    json += F(",\"reset_reason\":\""); json += ESP.getResetReason();
    json += F("\",\"bad_frames\":"); json += badFrames; json += '}';
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
  });
  server.on("/", []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(dashboardHtml), sizeof(dashboardHtml));
  });
  server.on("/style.css", []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/css; charset=utf-8", reinterpret_cast<const char*>(dashboardCss), sizeof(dashboardCss));
  });
  server.on("/app.js", []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "application/javascript; charset=utf-8", reinterpret_cast<const char*>(dashboardJs), sizeof(dashboardJs));
  });
  server.on("/report.js", []() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "application/javascript; charset=utf-8", reinterpret_cast<const char*>(dashboardReport), sizeof(dashboardReport));
  });
  server.begin();
  Serial.println(F("Live dashboard ready. NodeMCU D6 RX, 19200 baud. D5 unused."));
  Serial.print(F("Wi-Fi: ECU-Testbench-8266 | API: http://"));
  Serial.print(WiFi.softAPIP()); Serial.println(F("/api/live"));
}

void loop() {
  while (unoSerial.available()) {
    const char c = unoSerial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      frame[frameLength] = '\0';
      if (!discardFrame && frameLength && parseFrame(frame)) {
        // Drain every input packet, but don't let USB logging at 5 Hz
        // monopolize the CPU and delay HTTP responses or overflow RX.
        if (uint32_t(millis() - lastSerialReportMs) < 1000) {
          frameLength = 0; discardFrame = false; continue;
        }
        lastSerialReportMs = millis();
        Serial.println(telemetryJson());
        Serial.print(F("# SMART KEY: ")); Serial.print(smartKeyOn ? F("ON") : F("OFF - acquisition locked"));
        Serial.print(F(" | BRAKE: ")); Serial.println(brakePressed ? F("PRESSED") : F("RELEASED"));
        if (!smartKeyOn) { frameLength = 0; discardFrame = false; continue; }
        for (uint8_t i = 0; i < 5; ++i) {
          Serial.print(F("# ")); Serial.print(names[i]);
          Serial.print(F(" raw=")); Serial.print(rawValues[i], 2);
          Serial.print(F(" voltage=")); Serial.print(rawValues[i] * 5 / 1023, 3); Serial.println(F(" V"));
        }
        Serial.print(F("# D7 frequency=")); Serial.print(frequencyHz, 2);
        Serial.print(F(" Hz | mapped RPM=")); Serial.println(mappedRpm, 1);
      } else if (discardFrame || frameLength) ++badFrames;
      frameLength = 0; discardFrame = false;
    } else if (!discardFrame) {
      if (frameLength < sizeof(frame) - 1) frame[frameLength++] = c;
      else discardFrame = true;
    }
  }
  const uint32_t now = millis();
  const bool fresh = havePacket && uint32_t(now-lastPacketMs) <= 3000;
  if (!fresh || !smartKeyOn) { faultLimitedSpeed = 0; injectedFault = 0; }
  else {
    const float target = injectedFault ? 0 : mappedRpm;
    const float step = 100.0f * uint32_t(now-lastSpeedMs)/1000.0f;
    if (faultLimitedSpeed < target) faultLimitedSpeed = min(target, faultLimitedSpeed+step);
    else faultLimitedSpeed = max(target, faultLimitedSpeed-step);
  }
  lastSpeedMs = now;
  server.handleClient();
  yield();
}
