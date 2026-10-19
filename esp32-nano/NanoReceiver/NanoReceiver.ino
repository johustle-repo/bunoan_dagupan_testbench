#include <Arduino.h>
#include <esp_system.h>
#include <SoftwareSerial.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include "SensorConversion.h"
#include "DashboardAssets.h"
#include "LocalDisplay.h"
#include "BatteryMonitor.h"

// ESP32 GPIO17 RX receives the Nano D6 signal at 3.3 V logic.
// One-way link: no ESP32 TX connection is required.
SoftwareSerial nodemcu(17, 16); // Link from the Nano. // RX, TX on ESP32 Dev Module.
WebServer server(80);
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
float referenceVoltage[2] = {}; // Nano A5 (5 V) and A6 (3.3 V) supply references.
uint32_t sampleSequence = 0;
uint32_t lastSerialReportMs = 0;
uint8_t injectedFault = 0;
float faultLimitedSpeed = 0;
uint32_t lastSpeedMs = 0;

// Simulated engine RPM: the D7 speed is run through a 6-speed gearbox model.
// Ratios are engine rpm per km/h; 6th gear gives about 14,400 rpm at 300 km/h.
constexpr float GEAR_RPM_PER_KMH[6] = {120, 90, 73, 62, 54, 48};
constexpr float IDLE_RPM = 1300, RPM_REDLINE = 15000;
uint8_t gear = 0; // 0 = neutral.
float simulatedRpm = 0;

// TPS closed / wide-open endpoints, learned from the dashboard and kept in NVS.
constexpr float TPS_DEFAULT_CLOSED_RAW = 76, TPS_DEFAULT_WOT_RAW = 893;
constexpr float TPS_MIN_SPAN_RAW = 100; // About 0.5 V between endpoints.
Preferences settings;
float tpsClosedRaw = TPS_DEFAULT_CLOSED_RAW, tpsWotRaw = TPS_DEFAULT_WOT_RAW;

// Automatic circuit-fault DTCs. A condition must persist for
// DTC_CONFIRM_MS before the check-engine lamp turns on, as in an ECU.
constexpr uint32_t DTC_CONFIRM_MS = 2000;
uint8_t pendingFaultMask = 0, autoFaultMask = 0, autoFaultHighMask = 0;
uint32_t faultSinceMs[5] = {};
bool coolantWarning = false;

bool parseFrame(char* text) {
  char* star = strchr(text, '*');
  if (!star || strlen(star + 1) != 2) return false;
  uint8_t checksum = 0;
  for (char* p = text; p < star; ++p) checksum ^= uint8_t(*p);
  char* end = nullptr;
  const unsigned long expected = strtoul(star + 1, &end, 16);
  if (*end || expected != checksum) return false;
  *star = '\0';
  if (strncmp(text, "U4,", 3) != 0) return false;
  // key, brake, 5 x ADC (1/16 counts), Hz x100, RPM x100, Reference 1 and 2 in mV.
  constexpr uint8_t FIELD_COUNT = 11;
  constexpr unsigned long maximum[FIELD_COUNT] = {
      1, 1, 16368, 16368, 16368, 16368, 16368, 100000000UL, 30000, 6000, 6000};
  unsigned long values[FIELD_COUNT];
  char* p = text + 3;
  for (uint8_t i = 0; i < FIELD_COUNT; ++i) {
    if (*p < '0' || *p > '9') return false;
    values[i] = strtoul(p, &end, 10);
    if (values[i] > maximum[i]) return false;
    if (i < FIELD_COUNT - 1) {
      if (*end != ',') return false;
      p = end + 1;
    } else if (*end) {
      // Newer Nano firmware appends the D8 front wheel Hz x100; not used here.
      if (*end != ',' || end[1] < '0' || end[1] > '9') return false;
      strtoul(end + 1, &end, 10);
      if (*end) return false;
    }
  }
  smartKeyOn = values[0] == 1;
  brakePressed = values[1] == 1;
  for (uint8_t i = 0; i < 5; ++i) rawValues[i] = smartKeyOn ? values[i + 2] / 16.0f : 0;
  frequencyHz = smartKeyOn ? values[7] / 100.0f : 0;
  mappedRpm = smartKeyOn ? values[8] / 100.0f : 0;
  for (uint8_t i = 0; i < 2; ++i) referenceVoltage[i] = smartKeyOn ? values[i + 9] / 1000.0f : 0;
  lastPacketMs = millis();
  havePacket = true;
  if (!smartKeyOn) injectedFault = 0;
  ++sampleSequence;
  return true;
}

inline float channelVoltage(uint8_t i) { return rawValues[i] * 5.0f / 1023; }

void updateDiagnostics() {
  if (!smartKeyOn) {
    pendingFaultMask = autoFaultMask = 0;
    coolantWarning = false;
    return;
  }
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 5; ++i) {
    if (i == 2) continue; // O2 can legitimately sit at 0 V; injected faults only.
    const uint8_t bit = 1 << i;
    const float voltage = channelVoltage(i);
    if (circuitOk(voltage)) {
      pendingFaultMask &= ~bit;
      autoFaultMask &= ~bit;
      continue;
    }
    if (voltage > CIRCUIT_HIGH_V) autoFaultHighMask |= bit;
    else autoFaultHighMask &= ~bit;
    if (!(pendingFaultMask & bit)) {
      pendingFaultMask |= bit;
      faultSinceMs[i] = now;
    } else if (uint32_t(now - faultSinceMs[i]) >= DTC_CONFIRM_MS) {
      autoFaultMask |= bit;
    }
  }
  const SensorReading ect = convertTemperature(ECT_MODEL, channelVoltage(1));
  if (!ect.available) coolantWarning = false;
  else if (ect.value >= COOLANT_WARNING_ON_C) coolantWarning = true;
  else if (ect.value <= COOLANT_WARNING_OFF_C) coolantWarning = false;
}

// Shift points follow throttle like a rider: light throttle shifts up near
// 6000 rpm, wide open near 12,000 rpm. The hysteresis keeps a gear change
// from immediately reversing.
void updateGearbox(bool fresh) {
  if (!fresh || !smartKeyOn) { gear = 0; simulatedRpm = 0; return; }
  if (faultLimitedSpeed < 1.0f) { gear = 0; simulatedRpm = IDLE_RPM; return; }
  const SensorReading tps = convertTps(rawValues[4], tpsClosedRaw, tpsWotRaw, channelVoltage(4));
  const float throttle = tps.available ? tps.value : 0;
  const float upshiftRpm = 6000 + 60 * throttle, downshiftRpm = 3000 + 25 * throttle;
  if (gear == 0) gear = 1;
  while (gear < 6 && faultLimitedSpeed * GEAR_RPM_PER_KMH[gear - 1] > upshiftRpm) ++gear;
  while (gear > 1 && faultLimitedSpeed * GEAR_RPM_PER_KMH[gear - 1] < downshiftRpm) --gear;
  simulatedRpm = constrain(faultLimitedSpeed * GEAR_RPM_PER_KMH[gear - 1], IDLE_RPM, RPM_REDLINE);
}

void appendDtcList(String& json, uint8_t autoMask, uint8_t injectedMask) {
  json += F(",\"dtc\":[");
  bool first = true;
  for (uint8_t i = 0; i < 5; ++i) {
    const uint8_t bit = 1 << i;
    if (!((autoMask | injectedMask) & bit)) continue;
    // An injected demo fault takes precedence over a detected circuit fault.
    const bool injected = injectedMask & bit;
    const bool high = autoFaultHighMask & bit;
    if (!first) json += ',';
    first = false;
    json += F("{\"code\":\"");
    json += injected ? SENSOR_DTCS[i].general : high ? SENSOR_DTCS[i].high : SENSOR_DTCS[i].low;
    json += F("\",\"sensor\":\""); json += names[i];
    json += F("\",\"type\":\"");
    json += injected ? F("injected") : high ? F("circuit_high") : F("circuit_low");
    json += F("\",\"blink\":"); json += SENSOR_DTCS[i].blink;
    json += '}';
  }
  json += ']';
}

// Feeds the local TFT the same values /api/live reports.
void updateLocalDisplay() {
  DisplayData d{};
  const bool stale = !havePacket || uint32_t(millis() - lastPacketMs) > 3000;
  d.link = !havePacket ? DisplayData::WAITING : stale ? DisplayData::STALE : DisplayData::LIVE;
  d.keyOn = smartKeyOn;
  d.brake = brakePressed;
  const uint8_t activeInjected = !stale && smartKeyOn ? injectedFault : 0;
  const uint8_t activeAuto = !stale && smartKeyOn ? autoFaultMask : 0;
  d.checkEngine = activeInjected | activeAuto;
  d.coolantWarning = !stale && coolantWarning;
  d.speedKmh = faultLimitedSpeed;
  d.rpm = simulatedRpm;
  for (uint8_t i = 0; i < 5 && !d.dtcCode; ++i) {
    const uint8_t bit = 1 << i;
    if (!((activeAuto | activeInjected) & bit)) continue;
    d.dtcCode = activeInjected & bit ? SENSOR_DTCS[i].general
              : autoFaultHighMask & bit ? SENSOR_DTCS[i].high : SENSOR_DTCS[i].low;
    d.dtcSensor = names[i];
    d.milBlink = SENSOR_DTCS[i].blink;
  }
  d.iat = convertTemperature(IAT_MODEL, channelVoltage(0));
  d.ect = convertTemperature(ECT_MODEL, channelVoltage(1));
  d.o2 = convertOxygen(channelVoltage(2));
  d.map = convertMap(channelVoltage(3));
  d.tps = convertTps(rawValues[4], tpsClosedRaw, tpsWotRaw, channelVoltage(4));
  d.frequencyHz = frequencyHz;
  d.batteryAvailable = battery::latest.available;
  d.batteryV = battery::latest.voltage;
  localDisplay::update(d);
}

String telemetryJson() {
  const bool stale = !havePacket || uint32_t(millis() - lastPacketMs) > 3000;
  String json;
  json.reserve(2400);
  json = F("{\"source\":\"nano_via_esp32\",\"stale\":");
  json += stale ? F("true") : F("false");
  json += F(",\"bad_frames\":"); json += badFrames;
  if (!havePacket) { json += F(",\"sensors\":null}"); return json; }
  json += F(",\"sample_age_ms\":"); json += uint32_t(millis() - lastPacketMs);
  json += F(",\"sample_seq\":"); json += sampleSequence;
  json += F(",\"smart_key\":"); json += smartKeyOn ? F("true") : F("false");
  json += F(",\"acquisition_enabled\":"); json += smartKeyOn ? F("true") : F("false");
  json += F(",\"brake_pressed\":"); json += brakePressed ? F("true") : F("false");
  const uint8_t activeInjected = !stale && smartKeyOn ? injectedFault : 0;
  const uint8_t activeAuto = !stale && smartKeyOn ? autoFaultMask : 0;
  json += F(",\"check_engine\":"); json += activeInjected | activeAuto ? F("true") : F("false");
  json += F(",\"fault_mask\":"); json += activeInjected | activeAuto;
  json += F(",\"injected_fault_mask\":"); json += activeInjected;
  appendDtcList(json, activeAuto, activeInjected);
  json += F(",\"coolant_warning\":"); json += !stale && coolantWarning ? F("true") : F("false");
  json += F(",\"tps_closed_raw\":"); json += String(tpsClosedRaw, 1);
  json += F(",\"tps_wot_raw\":"); json += String(tpsWotRaw, 1);
  json += F(",\"simulated_speed_kmh\":"); json += String(faultLimitedSpeed, 1);
  json += F(",\"simulated_rpm\":"); json += String(simulatedRpm, 0);
  json += F(",\"gear\":"); json += gear;
  if (battery::latest.available) {
    json += F(",\"battery_v\":"); json += String(battery::latest.voltage, 2);
    json += F(",\"battery_a\":"); json += String(battery::latest.currentA, 3);
  } else json += F(",\"battery_v\":null,\"battery_a\":null");
  if (!smartKeyOn) { json += F(",\"sensors\":null,\"frequency_hz\":0,\"engine_rpm\":0}"); return json; }
  json += F(",\"reference_1_v\":"); json += String(referenceVoltage[0], 3);
  json += F(",\"reference_2_v\":"); json += String(referenceVoltage[1], 3);
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
      const SensorReading tps = convertTps(rawValues[i], tpsClosedRaw, tpsWotRaw, voltage);
      json += F(",\"throttle_percent\":"); json += tps.available ? String(tps.value, 1) : String(F("null"));
      json += F(",\"in_spec\":"); json += tps.inSpec ? F("true") : F("false");
      json += F(",\"calibration\":\"learned_endpoints\"");
    } else if (i == 2) {
      const OxygenReading o2 = convertOxygen(voltage);
      json += F(",\"lambda\":"); json += String(o2.lambda, 3);
      json += F(",\"afr\":"); json += o2.afrAvailable ? String(o2.afr, 1) : String(F("null"));
      json += F(",\"mixture\":\""); json += o2.mixture;
      json += F("\",\"in_spec\":"); json += o2.inSpec ? F("true") : F("false");
      json += F(",\"calibration\":\"client_lambda_encoding\"");
    } else {
      const SensorReading reading = i == 3 ? convertMap(voltage)
                                           : convertTemperature(i == 0 ? IAT_MODEL : ECT_MODEL, voltage);
      json += i == 3 ? F(",\"estimate_kpa\":") : F(",\"estimate_c\":");
      json += reading.available ? String(reading.value, 1) : String(F("null"));
      json += F(",\"in_spec\":"); json += reading.inSpec ? F("true") : F("false");
      json += i == 3 ? F(",\"calibration\":\"client_spec_linear\"") : F(",\"calibration\":\"client_spec_ntc_model\"");
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


  localDisplay::begin();
  battery::begin();
  nodemcu.begin(19200, SWSERIAL_8N1, 17, 16, false, 256);
  settings.begin("ecu-testbench", false);
  tpsClosedRaw = settings.getFloat("tpsClosed", TPS_DEFAULT_CLOSED_RAW);
  tpsWotRaw = settings.getFloat("tpsWot", TPS_DEFAULT_WOT_RAW);
  if (!(fabsf(tpsWotRaw - tpsClosedRaw) >= TPS_MIN_SPAN_RAW)) {
    tpsClosedRaw = TPS_DEFAULT_CLOSED_RAW;
    tpsWotRaw = TPS_DEFAULT_WOT_RAW;
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ECU-Testbench-Nano", "ECUdemo2026");
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
  server.on("/api/tps-learn", HTTP_POST, []() {
    server.sendHeader("Cache-Control", "no-store");
    const String action = server.arg("action");
    if (action == "reset") {
      tpsClosedRaw = TPS_DEFAULT_CLOSED_RAW;
      tpsWotRaw = TPS_DEFAULT_WOT_RAW;
    } else if (action == "closed" || action == "wot") {
      if (!havePacket || !smartKeyOn || uint32_t(millis()-lastPacketMs)>3000) {
        server.send(409,"application/json","{\"error\":\"Enable smart key and wait for live data\"}"); return;
      }
      if (!circuitOk(channelVoltage(4))) {
        server.send(409,"application/json","{\"error\":\"TPS circuit voltage is out of range\"}"); return;
      }
      const float raw = rawValues[4];
      const bool closed = action == "closed";
      if (fabsf(raw - (closed ? tpsWotRaw : tpsClosedRaw)) < TPS_MIN_SPAN_RAW) {
        server.send(409,"application/json","{\"error\":\"Closed and wide-open positions must differ by at least 0.5 V\"}"); return;
      }
      (closed ? tpsClosedRaw : tpsWotRaw) = raw;
    } else {
      server.send(400,"application/json","{\"error\":\"Invalid action\"}"); return;
    }
    settings.putFloat("tpsClosed", tpsClosedRaw);
    settings.putFloat("tpsWot", tpsWotRaw);
    server.send(200, "application/json", telemetryJson());
  });
  server.on("/api/live", []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", telemetryJson());
  });
  server.on("/api/health", []() {
    String json = F("{\"uptime_ms\":"); json += millis();
    json += F(",\"free_heap\":"); json += ESP.getFreeHeap();
    json += F(",\"reset_reason\":\""); json += String(static_cast<int>(esp_reset_reason()));
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
  Serial.println(F("Live dashboard ready. ESP32 GPIO17 RX, 19200 baud."));
  Serial.print(F("Wi-Fi: ECU-Testbench-Nano | API: http://"));
  Serial.print(WiFi.softAPIP()); Serial.println(F("/api/live"));
}

void loop() {
  while (nodemcu.available()) {
    const char c = nodemcu.read();
    if (c == '\r') continue;
    if (c == '\n') {
      frame[frameLength] = '\0';
      if (!discardFrame && frameLength && parseFrame(frame)) {
        updateDiagnostics();
        // Drain every input packet, but don't let USB logging at 5 Hz
        // monopolize the CPU and delay HTTP responses or overflow RX.
        if (uint32_t(millis() - lastSerialReportMs) < 1000) {
          frameLength = 0; discardFrame = false; continue;
        }
        lastSerialReportMs = millis();
        Serial.println(telemetryJson());
        Serial.print(F("# SMART KEY: ")); Serial.print(smartKeyOn ? F("ON") : F("OFF - acquisition locked"));
        Serial.print(F(" | BRAKE: ")); Serial.println(brakePressed ? F("PRESSED") : F("RELEASED"));
        if (battery::latest.available) {
          Serial.print(F("# BATTERY ")); Serial.print(battery::latest.voltage, 2);
          Serial.print(F(" V | ")); Serial.print(battery::latest.currentA, 3); Serial.println(F(" A"));
        } else Serial.println(F("# BATTERY: INA219 not found (GPIO21 SDA, GPIO22 SCL, 0x40)"));
        if (!smartKeyOn) { frameLength = 0; discardFrame = false; continue; }
        for (uint8_t i = 0; i < 5; ++i) {
          Serial.print(F("# ")); Serial.print(names[i]);
          Serial.print(F(" raw=")); Serial.print(rawValues[i], 2);
          Serial.print(F(" voltage=")); Serial.print(rawValues[i] * 5 / 1023, 3); Serial.println(F(" V"));
        }
        Serial.print(F("# REF1 A5=")); Serial.print(referenceVoltage[0], 3);
        Serial.print(F(" V | REF2 A6=")); Serial.print(referenceVoltage[1], 3); Serial.println(F(" V"));
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
  updateGearbox(fresh);
  battery::update();
  updateLocalDisplay();
  server.handleClient();
  yield();
}
