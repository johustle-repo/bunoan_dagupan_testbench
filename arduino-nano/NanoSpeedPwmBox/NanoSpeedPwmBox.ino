// Nano Speed / PWM Box
//
// Arduino Nano (ATmega328P, Old Bootloader). A rotary encoder sets a speed;
// two outputs give the matching wheel-speed frequency (km/h x 6 Hz,
// 0-1500 Hz = 0-250 km/h), D5 gives the speed itself (Hz = km/h), two inputs
// are measured, D12 goes HIGH from 10 km/h on IN 1, and D6 sweeps from
// 10 kHz down to 0 Hz in 3 s on an encoder long press, shown on 7 LEDs.
// A 16x2 I2C LCD shows one page at a time (encoder short press).
//
// Connector (4 outputs, 2 inputs):
//   D10  OUT  Freq Out, 0-1500 Hz square wave, 50 % duty   (Timer1 OC1B)
//   D9   OUT  Freq Out, 0-1500 Hz, same frequency as D10   (Timer1 OC1A)
//   D6   OUT  Fade: on an encoder long press, a 50 % square wave sliding
//             from 10 kHz down to 0 Hz in 3 s (linear), then LOW (Timer2)
//   D5   OUT  Speed: D9 / 6, so its Hz = km/h (0-250 Hz), 50 % duty
//   D8   IN   Freq In 1 (pin-change interrupt, internal pull-up), km/h = Hz / 6
//   A6   IN   Freq In 2 (analog-only pin, sampled by the ADC at 19 kHz:
//             edge above 3.5 V, reset below 1.5 V), km/h = Hz / 6. No pull-up.
// Extra:
//   D12  OUT  10 km/h logic: HIGH while IN 1 >= 10 km/h (60 Hz), LOW below 9.5
//   Encoder (DFRobot EC11) A -> A2, B -> A1, C (button) -> A0, + -> 5V, GND -> GND
//     turn on Out / In 1 / In 2: speed 0-250 km/h (D9 / D10 / D5),
//                  1 km/h per click (5 when spun fast); the page stays
//     turn on FAdE: D6 by hand 0-10 kHz, 100 Hz per click (500 fast), and
//                  the LEDs follow; it stops a running fade
//     short press: next display page  Out -> In 1 -> In 2 -> FAdE -> Out
//     long press : (1 s) start the D6 fade 10 kHz -> 0 Hz in 3 s (again)
//   LCD 16x2    I2C backpack: SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
//   Fade LEDs   7 LEDs, each pin -> 220-330 ohm resistor -> LED (+) long leg,
//               LED (-) -> GND. Bar graph of the D6 frequency: all 7 on at
//               10 kHz, one goes out every ~1430 Hz, all off at 0 Hz.
//               LED 1 (bottom) .. LED 7 (top) = D3, D11, A3, D2, D4, D7, D13
// USB Serial Monitor: 115200 baud, a status line twice a second.
//
// Timers: Timer0 = millis(), Timer1 = D9 / D10 (+ D5 from its interrupt),
// Timer2 = D6 fade, so analogWrite(), tone() and Servo can't be used.
// The ADC runs free on A6 (IN 2), so analogRead() can't be used either.

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>  // Library Manager: "LiquidCrystal I2C" by Frank de Brabander.
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
constexpr uint32_t FADE_MS = 3000;      // ...down to 0 Hz in this time.
constexpr uint16_t FADE_STEP_HZ = 100;       // FAdE page: D6 per knob click...
constexpr uint16_t FADE_FAST_STEP_HZ = 500;  // ...and when spun quickly.

constexpr uint8_t LCD_ADDR = 0x27;      // I2C LCD address; try 0x3F if nothing shows.

// ------------------------------- Pins ---------------------------------------
constexpr uint8_t PIN_OUT_A = 9, PIN_OUT_B = 10, PIN_FADE = 6, PIN_SPEED = 5, PIN_LOGIC = 12;
constexpr uint8_t PIN_IN1 = 8;  // IN 2 is A6 (ADC channel 6), read in the ADC interrupt.
constexpr uint8_t IN2_HIGH = 179, IN2_LOW = 77;  // ADC 8-bit thresholds: 3.5 V / 1.5 V.
constexpr uint8_t PIN_ENC_A = A2, PIN_ENC_B = A1, PIN_ENC_SW = A0;  // A, B, C (button).
// LCD: I2C on A4 (SDA) / A5 (SCL), fixed by the hardware.
// Fade bar graph: LED 1 (bottom, last to go out) .. LED 7 (top, first to go out).
// D13 is also the Nano's onboard L LED, which blinks briefly on reset.
constexpr uint8_t PIN_LED[] = {3, 11, A3, 2, 4, 7, 13};
constexpr uint8_t LED_COUNT = sizeof(PIN_LED);

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
      PORTB &= ~_BV(PB2);
      PORTD &= ~_BV(PD5);
      speedDiv = 0;
      return;
    }
    const bool slow = hz < 16;
    const uint32_t top = (slow ? 125000UL : 1000000UL) / hz - 1;
    TCCR1A = _BV(COM1B0);  // Toggle OC1B (D10) only: D9 is wired to A5 (SCL) on this board.
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
  Serial.println(F("# FADE start: D6 10 kHz -> 0 Hz in 3 s"));
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

// ========================= Fade LEDs: bar graph =============================
// LEDs lit = D6 frequency / 10 kHz x 7, rounded up: all 7 at 10 kHz, the top
// one goes out below 8571 Hz, ... LED 1 goes out when D6 stops (0 Hz).
// Start-up check (runs first, all pins still inputs): drive each output pin
// LOW alone and see whether A4 (SDA) or A5 (SCL) follows it, i.e. that pin is
// wired / bridged to the I2C bus.
void pinsCheckI2c() {
  static const uint8_t PINS[] = {5, 6, 10, 12, 3, 11, A3, 2, 4, 7, 13};
  pinMode(A4, INPUT_PULLUP); pinMode(A5, INPUT_PULLUP);
  delay(2);
  Serial.print(F("# I2C idle: SDA(A4) ")); Serial.print(digitalRead(A4));
  Serial.print(F(" SCL(A5) ")); Serial.println(digitalRead(A5));
  for (uint8_t pin : PINS) {
    pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
    delay(2);
    const bool sda = digitalRead(A4), scl = digitalRead(A5);
    pinMode(pin, INPUT);
    if (!sda || !scl) {
      Serial.print(F("# !! pin ")); Serial.print(pin); Serial.print(F(" pulls "));
      if (!sda) Serial.print(F("SDA(A4) "));
      if (!scl) Serial.print(F("SCL(A5) "));
      Serial.println(F("LOW: it is connected to A4/A5"));
    }
  }
  pinMode(A4, INPUT); pinMode(A5, INPUT);
}

void ledsBegin() {
  for (uint8_t pin : PIN_LED) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
}

uint8_t ledsLit = 0;  // LEDs on now (also drawn on the LCD).

void updateLeds() {
  uint8_t lit = fadeHz <= 0 ? 0 : uint8_t(ceilf(fadeHz * LED_COUNT / FADE_START_HZ));
  if (lit > LED_COUNT) lit = LED_COUNT;
  if (lit == ledsLit) return;
  ledsLit = lit;
  for (uint8_t i = 0; i < LED_COUNT; i++) digitalWrite(PIN_LED[i], i < lit ? HIGH : LOW);
}

// =========================== IN 1 / IN 2: inputs ============================
// IN 1: rising edges on D8 (PCINT0, PB0).
// IN 2: A6 is analog-only (no digital buffer, no pin-change interrupt), so the
// ADC runs free on it at 19.2 kHz (prescaler /64, 8-bit result) and the ADC
// interrupt finds the rising edges: above 3.5 V = edge, armed again below 1.5 V.
// That is 12 samples per period at 1500 Hz; edge times are +/- 52 us, which
// averages out over the 250 ms window. Every 250 ms the
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

volatile uint8_t in1Prev = 0;
ISR(PCINT0_vect) {  // D8 = PB0: IN 1.
  const uint8_t level = PINB & _BV(PB0);
  if (level && !in1Prev) in1.edge();
  in1Prev = level;
}

volatile uint8_t in2Adc = 0;  // Last A6 sample (0-255 = 0-5 V), for the status line.
bool in2High = false;
ISR(ADC_vect) {  // A6: IN 2.
  const uint8_t v = ADCH;
  in2Adc = v;
  if (!in2High && v >= IN2_HIGH) { in2High = true; in2.edge(); }
  else if (in2High && v <= IN2_LOW) in2High = false;
}

void in2Begin() {
  ADMUX = _BV(REFS0) | _BV(ADLAR) | 6;  // AVcc reference, left-adjusted (8-bit in ADCH), channel 6.
  ADCSRB = 0;                            // Free running.
  ADCSRA = _BV(ADEN) | _BV(ADSC) | _BV(ADATE) | _BV(ADIE) | _BV(ADPS2) | _BV(ADPS1);  // /64.
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

// ========================= Display (16x2 I2C LCD) ===========================
// HD44780 LCD with a PCF8574 I2C backpack on A4 (SDA) / A5 (SCL), driven by
// the LiquidCrystal_I2C library (Frank de Brabander, Library Manager), at the
// fixed address LCD_ADDR (0x27; try 0x3F if nothing shows). Only the
// characters that changed are sent.
// Pages (encoder short press): Out, In 1, In 2, FAdE.
enum Page : uint8_t { P_OUT, P_IN1, P_IN2, P_FADE, PAGE_COUNT };
uint8_t page = P_OUT;
const char* const PAGE_NAME[PAGE_COUNT] = {"OUT km/h", "IN 1 km/h", "IN 2 km/h", "FADE D6 Hz"};

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
char lcdShown[2][16];  // What the LCD shows now.

void lcdBegin() {
  Wire.begin();
  Wire.setWireTimeout(3000, true);  // A wiring fault on A4 / A5 can't freeze the box.
  Wire.beginTransmission(LCD_ADDR);
  const uint8_t ack = Wire.endTransmission();
  Serial.print(F("# LCD 0x")); Serial.print(LCD_ADDR, HEX);
  Serial.println(ack == 0 ? F(" answered") : F(" did NOT answer"));
  lcd.init();                       // Initialize the LCD.
  lcd.backlight();                  // Backlight on.
  lcd.setCursor(0, 0);              // Start-up screen for 1 s.
  lcd.print(F("Arduino Nano"));
  lcd.setCursor(0, 1);
  lcd.print(F("Speed/PWM Box"));
  delay(1000);
  lcd.clear();
  memset(lcdShown, 0, sizeof lcdShown);  // Draw everything on the first update.
}

// Send one 16-character line, only where it differs from what is shown.
void lcdLine(uint8_t row, const char* text) {
  char line[16];
  bool end = false;
  for (uint8_t i = 0; i < 16; i++) {
    if (!end && !text[i]) end = true;
    line[i] = end ? ' ' : text[i];
  }
  int8_t cursor = -1;
  for (uint8_t i = 0; i < 16; i++) {
    if (line[i] == lcdShown[row][i]) continue;
    if (cursor != i) lcd.setCursor(i, row);
    lcd.write(uint8_t(line[i]));
    lcdShown[row][i] = line[i];
    cursor = i + 1;
  }
}

// Value x10 as "123.4".
void fmt1(char* out, float v) {
  const long x = long(v * 10 + 0.5f);
  sprintf(out, "%ld.%ld", x / 10, x % 10);
}

bool logic = false;  // D12 state, set in loop().

uint32_t dispLastMs = 0;

void updateDisplay(uint32_t now) {
  if (now - dispLastMs < 150) return;
  dispLastMs = now;
  char l1[24], l2[24], a[12], b[12];
  switch (page) {
    case P_OUT:
      sprintf(l1, "OUT %3d km/h", kmh);
      sprintf(l2, "D10   %4u Hz", outHz);
      break;
    case P_IN1:
      fmt1(a, in1.hz / HZ_PER_KMH); fmt1(b, in1.hz);
      sprintf(l1, "IN1 D8 %skm/h", a);
      sprintf(l2, "%sHz  10k:%s", b, logic ? "ON" : "--");
      break;
    case P_IN2:
      fmt1(a, in2.hz / HZ_PER_KMH); fmt1(b, in2.hz);
      sprintf(l1, "IN2 A6 %skm/h", a);
      sprintf(l2, "%s Hz", b);
      break;
    default: {  // P_FADE: frequency and the same bar as the 7 LEDs.
      sprintf(l1, "FADE D6 %5u Hz", uint16_t(fadeHz + 0.5f));
      memcpy(l2, "[       ] ", 10);
      for (uint8_t i = 0; i < ledsLit; i++) l2[1 + i] = char(0xFF);  // Full block.
      strcpy(l2 + 10, fading ? "RUN" : "KNOB");
      break;
    }
  }
  lcdLine(0, l1);
  lcdLine(1, l2);
}

// =============================== Setup / loop ===============================
void setup() {
  Serial.begin(115200);
  Serial.println(F("# NANO SPEED / PWM BOX start"));
  pinsCheckI2c();

  pinMode(PIN_OUT_A, INPUT);  // D9 unused: on this board it is wired to A5 (LCD SCL).
  pinMode(PIN_OUT_B, OUTPUT); pinMode(PIN_SPEED, OUTPUT);
  digitalWrite(PIN_OUT_B, LOW); digitalWrite(PIN_SPEED, LOW);
  pinMode(PIN_LOGIC, OUTPUT); digitalWrite(PIN_LOGIC, LOW);
  setOutHz(0);
  fadeBegin();
  ledsBegin();

  pinMode(PIN_IN1, INPUT_PULLUP);  // D8 pull-up: unconnected reads HIGH (0 Hz), no mains hum on D12.
  in1Prev = PINB & _BV(PB0);
  PCMSK0 |= _BV(PCINT0);  // D8.
  PCICR |= _BV(PCIE0);
  in2Begin();  // A6.

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  encPrev = (((PINC >> PC2) & 1) << 1) | ((PINC >> PC1) & 1);
  PCMSK1 |= _BV(PCINT10) | _BV(PCINT9);  // A2 (A), A1 (B).
  PCICR |= _BV(PCIE1);

  lcdBegin();
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

    if (page == P_FADE) {
      // FAdE page: the knob sets D6 by hand (0-10 kHz), the LEDs follow.
      // Turning during a running fade stops the fade and takes over from there.
      fading = false;
      const long hz = constrain(long(fadeHz + 0.5f) + clicks * long(fast ? FADE_FAST_STEP_HZ : FADE_STEP_HZ),
                                0L, long(FADE_START_HZ));
      setFadeHz(hz);
      Serial.print(F("# D6 set ")); Serial.print(hz); Serial.println(F(" Hz"));
    } else {
      // Out / In 1 / In 2 pages: the knob sets the speed (D9 / D10 / D5).
      // On an In page it stays there, so a looped-back output can be watched.
      kmh = constrain(kmh + clicks * (fast ? FAST_STEP_KMH : 1), 0, MAX_KMH);
      applySpeed();
      Serial.print(F("# SPEED ")); Serial.print(kmh); Serial.print(F(" km/h = "));
      Serial.print(outHz); Serial.print(F(" Hz on D9 / D10, D5 "));
      Serial.print(kmh); Serial.println(F(" Hz"));
    }
  }

  // 2. Encoder button: short press = next page, long press (1 s) = D6 fade.
  static bool btnPrev = HIGH, longDone = false;
  static uint32_t btnDownMs = 0, btnChangeMs = 0;
  static bool btnStable = HIGH;
  const bool raw = digitalRead(PIN_ENC_SW);
  if (raw != btnPrev) { btnPrev = raw; btnChangeMs = now; }
  if (now - btnChangeMs >= 30 && raw != btnStable) {
    btnStable = raw;
    if (btnStable == LOW) { btnDownMs = now; longDone = false; }
    else if (!longDone) {
      page = (page + 1) % PAGE_COUNT;
      Serial.print(F("# DISPLAY page: ")); Serial.println(PAGE_NAME[page]);
    }
  }
  if (btnStable == LOW && !longDone && now - btnDownMs >= 1000) {
    longDone = true;
    startFade(now);
    page = P_FADE;  // Show the fade on the display.
  }

  // 3. D6 fade and its LED bar graph.
  updateFade(now);
  updateLeds();

  // 4. Inputs every 250 ms, and the 10 km/h logic output.
  static uint32_t measureMs = 0;
  if (now - measureMs >= 250) {
    measureMs = now;
    in1.measure();
    in2.measure();
    const float in1Kmh = in1.hz / HZ_PER_KMH;
    if (!logic && in1Kmh >= LOGIC_ON_KMH) { logic = true; Serial.println(F("# D12 ON: IN 1 reached 10 km/h")); }
    else if (logic && in1Kmh < LOGIC_OFF_KMH) logic = false;
    digitalWrite(PIN_LOGIC, logic ? HIGH : LOW);
  }

  // 5. Display.
  updateDisplay(now);

  // 6. Status line.
  static uint32_t reportMs = 0;
  if (now - reportMs >= 500) {
    reportMs = now;
    Serial.print(F("OUT ")); Serial.print(kmh); Serial.print(F(" km/h, D9/D10 ")); Serial.print(outHz);
    Serial.print(F(" Hz, D5 ")); Serial.print(outHz ? kmh : 0);
    Serial.print(F(" Hz | IN1 D8 ")); Serial.print(in1.hz, 1); Serial.print(F(" Hz = "));
    Serial.print(in1.hz / HZ_PER_KMH, 1);
    Serial.print(F(" km/h | IN2 A6 ")); Serial.print(in2.hz, 1); Serial.print(F(" Hz = "));
    Serial.print(in2.hz / HZ_PER_KMH, 1);
    Serial.print(F(" km/h (")); Serial.print(in2Adc * (5.0f / 255), 1); Serial.print(F(" V)"));
    Serial.print(F(" | 10KPH D12 ")); Serial.print(digitalRead(PIN_LOGIC) ? F("HIGH") : F("LOW"));
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
