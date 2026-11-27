// Nano OBD-II Tester (Set 2: diagnostic tool bench, CAN side)
//
// The scan tool half of Set 2. Talks OBD-II over CAN (MCP2515, 500 kbps,
// 11-bit IDs, ISO 15765-2) to the ECU simulator (Set 1, NanoSensorSimulator)
// and passes the answers to the ESP32 (EspObdDashboard), which shows them on
// a Wi-Fi web page and sends back the Read / Clear DTC commands.
//
//   ECU simulator Nano --CAN--> this Nano --SoftwareSerial 38400--> ESP32
//
// What it does:
//   - polls mode 01 PIDs 01, 05, 0F, 0B, 11, 14 in turn, one every 50 ms,
//     with RPM (PID 0C) between each of them so the tachometer stays smooth
//   - reads the stored DTCs (mode 03) and pending DTCs (mode 07) every 2 s and on READ
//   - clears the DTCs (mode 04) on a CLEAR command
//   - asks for the supported PIDs (PID 00) at start and every 10 s
//   Requests go out on 0x7DF, answers come from 0x7E8; for multi-frame
//   answers it sends the flow control on 0x7E0.
//
// Pins:
//   D10  OUT  MCP2515 CS
//   D11  OUT  MCP2515 SI  (SPI MOSI)
//   D12  IN   MCP2515 SO  (SPI MISO)
//   D13  OUT  MCP2515 SCK
//   D2   IN   MCP2515 INT
//   D9   OUT  SoftwareSerial TX -> 10k / 20k divider -> ESP32 GPIO16 (RX2)
//   D8   IN   SoftwareSerial RX <- ESP32 GPIO17 (TX2, 3.3 V is read as HIGH)
//   GND       common ground with the ESP32
// USB Serial (115200) shows the same lines, for the Serial Monitor.
//
// Lines to the ESP32 end in *HH, the XOR of every character before the '*'
// (as in NMEA). The ESP32 drops a line whose checksum is wrong, so a noisy
// wire can't put stray characters into a DTC. The USB Serial copy has no *HH.
//   PID,<pid>,<A>,<B>,<C>,<D>   mode 01 answer (hex PID, decimal bytes)
//   DTC,<n>,<code>,...          mode 03 answer, e.g. DTC,2,P0113,P0122
//   PEND,<n>,<code>,...         mode 07 answer (pending DTCs), e.g. PEND,1,P0217
//   CLR,OK                      mode 04 done
//   NEG,<mode>,<code>           negative answer (7F)
//   NORESP,<mode>,<pid>         no answer in 150 ms
//   ACK,READ / ACK,CLEAR        command from the ESP32 received
//   # ...                       information
// Commands from the ESP32: READ, CLEAR (each answered with ACK).
//
// Board: Arduino Nano (ATmega328P). Libraries: "mcp_can" by coryjfowler,
// SoftwareSerial (built in).
//
// Not AltSoftSerial: it sends by rewriting TCCR1A on every bit, and the
// MCP2515 CS on D10 (a Timer1 pin) makes every digitalWrite(10) do an
// unprotected read-modify-write of TCCR1A too. The two collided thousands of
// times a second and garbled the lines to the ESP32 (the "weird symbols").
// SoftwareSerial does not use Timer1. It cannot receive while it sends, so a
// READ / CLEAR can be missed; the ESP32 re-sends it until it gets the ACK.

#include <Arduino.h>
#include <SPI.h>
#include <mcp_can.h>
#include <SoftwareSerial.h>

constexpr uint8_t PIN_CAN_CS = 10;
constexpr uint8_t PIN_CAN_INT = 2;
constexpr uint8_t CAN_CRYSTAL = MCP_8MHZ;  // Crystal on the MCP2515 module ("8.000").
constexpr uint16_t CAN_RETRY_MS = 2000;
constexpr uint32_t OBD_FUNCTIONAL_ID = 0x7DF;
constexpr uint32_t OBD_PHYSICAL_ID = 0x7E0;   // Flow control to the ECU.
constexpr uint32_t OBD_RESPONSE_ID = 0x7E8;
constexpr uint16_t REQUEST_GAP_MS = 50;
constexpr uint16_t RESPONSE_TIMEOUT_MS = 150;
constexpr uint16_t DTC_PERIOD_MS = 2000;
constexpr uint16_t SUPPORTED_PERIOD_MS = 10000;
constexpr uint32_t ESP_BAUD = 38400;

const uint8_t POLL_PIDS[] = {0x0C, 0x01, 0x0C, 0x05, 0x0C, 0x0F, 0x0C, 0x0B, 0x0C, 0x11, 0x0C, 0x14};

MCP_CAN can(PIN_CAN_CS);
SoftwareSerial esp(8, 9);  // RX D8, TX D9.
bool canReady = false;

// One line to the ESP32, with its *HH checksum.
void espLine(const char* line) {
  uint8_t sum = 0;
  for (const char* p = line; *p; p++) sum ^= uint8_t(*p);
  char tail[6];
  snprintf(tail, sizeof(tail), "*%02X\n", sum);
  esp.print(line);
  esp.print(tail);
}

// Sends one line to both the ESP32 and the USB Serial Monitor.
void emit(const char* line) {
  Serial.println(line);
  espLine(line);
}

void info(const __FlashStringHelper* text) {
  char line[96];
  strncpy_P(line, reinterpret_cast<const char*>(text), sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  emit(line);
}

void canBegin() {
  canReady = can.begin(MCP_ANY, CAN_500KBPS, CAN_CRYSTAL) == CAN_OK;
  if (canReady) can.setMode(MCP_NORMAL);
  info(canReady ? F("# CAN MCP2515 OK, 500 kbps, 8 MHz crystal, OBD-II tester")
                : F("# CAN MCP2515 not found (check CS D10, SI D11, SO D12, SCK D13, 5V)"));
}

// ---------------------------- Requests --------------------------------------
bool waiting = false;          // A request is out, its answer not yet in.
uint8_t waitMode = 0, waitPid = 0;
uint32_t requestMs = 0;
bool readRequested = false, clearRequested = false;

bool sendRequest(uint8_t mode, uint8_t pid, bool withPid) {
  uint8_t frame[8] = {0};
  frame[0] = withPid ? 2 : 1;
  frame[1] = mode;
  if (withPid) frame[2] = pid;
  waiting = true;
  waitMode = mode;
  waitPid = withPid ? pid : 0;
  requestMs = millis();
  // A failed send means no node acknowledged the frame: the other MCP2515 is
  // unpowered, at another bit rate / crystal, or CANH / CANL / J1 are wrong.
  static int8_t acked = -1;
  const bool ok = can.sendMsgBuf(OBD_FUNCTIONAL_ID, 0, 8, frame) == CAN_OK;
  if (int8_t(ok) != acked) {
    acked = ok;
    char line[64];
    snprintf(line, sizeof(line), ok ? "# CAN TX acknowledged (TEC %u REC %u)" : "# CAN TX NOT acknowledged (TEC %u REC %u EFLG %02X)",
             can.errorCountTX(), can.errorCountRX(), can.getError());
    emit(line);
  }
  return ok;
}

void scheduleNext(uint32_t now) {
  static uint32_t lastDtcMs = 0, lastSupportedMs = 0;
  static uint8_t pidIndex = 0;
  static bool supportedAsked = false;
  if (waiting || now - requestMs < REQUEST_GAP_MS) return;

  if (clearRequested) { clearRequested = false; sendRequest(0x04, 0, false); return; }
  static bool pendingNext = false;  // After each mode 03, also ask mode 07.
  if (pendingNext) { pendingNext = false; sendRequest(0x07, 0, false); return; }
  if (readRequested || now - lastDtcMs >= DTC_PERIOD_MS) {
    readRequested = false;
    lastDtcMs = now;
    pendingNext = true;
    sendRequest(0x03, 0, false);
    return;
  }
  if (!supportedAsked || now - lastSupportedMs >= SUPPORTED_PERIOD_MS) {
    supportedAsked = true;
    lastSupportedMs = now;
    sendRequest(0x01, 0x00, true);
    return;
  }
  sendRequest(0x01, POLL_PIDS[pidIndex], true);
  pidIndex = (pidIndex + 1) % sizeof(POLL_PIDS);
}

// ---------------------------- Answers ---------------------------------------
void handleAnswer(const uint8_t* d, uint8_t n) {
  char line[96];
  if (n >= 2 && d[0] == 0x41) {
    uint8_t b[4] = {0, 0, 0, 0};
    for (uint8_t i = 0; i < 4 && i + 2 < n; i++) b[i] = d[i + 2];
    snprintf(line, sizeof(line), "PID,%02X,%u,%u,%u,%u", d[1], b[0], b[1], b[2], b[3]);
    emit(line);
    if (waitMode == 0x01 && waitPid == d[1]) waiting = false;
  } else if (n >= 2 && (d[0] == 0x43 || d[0] == 0x47)) {  // Stored (03) or pending (07) DTCs.
    const uint8_t count = d[1];
    int len = snprintf(line, sizeof(line), d[0] == 0x43 ? "DTC,%u" : "PEND,%u", count);
    for (uint8_t i = 0; i < count && 2 + 2 * i + 1 < n && len < int(sizeof(line)) - 8; i++) {
      const uint8_t hi = d[2 + 2 * i], lo = d[3 + 2 * i];
      len += snprintf(line + len, sizeof(line) - len, ",%c%u%X%02X", "PCBU"[hi >> 6], (hi >> 4) & 3, hi & 0x0F, lo);
    }
    emit(line);
    if (waitMode == (d[0] & 0x0F)) waiting = false;
  } else if (n >= 1 && d[0] == 0x44) {
    emit("CLR,OK");
    if (waitMode == 0x04) waiting = false;
    readRequested = true;  // Show the result of the clear straight away.
  } else if (n >= 3 && d[0] == 0x7F) {
    snprintf(line, sizeof(line), "NEG,%02X,%02X", d[1], d[2]);
    emit(line);
    if (waitMode == d[1]) waiting = false;
  }
}

// ISO 15765-2 receive: single frames, or first frame + consecutive frames.
uint8_t rxBuf[64];
uint8_t rxTotal = 0, rxHave = 0, rxNextSn = 0;
bool rxActive = false;

void handleFrame(const uint8_t* f, uint8_t len) {
  const uint8_t pci = f[0] >> 4;
  if (pci == 0x0) {                       // Single frame.
    const uint8_t n = f[0] & 0x0F;
    if (n >= 1 && n <= 7 && n < len) handleAnswer(f + 1, n);
  } else if (pci == 0x1) {                // First frame: send flow control.
    const uint16_t total = ((f[0] & 0x0F) << 8) | f[1];
    rxTotal = total < sizeof(rxBuf) ? total : sizeof(rxBuf);
    memcpy(rxBuf, f + 2, 6);
    rxHave = 6;
    rxNextSn = 1;
    rxActive = true;
    uint8_t fc[8] = {0x30, 0x00, 0x00, 0, 0, 0, 0, 0};  // Continue, no block limit, no gap.
    can.sendMsgBuf(OBD_PHYSICAL_ID, 0, 8, fc);
  } else if (pci == 0x2 && rxActive) {    // Consecutive frame.
    if ((f[0] & 0x0F) != (rxNextSn & 0x0F)) { rxActive = false; info(F("# OBD frame out of order")); return; }
    rxNextSn++;
    const uint8_t chunk = rxTotal - rxHave < 7 ? rxTotal - rxHave : 7;
    memcpy(rxBuf + rxHave, f + 1, chunk);
    rxHave += chunk;
    if (rxHave >= rxTotal) { rxActive = false; handleAnswer(rxBuf, rxTotal); }
  }
}

uint32_t rxFrames = 0, rxLastId = 0;

void readCan() {
  while (digitalRead(PIN_CAN_INT) == LOW || can.checkReceive() == CAN_MSGAVAIL) {
    uint32_t id;
    uint8_t len, buf[8];
    if (can.readMsgBuf(&id, &len, buf) != CAN_OK) break;
    rxFrames++; rxLastId = id;
    if ((id & 0x7FF) == OBD_RESPONSE_ID && len >= 1) handleFrame(buf, len);
  }
}

// Bus health, checked once a second, printed when the error counters change.
void printHealth(uint32_t now) {
  static uint32_t lastMs = 0;
  static uint8_t lastTec = 0, lastRec = 0, lastEflg = 0;
  if (now - lastMs < 1000) return;
  lastMs = now;
  const uint8_t tec = can.errorCountTX(), rec = can.errorCountRX(), eflg = can.getError();
  if (tec == lastTec && rec == lastRec && eflg == lastEflg) return;
  lastTec = tec; lastRec = rec; lastEflg = eflg;
  char line[72];
  snprintf(line, sizeof(line), "# CAN bus TEC %u REC %u EFLG %02X RX %lu last %03lX", tec, rec, eflg, rxFrames, rxLastId);
  emit(line);
}

// ---------------------------- ESP32 commands --------------------------------
void readEsp() {
  static char buf[16];
  static uint8_t len = 0;
  while (esp.available()) {
    const char c = esp.read();
    if (c == '\r') continue;
    if (c != '\n') { if (len < sizeof(buf) - 1) buf[len++] = c; continue; }
    buf[len] = '\0';
    len = 0;
    if (strcmp(buf, "READ") == 0) { readRequested = true; emit("ACK,READ"); }
    else if (strcmp(buf, "CLEAR") == 0) { clearRequested = true; emit("ACK,CLEAR"); }
  }
}

void setup() {
  Serial.begin(115200);
  esp.begin(ESP_BAUD);
  pinMode(PIN_CAN_INT, INPUT_PULLUP);
  info(F("# NANO OBD-II TESTER (CAN CS D10, ESP32 on D9 TX / D8 RX)"));
  canBegin();
}

void loop() {
  static uint32_t lastRetryMs = 0;
  const uint32_t now = millis();
  readEsp();
  if (!canReady) {
    if (now - lastRetryMs >= CAN_RETRY_MS) { lastRetryMs = now; canBegin(); }
    return;
  }
  readCan();
  printHealth(now);
  if (waiting && millis() - requestMs >= RESPONSE_TIMEOUT_MS) {
    waiting = false;
    rxActive = false;
    char line[24];
    snprintf(line, sizeof(line), "NORESP,%02X,%02X", waitMode, waitPid);
    emit(line);
  }
  scheduleNext(millis());
}
