// Wheel Speed Interface Module (WSIM) — Arduino Nano (ATmega328P, 16 MHz)
//
// Implements the hand-drawn "NANO" signal-module sketch:
//   * Brake switch / abrupt deceleration -> D6 held 13 s, D7 pulse bursts
//   * Front FREQ >= 60 Hz (10 km/h)      -> LOCK output, latched until power-off
//   * Front FREQ / 60 (RPM), / 6 (speed display)
//   * Front and rear FREQ passed through 1:1
//   * Rear FREQ higher than front        -> blinking TCS output
//
// Everything marked "ASSUMPTION" is not in the drawing and can be changed in
// the CONFIG section below.
//
// ---------------------------------------------------------------- PINOUT ----
//   D2  IN   FREQ input #1 (front)   INT0
//   D3  IN   FREQ input #2 (rear)    INT1
//   D4  OUT  TCS blinking output
//   D5  IN   Brake switch            INPUT_PULLUP, switch to GND = pressed
//   D6  OUT  Logic 1 for 13 s on brake event
//   D7  OUT  10 pulses/s with 0.5 s interruption, 2 s .. 10 s of D6 time
//   D8  OUT  FREQ output #1 (1:1 of front)
//   D9  OUT  FREQ output #2 (1:1 of rear)
//   D10 OUT  FREQ output / 60  (RPM)
//   D11 OUT  FREQ output / 6   (speed display)
//   D12 OUT  LOCK: logic 1 at 60 Hz, reset only at power-off
//   D13 OUT  Onboard LED heartbeat (1 Hz normal, 4 Hz once LOCK latched)
//
//   Test generator (EC11 / KY-040 rotary encoder):
//   A0  IN   Encoder A / CLK         pin-change interrupt
//   A1  IN   Encoder B / DT          pin-change interrupt
//   A2  IN   Encoder button / SW     INPUT_PULLUP, pressed = LOW
//   A3  OUT  Generated FRONT frequency 0-1500 Hz  -> jumper to D2
//   A4  OUT  Generated REAR  frequency 0-1500 Hz  -> jumper to D3
//   Remove the two jumpers when real wheel sensors are connected to D2 / D3.
//
//   Knob use:
//     NORMAL mode : turn -> FRONT and REAR change together (always equal)
//     press       -> REAR mode : turn -> only REAR changes, FRONT stays
//     press again -> back to NORMAL, REAR snaps back equal to FRONT
// ----------------------------------------------------------------------------

// =============================== CONFIG =====================================

// --- Brake event (D6 / D7) -------------------------------------------------
// Drawing: "if brake sw. pressed OR when speed is abruptly decrease".
// Set to true to require BOTH (brake pressed AND abrupt decel) instead.
const bool     BRAKE_REQUIRES_DECEL   = false;

// ASSUMPTION: "abrupt decrease" = front frequency falling at least this fast.
// 6 Hz = 1 km/h, so 130 Hz/s ~= 21.7 km/h/s ~= 6 m/s^2 (hard braking).
const float    DECEL_TRIGGER_HZ_PER_S = 130.0;
// ASSUMPTION: ignore decel detection below this speed (noise at low speed).
const float    DECEL_MIN_FREQ_HZ      = 60.0;   // 10 km/h

const uint32_t D6_HOLD_MS        = 13000;  // drawing: logic 1 for 13 s
const uint32_t D7_START_MS       = 2000;   // drawing text: starts after 2 s
const uint32_t D7_END_MS         = 10000;  // drawing: ends at 10 s
const uint32_t D7_BURST_MS       = 1000;   // 10 pulses at 10 pulses/s
const uint32_t D7_GAP_MS         = 500;    // drawing: interruption of .5 s
const uint32_t D7_PULSE_PERIOD_MS = 100;   // 10 pulses/s
const uint32_t D7_PULSE_HIGH_MS   = 50;    // ASSUMPTION: 50% duty

// --- LOCK (D12) ------------------------------------------------------------
const float    LOCK_FREQ_HZ      = 60.0;   // drawing: 60 Hz (10 km/h)
const uint32_t LOCK_CONFIRM_MS   = 200;    // ASSUMPTION: must hold 200 ms
// Drawing says logic 1; margin note says "logic 0". Flip here if needed.
const bool     LOCK_ACTIVE_HIGH  = true;

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

// --- Test generator / rotary encoder (A0..A4) ------------------------------
const uint16_t GEN_MAX_HZ        = 1500;   // 250 km/h
const uint16_t GEN_STEP_HZ       = 6;      // 1 km/h per click
const uint16_t GEN_FAST_STEP_HZ  = 30;     // 5 km/h per click when spun fast
const uint32_t GEN_FAST_MS       = 40;     // clicks closer than this = fast
const int8_t   ENC_STEPS_PER_CLICK = 4;    // EC11 / KY-040: 4 transitions/detent
const bool     ENC_REVERSE       = false;  // true if clockwise goes down

// --- Debug -----------------------------------------------------------------
#define DEBUG_SERIAL 1                     // 0 to disable serial output

// ============================== PINS ========================================
const uint8_t PIN_FRONT_IN = 2;   // PD2
const uint8_t PIN_REAR_IN  = 3;   // PD3
const uint8_t PIN_TCS      = 4;
const uint8_t PIN_BRAKE    = 5;
const uint8_t PIN_D6_HOLD  = 6;
const uint8_t PIN_D7_PULSE = 7;
const uint8_t PIN_FRONT_OUT = 8;  // PB0
const uint8_t PIN_REAR_OUT  = 9;  // PB1
const uint8_t PIN_DIV_RPM   = 10; // PB2
const uint8_t PIN_DIV_SPEED = 11; // PB3
const uint8_t PIN_LOCK      = 12;
const uint8_t PIN_LED       = 13;
const uint8_t PIN_ENC_A     = A0; // PC0
const uint8_t PIN_ENC_B     = A1; // PC1
const uint8_t PIN_ENC_SW    = A2;
const uint8_t PIN_GEN_FRONT = A3; // PC3
const uint8_t PIN_GEN_REAR  = A4; // PC4

// ========================== TEST GENERATOR ==================================
// Timer1 free-runs at 2 MHz (prescaler 8, 0.5 us/tick). Compare A drives the
// FRONT square wave on A3, compare B the REAR one on A4: each compare ISR
// toggles its pin and schedules the next toggle half a period later, so the
// two frequencies are fully independent. Half periods longer than the 16-bit
// timer (below ~31 Hz) are split into chunks.
volatile uint32_t genHalfTicks[2] = {0, 0};   // 0 = off (pin held LOW)
volatile uint32_t genRemain[2]    = {0, 0};
uint16_t genFrontHz = 0, genRearHz = 0;

inline void genStep(uint8_t ch, volatile uint16_t &ocr, uint8_t bit) {
  if (genRemain[ch] == 0) {
    if (genHalfTicks[ch] == 0) {       // off: keep LOW, check again in 10 ms
      PORTC &= ~bit;
      ocr += 20000;
      return;
    }
    PINC = bit;                        // toggle the output pin
    genRemain[ch] = genHalfTicks[ch];
  }
  uint32_t r = genRemain[ch];
  uint16_t chunk = r > 65000 ? 40000 : (uint16_t)r;  // last chunk never tiny
  ocr += chunk;
  genRemain[ch] = r - chunk;
}

ISR(TIMER1_COMPA_vect) { genStep(0, OCR1A, _BV(PC3)); }
ISR(TIMER1_COMPB_vect) { genStep(1, OCR1B, _BV(PC4)); }

void genSet(uint8_t ch, uint16_t hz) {
  // half period in 0.5 us ticks = 2 MHz / (2 * hz) = 1e6 / hz (rounded)
  uint32_t half = hz ? (1000000UL + hz / 2) / hz : 0;
  noInterrupts();
  genHalfTicks[ch] = half;
  interrupts();
}

void genBegin() {
  TCCR1A = 0;
  TCCR1B = _BV(CS11);                  // normal mode, clk/8
  OCR1A = TCNT1 + 1000;
  OCR1B = TCNT1 + 1500;
  TIFR1 = _BV(OCF1A) | _BV(OCF1B);
  TIMSK1 = _BV(OCIE1A) | _BV(OCIE1B);
}

// ============================ ROTARY ENCODER ================================
const int8_t encTable[16] = { 0, -1,  1,  0,
                              1,  0,  0, -1,
                             -1,  0,  0,  1,
                              0,  1, -1,  0 };
volatile uint8_t encPrev = 3;
volatile int8_t  encAcc = 0;
volatile int8_t  encPending = 0;       // whole clicks not yet applied

ISR(PCINT1_vect) {                     // A0 / A1 changed
  uint8_t s = PINC & 0x03;             // bit1 = B (A1), bit0 = A (A0)
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
  uint8_t s = PINC & 0x03;
  encPrev = ((s & 1) << 1) | (s >> 1);
  PCMSK1 |= _BV(PCINT8) | _BV(PCINT9); // A0, A1
  PCIFR  = _BV(PCIF1);
  PCICR |= _BV(PCIE1);
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

void isrFront() {
  if (PIND & _BV(PD2)) {               // rising edge
    PORTB |= _BV(PB0);                 // D8 follows input
    frontEdges++;
    frontLastUs = micros();
    if (++divRpmCnt >= DIV_RPM_HALF)     { divRpmCnt = 0;   PINB = _BV(PB2); } // toggle D10
    if (++divSpeedCnt >= DIV_SPEED_HALF) { divSpeedCnt = 0; PINB = _BV(PB3); } // toggle D11
  } else {
    PORTB &= ~_BV(PB0);
  }
}

void isrRear() {
  if (PIND & _BV(PD3)) {
    PORTB |= _BV(PB1);                 // D9 follows input
    rearEdges++;
    rearLastUs = micros();
  } else {
    PORTB &= ~_BV(PB1);
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

// ================================ STATE =====================================
const uint32_t SAMPLE_MS = 100;
const uint8_t  DECEL_SAMPLES = 4;          // compare against 300 ms ago
float    frontHist[DECEL_SAMPLES] = {0};
uint8_t  histIdx = 0;
bool     abruptDecel = false;

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
                           PIN_REAR_OUT, PIN_DIV_RPM, PIN_DIV_SPEED, PIN_LOCK,
                           PIN_LED };
  for (uint8_t p : outs) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  digitalWrite(PIN_LOCK, LOCK_ACTIVE_HIGH ? LOW : HIGH);  // not latched

  pinMode(PIN_GEN_FRONT, OUTPUT); digitalWrite(PIN_GEN_FRONT, LOW);
  pinMode(PIN_GEN_REAR, OUTPUT);  digitalWrite(PIN_GEN_REAR, LOW);
  genBegin();
  encBegin();

  attachInterrupt(digitalPinToInterrupt(PIN_FRONT_IN), isrFront, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_REAR_IN), isrRear, CHANGE);

#if DEBUG_SERIAL
  Serial.begin(115200);
  Serial.println(F("Wheel Speed Interface Module (WSIM) ready"));
#endif
}

// ============================ SUB-FUNCTIONS =================================
void readBrake(uint32_t now) {
  bool raw = digitalRead(PIN_BRAKE) == LOW;
  if (raw != brakeRaw) { brakeRaw = raw; brakeChangeMs = now; }
  if (now - brakeChangeMs >= 30) brakePressed = brakeRaw;   // 30 ms debounce
}

void sampleFrequencies() {
  front.update(frontEdges, frontLastUs);
  rear.update(rearEdges, rearLastUs);

  // Abrupt deceleration: drop over the last (DECEL_SAMPLES-1) samples.
  frontHist[histIdx] = front.hz;
  uint8_t oldest = (histIdx + 1) % DECEL_SAMPLES;
  histIdx = oldest;
  float old = frontHist[oldest];
  float rate = (old - front.hz) / ((DECEL_SAMPLES - 1) * SAMPLE_MS / 1000.0f);
  abruptDecel = old >= DECEL_MIN_FREQ_HZ && rate >= DECEL_TRIGGER_HZ_PER_S;
}

// D6 / D7 event. Triggers on the *start* of a condition so holding the brake
// does not re-trigger forever; a new event can start once the 13 s are over.
bool prevTrigger = false;

void updateBrakeEvent(uint32_t now) {
  bool trigger = BRAKE_REQUIRES_DECEL ? (brakePressed && abruptDecel)
                                      : (brakePressed || abruptDecel);
  if (trigger && !prevTrigger && !eventActive) {
    eventActive = true;
    eventStartMs = now;
  }
  prevTrigger = trigger;

  bool d6 = false, d7 = false;
  if (eventActive) {
    uint32_t t = now - eventStartMs;
    if (t >= D6_HOLD_MS) {
      eventActive = false;
    } else {
      d6 = true;
      if (t >= D7_START_MS && t < D7_END_MS) {
        uint32_t c = (t - D7_START_MS) % (D7_BURST_MS + D7_GAP_MS);
        d7 = c < D7_BURST_MS && (c % D7_PULSE_PERIOD_MS) < D7_PULSE_HIGH_MS;
      }
    }
  }
  digitalWrite(PIN_D6_HOLD, d6);
  digitalWrite(PIN_D7_PULSE, d7);
}

void updateLock(uint32_t now) {
  if (!lockLatched) {
    if (front.hz >= LOCK_FREQ_HZ) {
      if (lockAboveSinceMs == 0) lockAboveSinceMs = now ? now : 1;
      if (now - lockAboveSinceMs >= LOCK_CONFIRM_MS) lockLatched = true;
    } else {
      lockAboveSinceMs = 0;
    }
  }
  // Never cleared in code: only a power-off (reset) clears lockLatched.
  digitalWrite(PIN_LOCK, lockLatched == LOCK_ACTIVE_HIGH);
}

void updateTcs(uint32_t now) {
  bool slip = rear.hz >= TCS_MIN_REAR_HZ &&
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

void updateLed(uint32_t now) {
  uint32_t half = lockLatched ? 125 : 500;
  digitalWrite(PIN_LED, (now / half) % 2 == 0);
}

#if DEBUG_SERIAL
void printStatus() {
  Serial.print(F("F="));     Serial.print(front.hz, 1);
  Serial.print(F("Hz ("));   Serial.print(front.hz / 6.0f, 1);
  Serial.print(F("kph) R=")); Serial.print(rear.hz, 1);
  Serial.print(F("Hz brake=")); Serial.print(brakePressed);
  Serial.print(F(" decel=")); Serial.print(abruptDecel);
  Serial.print(F(" event=")); Serial.print(eventActive);
  Serial.print(F(" lock="));  Serial.print(lockLatched);
  Serial.print(F(" tcs="));   Serial.print(tcsActive);
  Serial.print(F(" | GEN F=")); Serial.print(genFrontHz);
  Serial.print(F(" R="));      Serial.print(genRearHz);
  Serial.println(rearOnlyMode ? F(" [REAR only]") : F(" [NORMAL]"));
}
#endif

// ================================= LOOP =====================================
uint32_t lastSampleMs = 0, lastPrintMs = 0;

void loop() {
  uint32_t now = millis();

  readBrake(now);
  updateEncoder(now);

  if (now - lastSampleMs >= SAMPLE_MS) {
    lastSampleMs += SAMPLE_MS;
    sampleFrequencies();
  }

  updateBrakeEvent(now);
  updateLock(now);
  updateTcs(now);
  updateLed(now);

#if DEBUG_SERIAL
  if (now - lastPrintMs >= 500) {
    lastPrintMs = now;
    printStatus();
  }
#endif
}
