// Nano Speed / PWM Box
//
// Arduino Nano (ATmega328P). One Nano, three sections (from the hand drawing):
//   1. Fade:  an encoder short press starts a 15 kHz PWM on D3 that fades from
//             100 % to 0 % duty in 3 s.
//   2. Inputs: Freq 1 / Freq 2 input are copied to Freq 1 / Freq 2 output;
//             D12 (60 Hz logic 1) goes HIGH when Freq 1 input reaches 60 Hz
//             (10 km/h) and stays HIGH (LOCKED) until the power is restarted or RESET is pressed.
//   3. Pot:   the EC11 encoder sets 0-250 km/h; Freq 1 / Freq 2 output in pot give
//             km/h x 6 Hz (0-1500 Hz). A 0.96" I2C OLED, if connected, shows the speed
//             (Freq / 6) on a gauge.
//
// Pins:
//   D10  OUT  Freq 1 output: copy of Freq 1 input (D8)
//   D9   OUT  Freq 2 output: copy of Freq 2 input (D7)
//   D8   IN   Freq 1 input (pin-change interrupt, internal pull-up)
//   D7   IN   Freq 2 input (pin-change interrupt, internal pull-up)
//   D6   OUT  Freq 1 output in pot, 0-1500 Hz square wave, 50 % duty
//   D5   OUT  Freq 2 output in pot, the same frequency as D6, in phase
//   D12  OUT  60 Hz logic 1: HIGH (LOCKED) once Freq 1 input >= 60 Hz (10 km/h);
//             only a power restart or the RESET button makes it LOW again
//   D3   OUT  Fade: 15 kHz PWM, 100 % -> 0 % duty in 3 s on an encoder short press, then LOW;
//             or, knob on D3, a 0-15000 Hz square wave set by hand, 50 % duty (Timer2 OC2B)
//   Encoder (EC11) A -> A2, B -> A1, C (button) -> A0, + -> 5V, GND -> GND
//        turn: speed 0-250 km/h, 1 km/h per click (5 when spun fast), or D3 Hz
//        short press: start the D3 15 kHz fade (again)
//        long press (1 s): knob on speed <-> knob on D3 (0-15000 Hz, 100 Hz per click, 500 fast)
//   A4 / A5   OLED SDA / SCL (SSD1306 128x64 I2C, 0x3C or 0x3D), auto-detected
//   DIP switch (3 pins, each switch pin -> GND, internal pull-ups; ON = closed = LOW,
//             open / not connected = OFF; DIP_TO_GND = false for a switch to 5V):
//        1 -> D2   D12 60 Hz logic 1:  OFF = D12 LOW and not locking, ON = working
//        2 -> D4   D6 / D5 encoder frequency out: OFF = stopped LOW, ON = back at the knob speed
//        3 -> D11  D3 15 kHz fade out: OFF = D3 LOW, no fade (short press ignored), ON = enabled
// USB Serial Monitor: 115200 baud, a status line twice a second.
//
// Timers: Timer0 = millis(), Timer1 = D6 / D5 (toggled in its interrupt),
// Timer2 = D3 PWM, so analogWrite(), tone() and Servo can't be used.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>      // Library Manager: "Adafruit GFX Library".
#include <Adafruit_SSD1306.h>  // Library Manager: "Adafruit SSD1306".
#include <util/atomic.h>

// ----------------------------- Settings -------------------------------------
constexpr int MAX_KMH = 250;            // 250 km/h = 1500 Hz.
constexpr int HZ_PER_KMH = 6;           // Speed formula: km/h = Hz / 6.
constexpr float LOCK_HZ = 59.5f;        // D12 locks at 60 Hz (10 km/h), with 0.5 Hz measuring margin.
constexpr uint32_t FADE_MS = 3000;      // D3: 100 % -> 0 % in this time.
constexpr int FAST_STEP_KMH = 5;       // Encoder spun quickly.
constexpr unsigned long FAST_MS = 40;  // Clicks closer than this = fast.
constexpr int8_t STEPS_PER_CLICK = 4;  // Encoder transitions per detent (EC11).
constexpr bool ENC_REVERSE = false;    // true if clockwise makes it slower.
// DIP switch wiring. true: switch common -> GND, internal pull-ups, ON = pin LOW
// (an open switch, or no DIP switch, reads OFF). false: common -> 5V, a 10 k
// pull-down resistor from each pin to GND, ON = pin HIGH.
constexpr bool DIP_TO_GND = true;

// ------------------------------- Pins ---------------------------------------
constexpr uint8_t PIN_COPY1 = 10, PIN_COPY2 = 9;  // Freq 1 / 2 output (copies of the inputs).
constexpr uint8_t PIN_IN1 = 8, PIN_IN2 = 7;       // Freq 1 / 2 input.
constexpr uint8_t PIN_POT1 = 6, PIN_POT2 = 5;     // Freq 1 / 2 output in pot.
constexpr uint8_t PIN_LOGIC = 12, PIN_FADE = 3;
constexpr uint8_t PIN_ENC_A = A2, PIN_ENC_B = A1, PIN_ENC_SW = A0;  // EC11 A, B, C (button).
constexpr uint8_t PIN_DIP_LOGIC = 2, PIN_DIP_SPEED = 4, PIN_DIP_FADE = 11;  // DIP 1 / 2 / 3.

// DIP switch state, ON = enabled (read at start-up and in updateDip()).
bool dipLogic = true, dipSpeed = true, dipFade = true;

// ========= D6 / D5: Freq 1 / 2 output in pot (Timer1 interrupt) ============
// D5 / D6 are Timer0's pins (millis), so Timer1 runs in CTC mode without pins
// and its compare interrupt toggles D6 and D5 together (one write: always in
// phase), 50 % duty. f = F_CPU / (2 x N x (TOP+1)).
//   /8 :  f = 1 MHz / (TOP+1)    for 16-1500 Hz
//   /64:  f = 125 kHz / (TOP+1)  below 16 Hz (6 Hz = 1 km/h)
uint16_t outHz = 0;

ISR(TIMER1_COMPA_vect) { PIND = _BV(PD6) | _BV(PD5); }  // Toggle D6 and D5.

void setOutHz(uint16_t hz) {
  outHz = hz;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (hz == 0) {  // Stopped: D6 / D5 LOW.
      TCCR1B = 0;
      TIMSK1 &= ~_BV(OCIE1A);
      PORTD &= ~(_BV(PD6) | _BV(PD5));
      return;
    }
    const bool slow = hz < 16;
    const uint32_t top = (slow ? 125000UL : 1000000UL) / hz - 1;
    TCCR1A = 0;  // No hardware pins (D9 / D10 are the input copies).
    TCCR1B = _BV(WGM12) | (slow ? (_BV(CS11) | _BV(CS10)) : _BV(CS11));  // CTC, /64 or /8.
    OCR1A = top;
    if (TCNT1 > top) TCNT1 = 0;  // New TOP below the count: don't wait for the wrap.
    TIMSK1 |= _BV(OCIE1A);
  }
}

// ================== D3: 15 kHz PWM fade 100 % -> 0 % (Timer2) ===============
// Fast PWM with TOP = OCR2A = 132, prescaler /8: 16 MHz / 8 / 133 = 15.04 kHz.
// OC2B (D3) is HIGH for OCR2B+1 of the 133 counts. 100 % and 0 % disconnect
// the timer from the pin and hold it HIGH / LOW (no 1-count glitch).
constexpr uint8_t PWM_TOP = 132;
bool fading = false;
uint32_t fadeStartMs = 0;
float fadeDuty = 0;  // 0-1, what D3 gives now.

void setFadeDuty(float d) {
  fadeDuty = constrain(d, 0.0f, 1.0f);
  const uint8_t n = uint8_t(fadeDuty * (PWM_TOP + 1) + 0.5f);  // HIGH counts of 133.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (n == 0 || n > PWM_TOP) {
      TCCR2A &= ~_BV(COM2B1);  // Pin back to PORTD.
      if (n == 0) PORTD &= ~_BV(PD3); else PORTD |= _BV(PD3);
    } else {
      OCR2B = n - 1;  // Double-buffered: takes effect at the next period.
      TCCR2A |= _BV(COM2B1);
    }
  }
}

void fadeBegin() {
  pinMode(PIN_FADE, OUTPUT);
  digitalWrite(PIN_FADE, LOW);
  OCR2A = PWM_TOP;
  TCCR2A = _BV(WGM21) | _BV(WGM20);  // Fast PWM, TOP = OCR2A (with WGM22 below).
  TCCR2B = _BV(WGM22) | _BV(CS21);   // /8.
  setFadeDuty(0);
}

// ============ D3: 0-15000 Hz by hand (Timer2, long press = knob) ============
// Same fast PWM mode, but TOP (OCR2A) and the prescaler set the frequency:
// f = F_CPU / (N x (TOP+1)), the smallest N that fits TOP in 8 bits, 50 %
// duty (OCR2B = half). 15000 Hz -> /8, TOP 132 (15.04 kHz); 100 Hz -> /1024,
// TOP 155. Within about 0.4 % of the set value. 0 Hz = LOW.
constexpr uint16_t D3_MAX_HZ = 15000, D3_STEP_HZ = 100, D3_FAST_STEP_HZ = 500;
uint16_t d3Hz = 0;  // Set by hand (0 while the fade runs).

void setD3Hz(uint16_t hz) {
  d3Hz = hz;
  fadeDuty = 0;
  struct Pre { uint16_t n; uint8_t cs; };
  static const Pre PRE[] = {{1, 1}, {8, 2}, {32, 3}, {64, 4}, {128, 5}, {256, 6}, {1024, 7}};
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (hz == 0) {
      TCCR2A &= ~_BV(COM2B1);
      PORTD &= ~_BV(PD3);
      return;
    }
    for (const Pre& p : PRE) {
      const uint32_t top = (F_CPU / p.n + hz / 2) / hz - 1;
      if (top > 255) continue;
      OCR2A = top;
      OCR2B = (top + 1) / 2 - 1;  // 50 %.
      TCCR2B = _BV(WGM22) | p.cs;
      if (TCNT2 > top) TCNT2 = 0;
      TCCR2A |= _BV(COM2B1);
      return;
    }
  }
}

void startFade(uint32_t now) {
  fading = true;
  fadeStartMs = now;
  d3Hz = 0;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {  // Back to 15 kHz (the knob may have changed it).
    OCR2A = PWM_TOP;
    TCCR2B = _BV(WGM22) | _BV(CS21);
    if (TCNT2 > PWM_TOP) TCNT2 = 0;
  }
  setFadeDuty(1);
  Serial.println(F("# FADE start: D3 15 kHz, 100 % -> 0 % in 3 s"));
}

void updateFade(uint32_t now) {
  if (!fading) return;
  static uint32_t lastMs = 0;
  if (now - lastMs < 5) return;  // New duty every 5 ms.
  lastMs = now;
  const uint32_t t = now - fadeStartMs;
  if (t >= FADE_MS) {
    fading = false;
    setFadeDuty(0);
    Serial.println(F("# FADE end: D3 0 % (LOW)"));
    return;
  }
  setFadeDuty(1.0f - float(t) / FADE_MS);
}

// Start-up check (runs first, all pins still inputs): drive each output pin
// LOW alone and see whether A4 (SDA) or A5 (SCL) follows it, i.e. that pin is
// wired / bridged to the I2C bus (D9 was, on the earlier perfboard).
void pinsCheckI2c() {
  static const uint8_t PINS[] = {10, 9, 6, 5, 12, 3};
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

// ===================== Freq 1 / 2 input, copies, measuring ==================
// Every change on D8 is written to D10 at once, every change on D7 to D9
// (pin-change interrupts, a few us late); rising edges are counted.
// Every 250 ms the frequency is the edges counted / time between the first
// and last edge; with only one edge in the window, the last period is used.
// No edge for 1 s (below 1 Hz) = 0 Hz.
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
ISR(PCINT0_vect) {  // D8 = PB0: Freq 1 input -> D10 = PB2.
  const uint8_t level = PINB & _BV(PB0);
  if (level) PORTB |= _BV(PB2); else PORTB &= ~_BV(PB2);
  if (level && !in1Prev) in1.edge();
  in1Prev = level;
}
ISR(PCINT2_vect) {  // D7 = PD7: Freq 2 input -> D9 = PB1.
  const uint8_t level = PIND & _BV(PD7);
  if (level) PORTB |= _BV(PB1); else PORTB &= ~_BV(PB1);
  if (level && !in2Prev) in2.edge();
  in2Prev = level;
}

// ============================== Encoder =====================================
// Quadrature decoding in the A2 (A) / A1 (B) pin-change interrupt (PCINT1),
// with the usual transition table; STEPS_PER_CLICK transitions = one click.
const int8_t ENC_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
volatile uint8_t encPrev = 0;
volatile int8_t encAcc = 0, encClicks = 0;

ISR(PCINT1_vect) {  // A2 = PC2 (A), A1 = PC1 (B).
  const uint8_t now = (((PINC >> PC2) & 1) << 1) | ((PINC >> PC1) & 1);
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
  encPrev = (((PINC >> PC2) & 1) << 1) | ((PINC >> PC1) & 1);
  PCMSK1 |= _BV(PCINT10) | _BV(PCINT9);  // A2 (A), A1 (B).
  PCICR |= _BV(PCIE1);
}

int kmh = 0;  // Speed set by the encoder.
bool knobD3 = false;  // Long press toggles: false = knob sets the speed, true = D3 Hz.

// Turns: speed (1 km/h per click, 5 when spun fast; D6 / D5 = km/h x 6 Hz),
// or, with the knob on D3, D3 0-15000 Hz (100 Hz per click, 500 fast).
// Turning on D3 during a fade stops the fade and carries on from 0 Hz.
void updateEncoder(uint32_t now) {
  static uint32_t lastClickMs = 0;
  int8_t clicks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { clicks = encClicks; encClicks = 0; }
  if (!clicks) return;
  if (ENC_REVERSE) clicks = -clicks;
  const bool fast = clicks > 1 || clicks < -1 || now - lastClickMs < FAST_MS;
  lastClickMs = now;
  if (knobD3) {
    fading = false;
    const long hz = constrain(long(d3Hz) + clicks * long(fast ? D3_FAST_STEP_HZ : D3_STEP_HZ), 0L, long(D3_MAX_HZ));
    if (dipFade) setD3Hz(hz); else d3Hz = hz;  // DIP 3 OFF: kept, D3 stays LOW until ON.
    Serial.print(F("# D3 ")); Serial.print(hz); Serial.println(dipFade ? F(" Hz") : F(" Hz (DIP 3 OFF: D3 LOW)"));
    return;
  }
  kmh = constrain(kmh + clicks * (fast ? FAST_STEP_KMH : 1), 0, MAX_KMH);
  if (dipSpeed) setOutHz(uint16_t(kmh) * HZ_PER_KMH);  // DIP 2 OFF: D6 / D5 stay LOW until ON.
  Serial.print(F("# SPEED ")); Serial.print(kmh); Serial.print(F(" km/h = "));
  Serial.print(outHz); Serial.println(F(" Hz on D6 / D5"));
}

// ========================= Display: OLED, auto-detected =====================
// OLED on A4 (SDA) / A5 (SCL): looked for at start-up (I2C probe of 0x3C,
// then 0x3D), then every 2 s while it is missing, so it can be plugged in
// while the box runs.
bool oledOk = false;
uint32_t oledProbeMs = 0;

//   F73%                   LOCK     <- fade duty while fading / D12 locked
//            gauge 0-250 kph
//   720Hz      120 kph              <- output in pot Hz, speed = Hz / 6
//   IN1 120   IN2 120               <- input speeds, km/h
Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000UL, 400000UL);

bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void oledDetect() {
  oledProbeMs = millis();
  static bool wireOn = false, told = false;
  if (!wireOn) {
    wireOn = true;
    Wire.begin();
    Wire.setClock(400000UL);
    Wire.setWireTimeout(3000, true);  // A wiring fault on A4 / A5 can't freeze the box.
  }
  uint8_t addr = 0;
  if (i2cProbe(0x3C)) addr = 0x3C;
  else if (i2cProbe(0x3D)) addr = 0x3D;
  oledOk = addr && oled.begin(SSD1306_SWITCHCAPVCC, addr, true, false);
  if (!oledOk) {
    if (!told) { told = true; Serial.println(F("# OLED not found (0x3C / 0x3D on A4 / A5)")); }
    return;
  }
  Serial.print(F("# OLED found at 0x")); Serial.println(addr, HEX);
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(28, 20); oled.print(F("Arduino Nano"));
  oled.setCursor(22, 34); oled.print(F("Speed/PWM Box"));
  oled.display();
  delay(1000);
}

constexpr int16_t GX = 64, GY = 36, GR = 34;  // Gauge centre and radius.

// Point on the gauge at v km/h (0 = left, 250 = right), radius r.
void gaugePoint(float v, float r, int16_t& x, int16_t& y) {
  const float a = PI + v / MAX_KMH * PI;
  x = GX + int16_t(lroundf(cosf(a) * r));
  y = GY + int16_t(lroundf(sinf(a) * r));
}

bool locked = false;  // D12, set in loop().

// ============================ DIP switch ====================================
// Read every 10 ms; a switch must read the same for 50 ms before it counts
// (contact bounce). Each change takes effect at once:
//   DIP 1 (D2)  D12 logic: OFF = D12 LOW, no locking; ON = D12 shows the lock again
//               (a lock from before is kept, a new one can happen).
//   DIP 2 (D4)  D6 / D5: OFF = stopped LOW (the knob still sets the speed);
//               ON = back at the knob speed.
//   DIP 3 (D11) D3: OFF = a running fade stops, D3 LOW, short press ignored;
//               ON = the fade works again (and a D3 Hz set by hand comes back).
void dipBegin() {
  const uint8_t mode = DIP_TO_GND ? INPUT_PULLUP : INPUT;  // To 5V: external pull-downs.
  pinMode(PIN_DIP_LOGIC, mode);
  pinMode(PIN_DIP_SPEED, mode);
  pinMode(PIN_DIP_FADE, mode);
  delay(2);
}

inline bool dipReadOn(uint8_t pin) { return (digitalRead(pin) == LOW) == DIP_TO_GND; }

void applyDip(uint8_t i, bool on) {
  switch (i) {
    case 0:
      dipLogic = on;
      digitalWrite(PIN_LOGIC, on && locked ? HIGH : LOW);
      Serial.println(on ? F("# DIP 1 (D2) ON: D12 60 Hz logic enabled") : F("# DIP 1 (D2) OFF: D12 60 Hz logic disabled (LOW)"));
      break;
    case 1:
      dipSpeed = on;
      setOutHz(on ? uint16_t(kmh) * HZ_PER_KMH : 0);
      Serial.println(on ? F("# DIP 2 (D4) ON: D6 / D5 encoder frequency out enabled") : F("# DIP 2 (D4) OFF: D6 / D5 encoder frequency out disabled (LOW)"));
      break;
    default:
      dipFade = on;
      if (!on) {
        const uint16_t keep = d3Hz;
        fading = false;
        setFadeDuty(0);  // D3 LOW.
        setD3Hz(0);
        d3Hz = keep;     // Comes back when DIP 3 is ON again.
      } else if (d3Hz) setD3Hz(d3Hz);
      Serial.println(on ? F("# DIP 3 (D11) ON: D3 15 kHz fade out enabled") : F("# DIP 3 (D11) OFF: D3 15 kHz fade out disabled (LOW)"));
      break;
  }
}

// force: apply all three now (start-up).
void updateDip(uint32_t now, bool force = false) {
  static const uint8_t PINS[3] = {PIN_DIP_LOGIC, PIN_DIP_SPEED, PIN_DIP_FADE};
  static bool stable[3] = {true, true, true}, last[3] = {true, true, true};
  static uint32_t changeMs[3] = {0, 0, 0}, readMs = 0;
  if (!force && now - readMs < 10) return;
  readMs = now;
  for (uint8_t i = 0; i < 3; i++) {
    const bool on = dipReadOn(PINS[i]);
    if (force) { last[i] = stable[i] = on; applyDip(i, on); continue; }
    if (on != last[i]) { last[i] = on; changeMs[i] = now; }
    if (on != stable[i] && now - changeMs[i] >= 50) { stable[i] = on; applyDip(i, on); }
  }
}

// The gauge shows the speed (0-250 kph), or, with the knob on D3, the D3
// frequency (0-15000 Hz on the same arc: ticks every 1500 Hz, long every 3000).
void drawOled() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  const float gaugeV = knobD3 ? float(d3Hz) * MAX_KMH / D3_MAX_HZ : kmh;  // Needle, in arc units.
  // Gauge: arc, ticks every 1/10 of the scale (long every 1/5), needle.
  for (int d = 0; d <= 180; d += 3) {
    int16_t x, y;
    gaugePoint(d * MAX_KMH / 180.0f, GR, x, y);
    oled.drawPixel(x, y, SSD1306_WHITE);
  }
  for (int v = 0; v <= MAX_KMH; v += 25) {
    int16_t x1, y1, x2, y2;
    gaugePoint(v, GR, x1, y1);
    gaugePoint(v, GR - (v % 50 ? 3 : 6), x2, y2);
    oled.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
  }
  int16_t nx, ny;
  gaugePoint(gaugeV, GR - 8, nx, ny);
  oled.drawLine(GX, GY, nx, ny, SSD1306_WHITE);
  oled.fillCircle(GX, GY, 2, SSD1306_WHITE);
  // Top corners: fade duty, LOCK.
  if (fading) {
    oled.setCursor(0, 0);
    oled.print(F("F")); oled.print(int(fadeDuty * 100 + 0.5f)); oled.print('%');
  }
  if (!dipLogic) { oled.setCursor(92, 1); oled.print(F("D12off")); }  // DIP 1 OFF: D12 held LOW.
  else if (locked) {
    oled.fillRect(102, 0, 26, 9, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(104, 1); oled.print(F("LOCK"));
    oled.setTextColor(SSD1306_WHITE);
  }
  // Big value under the gauge, the scale ends outside the arc.
  char s[8];
  oled.setTextSize(2);
  snprintf(s, sizeof s, "%u", knobD3 ? d3Hz : unsigned(kmh));
  const int16_t w = strlen(s) * 12;
  oled.setCursor(GX - w / 2, 40); oled.print(s);
  oled.setTextSize(1);
  oled.setCursor(GX + w / 2 + 3, 47); oled.print(knobD3 ? F("Hz") : F("kph"));
  oled.setCursor(GX - GR - 8, GY - 7); oled.print(F("0"));
  oled.setCursor(GX + GR + 3, GY - 7); oled.print(knobD3 ? F("15k") : F("250"));
  // Bottom row, knob on D3 (inverted): "KNOB: D3", Hz only; else the D6 Hz
  // beside the speed and the inputs in km/h.
  if (knobD3) {
    if (!fading) { oled.setCursor(0, 0); oled.print(dipFade ? F("D3") : F("D3 OFF")); }  // Top left (the fade % goes there while fading).
    oled.fillRect(0, 56, 128, 8, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(2, 57); oled.print(F("KNOB: D3  0-15000 Hz"));
    oled.setTextColor(SSD1306_WHITE);
    oled.display();
    return;
  }
  oled.setCursor(0, 47);
  if (dipSpeed) { oled.print(outHz); oled.print(F("Hz")); }
  else oled.print(F("OUT OFF"));  // DIP 2 OFF: D6 / D5 held LOW.
  if (!dipFade) { oled.setCursor(0, 0); oled.print(F("D3 OFF")); }  // DIP 3 OFF (never fading then).
  snprintf(s, sizeof s, "%3d", int(in1.hz / HZ_PER_KMH + 0.5f));
  oled.setCursor(0, 57); oled.print(F("IN1 ")); oled.print(s);
  snprintf(s, sizeof s, "%3d", int(in2.hz / HZ_PER_KMH + 0.5f));
  oled.setCursor(72, 57); oled.print(F("IN2 ")); oled.print(s);
  oled.display();  // ~25 ms; the outputs run on timers / interrupts meanwhile.
}

void updateDisplay(uint32_t now) {
  if (!oledOk && now - oledProbeMs >= 2000) oledDetect();
  static uint32_t lastMs = 0;
  if (now - lastMs < 100) return;
  lastMs = now;
  if (oledOk) drawOled();
}

// =============================== Setup / loop ===============================
void setup() {
  Serial.begin(115200);
  Serial.println(F("# NANO SPEED / PWM BOX start"));
  pinsCheckI2c();

  static const uint8_t OUTS[] = {PIN_COPY1, PIN_COPY2, PIN_POT1, PIN_POT2, PIN_LOGIC};
  for (uint8_t pin : OUTS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  encoderBegin();
  setOutHz(0);
  fadeBegin();
  dipBegin();
  updateDip(millis(), true);

  // Inputs with pull-up: unconnected reads HIGH (0 Hz), no mains hum, and
  // open-collector Hall sensors work. The copies start at the input level.
  pinMode(PIN_IN1, INPUT_PULLUP);
  pinMode(PIN_IN2, INPUT_PULLUP);
  delay(1);
  in1Prev = PINB & _BV(PB0);
  in2Prev = PIND & _BV(PD7);
  digitalWrite(PIN_COPY1, in1Prev ? HIGH : LOW);
  digitalWrite(PIN_COPY2, in2Prev ? HIGH : LOW);
  PCMSK0 |= _BV(PCINT0);   // D8.
  PCMSK2 |= _BV(PCINT23);  // D7.
  PCICR |= _BV(PCIE0) | _BV(PCIE2);

  oledDetect();
}

void loop() {
  const uint32_t now = millis();

  // 0. DIP switch: D12 logic / D6-D5 speed out / D3 fade on or off.
  updateDip(now);

  // 1. Encoder turns -> speed -> D6 / D5.
  updateEncoder(now);

  // 2. Encoder button (A0): short press starts the 3 s 15 kHz fade (again),
  //    long press (1 s) switches the knob between speed and D3 Hz.
  static bool swPrev = HIGH, swStable = HIGH, longDone = false;
  static uint32_t swChangeMs = 0, swDownMs = 0;
  const bool raw = digitalRead(PIN_ENC_SW);
  if (raw != swPrev) { swPrev = raw; swChangeMs = now; }
  if (now - swChangeMs >= 30 && raw != swStable) {
    swStable = raw;
    if (swStable == LOW) { swDownMs = now; longDone = false; }
    else if (!longDone) {  // Short press (released before 1 s): 15 kHz fade.
      if (dipFade) startFade(now);
      else Serial.println(F("# FADE ignored: DIP 3 (D11) OFF"));
    }
  }
  if (swStable == LOW && !longDone && now - swDownMs >= 1000) {
    longDone = true;  // Once per press.
    knobD3 = !knobD3;
    Serial.println(knobD3 ? F("# KNOB: D3 frequency 0-15000 Hz") : F("# KNOB: speed (D6 / D5)"));
  }

  // 3. D3 fade.
  updateFade(now);

  // 4. Inputs every 250 ms, and the 60 Hz LOCKED logic output.
  static uint32_t measureMs = 0;
  if (now - measureMs >= 250) {
    measureMs = now;
    in1.measure();
    in2.measure();
    if (dipLogic && !locked && in1.hz >= LOCK_HZ) {  // DIP 1 OFF: no locking.
      locked = true;
      digitalWrite(PIN_LOGIC, HIGH);
      Serial.println(F("# D12 LOCKED HIGH: Freq 1 input reached 60 Hz (power restart or RESET to clear)"));
    }
  }

  // 5. Display.
  updateDisplay(now);

  // 6. Status line.
  static uint32_t reportMs = 0;
  if (now - reportMs >= 500) {
    reportMs = now;
    Serial.print(F("SPEED"));
    Serial.print(F(" ")); Serial.print(kmh); Serial.print(F(" km/h, D6/D5 ")); Serial.print(outHz);
    Serial.print(F(" Hz | IN1 D8 ")); Serial.print(in1.hz, 1); Serial.print(F(" Hz -> D10"));
    Serial.print(F(" | IN2 D7 ")); Serial.print(in2.hz, 1); Serial.print(F(" Hz -> D9"));
    Serial.print(F(" | D12 ")); Serial.print(!dipLogic ? F("OFF (DIP 1)") : locked ? F("LOCKED") : F("LOW"));
    if (fading) { Serial.print(F(" | D3 FADE ")); Serial.print(int(fadeDuty * 100 + 0.5f)); Serial.print(F(" %")); }
    else { Serial.print(F(" | D3 ")); Serial.print(d3Hz); Serial.print(F(" Hz")); }
    Serial.print(knobD3 ? F(" | KNOB D3") : F(" | KNOB SPEED"));
    Serial.print(F(" | DIP ")); Serial.print(dipLogic ? F("1on ") : F("1off "));
    Serial.print(dipSpeed ? F("2on ") : F("2off ")); Serial.print(dipFade ? F("3on") : F("3off"));
    Serial.print(F(" | EC11 A(A2) ")); Serial.print(digitalRead(PIN_ENC_A)); Serial.print(F(" B(A1) ")); Serial.print(digitalRead(PIN_ENC_B)); Serial.print(F(" C(A0) ")); Serial.println(swStable == LOW ? F("pressed") : F("up"));
  }
}
