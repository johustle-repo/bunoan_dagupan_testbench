// Nano Sensor Simulator
//
// Replaces the five sensor knobs of the ECU testbench (IAT, ECT, O2, MAP, TPS).
// Each sensor is a PWM output with an RC filter, giving a steady 0-5 V into
// the Mega's A0-A4. One rotary encoder sets them, a 16x2 I2C LCD shows them.
// The values are set in real units (deg C, kPa, %), converted back to volts
// with the same formulas the Mega and ESP32 use (SensorConversion.h).
// The five values also go out on CAN (MCP2515, 500 kbps) every 100 ms to the
// gateway Nano (NanoCanGateway), which forwards them to the ESP32.
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
//   D2   IN   MCP2515 INT (optional here, only sending)
//   A2   IN   Encoder A / CLK
//   A1   IN   Encoder B / DT
//   A0   IN   Encoder button / SW (to GND = pressed)
//   A4   I/O  LCD SDA
//   A5   OUT  LCD SCL
//   GND       common ground with the Mega
//
// CAN frame, ID 0x100, 8 bytes:
//   0 IAT deg C + 40   1 ECT deg C + 40   2 O2 V x 100   3 MAP kPa   4 TPS %
//   5 HIGH-fault bits  6 LOW-fault bits (bit 0 IAT ... bit 4 TPS)   7 counter
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
constexpr uint32_t CAN_ID_SENSORS = 0x100;
constexpr uint16_t CAN_PERIOD_MS = 100;
constexpr uint16_t CAN_RETRY_MS = 2000;   // Re-try MCP2515 start-up this often if it failed.
constexpr uint8_t CAN_CRYSTAL = MCP_8MHZ; // Crystal on the MCP2515 module ("8.000").

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
};

Sensor sensors[SENSOR_COUNT] = {
  // name  ltr  pin  full scale       min  max   step fast  start  lowFault
  {"IAT", 'I', 5,  NANO_VCC,         15,  55,    1,   5,   25,   true,  FAULT_NONE},  // deg C
  {"ECT", 'E', 6,  NANO_VCC,         20,  120,   1,   5,   90,   true,  FAULT_NONE},  // deg C
  {"O2 ", 'O', 9,  O2_FULL_SCALE_V,  20,  1200,  10,  50,  450,  true,  FAULT_NONE},  // mV
  {"MAP", 'M', 10, NANO_VCC,         10,  100,   1,   5,   35,   false, FAULT_NONE},  // kPa
  {"TPS", 'T', 3,  NANO_VCC,         0,   100,   1,   5,   0,    false, FAULT_NONE},  // %
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

// ============================== CAN (MCP2515) ===============================
// Sends frame CAN_ID_SENSORS every CAN_PERIOD_MS. canState is shown in the
// last LCD character: 'C' = frames acknowledged by the gateway, 'x' = sent but
// no node acknowledged (gateway off / CANH-CANL / 120 ohm), '-' = no MCP2515.
MCP_CAN can(PIN_CAN_CS);
bool canReady = false;
char canState = '-';

void canBegin() {
  canReady = can.begin(MCP_ANY, CAN_500KBPS, CAN_CRYSTAL) == CAN_OK;
  if (canReady) can.setMode(MCP_NORMAL);
  Serial.println(canReady ? F("# CAN MCP2515 OK, 500 kbps, 8 MHz crystal")
                          : F("# CAN MCP2515 not found (check CS D8, SI D11, SO D12, SCK D13, 5V)"));
}

void updateCan(uint32_t now) {
  static uint32_t lastMs = 0, lastRetryMs = 0;
  static uint8_t counter = 0;
  if (!canReady) {
    if (now - lastRetryMs >= CAN_RETRY_MS) {
      lastRetryMs = now;
      canBegin();
      for (uint8_t i = 0; i < SENSOR_COUNT; i++) applyOutput(i);  // SPI start-up can touch D10.
    }
    if (canState != '-') { canState = '-'; lcdDirty = true; }
    return;
  }
  if (now - lastMs < CAN_PERIOD_MS) return;
  lastMs = now;

  uint8_t frame[8];
  frame[0] = uint8_t(sensors[IAT].value + 40);
  frame[1] = uint8_t(sensors[ECT].value + 40);
  frame[2] = uint8_t(sensors[O2].value / 10);
  frame[3] = uint8_t(sensors[MAP].value);
  frame[4] = uint8_t(sensors[TPS].value);
  frame[5] = frame[6] = 0;
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    if (sensors[i].fault == FAULT_HIGH) frame[5] |= 1 << i;
    if (sensors[i].fault == FAULT_LOW) frame[6] |= 1 << i;
  }
  frame[7] = counter++;

  const char state = can.sendMsgBuf(CAN_ID_SENSORS, 0, 8, frame) == CAN_OK ? 'C' : 'x';
  if (state != canState) {
    canState = state;
    lcdDirty = true;
    Serial.println(state == 'C' ? F("# CAN frames acknowledged") : F("# CAN no ACK (gateway off, CANH/CANL, 120 ohm?)"));
  }
}

// ========================= Display (16x2 I2C LCD) ===========================
//   IAT  25'C 2.300V   <- selected sensor: value (or HIGH / LOW fault) and output volts
//   [I] E  O  M  T C   <- [ ] = selected, ! = sensor in fault, last char = CAN state
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
  lcd.setCursor(0, 0); lcd.print(F("SENSOR SIMULATOR"));
  lcd.setCursor(0, 1); lcd.print(F("IAT ECT O2 MAP T"));
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
    line[i * 3] = i == selected ? '[' : ' ';
    line[i * 3 + 1] = sensors[i].letter;
    line[i * 3 + 2] = i == selected ? ']' : sensors[i].fault != FAULT_NONE ? '!' : ' ';
  }
  line[15] = canState;
  line[16] = '\0';
  lcdLine(1, line);
}

// ============================== Main ========================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("# NANO SENSOR SIMULATOR (IAT D5, ECT D6, O2 D9, MAP D10, TPS D3, CAN CS D8)"));

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
  updateCan(now);
  drawLcd();
}
