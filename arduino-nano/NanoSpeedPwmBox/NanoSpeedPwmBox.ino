// Nano Speed / PWM Box
//
// Arduino Nano (ATmega328P, Old Bootloader). A rotary encoder sets a speed;
// two outputs give the matching wheel-speed frequency (km/h x 6 Hz,
// 0-1500 Hz = 0-250 km/h), D5 gives the speed itself (Hz = km/h), two inputs
// are measured, D12 goes HIGH from 10 km/h on IN 1, and D6 sweeps from
// 10 kHz down to 0 Hz in 5 s on an encoder long press.
// A 4-digit TM1637 display shows one value at a time (encoder short press).
//
// Connector (4 outputs, 2 inputs):
//   D10  OUT  Freq Out, 0-1500 Hz square wave, 50 % duty   (Timer1 OC1B)
//   D9   OUT  Freq Out, 0-1500 Hz, same frequency as D10   (Timer1 OC1A)
//   D6   OUT  Fade: on an encoder long press, a 50 % square wave sliding
//             from 10 kHz down to 0 Hz in 5 s (linear), then LOW (Timer2)
//   D5   OUT  Speed: D9 / 6, so its Hz = km/h (0-250 Hz), 50 % duty
//   D8   IN   Freq In 1 (pin-change interrupt, internal pull-up), km/h = Hz / 6
//   D7   IN   Freq In 2 (pin-change interrupt), km/h = Hz / 6
// Extra:
//   D12  OUT  10 km/h logic: HIGH while IN 1 >= 10 km/h (60 Hz), LOW below 9.5
//   Encoder (DFRobot EC11) A -> A2, B -> A1, C (button) -> A0, + -> 5V, GND -> GND
//     turn       : speed 0-250 km/h, 1 km/h per click (5 when spun fast)
//     short press: next display page  Out -> In 1 -> In 2 -> FAdE -> Out
//     long press : (1 s) start the D6 fade 10 kHz -> 0 Hz in 5 s (again)
//   TM1637      CLK -> D4, DIO -> D2, VCC -> 5V, GND -> GND
//   Buzzer      active buzzer red (+) -> D13, black (-) -> GND: click on an
//               encoder press, beep when D12 turns on, and a long beep fading
//               from loud to silent with the D6 fade (long press, 5 s)
// USB Serial Monitor: 115200 baud, a status line twice a second.
//
// Timers: Timer0 = millis(), Timer1 = D9 / D10 (+ D5 from its interrupt),
// Timer2 = D6 fade, so analogWrite(), tone() and Servo can't be used.

#include <Arduino.h>
#include <util/atomic.h>

// ----------------------------- Settings -------------------------------------
constexpr int MAX_KMH = 250;           // 250 km/h = 1500 Hz.
constexpr int HZ_PER_KMH = 6;          // Speed formula: km/h = Hz / 6.
constexpr int FAST_STEP_KMH = 5;       // Encoder spun quickly.
constexpr unsigned long FAST_MS = 40;  // Clicks closer than this = fast.
constexpr int8_t STEPS_PER_CLICK = 4;  // Encoder transitions per detent (EC11 / KY-040).
constexpr bool ENC_REVERSE = false;    // true if clockwise makes it slower.

constexpr float LOGIC_ON_KMH = 10.0f;   // D12 HIGH from here (IN 1)...
constexpr float LOGIC_OFF_KMH = 9.5f;   // ...and LOW again below this.

constexpr float FADE_START_HZ = 10000;  // D6 fade start frequency...
constexpr uint32_t FADE_MS = 5000;      // ...down to 0 Hz in this time.
constexpr uint8_t DISP_BRIGHTNESS = 7;  // TM1637 0-7.

// ------------------------------- Pins ---------------------------------------
constexpr uint8_t PIN_OUT_A = 9, PIN_OUT_B = 10, PIN_FADE = 6, PIN_SPEED = 5, PIN_LOGIC = 12;
constexpr uint8_t PIN_IN1 = 8, PIN_IN2 = 7;
constexpr uint8_t PIN_ENC_A = A2, PIN_ENC_B = A1, PIN_ENC_SW = A0;  // A, B, C (button).
constexpr uint8_t PIN_DISP_CLK = 4, PIN_DISP_DIO = 2;
constexpr uint8_t PIN_BUZZER = 13;  // Active buzzer: red (+) -> D13, black (-) -> GND.
constexpr bool BUZZER_ON = true;    // false = silent.

// ============================ D13: active buzzer =============================
// Non-blocking beep patterns: up to 4 beeps of onMs with gapMs between them.
// The Nano's onboard L LED (also D13) lights with the buzzer.
//   encoder press: 30 ms click; D12 on (10 km/h): 200 ms.
// Fade beep (long press, with the D6 fade): one long beep that fades from
// loud to silent over FADE_MS. An active buzzer has a fixed tone and no
// volume control, so it is switched on and off 50 times a second
// (BUZZ_FADE_PERIOD_US) with the on-time falling from 100 % to 0 %.
constexpr uint32_t BUZZ_FADE_PERIOD_US = 20000;
bool buzzFade = false;
uint32_t buzzFadeStartMs = 0;
uint8_t beepsLeft = 0;
uint16_t beepOnMs = 0, beepGapMs = 0;
bool beepSounding = false;
uint32_t beepEdgeMs = 0;

void beep(uint16_t onMs, uint8_t count = 1, uint16_t gapMs = 80) {
  if (!BUZZER_ON) return;
  beepOnMs = onMs; beepGapMs = gapMs; beepsLeft = count;
  beepSounding = true;
  beepEdgeMs = millis();
  digitalWrite(PIN_BUZZER, HIGH);
}

void startBuzzFade(uint32_t now) {
  if (!BUZZER_ON) return;
  beepsLeft = 0;  // The fade replaces any running beep.
  buzzFade = true;
  buzzFadeStartMs = now;
}

void updateBuzzer(uint32_t now) {
  if (!beepsLeft) {
    if (!buzzFade) return;
    const uint32_t t = now - buzzFadeStartMs;
    if (t >= FADE_MS) { buzzFade = false; digitalWrite(PIN_BUZZER, LOW); return; }
    // On-time in this 20 ms period: 100 % at the start, 0 % at FADE_MS.
    const uint32_t onUs = uint32_t(float(BUZZ_FADE_PERIOD_US) * (1.0f - float(t) / FADE_MS));
    digitalWrite(PIN_BUZZER, micros() % BUZZ_FADE_PERIOD_US < onUs ? HIGH : LOW);
    return;
  }
  if (beepSounding && now - beepEdgeMs >= beepOnMs) {
    digitalWrite(PIN_BUZZER, LOW);
    beepSounding = false;
    beepEdgeMs = now;
    if (--beepsLeft == 0) return;
  } else if (!beepSounding && now - beepEdgeMs >= beepGapMs) {
    digitalWrite(PIN_BUZZER, HIGH);
    beepSounding = true;
    beepEdgeMs = now;
  }
}

// ============== D9 / D10: Timer1 generator, D5 = D9 / 6 ======================
// CTC mode (TOP = OCR1A), OC1A and OC1B toggle on every compare, so D9 and
// D10 give the same 50 % square wave, in phase. f = F_CPU / (2 x N x (TOP+1)).
//   /8 :  f = 1 MHz / (TOP+1)    for 16-1500 Hz
//   /64:  f = 125 kHz / (TOP+1)  below 16 Hz (6 Hz = 1 km/h)
// The compare interrupt counts the toggles: every 6th toggles D5, so D5 runs
// at exactly D9 / 6 (Hz = km/h), 50 % duty.
uint16_t outHz = 0;
volatile uint8_t speedDiv = 0;

ISR(TIMER1_COMPA_vect) {
  if (++speedDiv >= HZ_PER_KMH) { speedDiv = 0; PIND = _BV(PD5); }  // Toggle D5.
}

void setOutHz(uint16_t hz) {
  outHz = hz;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (hz == 0) {  // Stopped: D9 / D10 / D5 LOW.
      TCCR1A = 0;
      TCCR1B = 0;
      TIMSK1 &= ~_BV(OCIE1A);
      PORTB &= ~(_BV(PB1) | _BV(PB2));
      PORTD &= ~_BV(PD5);
      speedDiv = 0;
      return;
    }
    const bool slow = hz < 16;
    const uint32_t top = (slow ? 125000UL : 1000000UL) / hz - 1;
    TCCR1A = _BV(COM1A0) | _BV(COM1B0);                                  // Toggle OC1A / OC1B.
    TCCR1B = _BV(WGM12) | (slow ? (_BV(CS11) | _BV(CS10)) : _BV(CS11));  // CTC, /64 or /8.
    OCR1A = top;
    OCR1B = top;
    if (TCNT1 > top) TCNT1 = 0;  // New TOP below the count: don't wait for the wrap.
    TIMSK1 |= _BV(OCIE1A);       // D5 divider.
  }
}

// ===================== D6: 10 kHz -> 0 Hz fade (Timer2) =====================
// Timer2 CTC; every compare toggles D6 in the interrupt, so
// f = F_CPU / (2 x N x (OCR2A+1)). The prescaler N is the smallest that fits
// OCR2A in 8 bits: /8 down to 3.9 kHz, then /32, /64, /128, /256, /1024 (down
// to 31 Hz). The sweep is linear in time; below 31 Hz the output stops LOW.
bool fading = false;
uint32_t fadeStartMs = 0;
float fadeHz = 0;

ISR(TIMER2_COMPA_vect) { PIND = _BV(PD6); }  // Toggle D6.

void setFadeHz(float hz) {
  fadeHz = hz;
  struct Pre { uint16_t n; uint8_t cs; };
  static const Pre PRE[] = {{8, 2}, {32, 3}, {64, 4}, {128, 5}, {256, 6}, {1024, 7}};
  for (const Pre& p : PRE) {
    const float top = F_CPU / (2.0f * p.n * hz) - 1;
    if (hz > 0 && top <= 255) {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        OCR2A = uint8_t(top + 0.5f);
        TCCR2B = p.cs;
        if (TCNT2 > OCR2A) TCNT2 = 0;
        TIMSK2 |= _BV(OCIE2A);
      }
      return;
    }
  }
  // 0 Hz or below 31 Hz: stop, D6 LOW.
  fadeHz = 0;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TIMSK2 &= ~_BV(OCIE2A);
    TCCR2B = 0;
    PORTD &= ~_BV(PD6);
  }
}

void fadeBegin() {
  pinMode(PIN_FADE, OUTPUT);
  digitalWrite(PIN_FADE, LOW);
  TCCR2A = _BV(WGM21);  // CTC, no hardware pin (D6 is toggled in the interrupt).
  TCCR2B = 0;
  setFadeHz(0);
}

void startFade(uint32_t now) {
  fading = true;
  fadeStartMs = now;
  setFadeHz(FADE_START_HZ);
  Serial.println(F("# FADE start: D6 10 kHz -> 0 Hz in 5 s"));
  startBuzzFade(now);  // Loud -> silent with the D6 fade.
}

void updateFade(uint32_t now) {
  if (!fading) return;
  static uint32_t lastMs = 0;
  if (now - lastMs < 10) return;  // New frequency every 10 ms.
  lastMs = now;
  const uint32_t t = now - fadeStartMs;
  if (t >= FADE_MS) {
    fading = false;
    setFadeHz(0);
    Serial.println(F("# FADE end: D6 0 Hz (LOW)"));
    return;
  }
  setFadeHz(FADE_START_HZ * (1.0f - float(t) / FADE_MS));
}

// =========================== IN 1 / IN 2: inputs ============================
// Rising edges on D8 (PCINT0, PB0) and D7 (PCINT2, PD7). Every 250 ms the
// frequency is the edges counted / time between the first and last edge;
// with only one edge in the window, the last period is used. No edge for 1 s
// (below 1 Hz) = 0 Hz.
struct Input {
  volatile uint16_t count = 0;
  volatile uint32_t firstUs = 0, lastUs = 0, periodUs = 0;
  float hz = 0;

  // From the interrupt: one rising edge.
  inline void edge() {
    const uint32_t now = micros();
    if (lastUs) periodUs = now - lastUs;
    if (count == 0) firstUs = now;
    lastUs = now;
    count++;
  }

  // Every 250 ms from loop().
  void measure() {
    uint16_t n;
    uint32_t first, last, period;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      n = count; first = firstUs; last = lastUs; period = periodUs;
      count = 0;
    }
    if (last == 0 || micros() - last > 1000000UL) hz = 0;
    else if (n >= 2 && last != first) hz = (n - 1) * 1e6f / (last - first);
    else if (period) hz = 1e6f / period;
  }
};
Input in1, in2;

volatile uint8_t in1Prev = 0, in2Prev = 0;
ISR(PCINT0_vect) {  // D8 = PB0: IN 1.
  const uint8_t level = PINB & _BV(PB0);
  if (level && !in1Prev) in1.edge();
  in1Prev = level;
}
ISR(PCINT2_vect) {  // D7 = PD7: IN 2.
  const uint8_t level = PIND & _BV(PD7);
  if (level && !in2Prev) in2.edge();
  in2Prev = level;
}

// ============================== Encoder =====================================
// Quadrature decoding in the A2 (A) / A1 (B) pin-change interrupt (PCINT1), with the
// usual transition table; STEPS_PER_CLICK transitions = one click.
const int8_t ENC_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
volatile uint8_t encPrev = 0;
volatile int8_t encAcc = 0, encClicks = 0;
volatile uint16_t encChanges = 0;  // A / B changes seen (wiring check).
long encPosition = 0;              // Clicks since power-up (wiring check).

ISR(PCINT1_vect) {  // A2 = PC2 (A), A1 = PC1 (B).
  const uint8_t now = (((PINC >> PC2) & 1) << 1) | ((PINC >> PC1) & 1);
  encAcc += ENC_TABLE[(encPrev << 2) | now];
  encPrev = now;
  encChanges++;
  if (encAcc >= STEPS_PER_CLICK) { encAcc = 0; if (encClicks < 100) encClicks++; }
  else if (encAcc <= -STEPS_PER_CLICK) { encAcc = 0; if (encClicks > -100) encClicks--; }
}

int kmh = 0;  // Speed set by the encoder.

void applySpeed() { setOutHz(uint16_t(kmh) * HZ_PER_KMH); }

// ============================ Display (TM1637) ==============================
// Bit-banged TM1637 (as in WheelSpeedInterfaceModuleV2): CLK push-pull, DIO
// open-drain. Pages: Out km/h, In 1 km/h, In 2 km/h, FAdE (D6 Hz, 9999 max).
// Changing page shows its name for 0.8 s.
enum Page : uint8_t { P_OUT, P_IN1, P_IN2, P_FADE, PAGE_COUNT };
uint8_t page = P_OUT;
uint32_t labelUntilMs = 0;

const uint8_t SEG_DIGIT[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
const uint8_t PAGE_LABEL[PAGE_COUNT][4] = {
    {0x3F, 0x1C, 0x78, 0x00},   // "Out "
    {0x06, 0x54, 0x00, 0x06},   // "In 1"
    {0x06, 0x54, 0x00, 0x5B},   // "In 2"
    {0x71, 0x77, 0x5E, 0x79}};  // "FAdE"
const char* const PAGE_NAME[PAGE_COUNT] = {"OUT km/h", "IN 1 km/h", "IN 2 km/h", "FADE D6 Hz"};

inline void tmWait() { delayMicroseconds(5); }
inline void dioLow() { digitalWrite(PIN_DISP_DIO, LOW); pinMode(PIN_DISP_DIO, OUTPUT); }
inline void dioHigh() { pinMode(PIN_DISP_DIO, INPUT); }

void tmStart() { dioHigh(); digitalWrite(PIN_DISP_CLK, HIGH); tmWait(); dioLow(); tmWait(); }
void tmStop() {
  digitalWrite(PIN_DISP_CLK, LOW); dioLow(); tmWait();
  digitalWrite(PIN_DISP_CLK, HIGH); tmWait();
  dioHigh(); tmWait();
}
void tmByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++, b >>= 1) {  // LSB first.
    digitalWrite(PIN_DISP_CLK, LOW);
    if (b & 1) dioHigh(); else dioLow();
    tmWait();
    digitalWrite(PIN_DISP_CLK, HIGH); tmWait();
  }
  digitalWrite(PIN_DISP_CLK, LOW); dioHigh(); tmWait();  // ACK clock.
  digitalWrite(PIN_DISP_CLK, HIGH); tmWait();
  digitalWrite(PIN_DISP_CLK, LOW); tmWait();
}
void tmShow(const uint8_t seg[4]) {
  tmStart(); tmByte(0x40); tmStop();  // Auto-increment address.
  tmStart(); tmByte(0xC0);            // From digit 0.
  for (uint8_t i = 0; i < 4; i++) tmByte(seg[i]);
  tmStop();
  tmStart(); tmByte(0x88 | (DISP_BRIGHTNESS & 7)); tmStop();  // Display on.
}

// Right-aligned number without leading zeros ("   0").
void numberSegs(uint16_t v, uint8_t seg[4]) {
  for (int8_t i = 3; i >= 0; i--) {
    seg[i] = SEG_DIGIT[v % 10];
    v /= 10;
    if (v == 0) break;
  }
}

uint32_t dispLastMs = 0;
uint8_t shown[4] = {0xFF, 0xFF, 0xFF, 0xFF};

void updateDisplay(uint32_t now) {
  if (now - dispLastMs < 100) return;
  dispLastMs = now;
  uint8_t seg[4] = {0, 0, 0, 0};
  if (now < labelUntilMs) memcpy(seg, PAGE_LABEL[page], 4);
  else {
    const float v = page == P_OUT ? kmh
                  : page == P_IN1 ? in1.hz / HZ_PER_KMH
                  : page == P_IN2 ? in2.hz / HZ_PER_KMH
                                  : fadeHz;
    numberSegs(uint16_t(min(v + 0.5f, 9999.0f)), seg);
  }
  static uint8_t refresh = 0;  // Re-send every 2 s, in case the module was replugged.
  if (memcmp(seg, shown, 4) != 0 || ++refresh >= 20) {
    refresh = 0;
    memcpy(shown, seg, 4);
    tmShow(seg);
  }
}

// =============================== Setup / loop ===============================
void setup() {
  Serial.begin(115200);
  Serial.println(F("# NANO SPEED / PWM BOX start"));

  pinMode(PIN_OUT_A, OUTPUT); pinMode(PIN_OUT_B, OUTPUT); pinMode(PIN_SPEED, OUTPUT);
  digitalWrite(PIN_OUT_A, LOW); digitalWrite(PIN_OUT_B, LOW); digitalWrite(PIN_SPEED, LOW);
  pinMode(PIN_LOGIC, OUTPUT); digitalWrite(PIN_LOGIC, LOW);
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  setOutHz(0);
  fadeBegin();

  pinMode(PIN_IN1, INPUT_PULLUP);  // D8 pull-up: unconnected reads HIGH (0 Hz), no mains hum on D12.
  pinMode(PIN_IN2, INPUT);
  in1Prev = PINB & _BV(PB0);
  in2Prev = PIND & _BV(PD7);
  PCMSK0 |= _BV(PCINT0);   // D8.
  PCMSK2 |= _BV(PCINT23);  // D7.
  PCICR |= _BV(PCIE0) | _BV(PCIE2);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encPrev = (((PINC >> PC2) & 1) << 1) | ((PINC >> PC1) & 1);
  PCMSK1 |= _BV(PCINT10) | _BV(PCINT9);  // A2 (A), A1 (B).
  PCICR |= _BV(PCIE1);

  digitalWrite(PIN_DISP_CLK, HIGH); pinMode(PIN_DISP_CLK, OUTPUT);
  dioHigh();
  labelUntilMs = millis() + 800;  // Show "Out " first.
}

void loop() {
  const uint32_t now = millis();

  // 1. Encoder turns: speed.
  static uint32_t lastClickMs = 0;
  int8_t clicks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { clicks = encClicks; encClicks = 0; }
  if (ENC_REVERSE) clicks = -clicks;
  if (clicks) {
    encPosition += clicks;
    const bool fast = clicks > 1 || clicks < -1 || now - lastClickMs < FAST_MS;
    lastClickMs = now;
    kmh = constrain(kmh + clicks * (fast ? FAST_STEP_KMH : 1), 0, MAX_KMH);
    applySpeed();
    if (page != P_OUT) { page = P_OUT; labelUntilMs = 0; }  // Turning shows the speed.
    Serial.print(F("# SPEED ")); Serial.print(kmh); Serial.print(F(" km/h = "));
    Serial.print(outHz); Serial.print(F(" Hz on D9 / D10, D5 "));
    Serial.print(kmh); Serial.println(F(" Hz"));
  }

  // 2. Encoder button: short press = next page, long press (1 s) = D6 fade.
  static bool btnPrev = HIGH, longDone = false;
  static uint32_t btnDownMs = 0, btnChangeMs = 0;
  static bool btnStable = HIGH;
  const bool raw = digitalRead(PIN_ENC_SW);
  if (raw != btnPrev) { btnPrev = raw; btnChangeMs = now; }
  if (now - btnChangeMs >= 30 && raw != btnStable) {
    btnStable = raw;
    if (btnStable == LOW) { btnDownMs = now; longDone = false; beep(30); }  // Click.
    else if (!longDone) {
      page = (page + 1) % PAGE_COUNT;
      labelUntilMs = now + 800;
      Serial.print(F("# DISPLAY page: ")); Serial.println(PAGE_NAME[page]);
    }
  }
  if (btnStable == LOW && !longDone && now - btnDownMs >= 1000) {
    longDone = true;
    startFade(now);
    page = P_FADE; labelUntilMs = 0;  // Show the fade on the display.
  }

  // 3. D6 fade.
  updateFade(now);

  // 4. Inputs every 250 ms, and the 10 km/h logic output.
  static uint32_t measureMs = 0;
  if (now - measureMs >= 250) {
    measureMs = now;
    in1.measure();
    in2.measure();
    const float in1Kmh = in1.hz / HZ_PER_KMH;
    static bool logic = false;
    if (!logic && in1Kmh >= LOGIC_ON_KMH) { logic = true; beep(200); Serial.println(F("# D12 ON: IN 1 reached 10 km/h")); }
    else if (logic && in1Kmh < LOGIC_OFF_KMH) logic = false;
    digitalWrite(PIN_LOGIC, logic ? HIGH : LOW);
  }

  // 5. Buzzer and display.
  updateBuzzer(now);
  updateDisplay(now);

  // 6. Status line.
  static uint32_t reportMs = 0;
  if (now - reportMs >= 500) {
    reportMs = now;
    Serial.print(F("OUT ")); Serial.print(kmh); Serial.print(F(" km/h, D9/D10 ")); Serial.print(outHz);
    Serial.print(F(" Hz, D5 ")); Serial.print(outHz ? kmh : 0);
    Serial.print(F(" Hz | IN1 D8 ")); Serial.print(in1.hz, 1); Serial.print(F(" Hz = "));
    Serial.print(in1.hz / HZ_PER_KMH, 1);
    Serial.print(F(" km/h | IN2 D7 ")); Serial.print(in2.hz, 1); Serial.print(F(" Hz = "));
    Serial.print(in2.hz / HZ_PER_KMH, 1);
    Serial.print(F(" km/h | 10KPH D12 ")); Serial.print(digitalRead(PIN_LOGIC) ? F("HIGH") : F("LOW"));
    Serial.print(F(" | FADE D6 ")); Serial.print(fadeHz, 0); Serial.print(F(" Hz"));
    Serial.print(F(" | page ")); Serial.print(PAGE_NAME[page]);
    uint16_t changes;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { changes = encChanges; }
    Serial.print(F(" | EC11 A(A2) ")); Serial.print(digitalRead(PIN_ENC_A));
    Serial.print(F(" B(A1) ")); Serial.print(digitalRead(PIN_ENC_B));
    Serial.print(F(" C(A0) ")); Serial.print(digitalRead(PIN_ENC_SW));
    Serial.print(F(" changes ")); Serial.print(changes);
    Serial.print(F(" clicks ")); Serial.println(encPosition);
  }
}
