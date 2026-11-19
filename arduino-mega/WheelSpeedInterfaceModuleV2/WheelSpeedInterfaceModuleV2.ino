// Wheel Speed Interface Module (WSIM) v2 — Mega 2560 Pro (ATmega2560, 16 MHz)
//
// Straight port of the Nano WSIM (arduino-nano/WheelSpeedInterfaceModule).
// D2..D12 keep the same jobs as on the Nano. Moved on the Mega:
//   * 4-digit display  D13/A5  -> D22/D23
//   * Encoder          A0..A2  -> A8..A10   (port K has pin-change interrupts)
//   * Generator out    A3/A4   -> D46/D45   (Timer5 OC5A/OC5B hardware toggle)
//
// Implements the hand-drawn "NANO" signal-module sketch:
//   * Brake switch / abrupt deceleration -> D6 held 13 s, D7 pulse bursts (only at 10 km/h or more)
//   * Front FREQ >= 60 Hz (10 km/h)      -> LOCK output on D12, latched until power-off,
//                                           and D6 turns on once for 1 s (no D7)
//   * Front FREQ / 60 (RPM), / 6 (speed display)
//   * Front and rear FREQ passed through 1:1
//   * Rear FREQ higher than front        -> blinking TCS output
//
// v2 ARMING: two momentary buttons (D24, D25) must each be pressed once after
// power-up. Each press latches its condition (LED on D26 / D27) until
// power-off, in any order. Until BOTH are latched the WSIM is blocked: D4,
// D6..D12 stay LOW (no D6/D7 even above 10 km/h, LOCK/ABS light D12 off) and
// the display shows "----". The test generator keeps working.
//
// Everything marked "ASSUMPTION" is not in the drawing and can be changed in
// the CONFIG section below.
//
// ---------------------------------------------------------------- PINOUT ----
//   D2  IN   FREQ input #1 (front)   INT4 (PE4)
//   D3  IN   FREQ input #2 (rear)    INT5 (PE5)
//   D4  OUT  TCS blinking output
//   D5  IN   Brake switch            INPUT_PULLUP, switch to GND = pressed
//   D6  OUT  Logic 1 for 13 s on brake event, starting 0.5 s after the trigger;
//            also a 1 s pulse once when LOCK latches at 10 km/h
//   D7  OUT  10 pulses/s with 0.5 s interruption, 2 s .. 10 s of D6 time;
//            also ON for the first 1 s after power-up (before arming)
//   D8  OUT  FREQ output #1 (1:1 of front)
//   D9  OUT  FREQ output #2 (1:1 of rear)
//   D10 OUT  FREQ output / 60  (RPM)
//   D11 OUT  FREQ output / 6   (speed display)
//   D12 OUT  LOCK: LOW until LOCK latches, then HIGH until power-off
//   D22 OUT  4-digit display CLK (TM1637), shows front speed in km/h
//   D23 I/O  4-digit display DIO (TM1637); display VCC -> 5V, GND -> GND
//   D13      free (onboard LED, kept LOW)
//   D20 I/O  0.96" OLED SDA (SSD1306 128x64 I2C, addr 0x3C or 0x3D)
//   D21 OUT  0.96" OLED SCL; OLED VCC -> 5V, GND -> GND
//            Shows FRONT and REAR speed in km/h (and Hz).
//   D28 OUT  Relay (module IN or transistor): clicks like a motor, front
//            speed 0..250 km/h -> 0..30 clicks/s. Silent when stopped or not armed.
//
//   Arming (both needed before the WSIM works):
//   D24 IN   Condition 1 button      INPUT_PULLUP, momentary to GND
//   D25 IN   Condition 2 button      INPUT_PULLUP, momentary to GND
//   D26 OUT  Condition 1 LED         HIGH once button 1 was pressed
//   D27 OUT  Condition 2 LED         HIGH once button 2 was pressed
//
//   Test generator (EC11 / KY-040 rotary encoder):
//   A8  IN   Encoder A / CLK         PCINT16 (PK0)
//   A9  IN   Encoder B / DT          PCINT17 (PK1)
//   A10 IN   Encoder button / SW     INPUT_PULLUP, pressed = LOW
//   D46 OUT  Generated FRONT frequency 0-1500 Hz  -> jumper to D2  (OC5A, PL3)
//   D45 OUT  Generated REAR  frequency 0-1500 Hz  -> jumper to D3  (OC5B, PL4)
//   Remove the two jumpers when real wheel sensors are connected to D2 / D3.
//
//   Knob use:
//     NORMAL mode : turn -> FRONT and REAR change together (always equal)
//     press       -> REAR mode : turn -> only REAR changes, FRONT stays
//     press again -> back to NORMAL, REAR snaps back equal to FRONT
// ----------------------------------------------------------------------------

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =============================== CONFIG =====================================

// --- Brake event (D6 / D7) -------------------------------------------------
// Drawing: "if brake sw. pressed OR when speed is abruptly decrease".
// Set to true to require BOTH (brake pressed AND abrupt decel) instead.
const bool     BRAKE_REQUIRES_DECEL   = false;

// "Sudden drop" = front speed falls by at least DECEL_DROP_KPH below the
// highest speed of the last DECEL_WINDOW_MS. 5 km/h in 300 ms ~= 16.7 km/h/s
// ~= 4.6 m/s^2. Checked every 100 ms, so it reacts within ~0.1 s.
const float    DECEL_DROP_KPH    = 5.0;
const uint16_t DECEL_WINDOW_MS   = 300;    // multiple of SAMPLE_MS (100 ms)

// D6 / D7 only start when the speed is >= this, in whole km/h as shown on the
// displays (Hz / 6, rounded; the measured 60 Hz is often 59.99 Hz).
//   * Sudden drop: the speed BEFORE the drop (peak of the window) must be
//     >= 10, so a drop from 40 to 0 km/h still triggers.
//   * Brake switch: the current speed must be >= 10.
const int16_t  EVENT_MIN_KPH     = 10;
const uint32_t D6_DELAY_MS       = 500;    // D6 turns on this long after the trigger
const uint32_t D6_HOLD_MS        = 13000;  // drawing: logic 1 for 13 s
const uint32_t D7_START_MS       = 2000;   // drawing text: starts after 2 s
const uint32_t D7_END_MS         = 10000;  // drawing: ends at 10 s
const uint32_t D7_BURST_MS       = 1000;   // 10 pulses at 10 pulses/s
const uint32_t D7_GAP_MS         = 500;    // drawing: interruption of .5 s
const uint32_t D7_PULSE_PERIOD_MS = 100;   // 10 pulses/s
const uint32_t D7_PULSE_HIGH_MS   = 50;    // ASSUMPTION: 50% duty
const uint32_t BOOT_D7_ON_MS      = 1000;  // D7 on for 1 s at power-up, then off

// --- LOCK (D12) ------------------------------------------------------------
const int16_t  LOCK_KPH          = 10;     // drawing: 60 Hz; D12 + D6 pulse at >= 10 km/h shown
const uint32_t LOCK_CONFIRM_MS   = 200;    // ASSUMPTION: must hold 200 ms
const uint32_t LOCK_D6_PULSE_MS  = 1000;   // D6 on for 1 s when LOCK latches
// D12 is LOW at power-up and goes HIGH only once LOCK is active.

// --- TCS (D4) --------------------------------------------------------------
// ASSUMPTION: "rear higher than front" = rear exceeds front by this margin.
const float    TCS_MARGIN        = 0.10;   // 10 %
const float    TCS_MIN_REAR_HZ   = 30.0;   // ASSUMPTION: ignore below 5 km/h
const uint32_t TCS_CONFIRM_MS    = 200;    // ASSUMPTION: slip must last 200 ms
const uint32_t TCS_BLINK_HALF_MS = 250;    // ASSUMPTION: 2 Hz blink

// --- Dividers (D10 / D11) --------------------------------------------------
// Output toggles every N/2 rising edges -> square wave at input / N.
const uint8_t  DIV_RPM_HALF      = 30;     // / 60
const uint8_t  DIV_SPEED_HALF    = 3;      // / 6

// --- Test generator / rotary encoder (A8..A10, D45/D46) --------------------
const uint16_t GEN_MAX_HZ        = 1500;   // 250 km/h
const uint16_t GEN_STEP_HZ       = 6;      // 1 km/h per click
const uint16_t GEN_FAST_STEP_HZ  = 30;     // 5 km/h per click when spun fast
const uint32_t GEN_FAST_MS       = 40;     // clicks closer than this = fast
const int8_t   ENC_STEPS_PER_CLICK = 4;    // EC11 / KY-040: 4 transitions/detent
const bool     ENC_REVERSE       = false;  // true if clockwise goes down

// --- 4-digit display (TM1637 on D22 / D23) ---------------------------------
const uint8_t  DISP_BRIGHTNESS   = 4;      // 0 (dim) .. 7 (bright)
const uint16_t DISP_UPDATE_MS    = 200;    // speed refresh rate

// --- 0.96" OLED (SSD1306 128x64 on I2C D20 / D21) ---------------------------
const uint16_t OLED_UPDATE_MS    = 200;    // redraw rate
const uint8_t  OLED_CHUNKS_PER_LOOP = 2;   // 16-byte I2C chunks sent per loop pass

// --- Speed relay (D28) -----------------------------------------------------
// Click rate maps linearly 0..250 km/h -> 0..30 clicks/s:
//   clicks/s = front km/h / RELAY_FULL_KPH * RELAY_MAX_CLICK_HZ
//   -> 50 km/h = 6/s, 125 km/h = 15/s, 250 km/h = 30/s
const float    RELAY_FULL_KPH    = 250.0;  // speed that gives the max rate
const float    RELAY_MAX_CLICK_HZ = 30.0;  // clicks/s at RELAY_FULL_KPH
const float    RELAY_MIN_KPH     = 1.0;    // below this the relay is silent
const uint8_t  RELAY_JITTER_PCT  = 8;      // random +/- per click (engine lope), 0 = exact
const uint16_t RELAY_ON_MAX_MS   = 25;     // click ON time, 50 % duty ...
const uint16_t RELAY_ON_MIN_MS   = 12;     // ... but never below relay pull-in
const bool     RELAY_ACTIVE_LOW  = false;  // true for relay modules that switch on LOW

// --- Arming buttons (D24 / D25) --------------------------------------------
const uint16_t ARM_DEBOUNCE_MS   = 30;     // press must be stable this long

// --- Debug -----------------------------------------------------------------
#define DEBUG_SERIAL 1                     // 0 to disable serial output

// ============================== PINS ========================================
// Mega port registers used by the ISRs (Nano used PORTD / PORTB / PORTC):
//   D2 PE4, D3 PE5, D8 PH5, D9 PH6, D10 PB4, D11 PB5, D45 PL4, D46 PL3,
//   A8 PK0, A9 PK1.
const uint8_t PIN_FRONT_IN = 2;   // PE4, INT4
const uint8_t PIN_REAR_IN  = 3;   // PE5, INT5
const uint8_t PIN_TCS      = 4;
const uint8_t PIN_BRAKE    = 5;
const uint8_t PIN_D6_HOLD  = 6;
const uint8_t PIN_D7_PULSE = 7;
const uint8_t PIN_FRONT_OUT = 8;  // PH5
const uint8_t PIN_REAR_OUT  = 9;  // PH6
const uint8_t PIN_DIV_RPM   = 10; // PB4
const uint8_t PIN_DIV_SPEED = 11; // PB5
const uint8_t PIN_LOCK      = 12;
const uint8_t PIN_LED       = 13; // onboard LED, unused
const uint8_t PIN_DISP_CLK  = 22; // TM1637 CLK
const uint8_t PIN_DISP_DIO  = 23; // TM1637 DIO
const uint8_t PIN_ENC_A     = A8; // PK0, PCINT16
const uint8_t PIN_ENC_B     = A9; // PK1, PCINT17
const uint8_t PIN_ENC_SW    = A10;
const uint8_t PIN_GEN_FRONT = 46; // PL3, OC5A
const uint8_t PIN_GEN_REAR  = 45; // PL4, OC5B
const uint8_t PIN_ARM_BTN[2] = { 24, 25 };  // condition 1 / 2 buttons
const uint8_t PIN_ARM_LED[2] = { 26, 27 };  // condition 1 / 2 LEDs
const uint8_t PIN_RELAY     = 28; // speed relay

// Set once both arming conditions are latched; read by the capture ISRs.
volatile bool armed = false;

// ========================== TEST GENERATOR ==================================
// Timer5 free-runs at 2 MHz (prescaler 8, 0.5 us/tick). Compare A makes the
// FRONT square wave on D46 (OC5A), compare B the REAR one on D45 (OC5B). The
// pin is toggled by the timer HARDWARE at the exact compare match, so ISR
// latency adds no jitter; the ISR only schedules the next match half a period
// later. The two frequencies are fully independent.
//
// Half periods longer than the 16-bit timer (below ~31 Hz) are split into
// chunks: during the intermediate chunks the hardware toggle is disconnected
// (pin held at its level through PORTL), and it is reconnected for the last
// chunk only.
volatile uint32_t genHalfTicks[2] = {0, 0};   // 0 = off (pin held LOW)
uint32_t genRemain[2] = {0, 0};               // ISR-only
bool     genOff[2]    = {true, true};         // ISR-only
uint16_t genFrontHz = 0, genRearHz = 0;

// COM5x1:0 = 01 toggle on match, 10 clear on match, 00 disconnected.
inline void genStep(uint8_t ch, volatile uint16_t &ocr, uint8_t com0, uint8_t com1,
                    uint8_t foc, uint8_t bit) {
  uint32_t half = genHalfTicks[ch];
  if (half == 0) {                     // off: force pin and OC latch LOW
    if (!genOff[ch]) {
      genOff[ch] = true;
      genRemain[ch] = 0;
      TCCR5A = (TCCR5A & ~com0) | com1;          // clear-on-match mode
      TCCR5C = foc;                              // force a match -> OC5x = 0
      TCCR5A &= ~(com0 | com1);                  // disconnect
      PORTL &= ~bit;                             // pin LOW via PORT
    }
    ocr += 20000;                      // check again in 10 ms
    return;
  }
  genOff[ch] = false;
  if (genRemain[ch] == 0) genRemain[ch] = half;  // start of a new half period
  uint32_t r = genRemain[ch];
  uint16_t chunk = r > 65000 ? 40000 : (uint16_t)r;  // last chunk never tiny
  genRemain[ch] = r - chunk;
  ocr += chunk;
  if (genRemain[ch] == 0) {            // next match ends the half period: toggle
    TCCR5A = (TCCR5A & ~com1) | com0;
  } else {                             // intermediate chunk: hold the level
    if (PINL & bit) PORTL |= bit; else PORTL &= ~bit;
    TCCR5A &= ~(com0 | com1);
  }
}

ISR(TIMER5_COMPA_vect) { genStep(0, OCR5A, _BV(COM5A0), _BV(COM5A1), _BV(FOC5A), _BV(PL3)); }
ISR(TIMER5_COMPB_vect) { genStep(1, OCR5B, _BV(COM5B0), _BV(COM5B1), _BV(FOC5B), _BV(PL4)); }

void genSet(uint8_t ch, uint16_t hz) {
  // half period in 0.5 us ticks = 2 MHz / (2 * hz) = 1e6 / hz (rounded)
  uint32_t half = hz ? (1000000UL + hz / 2) / hz : 0;
  noInterrupts();
  genHalfTicks[ch] = half;
  interrupts();
}

void genBegin() {
  pinMode(PIN_GEN_FRONT, OUTPUT); digitalWrite(PIN_GEN_FRONT, LOW);
  pinMode(PIN_GEN_REAR, OUTPUT);  digitalWrite(PIN_GEN_REAR, LOW);
  TCCR5A = 0;                          // normal mode, outputs disconnected
  TCCR5B = _BV(CS51);                  // clk/8
  OCR5A = TCNT5 + 1000;
  OCR5B = TCNT5 + 1500;
  TIFR5 = _BV(OCF5A) | _BV(OCF5B);
  TIMSK5 = _BV(OCIE5A) | _BV(OCIE5B);
}

// ============================ ROTARY ENCODER ================================
const int8_t encTable[16] = { 0, -1,  1,  0,
                              1,  0,  0, -1,
                             -1,  0,  0,  1,
                              0,  1, -1,  0 };
volatile uint8_t encPrev = 3;
volatile int8_t  encAcc = 0;
volatile int8_t  encPending = 0;       // whole clicks not yet applied

ISR(PCINT2_vect) {                     // A8 / A9 changed
  uint8_t s = PINK & 0x03;             // bit1 = B (A9), bit0 = A (A8)
  s = ((s & 1) << 1) | (s >> 1);       // -> A << 1 | B
  if (s == encPrev) return;
  encAcc += encTable[(encPrev << 2) | s];
  encPrev = s;
  if (encAcc >= ENC_STEPS_PER_CLICK)       { if (encPending <  100) encPending++; encAcc = 0; }
  else if (encAcc <= -ENC_STEPS_PER_CLICK) { if (encPending > -100) encPending--; encAcc = 0; }
}

bool     rearOnlyMode = false;         // false = NORMAL (front == rear)
bool     encBtnRaw = false, encBtnState = false;
uint32_t encBtnChangeMs = 0, lastClickMs = 0;

void encBegin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  uint8_t s = PINK & 0x03;
  encPrev = ((s & 1) << 1) | (s >> 1);
  PCMSK2 |= _BV(PCINT16) | _BV(PCINT17); // A8, A9
  PCIFR  = _BV(PCIF2);
  PCICR |= _BV(PCIE2);
}

void applyGenerator() {
  genSet(0, genFrontHz);
  genSet(1, genRearHz);
}

int16_t clampHz(int32_t v) { return v < 0 ? 0 : (v > GEN_MAX_HZ ? GEN_MAX_HZ : v); }

void updateEncoder(uint32_t now) {
  // Button: toggle mode on each debounced press.
  bool raw = digitalRead(PIN_ENC_SW) == LOW;
  if (raw != encBtnRaw) { encBtnRaw = raw; encBtnChangeMs = now; }
  if (now - encBtnChangeMs >= 30 && encBtnRaw != encBtnState) {
    encBtnState = encBtnRaw;
    if (encBtnState) {
      rearOnlyMode = !rearOnlyMode;
      if (!rearOnlyMode) genRearHz = genFrontHz;   // back to equal
      applyGenerator();
#if DEBUG_SERIAL
      Serial.println(rearOnlyMode ? F("# MODE: REAR only") : F("# MODE: NORMAL (front = rear)"));
#endif
    }
  }

  // Rotation.
  noInterrupts();
  int8_t clicks = encPending;
  encPending = 0;
  interrupts();
  if (clicks == 0) return;
  if (ENC_REVERSE) clicks = -clicks;

  uint16_t step = (now - lastClickMs < GEN_FAST_MS) ? GEN_FAST_STEP_HZ : GEN_STEP_HZ;
  lastClickMs = now;
  int32_t delta = (int32_t)clicks * step;

  if (rearOnlyMode) {
    genRearHz = clampHz((int32_t)genRearHz + delta);
  } else {
    genFrontHz = clampHz((int32_t)genFrontHz + delta);
    genRearHz = genFrontHz;
  }
  applyGenerator();
}

// ========================== FREQUENCY CAPTURE ===============================
// ISRs use direct port access so the 1:1 outputs and dividers have minimal lag.
volatile uint32_t frontEdges = 0, frontLastUs = 0;
volatile uint32_t rearEdges  = 0, rearLastUs  = 0;
volatile uint8_t  divRpmCnt = 0, divSpeedCnt = 0;

// Note: PORTH is not in the bit-addressable I/O range on the ATmega2560, so
// D8/D9 writes are read-modify-write; safe because only these ISRs touch PORTH
// after setup(). Edges are always counted (speed is measured), but D8..D11
// stay LOW until the module is armed.
void isrFront() {
  if (PINE & _BV(PE4)) {               // rising edge
    frontEdges++;
    frontLastUs = micros();
    if (!armed) return;
    PORTH |= _BV(PH5);                 // D8 follows input
    if (++divRpmCnt >= DIV_RPM_HALF)     { divRpmCnt = 0;   PINB = _BV(PB4); } // toggle D10
    if (++divSpeedCnt >= DIV_SPEED_HALF) { divSpeedCnt = 0; PINB = _BV(PB5); } // toggle D11
  } else {
    PORTH &= ~_BV(PH5);
  }
}

void isrRear() {
  if (PINE & _BV(PE5)) {
    rearEdges++;
    rearLastUs = micros();
    if (armed) PORTH |= _BV(PH6);      // D9 follows input
  } else {
    PORTH &= ~_BV(PH6);
  }
}

// Frequency from edges counted between two samples, timed edge-to-edge for
// accuracy. Decays toward 0 when edges stop arriving.
struct FreqMeter {
  uint32_t prevEdges = 0, prevLastUs = 0;
  float hz = 0;

  void update(volatile uint32_t &edges, volatile uint32_t &lastUs) {
    noInterrupts();
    uint32_t e = edges, t = lastUs;
    interrupts();
    uint32_t dE = e - prevEdges;
    uint32_t dT = t - prevLastUs;
    if (dE > 0 && prevEdges != 0 && dT > 0) {
      hz = dE * 1e6f / dT;
    } else {
      uint32_t since = micros() - t;
      if (e == 0 || since > 500000UL) hz = 0;          // < 2 Hz -> stopped
      else if (since > 0) hz = min(hz, 1e6f / since);  // slowing down
    }
    if (dE > 0) { prevEdges = e; prevLastUs = t; }
  }
};

FreqMeter front, rear;

// Front speed in whole km/h, rounded exactly like the TM1637 and OLED show it.
int16_t frontKph() { return (int16_t)(front.hz / 6.0f + 0.5f); }

// ================================ STATE =====================================
const uint32_t SAMPLE_MS = 100;
const uint8_t  DECEL_SAMPLES = DECEL_WINDOW_MS / SAMPLE_MS + 1;  // now + window
float    frontHist[DECEL_SAMPLES] = {0};
uint8_t  histIdx = 0;
bool     abruptDecel = false;      // sudden drop from >= EVENT_MIN_KPH
float    decelPeakKph = 0;         // speed before the drop, for serial

bool     brakePressed = false, brakeRaw = false;
uint32_t brakeChangeMs = 0;

bool     eventActive = false;
uint32_t eventStartMs = 0;

bool     lockLatched = false;
uint32_t lockAboveSinceMs = 0;

bool     tcsActive = false;
uint32_t tcsSlipSinceMs = 0;

// ================================ SETUP =====================================
void setup() {
  pinMode(PIN_FRONT_IN, INPUT);
  pinMode(PIN_REAR_IN, INPUT);
  pinMode(PIN_BRAKE, INPUT_PULLUP);

  const uint8_t outs[] = { PIN_TCS, PIN_D6_HOLD, PIN_D7_PULSE, PIN_FRONT_OUT,
                           PIN_REAR_OUT, PIN_DIV_RPM, PIN_DIV_SPEED, PIN_LOCK };
  for (uint8_t p : outs) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  digitalWrite(PIN_LOCK, LOW);          // LOCK not active yet
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);
  for (uint8_t i = 0; i < 2; i++) {
    pinMode(PIN_ARM_BTN[i], INPUT_PULLUP);
    pinMode(PIN_ARM_LED[i], OUTPUT); digitalWrite(PIN_ARM_LED[i], LOW);
  }
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW);   // relay off
  pinMode(PIN_RELAY, OUTPUT);

  genBegin();
  encBegin();
  dispBegin();

  attachInterrupt(digitalPinToInterrupt(PIN_FRONT_IN), isrFront, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_REAR_IN), isrRear, CHANGE);

#if DEBUG_SERIAL
  Serial.begin(115200);
  Serial.println(F("Wheel Speed Interface Module (WSIM) v2 Mega ready"));
#endif
  oledBegin();
  randomSeed(analogRead(A15) ^ micros());   // A15 floating -> random engine lope
}

// ============================ SUB-FUNCTIONS =================================
// Arming: each momentary button latches its condition until power-off.
bool     armCond[2] = { false, false };
bool     armRaw[2]  = { false, false };
uint32_t armChangeMs[2] = { 0, 0 };

void readArm(uint32_t now) {
  for (uint8_t i = 0; i < 2; i++) {
    if (armCond[i]) continue;                      // already latched
    bool raw = digitalRead(PIN_ARM_BTN[i]) == LOW;
    if (raw != armRaw[i]) { armRaw[i] = raw; armChangeMs[i] = now; }
    if (armRaw[i] && now - armChangeMs[i] >= ARM_DEBOUNCE_MS) {
      armCond[i] = true;
      digitalWrite(PIN_ARM_LED[i], HIGH);
#if DEBUG_SERIAL
      Serial.print(F("# CONDITION ")); Serial.print(i + 1); Serial.println(F(" met"));
#endif
    }
  }
  if (!armed && armCond[0] && armCond[1]) {
    armed = true;
#if DEBUG_SERIAL
    Serial.println(F("# ARMED: WSIM active"));
#endif
  }
}

void readBrake(uint32_t now) {
  bool raw = digitalRead(PIN_BRAKE) == LOW;
  if (raw != brakeRaw) { brakeRaw = raw; brakeChangeMs = now; }
  if (now - brakeChangeMs >= 30) brakePressed = brakeRaw;   // 30 ms debounce
}

void sampleFrequencies() {
  front.update(frontEdges, frontLastUs);
  rear.update(rearEdges, rearLastUs);

  // Sudden drop: current speed vs the highest speed in the window.
  frontHist[histIdx] = front.hz;
  histIdx = (histIdx + 1) % DECEL_SAMPLES;
  float peakHz = 0;
  for (uint8_t i = 0; i < DECEL_SAMPLES; i++) peakHz = max(peakHz, frontHist[i]);
  float peakKph = peakHz / 6.0f;
  float dropKph = peakKph - front.hz / 6.0f;
  abruptDecel = (int16_t)(peakKph + 0.5f) >= EVENT_MIN_KPH && dropKph >= DECEL_DROP_KPH;
  if (abruptDecel) decelPeakKph = peakKph;
}

// D6 / D7 event. Triggers on the *start* of a condition so holding the brake
// does not re-trigger forever; a new event can start once the 13 s are over.
// When LOCK latches (10 km/h) D6 alone turns on for 1 s, once per power-on.
bool prevTrigger = false;
bool lockEventPending = false;             // set by updateLock() on latch
bool lockPulseActive = false;
uint32_t lockPulseStartMs = 0;

void updateBrakeEvent(uint32_t now) {
  // abruptDecel already includes "speed before the drop >= 10 km/h"; the
  // brake switch needs the current speed >= 10 km/h.
  bool brakeOk = brakePressed && frontKph() >= EVENT_MIN_KPH;
  bool trigger = BRAKE_REQUIRES_DECEL ? (brakePressed && abruptDecel)
                                      : (brakeOk || abruptDecel);
  // A running event finishes even if the speed then drops (it does when
  // braking). Nothing starts until armed. Edge-triggered: a trigger that is
  // still true when the event ends does not restart it.
  if (armed && trigger && !prevTrigger && !eventActive) {
    eventActive = true;
    eventStartMs = now;
#if DEBUG_SERIAL
    Serial.print(F("# EVENT: "));
    if (abruptDecel) { Serial.print(F("sudden drop from ")); Serial.print(decelPeakKph, 0);
                       Serial.print(F(" to ")); Serial.print(front.hz / 6.0f, 0); Serial.println(F(" km/h")); }
    else Serial.println(F("brake switch"));
#endif
  }
  prevTrigger = trigger;

  // One-shot LOCK pulse: D6 on for 1 s right away, D7 stays off.
  if (lockEventPending) {
    lockEventPending = false;
    lockPulseActive = true;
    lockPulseStartMs = now;
  }
  if (lockPulseActive && now - lockPulseStartMs >= LOCK_D6_PULSE_MS) lockPulseActive = false;

  bool d6 = false, d7 = false;
  if (eventActive) {
    uint32_t t = now - eventStartMs;
    if (t >= D6_DELAY_MS + D6_HOLD_MS) {
      eventActive = false;
    } else {
      d6 = t >= D6_DELAY_MS;
      if (t >= D7_START_MS && t < D7_END_MS) {
        uint32_t c = (t - D7_START_MS) % (D7_BURST_MS + D7_GAP_MS);
        d7 = c < D7_BURST_MS && (c % D7_PULSE_PERIOD_MS) < D7_PULSE_HIGH_MS;
      }
    }
  }
  if (lockPulseActive) d6 = true;
  // Power-up indication: D7 on for the first second after power-on / reset,
  // even while the module is still blocked (not armed).
  if (now < BOOT_D7_ON_MS) d7 = true;
  digitalWrite(PIN_D6_HOLD, d6);
  digitalWrite(PIN_D7_PULSE, d7);
}

void updateLock(uint32_t now) {
  if (!lockLatched) {
    if (armed && frontKph() >= LOCK_KPH) {         // LOCK can't latch until armed
      if (lockAboveSinceMs == 0) lockAboveSinceMs = now ? now : 1;
      if (now - lockAboveSinceMs >= LOCK_CONFIRM_MS) {
        lockLatched = true;
        lockEventPending = true;     // D6 1 s pulse once (latch never repeats)
      }
    } else {
      lockAboveSinceMs = 0;
    }
  }
  // Never cleared in code: only a power-off (reset) clears lockLatched.
  digitalWrite(PIN_LOCK, lockLatched ? HIGH : LOW);   // D12 on only when LOCK active
}

void updateTcs(uint32_t now) {
  bool slip = armed && rear.hz >= TCS_MIN_REAR_HZ &&
              rear.hz > front.hz * (1.0f + TCS_MARGIN);
  // Hysteresis: once active, stays active until rear drops within half margin.
  bool clear = rear.hz < TCS_MIN_REAR_HZ ||
               rear.hz <= front.hz * (1.0f + TCS_MARGIN / 2);

  if (!tcsActive) {
    if (slip) {
      if (tcsSlipSinceMs == 0) tcsSlipSinceMs = now ? now : 1;
      if (now - tcsSlipSinceMs >= TCS_CONFIRM_MS) tcsActive = true;
    } else {
      tcsSlipSinceMs = 0;
    }
  } else if (clear) {
    tcsActive = false;
    tcsSlipSinceMs = 0;
  }
  bool on = tcsActive && ((now / TCS_BLINK_HALF_MS) % 2 == 0);
  digitalWrite(PIN_TCS, on);
}

// ============================ SPEED RELAY (D28) =============================
// Speed relay (see CONFIG): 0..250 km/h -> 0..30 clicks/s. The click rate is
// re-computed from the CURRENT speed on every loop pass, so it reacts at once.
// Silent when stopped and until armed.
bool     relayOn = false;
bool     relayIdle = true;                 // true = next click starts at once
uint32_t relayClickMs = 0;                 // start of the last click
uint16_t relayOnMs = RELAY_ON_MAX_MS;      // ON time of the current click
int8_t   relayJitter = 0;                  // % for the current gap
float    relayHz = 0;                      // current click rate, for serial

void relayWrite(bool on) {
  relayOn = on;
  digitalWrite(PIN_RELAY, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void updateRelay(uint32_t now) {
  if (relayOn && now - relayClickMs >= relayOnMs) relayWrite(false);
  float kph = front.hz / 6.0f;
  if (!armed || kph < RELAY_MIN_KPH) { relayIdle = true; relayHz = 0; return; }

  float hz = min(kph, RELAY_FULL_KPH) / RELAY_FULL_KPH * RELAY_MAX_CLICK_HZ;
  relayHz = hz;
  uint32_t periodMs = (uint32_t)(1000.0f / hz);
  periodMs = periodMs * (100 + relayJitter) / 100;      // engine lope
  uint16_t onMs = constrain(periodMs / 2, RELAY_ON_MIN_MS, RELAY_ON_MAX_MS);
  if (periodMs < 2UL * onMs) periodMs = 2UL * onMs;

  if (relayIdle || now - relayClickMs >= periodMs) {
    relayIdle = false;
    relayClickMs = now;
    relayOnMs = onMs;
    relayJitter = (int8_t)random(-RELAY_JITTER_PCT, RELAY_JITTER_PCT + 1);
    relayWrite(true);
  }
}

// ======================== 4-DIGIT DISPLAY (TM1637) ==========================
// Shows front speed in whole km/h (front Hz / 6), right-aligned, "   0" when
// stopped. Bit-banged TM1637 protocol: CLK is driven push-pull, DIO is
// open-drain (OUTPUT LOW / INPUT released).
const uint8_t SEG_DIGIT[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66,
                                0x6D, 0x7D, 0x07, 0x7F, 0x6F };

inline void tmWait()  { delayMicroseconds(5); }
inline void dioLow()  { digitalWrite(PIN_DISP_DIO, LOW); pinMode(PIN_DISP_DIO, OUTPUT); }
inline void dioHigh() { pinMode(PIN_DISP_DIO, INPUT); }

void tmStart() {
  dioHigh(); digitalWrite(PIN_DISP_CLK, HIGH); tmWait();
  dioLow(); tmWait();
}

void tmStop() {
  digitalWrite(PIN_DISP_CLK, LOW); dioLow(); tmWait();
  digitalWrite(PIN_DISP_CLK, HIGH); tmWait();
  dioHigh(); tmWait();
}

void tmByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++, b >>= 1) {       // LSB first
    digitalWrite(PIN_DISP_CLK, LOW);
    if (b & 1) dioHigh(); else dioLow();
    tmWait();
    digitalWrite(PIN_DISP_CLK, HIGH); tmWait();
  }
  digitalWrite(PIN_DISP_CLK, LOW); dioHigh(); tmWait();   // ACK clock
  digitalWrite(PIN_DISP_CLK, HIGH); tmWait();
  digitalWrite(PIN_DISP_CLK, LOW); tmWait();
}

const int32_t DISP_DASHES = -2;                    // "----" = not armed yet

void dispShow(int32_t value) {
  uint8_t seg[4] = { 0, 0, 0, 0 };
  if (value == DISP_DASHES) {
    for (uint8_t i = 0; i < 4; i++) seg[i] = 0x40;   // segment G only
  } else {
    for (int8_t i = 3; i >= 0; i--) {              // right-aligned, no leading 0s
      seg[i] = SEG_DIGIT[value % 10];
      value /= 10;
      if (value == 0) break;
    }
  }
  tmStart(); tmByte(0x40); tmStop();               // auto-increment address
  tmStart(); tmByte(0xC0);                         // start at digit 0
  for (uint8_t i = 0; i < 4; i++) tmByte(seg[i]);
  tmStop();
  tmStart(); tmByte(0x88 | (DISP_BRIGHTNESS & 7)); tmStop();   // display on
}

void dispBegin() {
  digitalWrite(PIN_DISP_CLK, HIGH); pinMode(PIN_DISP_CLK, OUTPUT);
  dioHigh();
  dispShow(DISP_DASHES);
}

uint32_t dispLastMs = 0;
int32_t  dispShown = -1;

void updateDisplay(uint32_t now) {
  if (now - dispLastMs < DISP_UPDATE_MS) return;
  dispLastMs = now;
  int32_t kph = armed ? (int32_t)min(front.hz / 6.0f + 0.5f, 9999.0f) : DISP_DASHES;
  // Re-send every 2 s even when unchanged, in case the module was replugged.
  static uint8_t refresh = 0;
  if (kph != dispShown || ++refresh >= 2000 / DISP_UPDATE_MS) {
    refresh = 0;
    dispShown = kph;
    dispShow(kph);
  }
}

// ======================= 0.96" OLED (SSD1306, I2C) ==========================
// Status bar on top, then FRONT and REAR rows:
//   [COND1] [COND2]   ARMED     <- box filled once that button was pressed
//   FRONT        20 km/h        <- big digits
//   120Hz
//   REAR         22 km/h
//   132Hz
// Shows "---" and "BLOCKED" until both conditions are met.
//
// The Adafruit library is used for init and drawing into its 1 KB buffer, but
// the buffer is NOT sent with display() (that blocks ~25 ms and would distort
// the 50 ms D7 pulses). Instead it is streamed in 16-byte chunks, a couple per
// loop pass. If no OLED answers, it is re-probed every 2 s (hot-plug works).
Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000UL, 400000UL);
bool     oledOk = false;
uint8_t  oledAddr = 0x3C;
uint32_t oledLastMs = 0, oledProbeMs = 0;
int16_t  oledPos = -1;                       // next buffer byte to send, -1 = idle
int16_t  oledShownF = -1, oledShownR = -1;
uint8_t  oledShownCond = 0xFF;
uint8_t  oledRefresh = 0;

bool oledProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void oledBegin() {
  Wire.begin();
  Wire.setClock(400000UL);
  Wire.setWireTimeout(3000, true);          // never hang on a stuck bus
  oledProbeMs = millis();
  if (oledProbe(0x3C)) oledAddr = 0x3C;
  else if (oledProbe(0x3D)) oledAddr = 0x3D;
  else { oledOk = false; return; }
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, oledAddr, true, false);
  oledShownF = oledShownR = -1;             // force a redraw
  oledPos = -1;
#if DEBUG_SERIAL
  if (oledOk) { Serial.print(F("# OLED found at 0x")); Serial.println(oledAddr, HEX); }
#endif
}

// Condition box in the status bar: filled (inverse text) once met, outline only
// while not met yet.
// Two-color modules: rows 0..15 are yellow, 16..63 blue (fixed in the glass).
// The status bar is centred in the yellow strip, the speed rows are all blue.
const int16_t OLED_BAR_Y   = 3;            // status boxes, rows 3..11
const int16_t OLED_FRONT_Y = 17;           // big digits rows 17..37
const int16_t OLED_REAR_Y  = 41;           // big digits rows 41..61

void oledCond(int16_t x, const __FlashStringHelper *label, bool met) {
  oled.setTextSize(1);
  if (met) {
    oled.fillRect(x, OLED_BAR_Y, 34, 9, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
  } else {
    oled.drawRect(x, OLED_BAR_Y, 34, 9, SSD1306_WHITE);
    oled.setTextColor(SSD1306_WHITE);
  }
  oled.setCursor(x + 2, OLED_BAR_Y + 1);
  oled.print(label);
  oled.setTextColor(SSD1306_WHITE);
}

// One speed row: label and Hz on the left, big km/h digits on the right.
void oledRow(int16_t y, const __FlashStringHelper *label, int16_t kph, float hz) {
  char buf[12];
  oled.setTextSize(1);
  oled.setCursor(0, y);
  oled.print(label);
  if (armed) {
    snprintf(buf, sizeof buf, "%uHz", (unsigned)(hz + 0.5f));
    oled.setCursor(0, y + 11);
    oled.print(buf);
  }

  if (armed) snprintf(buf, sizeof buf, "%d", kph); else strcpy(buf, "---");
  oled.setTextSize(3);                       // 18 x 24 px per character
  oled.setCursor(96 - 18 * strlen(buf), y);
  oled.print(buf);
  oled.setTextSize(1);
  oled.setCursor(102, y + 13);
  oled.print(F("km/h"));
}

void updateOled(uint32_t now) {
  if (!oledOk) {
    if (now - oledProbeMs >= 2000) oledBegin();
    return;
  }

  if (oledPos >= 0) {                        // streaming a frame
    uint8_t *b = oled.getBuffer();
    for (uint8_t c = 0; c < OLED_CHUNKS_PER_LOOP && oledPos < 1024; c++) {
      Wire.beginTransmission(oledAddr);
      Wire.write(0x40);                      // data
      Wire.write(b + oledPos, 16);
      if (Wire.endTransmission() != 0) { oledOk = false; oledProbeMs = now; return; }
      oledPos += 16;
    }
    if (oledPos >= 1024) oledPos = -1;
    return;
  }

  if (now - oledLastMs < OLED_UPDATE_MS) return;
  oledLastMs = now;
  int16_t f = armed ? (int16_t)min(front.hz / 6.0f + 0.5f, 999.0f) : -2;
  int16_t r = armed ? (int16_t)min(rear.hz  / 6.0f + 0.5f, 999.0f) : -2;
  uint8_t cond = armCond[0] | (armCond[1] << 1);
  // Redraw on change, and every 2 s anyway (Hz text, replugged module).
  if (f == oledShownF && r == oledShownR && cond == oledShownCond &&
      ++oledRefresh < 2000 / OLED_UPDATE_MS) return;
  oledRefresh = 0;
  oledShownF = f; oledShownR = r; oledShownCond = cond;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oledCond(0,  F("COND1"), armCond[0]);
  oledCond(38, F("COND2"), armCond[1]);
  oled.setCursor(armed ? 98 : 80, OLED_BAR_Y + 1);
  oled.print(armed ? F("ARMED") : F("BLOCKED"));
  oledRow(OLED_FRONT_Y, F("FRONT"), f, front.hz);
  oledRow(OLED_REAR_Y,  F("REAR"),  r, rear.hz);

  // Frame window = whole screen, then stream the buffer from byte 0.
  Wire.beginTransmission(oledAddr);
  Wire.write(0x00);                          // commands
  const uint8_t win[] = { 0x21, 0, 127, 0x22, 0, 7 };
  Wire.write(win, sizeof win);
  if (Wire.endTransmission() != 0) { oledOk = false; oledProbeMs = now; return; }
  oledPos = 0;
}

#if DEBUG_SERIAL
void printStatus() {
  Serial.print(F("ARM="));   Serial.print(armCond[0]); Serial.print(armCond[1]);
  Serial.print(armed ? F(" ") : F(" [BLOCKED] "));
  Serial.print(F("F="));     Serial.print(front.hz, 1);
  Serial.print(F("Hz ("));   Serial.print(front.hz / 6.0f, 1);
  Serial.print(F("kph)"));
  Serial.print(F(" R="));    Serial.print(rear.hz, 1);
  Serial.print(F("Hz brake=")); Serial.print(brakePressed);
  Serial.print(F(" decel=")); Serial.print(abruptDecel);
  Serial.print(F(" event=")); Serial.print(eventActive);
  Serial.print(F(" lock="));  Serial.print(lockLatched);
  Serial.print(F(" tcs="));   Serial.print(tcsActive);
  Serial.print(F(" relay=")); Serial.print(relayHz, 1); Serial.print(F("/s"));
  Serial.print(F(" | GEN F=")); Serial.print(genFrontHz);
  Serial.print(F(" R="));      Serial.print(genRearHz);
  Serial.println(rearOnlyMode ? F(" [REAR only]") : F(" [NORMAL]"));
}
#endif

// ================================= LOOP =====================================
uint32_t lastSampleMs = 0, lastPrintMs = 0;

void loop() {
  uint32_t now = millis();

  readArm(now);
  readBrake(now);
  updateEncoder(now);

  if (now - lastSampleMs >= SAMPLE_MS) {
    lastSampleMs += SAMPLE_MS;
    sampleFrequencies();
  }

  updateBrakeEvent(now);
  updateLock(now);
  updateTcs(now);
  updateRelay(now);
  updateDisplay(now);
  updateOled(now);

#if DEBUG_SERIAL
  if (now - lastPrintMs >= 500) {
    lastPrintMs = now;
    printStatus();
  }
#endif
}
