// ESP32 OBD-II Dashboard (Set 2: diagnostic tool bench, display side)
//
// The screen of the scan tool. The Nano OBD-II Tester talks CAN to the ECU
// simulator and sends its answers here as text lines; this ESP32 decodes them
// and shows a scan-tool page over Wi-Fi: tachometer, live data with normal /
// caution / danger zones, MIL and DTCs (as of the last Read DTC press), the
// Read DTC / Clear DTC buttons (sent back to the Nano as READ / CLEAR) and an
// activity log.
//
//   ECU simulator (Set 1) --CAN--> Nano OBD-II Tester --38400--> this ESP32
//
// Link checks (against stray characters in the DTCs):
//   - every line from the Nano ends in *HH (XOR checksum); a bad line is dropped
//   - only printable ASCII is accepted, and a DTC must look like P0113
//   - dropped lines are counted ("link errors" at the bottom of the page)
// Commands: READ / CLEAR are re-sent up to 3 times until the Nano answers
// ACK; the page tells whether the ECU cleared the codes, or which code came
// back because its fault is still active on Set 1.
//
// Wiring (ESP32 WROOM-32 DevKit, 38 pins):
//   GPIO16 (RX2) <- 20k to GND, 10k from the Nano's D9  (5 V -> 3.3 V)
//   GPIO17 (TX2) -> Nano D8 (direct; 3.3 V is a valid HIGH for the Nano)
//   GND          -- Nano GND
//   USB          power + Serial Monitor (115200)
//
// Wi-Fi: join "ECU-OBD" (password ECUdemo2026), open http://192.168.4.1/
//   /api/live       readings as JSON
//   /api/cmd?c=read | clear
//
// Board: "ESP32 Dev Module" (esp32 core 3.x). No extra libraries.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "page.h"

constexpr int PIN_RX2 = 16;
constexpr int PIN_TX2 = 17;
constexpr uint32_t LINK_BAUD = 38400;
constexpr uint32_t ECU_STALE_MS = 1500;    // No OBD answer for this long = ECU offline.
constexpr uint32_t TESTER_STALE_MS = 1500; // No line at all for this long = tester offline.
constexpr uint32_t CMD_ACK_MS = 600;       // Re-send READ / CLEAR if no ACK in this time.
constexpr uint8_t CMD_TRIES = 3;
constexpr uint32_t CLEAR_DONE_MS = 2000;   // ECU must confirm the clear (44) in this time.
const char* AP_SSID = "ECU-OBD";
const char* AP_PASS = "ECUdemo2026";

struct Live {
  // Mode 01 values, decoded. valid* = seen at least once.
  int ect = 0, iat = 0, map = 0, rpm = 0;
  float tps = 0, o2 = 0;
  bool validEct = false, validIat = false, validMap = false, validTps = false, validO2 = false, validRpm = false;
  bool mil = false;
  int dtcCount = 0;
  uint32_t supported = 0;
  // Mode 03 / 07.
  String dtcs = "";        // "P0113,P0122"
  String pend = "";        // Mode 07 pending DTCs, same format.
  bool dtcRead = false;
  uint32_t dtcMs = 0;
  // Status.
  uint32_t lastAnswerMs = 0, lastLineMs = 0, answers = 0, noResponse = 0, badLines = 0;
} live;

WebServer server(80);

// ------------------------------ Activity log --------------------------------
enum Level : uint8_t { LV_INFO, LV_OK, LV_WARN, LV_BAD };
constexpr uint8_t LOG_SIZE = 10;
struct LogEntry { uint32_t ms; Level level; String text; };
LogEntry logBuf[LOG_SIZE];
uint8_t logHead = 0, logCount = 0;

void event(Level level, const String& text) {
  const uint8_t last = (logHead + LOG_SIZE - 1) % LOG_SIZE;
  if (logCount && logBuf[last].text == text) { logBuf[last].ms = millis(); logBuf[last].level = level; }
  else {
    logBuf[logHead] = {millis(), level, text};
    logHead = (logHead + 1) % LOG_SIZE;
    if (logCount < LOG_SIZE) logCount++;
  }
  Serial.println("# " + text);
}

// ------------------------------ Commands ------------------------------------
struct Command {
  const char* name = nullptr;  // "READ" / "CLEAR" while waiting for the ACK.
  uint8_t tries = 0;
  uint32_t sentMs = 0;
} cmd;

// Clear DTC progress: waiting for the ACK, for the ECU's 44, then for the DTC read after it.
enum ClearState : uint8_t { CLR_IDLE, CLR_WAIT_ACK, CLR_WAIT_ECU, CLR_WAIT_READ } clearState = CLR_IDLE;
uint32_t clearMs = 0;
String clearedCodes = "";  // DTCs stored when Clear was pressed.
// Read DTC: the codes are only read when the button is pressed (and after a
// clear). Waiting for the mode 03 + 07 answers.
bool readWait = false;
uint32_t readMs = 0;
constexpr uint32_t READ_DONE_MS = 3000;

void sendCmd() {
  Serial2.print(cmd.name);
  Serial2.print('\n');
  cmd.tries++;
  cmd.sentMs = millis();
}

void startCmd(const char* name) {
  cmd.name = name;
  cmd.tries = 0;
  sendCmd();
}

void updateCommands() {
  const uint32_t now = millis();
  if (cmd.name && now - cmd.sentMs >= CMD_ACK_MS) {
    if (cmd.tries < CMD_TRIES) sendCmd();
    else {
      event(LV_BAD, String("The Nano tester did not answer ") + cmd.name +
                    ". Check the wire ESP32 GPIO17 -> Nano D8, and GND.");
      if (strcmp(cmd.name, "CLEAR") == 0) clearState = CLR_IDLE;
      cmd.name = nullptr;
    }
  }
  if (clearState == CLR_WAIT_ECU && now - clearMs >= CLEAR_DONE_MS) {
    event(LV_BAD, "The ECU did not confirm the clear (no mode 04 answer).");
    clearState = CLR_IDLE;
  }
  if (clearState == CLR_WAIT_READ && now - clearMs >= CLEAR_DONE_MS + 3000) clearState = CLR_IDLE;
  if (readWait && now - readMs >= READ_DONE_MS) {
    readWait = false;
    event(LV_BAD, "No answer to Read DTC from the ECU.");
  }
}

// ------------------------------ Link parsing --------------------------------
// Splits "A,B,C" into fields (in place).
int splitFields(char* s, char** out, int max) {
  int n = 0;
  out[n++] = s;
  for (char* p = s; *p && n < max; p++)
    if (*p == ',') { *p = '\0'; out[n++] = p + 1; }
  return n;
}

// "P0113": P/C/B/U, digit 0-3, three hex digits.
bool validDtc(const char* s) {
  if (strlen(s) != 5 || !strchr("PCBU", s[0]) || s[1] < '0' || s[1] > '3') return false;
  for (int i = 2; i < 5; i++) if (!isxdigit(uint8_t(s[i])) || islower(uint8_t(s[i]))) return false;
  return true;
}

// DTC / PEND fields -> "P0113,P0122". False if a code is malformed (line dropped).
bool dtcList(char** f, int n, String& list) {
  list = "";
  for (int i = 2; i < n; i++) {
    if (!validDtc(f[i])) return false;
    if (list.length()) list += ',';
    list += f[i];
  }
  return true;
}

void bad(const char* why) {
  live.badLines++;
  Serial.printf("# Link: dropped a line (%s)\n", why);
}

void parseLine(char* line) {
  live.lastLineMs = millis();
  if (line[0] == '#') {  // Information from the Nano.
    if (strncmp(line, "# CAN bus", 9) != 0) event(LV_INFO, String("Tester: ") + (line + 2));
    return;
  }
  char* f[24];
  const int n = splitFields(line, f, 24);

  if (strcmp(f[0], "PID") == 0 && n >= 6) {
    const int pid = strtol(f[1], nullptr, 16);
    const int a = atoi(f[2]), b = atoi(f[3]), c = atoi(f[4]), d = atoi(f[5]);
    live.lastAnswerMs = millis();
    live.answers++;
    switch (pid) {
      case 0x00: live.supported = (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | d; break;
      case 0x01: live.mil = a & 0x80; live.dtcCount = a & 0x7F; break;
      case 0x05: live.ect = a - 40; live.validEct = true; break;
      case 0x0B: live.map = a; live.validMap = true; break;
      case 0x0C: live.rpm = (a * 256 + b) / 4; live.validRpm = true; break;
      case 0x0F: live.iat = a - 40; live.validIat = true; break;
      case 0x11: live.tps = a * 100.0f / 255.0f; live.validTps = true; break;
      case 0x14: live.o2 = a / 200.0f; live.validO2 = true; break;
    }
  } else if (strcmp(f[0], "DTC") == 0 && n >= 2) {
    String list;
    if (!dtcList(f, n, list)) { bad("malformed DTC"); return; }
    live.lastAnswerMs = millis();
    live.answers++;
    if (clearState == CLR_WAIT_READ) {
      clearState = CLR_IDLE;
      readWait = false;
      if (list.length())
        event(LV_WARN, "Cleared, but " + list + " is still stored. Press Clear DTC again.");
      else event(LV_OK, (clearedCodes.length() ? "Cleared " + clearedCodes + ". " : String("Cleared. ")) +
                        "Set 1 reset to normal, no DTCs stored, MIL off.");
    }
    live.dtcs = list;
    live.dtcRead = true;
    live.dtcMs = millis();
  } else if (strcmp(f[0], "PEND") == 0 && n >= 2) {
    String list;
    if (!dtcList(f, n, list)) { bad("malformed pending DTC"); return; }
    live.lastAnswerMs = millis();
    live.pend = list;
    if (readWait) {  // Result of the Read DTC button.
      readWait = false;
      if (!live.dtcs.length() && !list.length()) event(LV_OK, "Read DTC: no DTCs stored or pending.");
      else event(LV_WARN, "Read DTC: " + (live.dtcs.length() ? "stored " + live.dtcs : String("none stored")) +
                          (list.length() ? ", pending " + list : String("")));
    }
  } else if (strcmp(f[0], "ACK") == 0 && n >= 2) {
    if (cmd.name && strcmp(cmd.name, f[1]) == 0) {
      cmd.name = nullptr;
      if (strcmp(f[1], "CLEAR") == 0 && clearState == CLR_WAIT_ACK) { clearState = CLR_WAIT_ECU; clearMs = millis(); }
    }
  } else if (strcmp(f[0], "CLR") == 0) {
    live.lastAnswerMs = millis();
    live.mil = false;  // Until PID 01 says otherwise.
    if (clearState == CLR_WAIT_ECU || clearState == CLR_WAIT_ACK) { clearState = CLR_WAIT_READ; clearMs = millis(); cmd.name = nullptr; }
    event(LV_INFO, "ECU confirmed the clear (mode 04), reading the DTCs again...");
  } else if (strcmp(f[0], "NEG") == 0 && n >= 3) {
    event(LV_BAD, String("ECU refused mode ") + f[1] + " (code " + f[2] + ")");
    if (strcmp(f[1], "04") == 0) clearState = CLR_IDLE;
  } else if (strcmp(f[0], "NORESP") == 0) {
    live.noResponse++;
    if (n >= 2 && strcmp(f[1], "04") == 0) {
      event(LV_BAD, "The ECU did not answer the clear (mode 04).");
      clearState = CLR_IDLE;
    }
  } else {
    bad("unknown line");
  }
}

// Checks the *HH checksum and that the line is plain ASCII, then parses it.
void checkLine(char* line, uint8_t len) {
  for (uint8_t i = 0; i < len; i++)
    if (line[i] < 32 || line[i] > 126) {
      bad("noise");
      static uint32_t lastDumpMs = 0;  // One hex dump a second, to diagnose the wire.
      if (millis() - lastDumpMs >= 1000) {
        lastDumpMs = millis();
        Serial.print("# Link bytes:");
        for (uint8_t k = 0; k < len; k++) Serial.printf(" %02X", uint8_t(line[k]));
        Serial.println();
      }
      return;
    }
  if (len < 4 || line[len - 3] != '*') { bad("no checksum"); return; }
  uint8_t sum = 0;
  for (uint8_t i = 0; i < len - 3; i++) sum ^= uint8_t(line[i]);
  if (strtol(line + len - 2, nullptr, 16) != sum || !isxdigit(uint8_t(line[len - 2])) || !isxdigit(uint8_t(line[len - 1]))) {
    bad("checksum");
    return;
  }
  line[len - 3] = '\0';
  parseLine(line);
}

void readLink() {
  static char buf[128];
  static uint8_t len = 0;
  static bool overflow = false;
  while (Serial2.available()) {
    const char c = Serial2.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = '\0';
      if (overflow) bad("too long");
      else if (len) checkLine(buf, len);
      len = 0;
      overflow = false;
    }
    else if (len < sizeof(buf) - 1) buf[len++] = c;
    else overflow = true;
  }
}

// ------------------------------ Web server ----------------------------------
float afr() { return 15.2f + (live.o2 - 0.1f) * (14.0f - 15.2f) / 0.8f; }

String jsonNum(bool valid, float v, int decimals) { return valid ? String(v, decimals) : String("null"); }

// Appends a JSON string, escaped, ASCII only.
void jsonStr(String& j, const String& s) {
  j += '"';
  for (unsigned i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '"' || c == '\\') { j += '\\'; j += c; }
    else if (c >= 32 && c <= 126) j += c;
  }
  j += '"';
}

// "P0113,P0122" -> ["P0113","P0122"]
void jsonList(String& j, const String& list) {
  j += '[';
  if (list.length()) { j += '"'; String s = list; s.replace(",", "\",\""); j += s; j += '"'; }
  j += ']';
}

void handleLive() {
  const uint32_t now = millis();
  const bool ecu = live.answers && now - live.lastAnswerMs < ECU_STALE_MS;
  const bool tester = live.lastLineMs && now - live.lastLineMs < TESTER_STALE_MS;
  String j;
  j.reserve(2000);
  j += "{\"state\":\"";
  j += ecu ? "ECU ONLINE" : tester ? "NO ECU ANSWER" : "NO TESTER";
  j += "\",\"ecu\":"; j += ecu ? "true" : "false";
  j += ",\"tester\":"; j += tester ? "true" : "false";
  j += ",\"rpm\":"; j += jsonNum(live.validRpm, live.rpm, 0);
  j += ",\"ect\":"; j += jsonNum(live.validEct, live.ect, 0);
  j += ",\"iat\":"; j += jsonNum(live.validIat, live.iat, 0);
  j += ",\"map\":"; j += jsonNum(live.validMap, live.map, 0);
  j += ",\"tps\":"; j += jsonNum(live.validTps, live.tps, 0);
  j += ",\"o2\":"; j += jsonNum(live.validO2, live.o2, 3);
  j += ",\"afr\":"; j += jsonNum(live.validO2, afr(), 1);
  // The DTC card shows the last Read DTC result only: MIL = stored DTCs in that read.
  int stored = 0;
  if (live.dtcs.length()) { stored = 1; for (unsigned i = 0; i < live.dtcs.length(); i++) if (live.dtcs[i] == ',') stored++; }
  j += ",\"mil\":"; j += stored ? "true" : "false";
  j += ",\"dtcCount\":"; j += stored;
  j += ",\"dtcRead\":"; j += live.dtcRead ? "true" : "false";
  j += ",\"dtcAge\":"; j += live.dtcRead ? now - live.dtcMs : 0;
  j += ",\"dtcs\":"; jsonList(j, live.dtcs);
  j += ",\"pend\":"; jsonList(j, live.pend);
  j += ",\"busy\":\"";
  j += clearState != CLR_IDLE ? "clear" : readWait ? "read" : "";
  j += "\",\"supported\":\""; j += String(live.supported, HEX);
  j += "\",\"answers\":"; j += live.answers;
  j += ",\"noResp\":"; j += live.noResponse;
  j += ",\"bad\":"; j += live.badLines;
  j += ",\"log\":[";
  for (uint8_t i = 0; i < logCount; i++) {  // Newest first.
    const LogEntry& e = logBuf[(logHead + LOG_SIZE - 1 - i) % LOG_SIZE];
    if (i) j += ',';
    j += "{\"age\":"; j += now - e.ms;
    j += ",\"lv\":"; j += int(e.level);
    j += ",\"msg\":"; jsonStr(j, e.text);
    j += '}';
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleCmd() {
  const String c = server.arg("c");
  if (c == "read") {
    startCmd("READ");
    readWait = true;
    readMs = millis();
    event(LV_INFO, "Reading DTCs...");
  } else if (c == "clear") {
    clearedCodes = live.dtcs;
    clearState = CLR_WAIT_ACK;
    clearMs = millis();
    startCmd("CLEAR");
    event(LV_INFO, "Clearing DTCs...");
  } else {
    server.send(400, "text/plain", "use c=read or c=clear");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}


void setup() {
  Serial.begin(115200);
  Serial2.begin(LINK_BAUD, SERIAL_8N1, PIN_RX2, PIN_TX2);
  Serial.println("# ESP32 OBD-II DASHBOARD (Nano tester on GPIO16 / GPIO17)");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("# Wi-Fi: %s  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", [] { server.send_P(200, "text/html; charset=utf-8", PAGE); });
  server.on("/api/live", handleLive);
  server.on("/api/cmd", handleCmd);
  server.begin();
}

void loop() {
  static uint32_t lastPrintMs = 0;
  readLink();
  updateCommands();
  server.handleClient();

  if (millis() - lastPrintMs >= 1000) {  // Once a second on the USB Serial Monitor.
    lastPrintMs = millis();
    const uint32_t now = millis();
    if (!live.lastLineMs || now - live.lastLineMs >= TESTER_STALE_MS) { Serial.println("# No data from the Nano tester on GPIO16"); return; }
    if (!live.answers || now - live.lastAnswerMs >= ECU_STALE_MS) { Serial.println("# Tester OK, NO ANSWER from the ECU over CAN"); return; }
    Serial.printf("RPM %d | ECT %d C | IAT %d C | MAP %d kPa | TPS %.0f %% | O2 %.3f V (AFR %.1f) | MIL %s | DTCs %s | link errors %lu\n",
                  live.rpm, live.ect, live.iat, live.map, live.tps, live.o2, afr(), live.mil ? "ON" : "off",
                  live.dtcs.length() ? live.dtcs.c_str() : "none", (unsigned long)live.badLines);
  }
}
