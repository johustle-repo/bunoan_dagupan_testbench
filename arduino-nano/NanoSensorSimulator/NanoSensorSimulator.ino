// Nano Sensor Simulator (Set 1: ECU simulator)
//
// Replaces the five sensor knobs of the ECU testbench (IAT, ECT, O2, MAP, TPS).
// Each sensor is a PWM output with an RC filter, giving a steady 0-5 V into
// the Mega's A0-A4. One rotary encoder sets them, a 16x2 I2C LCD shows them.
// The values are set in real units (deg C, kPa, %), converted back to volts
// with the same formulas the Mega and ESP32 use (SensorConversion.h).
//
// It is also an OBD-II ECU on CAN (MCP2515, 500 kbps, 11-bit IDs): it answers
// a scan tool (Set 2: NanoObdTester + EspObdDashboard) on 0x7E8 for requests
// on 0x7DF (functional) or 0x7E0 (physical), ISO 15765-2 single and
// multi-frame.
//   Mode 01  PID 00 supported PIDs, 01 MIL + DTC count, 05 ECT, 0B MAP,
//            0F IAT, 11 TPS, 14 O2 B1S1 voltage
//   Mode 03  stored DTCs      Mode 04  clear DTCs (an active fault sets its DTC again)
//   Mode 07  pending DTCs
// Range / engine checks (thresholds under "Range and engine checks"):
//   outside NORMAL -> pending DTC, beyond SAFE -> confirmed DTC + MIL
//   P0217 engine overheat (ECT), P0111 IAT range, P0171 too lean / P0172 too
//   rich (O2), P0106 MAP high at closed throttle, P0121 TPS open with no load
// A sensor with an active fault reports its fail-safe value in mode 01, like
// a real ECU: IAT 20 C, ECT 80 C, O2 0.45 V, MAP 100 kPa, TPS 0 %.
//
// Encoder:
//   Turn          change the selected sensor (spin fast = bigger steps)
//   Short press   next sensor: IAT -> ECT -> O2 -> MAP -> TPS
//   Long press    fault on the selected sensor: OK -> HIGH -> LOW -> OK
//                 HIGH = output above the sensor's ERR limit (open circuit),
//                 LOW  = output 0 V (short to ground; IAT / ECT / O2 only,
//                 MAP and TPS have no low limit so they go OK -> HIGH -> OK)
//
// Pins:
//   D5   OUT  IAT  (PWM 980 Hz)  -> 10k -> Mega A0, 10 uF to GND
//   D6   OUT  ECT  (PWM 980 Hz)  -> 10k -> Mega A1, 10 uF to GND
//   D9   OUT  O2   (PWM 31 kHz)  -> 10k -> Mega A2, 3.3k + 10 uF to GND (0-1.24 V)
//   D10  OUT  MAP  (PWM 31 kHz)  -> 10k -> Mega A3, 10 uF to GND
//   D3   OUT  TPS  (PWM 31 kHz)  -> 10k -> Mega A4, 10 uF to GND
//   D8   OUT  MCP2515 CS
//   D11  OUT  MCP2515 SI  (SPI MOSI)
//   D12  IN   MCP2515 SO  (SPI MISO)
//   D13  OUT  MCP2515 SCK
//   D2   IN   MCP2515 INT (LOW = request waiting)
//   A2   IN   Encoder A / CLK
//   A1   IN   Encoder B / DT
//   A0   IN   Encoder button / SW (to GND = pressed)
//   A4   I/O  LCD SDA
//   A5   OUT  LCD SCL
//   GND       common ground with the Mega
//
// Board: Arduino Nano (ATmega328P). Libraries: "LiquidCrystal I2C" by Frank de
// Brabander, "mcp_can" by coryjfowler.

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h>
#include <mcp_can.h>
#include <util/atomic.h>

// ============================== Settings ====================================
constexpr float NANO_VCC = 5.00f;  // Measure the Nano's 5V pin and enter it here for accurate outputs.
// O2 output divider: 10k series, 3.3k to GND -> full scale = 5 V x 3.3 / 13.3.
constexpr float O2_R_SERIES = 10000.0f;
constexpr float O2_R_GROUND = 3300.0f;
constexpr float O2_FULL_SCALE_V = NANO_VCC * O2_R_GROUND / (O2_R_SERIES + O2_R_GROUND);

constexpr bool ENC_REVERSE = false;       // true if turning clockwise lowers the value.
constexpr int8_t STEPS_PER_CLICK = 4;     // Encoder transitions per detent (KY-040 / EC11).
constexpr uint16_t FAST_MS = 60;          // Clicks closer than this count as a fast spin.
constexpr uint16_t LONG_PRESS_MS = 800;
constexpr uint16_t DEBOUNCE_MS = 25;
constexpr uint8_t LCD_ADDR = 0x27;        // I2C LCD address; try 0x3F if nothing shows.

constexpr uint8_t PIN_ENC_A = A2;  // PC2 / PCINT10
constexpr uint8_t PIN_ENC_B = A1;  // PC1 / PCINT9
constexpr uint8_t PIN_ENC_SW = A0;

constexpr uint8_t PIN_CAN_CS = 8;
constexpr uint8_t PIN_CAN_INT = 2;
constexpr uint8_t CAN_CRYSTAL = MCP_8MHZ; // Crystal on the MCP2515 module ("8.000").
constexpr uint16_t CAN_RETRY_MS = 2000;   // Re-try MCP2515 start-up this often if it failed.
constexpr uint32_t OBD_FUNCTIONAL_ID = 0x7DF;  // Scan tool -> all ECUs.
constexpr uint32_t OBD_REQUEST_ID = 0x7E0;     // Scan tool -> this ECU (also flow control).
constexpr uint32_t OBD_RESPONSE_ID = 0x7E8;    // This ECU -> scan tool.
constexpr uint16_t FLOW_CONTROL_WAIT_MS = 150;
constexpr uint16_t TESTER_ACTIVE_MS = 2000;    // LCD shows 'C' this long after a request.

// ============================== Sensors =====================================
// Same tables and limits as SensorConversion.h on the Mega / ESP32.
constexpr int IAT_N = 5;
const float iatVolts[IAT_N] = {2.806f, 2.300f, 1.850f, 1.550f, 1.300f};
const float iatTemps[IAT_N] = {15.0f, 25.0f, 35.0f, 45.0f, 55.0f};
constexpr int ECT_N = 6;
const float ectVolts[ECT_N] = {3.564f, 2.500f, 1.500f, 0.800f, 0.300f, 0.100f};
const float ectTemps[ECT_N] = {20.0f, 40.0f, 65.0f, 90.0f, 110.0f, 120.0f};

// Temperature -> volts: linear interpolation in the table (temps rising, volts falling).
float tempToVolts(float t, const float* volts, const float* temps, int n) {
  if (t <= temps[0]) return volts[0];
  if (t >= temps[n - 1]) return volts[n - 1];
  for (int i = 0; i < n - 1; i++) {
    if (t <= temps[i + 1]) {
      const float f = (t - temps[i]) / (temps[i + 1] - temps[i]);
      return volts[i] + f * (volts[i + 1] - volts[i]);
    }
  }
  return volts[n - 1];
}

enum SensorId : uint8_t { IAT, ECT, O2, MAP, TPS, SENSOR_COUNT };
enum Fault : uint8_t { FAULT_NONE, FAULT_HIGH, FAULT_LOW };

struct Sensor {
  const char* name;   // 3 chars for the LCD.
  char letter;        // Line 2 overview.
  uint8_t pin;
  float fullScaleV;   // Output volts at 100 % duty.
  int16_t minVal, maxVal, step, fastStep;
  int16_t value;      // In units below (O2: millivolts).
  bool hasLowFault;   // Sensor has a low ERR limit that 0 V crosses.
  Fault fault;
  int16_t failSafe;   // Value reported over OBD while the fault is active.
  uint16_t dtcLow, dtcHigh;  // P-codes, as the 2 OBD bytes (P0112 = 0x0112).
};

Sensor sensors[SENSOR_COUNT] = {
  // name  ltr  pin  full scale       min  max   step fast  start  lowFault  fault       safe  DTC low DTC high
  {"IAT", 'I', 5,  NANO_VCC,         15,  55,    1,   5,   25,   true,  FAULT_NONE, 20,   0x0112, 0x0113},  // deg C
  {"ECT", 'E', 6,  NANO_VCC,         20,  120,   1,   5,   90,   true,  FAULT_NONE, 80,   0x0117, 0x0118},  // deg C
  {"O2 ", 'O', 9,  O2_FULL_SCALE_V,  20,  1200,  10,  50,  450,  true,  FAULT_NONE, 450,  0x0131, 0x0132},  // mV
  {"MAP", 'M', 10, NANO_VCC,         10,  100,   1,   5,   35,   false, FAULT_NONE, 100,  0x0107, 0x0108},  // kPa
  {"TPS", 'T', 3,  NANO_VCC,         0,   100,   1,   5,   0,    false, FAULT_NONE, 0,    0x0122, 0x0123},  // %
};

// Volts the sensor should give for its value (no fault).
float targetVolts(uint8_t id) {
  const int16_t v = sensors[id].value;
  switch (id) {
    case IAT: return tempToVolts(v, iatVolts, iatTemps, IAT_N);
    case ECT: return tempToVolts(v, ectVolts, ectTemps, ECT_N);
    case O2:  return v / 1000.0f;
    case MAP: return (v - 10) * 4.21f / 90.0f;  // 0 V = 10 kPa, 4.21 V = 100 kPa.
    case TPS: return v * 4.25f / 100.0f;        // 0 V = 0 %, 4.25 V = 100 %.
  }
  return 0;
}

// AFR the Mega shows for an O2 voltage: 0.1 V = 15.2:1, 0.9 V = 14.0:1.
float o2ToAfr(float v) { return 15.2f + (v - 0.1f) * (14.0f - 15.2f) / 0.8f; }

uint8_t duty[SENSOR_COUNT];  // PWM duty now on each pin.

// Output volts for a duty, as the filter will give them.
float dutyVolts(uint8_t id) { return duty[id] * sensors[id].fullScaleV / 255.0f; }

void applyOutput(uint8_t id) {
  const Sensor& s = sensors[id];
  if (s.fault == FAULT_HIGH) duty[id] = 255;      // Above every high ERR limit.
  else if (s.fault == FAULT_LOW) duty[id] = 0;    // Below every low ERR limit.
  else duty[id] = uint8_t(constrain(lroundf(targetVolts(id) / s.fullScaleV * 255.0f), 0L, 255L));
  analogWrite(s.pin, duty[id]);

  Serial.print(F("# ")); Serial.print(s.name); Serial.print(' ');
  if (s.fault == FAULT_HIGH) Serial.print(F("FAULT HIGH"));
  else if (s.fault == FAULT_LOW) Serial.print(F("FAULT LOW"));
  else if (id == O2) { Serial.print(s.value / 1000.0f, 2); Serial.print(F(" V, AFR ")); Serial.print(o2ToAfr(s.value / 1000.0f), 1); }
  else { Serial.print(s.value); Serial.print(id == MAP ? F(" kPa") : id == TPS ? F(" %") : F(" C")); }
  Serial.print(F(" -> ")); Serial.print(dutyVolts(id), 3);
  Serial.print(F(" V (duty ")); Serial.print(duty[id]); Serial.println(')');
}

// ============================== Encoder =====================================
// Quadrature decoding in the A2 (A) / A1 (B) pin-change interrupt (PCINT1).
const int8_t ENC_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
volatile uint8_t encPrev = 0;
volatile int8_t encAcc = 0, encClicks = 0;

inline uint8_t encRead() { return (((PINC >> PC2) & 1) << 1) | ((PINC >> PC1) & 1); }

ISR(PCINT1_vect) {
  const uint8_t now = encRead();
  encAcc += ENC_TABLE[(encPrev << 2) | now];
  encPrev = now;
  if (encAcc >= STEPS_PER_CLICK) { encAcc = 0; if (encClicks < 100) encClicks++; }
  else if (encAcc <= -STEPS_PER_CLICK) { encAcc = 0; if (encClicks > -100) encClicks--; }
}

void encoderBegin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  delay(1);
  encPrev = encRead();
  PCMSK1 |= _BV(PCINT10) | _BV(PCINT9);  // A2, A1.
  PCICR |= _BV(PCIE1);
}

uint8_t selected = IAT;
bool lcdDirty = true;

void updateEncoder(uint32_t now) {
  static uint32_t lastClickMs = 0;
  int8_t clicks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { clicks = encClicks; encClicks = 0; }
  if (!clicks) return;
  if (ENC_REVERSE) clicks = -clicks;
  const bool fast = clicks > 1 || clicks < -1 || now - lastClickMs < FAST_MS;
  lastClickMs = now;

  Sensor& s = sensors[selected];
  if (s.fault != FAULT_NONE) {  // Turning clears a fault and goes back to the value.
    s.fault = FAULT_NONE;
  } else {
    s.value = constrain(s.value + clicks * (fast ? s.fastStep : s.step), s.minVal, s.maxVal);
  }
  applyOutput(selected);
  lcdDirty = true;
}

// Short press (on release): next sensor. Long press (while held): next fault state.
void updateButton(uint32_t now) {
  static bool stable = false, lastRaw = false, longDone = false;
  static uint32_t changedMs = 0, pressedMs = 0;
  const bool raw = digitalRead(PIN_ENC_SW) == LOW;
  if (raw != lastRaw) { lastRaw = raw; changedMs = now; }
  if (now - changedMs >= DEBOUNCE_MS && raw != stable) {
    stable = raw;
    if (stable) { pressedMs = now; longDone = false; }
    else if (!longDone) {
      selected = (selected + 1) % SENSOR_COUNT;
      Serial.print(F("# Selected ")); Serial.println(sensors[selected].name);
      lcdDirty = true;
    }
  }
  if (stable && !longDone && now - pressedMs >= LONG_PRESS_MS) {
    longDone = true;
    Sensor& s = sensors[selected];
    if (s.fault == FAULT_NONE) s.fault = FAULT_HIGH;
    else if (s.fault == FAULT_HIGH && s.hasLowFault) s.fault = FAULT_LOW;
    else s.fault = FAULT_NONE;
    applyOutput(selected);
    lcdDirty = true;
  }
}

// ============================== OBD-II ECU ==================================
// Stored DTCs: a fault sets its DTC at once; the DTC stays stored after the
// fault is gone, until a scan tool sends mode 04. MIL = any DTC stored.
uint8_t storedHigh = 0, storedLow = 0;  // Bit i = sensor i.

void updateStoredDtcs() {
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    const uint8_t bit = 1 << i;
    if (sensors[i].fault == FAULT_HIGH && !(storedHigh & bit)) {
      storedHigh |= bit;
      Serial.print(F("# DTC stored P")); Serial.println(sensors[i].dtcHigh, HEX);
    }
    if (sensors[i].fault == FAULT_LOW && !(storedLow & bit)) {
      storedLow |= bit;
      Serial.print(F("# DTC stored P")); Serial.println(sensors[i].dtcLow, HEX);
    }
  }
}

// ----------------------- Range and engine checks ----------------------------
// Besides the circuit faults above, the ECU watches whether the values stay
// in their NORMAL range and inside their SAFE limit:
//   outside normal        -> PENDING DTC (mode 07), no MIL
//   beyond the safe limit -> CONFIRMED DTC (mode 03) + MIL, stored until mode 04
// A level must hold for RANGE_CONFIRM_MS (no DTC while sweeping the knob).
// A check is skipped while one of its sensors has a circuit fault.
constexpr uint16_t RANGE_CONFIRM_MS = 1000;
constexpr int16_t ECT_NORMAL_MAX = 105, ECT_SAFE_MAX = 115;    // deg C  -> P0217 engine overheat
constexpr int16_t IAT_NORMAL_MAX = 45,  IAT_SAFE_MAX = 50;     // deg C  -> P0111 IAT range
constexpr int16_t O2_LEAN_NORMAL = 200, O2_LEAN_SAFE = 100;    // mV, below -> P0171 too lean
constexpr int16_t O2_RICH_NORMAL = 800, O2_RICH_SAFE = 900;    // mV, above -> P0172 too rich
constexpr int16_t MAP_IDLE_TPS_MAX = 5;                        // %, throttle closed for the MAP check
constexpr int16_t MAP_IDLE_NORMAL = 60, MAP_IDLE_SAFE = 80;    // kPa at closed throttle -> P0106 MAP range
constexpr int16_t TPS_NOLOAD_MAP_MAX = 30;                     // kPa, no engine load for the TPS check
constexpr int16_t TPS_NOLOAD_NORMAL = 50, TPS_NOLOAD_SAFE = 80;// % with no load -> P0121 TPS range

enum CheckId : uint8_t { CHK_OVERHEAT, CHK_IAT, CHK_LEAN, CHK_RICH, CHK_MAP, CHK_TPS, CHECK_COUNT };
struct RangeCheck {
  uint16_t dtc;
  const char* text;
  uint8_t sensor;  // Sensor marked on the LCD.
  uint8_t level;   // Accepted level: 0 normal, 1 outside normal, 2 beyond safe.
  uint8_t candidate;
  uint32_t sinceMs;
};
RangeCheck checks[CHECK_COUNT] = {
  {0x0217, "engine overheat", ECT, 0, 0, 0},
  {0x0111, "IAT range",       IAT, 0, 0, 0},
  {0x0171, "system too lean", O2,  0, 0, 0},
  {0x0172, "system too rich", O2,  0, 0, 0},
  {0x0106, "MAP range",       MAP, 0, 0, 0},
  {0x0121, "TPS range",       TPS, 0, 0, 0},
};
uint8_t confirmedChecks = 0;  // Bit c = check c confirmed (latched until mode 04).

bool ok(uint8_t id) { return sensors[id].fault == FAULT_NONE; }
uint8_t above(int16_t v, int16_t normal, int16_t safe) { return v > safe ? 2 : v > normal ? 1 : 0; }
uint8_t below(int16_t v, int16_t normal, int16_t safe) { return v < safe ? 2 : v < normal ? 1 : 0; }

// Level the values give now, or 0xFF when the check can't run (circuit fault).
uint8_t evaluate(uint8_t c) {
  const int16_t ect = sensors[ECT].value, iat = sensors[IAT].value, o2 = sensors[O2].value;
  const int16_t map = sensors[MAP].value, tps = sensors[TPS].value;
  switch (c) {
    case CHK_OVERHEAT: return ok(ECT) ? above(ect, ECT_NORMAL_MAX, ECT_SAFE_MAX) : 0xFF;
    case CHK_IAT:      return ok(IAT) ? above(iat, IAT_NORMAL_MAX, IAT_SAFE_MAX) : 0xFF;
    case CHK_LEAN:     return ok(O2) ? below(o2, O2_LEAN_NORMAL, O2_LEAN_SAFE) : 0xFF;
    case CHK_RICH:     return ok(O2) ? above(o2, O2_RICH_NORMAL, O2_RICH_SAFE) : 0xFF;
    case CHK_MAP:
      if (!ok(MAP) || !ok(TPS)) return 0xFF;
      return tps <= MAP_IDLE_TPS_MAX ? above(map, MAP_IDLE_NORMAL, MAP_IDLE_SAFE) : 0;
    case CHK_TPS:
      if (!ok(MAP) || !ok(TPS)) return 0xFF;
      return map <= TPS_NOLOAD_MAP_MAX ? above(tps, TPS_NOLOAD_NORMAL, TPS_NOLOAD_SAFE) : 0;
  }
  return 0;
}

void updateRangeChecks(uint32_t now) {
  for (uint8_t c = 0; c < CHECK_COUNT; c++) {
    RangeCheck& k = checks[c];
    const uint8_t level = evaluate(c);
    if (level == 0xFF) continue;  // Monitor disabled: keep the last result.
    if (level != k.candidate) { k.candidate = level; k.sinceMs = now; }
    if (k.candidate == k.level || now - k.sinceMs < RANGE_CONFIRM_MS) continue;
    k.level = k.candidate;
    lcdDirty = true;
    Serial.print(F("# P")); Serial.print(k.dtc < 0x1000 ? F("0") : F("")); Serial.print(k.dtc, HEX);
    Serial.print(' '); Serial.print(k.text);
    if (k.level == 2) {
      if (!(confirmedChecks & (1 << c))) { confirmedChecks |= 1 << c; Serial.println(F(": beyond SAFE limit, DTC CONFIRMED, MIL on")); }
      else Serial.println(F(": beyond SAFE limit again (already stored)"));
    }
    else if (k.level == 1) Serial.println(F(": outside NORMAL range, DTC PENDING"));
    else Serial.println(F(": back to normal"));
  }
}

uint8_t pendingMask() {
  uint8_t m = 0;
  for (uint8_t c = 0; c < CHECK_COUNT; c++) if (checks[c].level >= 1) m |= 1 << c;
  return m;
}

uint8_t bitCount(uint8_t v) { uint8_t n = 0; for (; v; v >>= 1) n += v & 1; return n; }

uint8_t storedCount() {
  uint8_t n = bitCount(confirmedChecks);
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) n += ((storedHigh >> i) & 1) + ((storedLow >> i) & 1);
  return n;
}

// Value the ECU reports: the set value, or the fail-safe one during a fault.
int16_t reported(uint8_t id) { return sensors[id].fault != FAULT_NONE ? sensors[id].failSafe : sensors[id].value; }

MCP_CAN can(PIN_CAN_CS);
bool canReady = false;
char canState = '-';           // LCD: 'C' scan tool active, '.' CAN OK / idle, '-' no MCP2515.
uint32_t lastRequestMs = 0;
bool testerSeen = false;

void canBegin() {
  canReady = can.begin(MCP_ANY, CAN_500KBPS, CAN_CRYSTAL) == CAN_OK;
  if (canReady && can.setMode(MCP_NORMAL) != MCP2515_OK) {
    Serial.println(F("# CAN MCP2515 did not enter NORMAL mode, retrying"));
    canReady = false;
  }
  Serial.println(canReady ? F("# CAN MCP2515 OK, 500 kbps, 8 MHz crystal, OBD-II ECU on 0x7E8")
                          : F("# CAN MCP2515 not found (check CS D8, SI D11, SO D12, SCK D13, 5V)"));
}

bool canSend(uint8_t* frame) { return can.sendMsgBuf(OBD_RESPONSE_ID, 0, 8, frame) == CAN_OK; }

// Waits for the scan tool's flow control (0x30 = continue) on 0x7E0.
bool waitFlowControl(uint8_t& stMin) {
  const uint32_t start = millis();
  while (millis() - start < FLOW_CONTROL_WAIT_MS) {
    if (can.checkReceive() != CAN_MSGAVAIL) continue;
    uint32_t id;
    uint8_t len, buf[8];
    if (can.readMsgBuf(&id, &len, buf) != CAN_OK) continue;
    if ((id & 0x7FF) == OBD_REQUEST_ID && len >= 3 && buf[0] == 0x30) {
      stMin = buf[2] <= 127 ? buf[2] : 1;
      return true;
    }
  }
  return false;
}

// ISO 15765-2: single frame up to 7 bytes, else first frame + consecutive frames.
void sendResponse(const uint8_t* data, uint8_t n) {
  uint8_t frame[8];
  if (n <= 7) {
    memset(frame, 0x00, 8);
    frame[0] = n;
    memcpy(frame + 1, data, n);
    canSend(frame);
    return;
  }
  frame[0] = 0x10;
  frame[1] = n;
  memcpy(frame + 2, data, 6);
  if (!canSend(frame)) return;
  uint8_t stMin = 0;
  if (!waitFlowControl(stMin)) { Serial.println(F("# OBD no flow control from the scan tool")); return; }
  uint8_t idx = 6, sn = 1;
  while (idx < n) {
    memset(frame, 0x00, 8);
    frame[0] = 0x20 | (sn++ & 0x0F);
    const uint8_t chunk = n - idx < 7 ? n - idx : 7;
    memcpy(frame + 1, data + idx, chunk);
    idx += chunk;
    delay(stMin ? stMin : 1);
    canSend(frame);
  }
}

constexpr uint8_t SUPPORTED_PIDS[] = {0x01, 0x05, 0x0B, 0x0F, 0x11, 0x14};

void handleRequest(const uint8_t* buf, uint8_t len) {
  if (len < 2 || (buf[0] & 0xF0) != 0x00) return;  // Single-frame requests only.
  const uint8_t n = buf[0] & 0x0F;
  if (n < 1 || n > 7) return;
  const uint8_t mode = buf[1];
  uint8_t r[2 + 2 * (2 * SENSOR_COUNT + CHECK_COUNT)];
  uint8_t rn = 0;

  if (mode == 0x01 && n >= 2) {
    const uint8_t pid = buf[2];
    r[0] = 0x41; r[1] = pid;
    switch (pid) {
      case 0x00: {
        uint32_t bits = 0;
        for (uint8_t p : SUPPORTED_PIDS) bits |= 1UL << (32 - p);
        r[2] = bits >> 24; r[3] = bits >> 16; r[4] = bits >> 8; r[5] = bits;
        rn = 6;
        break;
      }
      case 0x01: {
        const uint8_t count = storedCount();
        r[2] = (count ? 0x80 : 0x00) | count;  // MIL + number of DTCs.
        r[3] = 0x00; r[4] = 0x00; r[5] = 0x00;
        rn = 6;
        break;
      }
      case 0x05: r[2] = uint8_t(reported(ECT) + 40); rn = 3; break;
      case 0x0B: r[2] = uint8_t(reported(MAP)); rn = 3; break;
      case 0x0F: r[2] = uint8_t(reported(IAT) + 40); rn = 3; break;
      case 0x11: r[2] = uint8_t((reported(TPS) * 255L + 50) / 100); rn = 3; break;
      case 0x14: r[2] = uint8_t(reported(O2) / 5); r[3] = 0xFF; rn = 4; break;  // A = V x 200, B = FF: no trim.
      default: return;  // Unsupported PID: no answer, as on a real ECU.
    }
  } else if (mode == 0x03) {
    r[0] = 0x43;
    r[1] = storedCount();
    rn = 2;
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
      if ((storedHigh >> i) & 1) { r[rn++] = sensors[i].dtcHigh >> 8; r[rn++] = sensors[i].dtcHigh & 0xFF; }
      if ((storedLow >> i) & 1) { r[rn++] = sensors[i].dtcLow >> 8; r[rn++] = sensors[i].dtcLow & 0xFF; }
    }
    for (uint8_t c = 0; c < CHECK_COUNT; c++)
      if ((confirmedChecks >> c) & 1) { r[rn++] = checks[c].dtc >> 8; r[rn++] = checks[c].dtc & 0xFF; }
  } else if (mode == 0x07) {  // Pending DTCs: range checks outside normal now.
    const uint8_t m = pendingMask();
    r[0] = 0x47;
    r[1] = bitCount(m);
    rn = 2;
    for (uint8_t c = 0; c < CHECK_COUNT; c++)
      if ((m >> c) & 1) { r[rn++] = checks[c].dtc >> 8; r[rn++] = checks[c].dtc & 0xFF; }
  } else if (mode == 0x04) {
    storedHigh = storedLow = 0;  // Active faults set theirs again on the next loop.
    confirmedChecks = 0;         // A check still beyond safe confirms again on the next loop.
    for (uint8_t c = 0; c < CHECK_COUNT; c++)
      if (checks[c].level == 2) confirmedChecks |= 1 << c;
    Serial.println(F("# DTCs cleared by the scan tool"));
    r[0] = 0x44;
    rn = 1;
  } else {
    return;  // Other modes: not supported.
  }
  sendResponse(r, rn);
}

void updateCan(uint32_t now) {
  static uint32_t lastRetryMs = 0;
  if (!canReady) {
    if (now - lastRetryMs >= CAN_RETRY_MS) {
      lastRetryMs = now;
      canBegin();
      for (uint8_t i = 0; i < SENSOR_COUNT; i++) applyOutput(i);  // SPI start-up can touch D10.
    }
    if (canState != '-') { canState = '-'; lcdDirty = true; }
    return;
  }

  static uint32_t rxFrames = 0, rxLastId = 0;
  while (digitalRead(PIN_CAN_INT) == LOW || can.checkReceive() == CAN_MSGAVAIL) {
    uint32_t id;
    uint8_t len, buf[8];
    if (can.readMsgBuf(&id, &len, buf) != CAN_OK) break;
    rxFrames++; rxLastId = id;
    id &= 0x7FF;
    if (id != OBD_FUNCTIONAL_ID && id != OBD_REQUEST_ID) continue;
    lastRequestMs = now;
    if (!testerSeen) { testerSeen = true; Serial.println(F("# OBD scan tool connected")); }
    handleRequest(buf, len);
  }

  const bool active = testerSeen && now - lastRequestMs < TESTER_ACTIVE_MS;
  if (testerSeen && !active) { testerSeen = false; Serial.println(F("# OBD scan tool gone")); }
  // Bus health on the Serial Monitor whenever the MCP2515 error counters change.
  static uint32_t lastHealthMs = 0;
  static uint8_t lastTec = 0, lastRec = 0, lastEflg = 0;
  if (now - lastHealthMs >= 1000) {
    lastHealthMs = now;
    const uint8_t tec = can.errorCountTX(), rec = can.errorCountRX(), eflg = can.getError();
    if (tec != lastTec || rec != lastRec || eflg != lastEflg) {
      lastTec = tec; lastRec = rec; lastEflg = eflg;
      char line[72];
      snprintf(line, sizeof(line), "# CAN bus TEC %u REC %u EFLG %02X RX %lu last %03lX", tec, rec, eflg, rxFrames, rxLastId);
      Serial.println(line);
    }
  }

  const char state = active ? 'C' : '.';
  if (state != canState) { canState = state; lcdDirty = true; }
}

// ========================= Display (16x2 I2C LCD) ===========================
//   IAT  25'C 2.300V   <- selected sensor: value (or HIGH / LOW fault) and output volts
//   [I] E  O  M  T C   <- [ ] = selected, ! = circuit fault or beyond safe, * = outside normal,
//                         last char: C = scan tool connected, . = CAN idle, - = no MCP2515
// LiquidCrystal_I2C library (Frank de Brabander) at the fixed address LCD_ADDR.
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
char lcdShown[2][17];

void lcdBegin() {
  Wire.begin();
  Wire.setWireTimeout(3000, true);  // A wiring fault on A4 / A5 can't freeze the simulator.
  Wire.beginTransmission(LCD_ADDR);
  const uint8_t ack = Wire.endTransmission();
  Serial.print(F("# LCD 0x")); Serial.print(LCD_ADDR, HEX);
  Serial.println(ack == 0 ? F(" answered") : F(" did NOT answer"));
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print(F("ECU SIMULATOR"));
  lcd.setCursor(0, 1); lcd.print(F("OBD-II CAN 500k"));
  delay(1200);
  lcd.clear();
  memset(lcdShown, ' ', sizeof(lcdShown));  // Matches the cleared screen.
  lcdDirty = true;
}

// Writes only the characters that changed, so the LCD does not flicker.
void lcdLine(uint8_t row, const char* text) {
  char line[17];
  snprintf(line, sizeof(line), "%-16s", text);
  for (uint8_t c = 0; c < 16; c++) {
    if (lcdShown[row][c] != line[c]) {
      lcd.setCursor(c, row);
      lcd.write(uint8_t(line[c]));
      lcdShown[row][c] = line[c];
    }
  }
}

void drawLcd() {
  if (!lcdDirty) return;
  lcdDirty = false;
  const Sensor& s = sensors[selected];

  char value[8];
  if (s.fault == FAULT_HIGH) strcpy(value, " HIGH ");
  else if (s.fault == FAULT_LOW) strcpy(value, "  LOW ");
  else if (selected == O2) {  // AFR the dashboard will show.
    const int afr10 = int(lroundf(o2ToAfr(s.value / 1000.0f) * 10));
    snprintf(value, sizeof(value), "%2d.%d:1", afr10 / 10, afr10 % 10);
  }
  else if (selected == MAP) snprintf(value, sizeof(value), "%3dkPa", s.value);
  else if (selected == TPS) snprintf(value, sizeof(value), "%5d%%", s.value);
  else snprintf(value, sizeof(value), "%4d\xDF" "C", s.value);  // 0xDF = degree sign on the HD44780.

  const int mv = int(lroundf(dutyVolts(selected) * 1000));
  char line[17];
  snprintf(line, sizeof(line), "%s%s %d.%03dV", s.name, value, mv / 1000, mv % 1000);
  lcdLine(0, line);

  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    uint8_t level = 0;  // Worst range check on this sensor.
    for (uint8_t c = 0; c < CHECK_COUNT; c++)
      if (checks[c].sensor == i && checks[c].level > level) level = checks[c].level;
    line[i * 3] = i == selected ? '[' : ' ';
    line[i * 3 + 1] = sensors[i].letter;
    line[i * 3 + 2] = i == selected ? ']'
                    : sensors[i].fault != FAULT_NONE || level == 2 ? '!'
                    : level == 1 ? '*' : ' ';
  }
  line[15] = canState;
  line[16] = '\0';
  lcdLine(1, line);
}

// ============================== Main ========================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("# NANO ECU SIMULATOR, OBD-II (IAT D5, ECT D6, O2 D9, MAP D10, TPS D3, CAN CS D8)"));

  pinMode(PIN_CAN_INT, INPUT_PULLUP);
  canBegin();  // Before the PWM outputs: SPI start-up sets D10 (SS).

  for (uint8_t i = 0; i < SENSOR_COUNT; i++) pinMode(sensors[i].pin, OUTPUT);
  // Timer1 (D9, D10) and Timer2 (D3) to 31.4 kHz for low ripple.
  // Timer0 (D5, D6) stays at 980 Hz because millis() depends on it.
  TCCR1B = (TCCR1B & 0xF8) | 0x01;
  TCCR2B = (TCCR2B & 0xF8) | 0x01;
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) applyOutput(i);

  encoderBegin();
  lcdBegin();
}

void loop() {
  const uint32_t now = millis();
  updateEncoder(now);
  updateButton(now);
  updateStoredDtcs();
  updateRangeChecks(now);
  updateCan(now);
  drawLcd();
}
