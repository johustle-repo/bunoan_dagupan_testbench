#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "AbsDisplay.h"
#include "DashboardPage.h"
#include "FreqInput.h"
#include "Encoder.h"
#include "BrakeButton.h"

// ABS tester: ESP32 + 2.8" ILI9341 (Adafruit libraries), fed by the Arduino
// Nano running arduino-nano/NanoAbsTester (or the Mega 2560 Pro running
// arduino-mega/MegaAbsTester: same frames). It sends "A1" frames:
//   key, brake, D7 rear frequency x100, rear speed x100, REF1 / REF2 mV,
//   current mA, battery mV, D8 front frequency x100, front speed x100, RPM,
//   TCS (1 steady, 0 blinking), ABS (1 active), D2 Declog Hz x100 and the
//   rear / front / Declog pot generator settings in Hz x100 (17 fields: Nano),
//   plus the A5 page pot reading and page (19 fields: Mega). Older
//   firmware stops after the battery, the front speed, ABS or Declog. REF2 and
//   current are not wired (sent as 0) and are ignored.
// Speed 1 (rear), speed 2 (front), RPM, TCS and ABS are shown exactly as the
// Nano works them out (speed = Hz / 6 km/h, RPM = D7 Hz x 15000 / 1560).
// Mega TX1 (D18) or Nano D5 (SoftwareSerial TX) -> ESP32 RX2 (GPIO16), common GND,
// 115200 baud 8N1 on hardware UART2. Only lines starting with "A1," are used.
// Wi-Fi hotspot with the dashboard at http://192.168.4.1/ and the readings
// as JSON at /api/live.
// LCD pages: swipe left on the touchscreen for page 2, right for page 1 (REAR / FRONT speed);
// page 2 (the 0-2000 Hz GPIO34 speed sensor gauge and SPEED bar, FreqInput.h; page 3 the DECLOG gauge and bar).
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
uint8_t page = 1;  // LCD page, 1-3: the Mega's A5 page pot, or swipe left (next) / right (previous).
// Encoder page navigation state (see encoderNav()).
enum class Nav : uint8_t { FREE, SELECTING, LOCKED };
Nav nav = Nav::FREE;
uint8_t selPage = 1;  // Page the encoder points at while selecting.
uint32_t frameCount = 0;  // Good frames so far (the page pot is read once per frame).
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
  // 17 (+ the three generator settings), 18 (Nano with the speed encoder: + the wheel it adjusts,
  // 1 rear / 2 front), 19 (+ the older Mega's A5 page pot reading 0-1023 and page 1-3)
  // or 20 (Mega with the speed encoder: 0,0 for the page pot, then the wheel it adjusts).
  constexpr uint8_t MIN_FIELDS = 8, FRONT_FIELDS = 10, LAMP_FIELDS = 13, DECLOG_FIELDS = 14, GEN_FIELDS = 17,
                    NANO_ENC_FIELDS = 18, POT_FIELDS = 19, MAX_FIELDS = 20;
  // REF1 and battery: up to 30 V (0-30 V dividers, as on the ECU testbench).
  constexpr unsigned long maximum[MAX_FIELDS] = {1, 1, 100000000UL, 25000, 31000, 5500, 10500, 31000, 100000000UL, 25000,
                                                 15000, 1, 1, 100000000UL, 250000, 250000, 25000, 1023, 3, 2};
  unsigned long values[MAX_FIELDS];
  uint8_t parsed = 0;
  char* p = text + 3;
  for (uint8_t i = 0; i < MAX_FIELDS; ++i) {
    if (*p < '0' || *p > '9') return false;
    values[i] = strtoul(p, &end, 10);
    if (values[i] > maximum[i]) return false;
    parsed = i + 1;
    if (!*end && (parsed == MIN_FIELDS || parsed == FRONT_FIELDS || parsed == LAMP_FIELDS ||
                    parsed == DECLOG_FIELDS || parsed == GEN_FIELDS || parsed == NANO_ENC_FIELDS ||
                    parsed == POT_FIELDS || parsed == MAX_FIELDS)) break;
    if (*end != ',' || parsed == MAX_FIELDS) return false;
    p = end + 1;
  }
  if (parsed == NANO_ENC_FIELDS && values[17] > 2) return false;  // Nano's encoder wheel: 1 or 2.
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
  latest.sender = parsed >= POT_FIELDS ? AbsData::MEGA : AbsData::NANO;
  latest.gens = parsed >= GEN_FIELDS;
  for (uint8_t i = 0; i < 3; ++i) latest.genHz[i] = latest.gens ? values[14 + i] / 100.0f : 0;
  latest.pagePot = parsed >= POT_FIELDS && values[18] >= 1;  // The encoder Mega sends page 0: no pot.
  latest.speedSel = parsed == MAX_FIELDS ? values[19] : parsed == NANO_ENC_FIELDS ? values[17] : 0;
  latest.potRaw = latest.pagePot ? values[17] : 0;
  latest.potPage = latest.pagePot ? values[18] : 0;
  ++frameCount;
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
  json += F(",\"brake\":"); json += latest.brake || brakeButton::held ? F("true") : F("false");
  json += F(",\"brake_button\":"); json += brakeButton::held ? F("true") : F("false");
  json += F(",\"speed_kmh\":"); json += String(brakeButton::rearKmh(latest.speedKmh), 1);
  json += F(",\"frequency_hz\":"); json += String(latest.frequencyHz, 1);
  json += F(",\"front\":"); json += latest.front ? F("true") : F("false");
  json += F(",\"front_speed_kmh\":"); json += String(brakeButton::frontKmh(latest.frontKmh), 1);
  json += F(",\"front_frequency_hz\":"); json += String(latest.frontHz, 1);
  json += F(",\"rpm\":"); json += latest.rpm;
  json += F(",\"declog_hz\":"); json += String(latest.declogHz, 1);
  json += F(",\"declog_percent\":"); json += String(percent, 1);
  json += F(",\"ref1_v\":"); json += String(latest.ref1V, 3);
  json += F(",\"battery_v\":"); json += String(latest.batteryV, 3);
  json += F(",\"input_hz\":"); json += String(freqInput::hz, 1);
  // Page 2 speed sensor: GPIO34 Hz / 10 = km/h (0-200); null = NO SIGNAL.
  json += F(",\"input_kmh\":");
  json += freqInput::hz > 0 ? String(min(freqInput::hz / INPUT_HZ_PER_KMH, INPUT_MAX_KMH), 1) : String("null");
  json += F(",\"page\":"); json += page;
  json += F(",\"page_mode\":\"");
  json += nav == Nav::LOCKED ? F("locked") : nav == Nav::SELECTING ? F("select") : F("free");
  json += '"';
  json += F(",\"sender\":");
  json += latest.sender == AbsData::MEGA ? F("\"mega\"") : latest.sender == AbsData::NANO ? F("\"nano\"") : F("null");
  // Generator settings (null with older sender firmware).
  // Nano: KY-040 speed encoder (A0 / A1 / A5) -> rear D9 / front D10, Declog pot A2 -> D3.
  // Mega: KY-040 speed encoder (D22 / D23 / D24) -> rear D44 / front D3, Declog pot A2 -> D11.
  const char* const GEN_KEYS[3] = {",\"gen_rear_hz\":", ",\"gen_front_hz\":", ",\"gen_declog_hz\":"};
  for (uint8_t i = 0; i < 3; ++i) { json += GEN_KEYS[i]; json += latest.gens ? String(latest.genHz[i], 1) : String("null"); }
  // Wheel the sender's speed encoder adjusts ("rear" / "front"; null without one).
  json += F(",\"speed_encoder\":");
  json += latest.speedSel == 1 ? F("\"rear\"") : latest.speedSel == 2 ? F("\"front\"") : F("null");
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

// Pages 1-3, wrapping round.
inline uint8_t wrapPage(int p) {
  const int n = absDisplay::PAGE_COUNT;
  return uint8_t(((p - 1) % n + n) % n + 1);
}

// Page navigation with the Mega's A5 page pot: the Mega splits the 0-1023
// reading into pages (0-341 page 1, 342-682 page 2, 683-1023 page 3) and
// sends the page in every frame. The screen goes to that page whenever the
// pot's page changes, and when the link (re)starts. A swipe still changes the
// page in between; it holds until the pot is turned into another page.
struct PagePotFollow {
  uint8_t lastPotPage = 0;  // 0 = not seen yet (or link lost).
  uint32_t seenFrame = 0;

  // apply false (encoder selecting or locked): keep track of the pot but
  // leave the page alone.
  void update(bool fresh, bool apply) {
    if (!fresh || !latest.pagePot) { lastPotPage = 0; return; }
    if (frameCount == seenFrame) return;
    seenFrame = frameCount;
    if (latest.potPage == lastPotPage) return;
    lastPotPage = latest.potPage;
    if (!apply) return;
    page = latest.potPage;
    Serial.print(F("# PAGE POT A5 ")); Serial.print(latest.potRaw); Serial.print(F(" -> page ")); Serial.println(page);
  }
} pagePotFollow;

// KY-040 encoder page selection (Encoder.h): press to start selecting, turn
// to pick a page (the footer's blinking dot), press again to go to it and
// lock it there. FREE is the start-up state (swipes and the page pot work as
// before); after the first press the screen alternates SELECTING / LOCKED.
void encoderNav() {
  encoder::poll();
  const int8_t steps = encoder::takeSteps();
  if (encoder::pressed()) {
    if (nav != Nav::SELECTING) {
      nav = Nav::SELECTING;
      selPage = page;
      encoder::clearSteps();
      Serial.print(F("# ENCODER press -> SELECT (page ")); Serial.print(page); Serial.println(')');
    } else {
      nav = Nav::LOCKED;
      page = selPage;
      Serial.print(F("# ENCODER press -> LOCKED on page ")); Serial.println(page);
    }
    return;
  }
  if (steps && nav == Nav::SELECTING) {
    selPage = wrapPage(selPage + steps);
    Serial.print(F("# ENCODER turn ")); Serial.print(steps); Serial.print(F(" -> select page "));
    Serial.println(selPage);
  }
}

void setup() {
  Serial.begin(115200);
  absDisplay::begin();
  freqInput::begin();
  encoder::begin();
  brakeButton::begin();
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
  Serial.println(F("ABS tester (2.8\") ready. Nano D5 or Mega TX1 (D18) link on GPIO16 (RX2), 115200 baud."));
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
      } else Serial.print(F("n/a (old sender firmware)"));
      Serial.print(F(" | DECLOG D2 ")); Serial.print(latest.declogHz, 1);
      Serial.print(F(" Hz | REF1 ")); Serial.print(latest.ref1V, 2);
      Serial.print(F(" V | BATTERY ")); Serial.print(latest.batteryV, 2);
      if (latest.gens) {
        const bool mega = latest.sender == AbsData::MEGA;
        const bool enc = latest.speedSel != 0;  // Sender with the speed encoder.
        Serial.print(enc ? (mega ? F(" V | ENC REAR ->D44 ") : F(" V | ENC REAR ->D9 "))
                         : mega ? F(" V | POT REAR A1->D44 ") : F(" V | POT REAR A0->D9 "));
        Serial.print(latest.genHz[0], 0);
        Serial.print(enc ? (mega ? F(" Hz, FRONT ->D3 ") : F(" Hz, FRONT ->D10 "))
                         : mega ? F(" Hz, FRONT A0->D3 ") : F(" Hz, FRONT A1->D10 "));
        Serial.print(latest.genHz[1], 0);
        Serial.print(mega ? F(" Hz, DECLOG A2->D11 ") : F(" Hz, DECLOG A2->D3 "));
        Serial.print(latest.genHz[2], 0); Serial.print(F(" Hz"));
        if (enc) Serial.print(latest.speedSel == 1 ? F(" (adjusting REAR)") : F(" (adjusting FRONT)"));
      } else Serial.print(F(" V"));
      Serial.print(F(" | ")); Serial.print(senderName(latest.sender));
      Serial.print(F(" | SENSOR GPIO34 "));
      if (freqInput::hz > 0) {
        Serial.print(freqInput::hz, 1); Serial.print(F(" Hz = "));
        Serial.print(min(freqInput::hz / INPUT_HZ_PER_KMH, INPUT_MAX_KMH), 1); Serial.print(F(" km/h"));
      } else Serial.print(F("NO SIGNAL"));
      if (brakeButton::held) { Serial.print(F(" | BRAKE GPIO13 rear ")); Serial.print(brakeButton::rear, 0); Serial.print(F(" front ")); Serial.print(brakeButton::front, 0); Serial.print(F(" km/h")); }
      Serial.print(F(" | bad frames ")); Serial.print(badFrames);
      Serial.print(F(" | ")); encoder::printStatus(Serial); Serial.println();
    } else {
      Serial.print(F("# NO DATA from the ")); Serial.print(senderName(latest.sender));
      Serial.print(F(": ")); Serial.print(bytesReceived);
      Serial.print(F(" bytes received on GPIO16, bad frames ")); Serial.print(badFrames);
      if (lastBadFrame[0]) { Serial.print(F(" | last line: ")); Serial.print(lastBadFrame); }
      Serial.print(bytesReceived ? F("") : F(" | check Nano D5 / Mega TX1 (D18) -> GPIO16 and GND"));
      Serial.print(F(" | SENSOR GPIO34 ")); Serial.print(freqInput::hz, 1); Serial.print(F(" Hz | "));
      encoder::printStatus(Serial); Serial.println();
    }
  }
  AbsData d = latest;
  const bool fresh = havePacket && uint32_t(millis() - lastPacketMs) <= 3000;
  d.link = !havePacket ? AbsData::WAITING : !fresh ? AbsData::STALE : AbsData::LIVE;
  d.inputHz = freqInput::update();
  // Brake button (GPIO13): shown rear / front speeds drop 10 km/h per 100 ms to 0 while held.
  brakeButton::update(latest.speedKmh, latest.frontKmh);
  d.speedKmh = brakeButton::rearKmh(latest.speedKmh);
  d.frontKmh = brakeButton::frontKmh(latest.frontKmh);
  d.brake = latest.brake || brakeButton::held;
  // ABS light: follows the rear speed while live; losing the link resets it
  // (the next start begins with it on).
  if (fresh) absLamp.update(d.speedKmh); else absLamp.reset();
  d.absLamp = absLamp.on;
  encoderNav();
  pagePotFollow.update(fresh, nav == Nav::FREE);
  // Swipe left: next page, swipe right: previous page. While selecting a
  // swipe moves the selection instead; while locked swipes are ignored.
  const int8_t s = absDisplay::swipe();
  if (s) {
    Serial.print(F("# SWIPE ")); Serial.print(s < 0 ? F("left") : F("right"));
    Serial.print(F(" (raw dx ")); Serial.print(absDisplay::lastSwipeDx); Serial.print(F(", dy "));
    Serial.print(absDisplay::lastSwipeDy);
    if (nav == Nav::LOCKED) Serial.println(F(") ignored, page locked"));
    else if (nav == Nav::SELECTING) {
      selPage = wrapPage(selPage - s);
      Serial.print(F(") -> select page ")); Serial.println(selPage);
    } else {
      page = wrapPage(page - s);
      Serial.print(F(") -> page ")); Serial.println(page);
    }
  }
  absDisplay::update(d, page, nav == Nav::SELECTING ? selPage : 0, nav == Nav::LOCKED);
  server.handleClient();
  yield();
}
