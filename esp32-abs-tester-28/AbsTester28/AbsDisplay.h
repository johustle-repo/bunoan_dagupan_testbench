#pragma once
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "AaGraphics.h"
#include "FreqInput.h"  // freqInput::guard() around the touch panel's T_CS.
#include "LogoBitmap.h"  // TRN TECHRONIKS power-up logo.

// ABS tester on the 2.8" ILI9341 TFT (landscape 320 x 240). Everything is
// drawn anti-aliased (AaGraphics.h) with the Barlow Semi Condensed fonts in
// AaFonts.h, into off-screen strips that are pushed in one SPI burst each, so
// nothing flickers. Two pages: swipe left for page 2, swipe right for page 1.
//   Header (both pages): SEN VCC (REF1) | BAT.
//   Page 1: one FRONT (D8) speedometer, 0-250 km/h, with the REAR (D7)
//           speed bar below.
//   Page 2: SPEED SENSOR gauge for the ESP32's own frequency input (GPIO34),
//           0-2000 Hz shown as 0-200 km/h, with the SPEED bar below (km/h);
//           NO SIGNAL when no pulses arrive.
//   Page 3: one 0-100 Hz DECLOG gauge (the sender's D2) and the DECLOG bar
//           below (0-100 Hz shown as 0-100 %).
//   Footer: PAGE n/3 | ABS light | page dots | TCS light | page name
//   (the ABS / TCS lights on page 1 only).
// Page change: the Mega's A5 page pot (0-341 page 1, 342-682 page 2,
// 683-1023 page 3), or a swipe left / right on the touchscreen.
// Page 1 needs the sender (WAITING FOR DATA / NO LINK until data arrives, then
// the switch-on self-test); pages 2 and 3 work without it (DECLOG then
// shows 0 and NO LINK).
// Touch: the XPT2046 shares the display's SPI bus (T_CLK 18, T_DIN 23,
// T_DO 19) with its own chip select, T_CS on GPIO33. Only the direction of a
// swipe is used, so no calibration. T_IRQ is not used (unconnected or on
// GPIO35, which is then not used for anything else); it must not be wired
// to GPIO34 (that is the frequency input).
constexpr uint8_t TFT_SCK = 18, TFT_MISO = 19, TFT_MOSI = 23;
constexpr uint8_t TFT_CS = 27, TFT_DC = 25, TFT_RST = 26, TOUCH_CS = 33;
constexpr uint32_t GAUGE_REFRESH_MS = 40, TEXT_REFRESH_MS = 100;
constexpr uint32_t SELF_TEST_MS = 1600;  // Switch-on sweep.
// Swipe: at least SWIPE_MIN raw touch units sideways (about 1/6 of the screen
// width), twice as far sideways as up / down, finger lifted within 1.2 s.
// If left and right come out swapped on a panel, set SWIPE_FLIP to true.
constexpr uint32_t TOUCH_POLL_MS = 20, SWIPE_MAX_MS = 1200;
constexpr int TOUCH_PRESSURE = 400, SWIPE_MIN = 600;
constexpr bool SWIPE_FLIP = false;
// REAR (D7) and FRONT (D8): wheel speed = Hz / 6 km/h, 0-250 km/h, the same
// formula as the Nano. DECLOG: the D2 frequency, 0-100 Hz = 0-100 %.
constexpr float ABS_SPEED_MAX = 250, SPEED_RED = 210, HZ_PER_KMH = 6;
constexpr float DECLOG_MAX_HZ = 100;
// Page 2 speed sensor (Nano D6, or GPIO34): 0-120 Hz = 0-20 km/h (6 Hz per
// km/h), gauge numbers every 4 km/h, no red zone.
constexpr float INPUT_MAX_HZ = 120, INPUT_HZ_PER_KMH = 6;
constexpr float INPUT_MAX_KMH = INPUT_MAX_HZ / INPUT_HZ_PER_KMH, INPUT_RED_KMH = INPUT_MAX_KMH + 1;
constexpr float ABS_CHECK_KMH = 10;   // ABS lamp: off from this speed up, on below (see AbsLamp).

// What the screen shows, filled by the sketch each loop.
struct AbsData {
  enum Link : uint8_t { WAITING, LIVE, STALE } link;
  bool keyOn, brake;
  float speedKmh, frequencyHz;   // Rear, D7.
  bool front;                    // Frame carries the D8 front sensor.
  float frontKmh, frontHz;
  float ref1V, batteryV;         // A0, A1.
  bool nanoLamps;                // Frame carries the Nano's RPM, TCS and ABS.
  uint16_t rpm;                  // D7 Hz x 15000 / 1560, from the Nano.
  bool tcsSteady, absActive;     // The Nano's TCS state (speeds equal) and ABS (D12) LED state.
  float declogHz;                // D2, 0 with older firmware.
  // Sender board, from the frame length: the Mega adds the A5 page pot
  // (19 fields), the Nano doesn't. Kept after the link drops.
  enum Sender : uint8_t { UNKNOWN, NANO, MEGA } sender;
  bool gens;                     // Frame carries the pot generator settings.
  // Pots, rear / front (0-1500 Hz) / Declog (0-100 Hz):
  //   Nano: A0 -> D9, A1 -> D10, A2 -> D3.  Mega: A1 -> D44, A0 -> D3, A2 -> D11.
  float genHz[3];
  float inputHz;                 // Page 2 speed sensor Hz: Nano D6 (sensorHz) or ESP32 GPIO34.
  bool nanoSensor;               // Frame carries the Nano's D6 speed sensor (21 fields).
  float sensorHz;                // Nano D6 frequency, Hz.
  uint8_t d6State;               // Nano D6 check: 0 OK, 1 open / short (stuck LOW), 2 stuck HIGH (23 fields).
  bool absPin;                   // Frame carries the Nano's ABS light input (A7) (22 fields).
  bool absPinOn;                 // D13 HIGH: ABS light on.
  bool pagePot;                  // Frame carries the Mega's A5 page pot.
  uint16_t potRaw;               // A5 reading, 0-1023.
  uint8_t potPage;               // Page picked by the pot, 1-3.
  uint8_t speedSel;              // Wheel the sender's speed encoder adjusts: 1 rear, 2 front, 0 none / older firmware.
  bool absLamp;                  // ABS light: Nano A7 input, or AbsLamp (rear speed).
  // Real ABS ECU check, from the Nano (25th / 26th A1 fields): its D9 / D10
  // outputs go through the testbench and the ECU and come back on D7 / D8.
  //   ECU_UNKNOWN  not checked yet (knob at 0 since start-up)
  //   ECU_OK       pulses come back on both wheels
  //   ECU_REAR / ECU_FRONT  that wheel's pulses don't come back (REAR / FRONT NO RETURN)
  //   ECU_LOST     neither comes back (ECU DISCONNECTED)
  //   ECU_RESET    the GPIO13 relay is cutting the ECU power (ESP32 side)
  enum Ecu : uint8_t { ECU_UNKNOWN, ECU_OK, ECU_REAR, ECU_FRONT, ECU_LOST, ECU_RESET } ecu;
  bool ecuField;                 // Frame carries the ECU check (26 fields).
  bool ecuChecking;              // Knob turned in the last 3 s: white CHECKING lamp.
  uint16_t knobHz;               // Nano knob setting in Hz (27 fields), also while D9 / D10 are cut.
  uint8_t nanoPage;              // Page from the Nano page / Declog encoder (28 fields), 0 = none.
  uint8_t nanoNav;               // Nano encoder: 0 free, 1 selecting, 2 locked, 3 Declog adjust (30 fields).
  uint8_t nanoSel;               // Nano encoder: page being selected (1-3), 0 when not selecting.
};

inline const char* senderName(AbsData::Sender s) {
  return s == AbsData::MEGA ? "MEGA" : s == AbsData::NANO ? "NANO" : "SENDER";
}

// ABS light, like a real bike's start-up check, worked out from the REAR speed:
//   switched on (live data arrives): ON;
//   rear speed reaches 10 km/h: OFF (check passed);
//   after that: below 10 km/h ON again, 0 km/h OFF, 10 km/h and above OFF.
// Losing the Nano link counts as switching off, so the next start begins ON.
struct AbsLamp {
  bool checked = false;  // Has reached 10 km/h since switch-on.
  bool on = true;
  void reset() { checked = false; on = true; }
  void update(float rearKmh) {
    if (!checked) {
      if (rearKmh >= ABS_CHECK_KMH) { checked = true; on = false; }
      else on = true;
      return;
    }
    if (rearKmh < 0.5f) on = false;                 // Shows as 0 km/h.
    else if (rearKmh < ABS_CHECK_KMH) on = true;
    else on = false;                                // 10 km/h and above.
  }
};

// TCS light: blinks while the rear and front speeds differ (to the whole
// km/h, the Nano's rule), off when they are equal.
inline bool tcsBlinking(const AbsData& d) {
  if (d.nanoLamps) return !d.tcsSteady;
  const float rear = constrain(d.speedKmh, 0.0f, ABS_SPEED_MAX), front = constrain(d.frontKmh, 0.0f, ABS_SPEED_MAX);
  return lroundf(rear) != lroundf(front);
}

// SENSOR (web dashboard only): "ok" both wheel sensors give pulses, "on" only
// one does, "off" none.
inline const char* sensorState(const AbsData& d) {
  const bool rear = d.frequencyHz > 0, front = d.front && d.frontHz > 0;
  return rear && front ? "ok" : rear || front ? "on" : "off";
}

namespace absDisplay {

using aa::rgb;
constexpr int W = 320, H = 240;

// Palette: near-black navy background, slightly lifted panels, cyan accent.
constexpr uint16_t BG = rgb(0x05, 0x08, 0x0d), PANEL = rgb(0x0b, 0x13, 0x1d), PANEL_EDGE = rgb(0x1d, 0x2a, 0x3a);
constexpr uint16_t TEXT = rgb(0xee, 0xf4, 0xfa), MUTED = rgb(0x8b, 0x9b, 0xb0), DIM = rgb(0x3b, 0x48, 0x59);
constexpr uint16_t ACCENT = rgb(0x4f, 0xd2, 0xf5), AMBER = rgb(0xff, 0xb5, 0x47), RED = rgb(0xff, 0x4b, 0x4b);
constexpr uint16_t GREEN = rgb(0x3d, 0xdc, 0x84), TRACK = rgb(0x17, 0x22, 0x31);
constexpr uint16_t FACE = rgb(0x0e, 0x18, 0x25), PLATE = rgb(0x06, 0x0b, 0x12), PLATE_EDGE = rgb(0x22, 0x30, 0x40);
constexpr uint16_t OFF_FILL = rgb(0x0f, 0x17, 0x22), OFF_EDGE = rgb(0x1f, 0x2b, 0x3a), OFF_TEXT = rgb(0x43, 0x51, 0x67);
constexpr uint16_t NEEDLE_CORE = rgb(0xff, 0xb4, 0xb4);

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ---- Layout ----
// Header (both pages): SEN VCC (REF1) | BAT.
constexpr int TOP_Y = 3, TOP_H = 30;
constexpr int TOP_X[2] = {4, 162}, TOP_W = 154;
// Pages 1-3: one big gauge and a bar panel below (page 1: FRONT gauge,
// REAR bar; page 2: speed sensor gauge and bar; page 3: DECLOG).
constexpr int P2X = 4, P2Y = 37, P2W = 312, P2H = 142;
constexpr float DCX2 = 160, DCY2 = P2Y + 124, DR2 = 106;
constexpr int PLATE_W = 88, PLATE_H = 44, PLATE_Y = DCY2 - 46;
constexpr int DY = 183, DH = 36;                 // DECLOG panel.
// Footer (both pages): ABS light | page dots | TCS light.
constexpr int DOTS_X = 140, DOTS_Y = 222, DOTS_W = 40, DOTS_H = 16;
constexpr int LAMP_X[2] = {88, 188}, LAMP_W = 44, LAMP_H = 16, LAMP_Y = 222;
constexpr int NAV_X = 6, NAME_X = 234, NAV_W = 80;   // Footer: "PAGE 1/3" left, page name right.
constexpr uint8_t PAGE_COUNT = 3;
const char* const PAGE_NAMES[PAGE_COUNT] = {"SPEED", "SENSOR", "DECLOG"};
// Page 3 (placeholder for now).
constexpr int P3X = 4, P3Y = 37, P3W = 312, P3H = 182;

const char* const TOP_LABELS[2] = {"SEN VCC", "BAT"};  // Left: REF1 (A3), shown as SEN VCC (sensor VCC).
const uint16_t TOP_ACCENTS[2] = {ACCENT, AMBER};
const char* const LAMP_LABELS[2] = {"ABS", "TCS"};
const uint16_t LAMP_COLORS[2] = {AMBER, AMBER};
// Page 2 footer: the speed sensor's logic level, LOW (0) yellow, HIGH (1) green.
constexpr uint16_t YELLOW = rgb(0xff, 0xd8, 0x2a);
const char* const LOGIC_LABELS[2] = {"LOW", "HIGH"};
const uint16_t LOGIC_COLORS[2] = {YELLOW, GREEN};

// Strips of the screen are drawn here, then pushed.
constexpr int SCRATCH_PX = 320 * 28;
uint16_t scratch[SCRATCH_PX];
// Static dial of the current page, drawn once per page change. Null if the heap had no room; the dial is
// then drawn again with every gauge frame.
uint16_t* dialBg = nullptr;

enum Mode : uint8_t { NONE, LOCKED, PAGE1, PAGE2, PAGE3 } mode = NONE;
uint8_t dialFor = 0;  // Page whose dial is in dialBg (0 = none).
enum Slot : uint8_t { S_TOP0, S_TOP1, S_LAMPS, S_DOTS, S_BAR0, S_BAR1, S_DECLOG, S_LOCK, S_INPUT, SLOT_COUNT };
char shown[SLOT_COUNT][40];
uint32_t lastGaugeMs = 0, lastTextMs = 0, selfTestStartMs = 0;
bool selfTestActive = false;
int32_t shownGaugeKey[2] = {-1, -1};
int shownKnobHz = -2;  // Knob Hz in the page 1 gauge (-1 = no knob lamp).
int shownOutKmh = -1;  // "Speed out N kph" in the page 1 gauge (0 when not shown).
float easeFront = 0, easeInput = 0, easeDeclog = 0;  // Needle positions, eased toward the readings.

// True (and remembered) when a slot's content key differs from what is shown.
bool changed(uint8_t slot, const char* key) {
  if (!strcmp(shown[slot], key)) return false;
  strlcpy(shown[slot], key, sizeof(shown[slot]));
  return true;
}

// Draws the screen rectangle (x, y, w, h) strip by strip with draw(buf) and
// pushes each strip. draw must paint the whole area, background first.
template <class F>
void render(int x, int y, int w, int h, F draw) {
  const int rows = max(1, SCRATCH_PX / w);
  for (int y0 = y; y0 < y + h; y0 += rows) {
    aa::Buf b{scratch, w, min(rows, y + h - y0), x, y0};
    draw(b);
    tft.drawRGBBitmap(x, y0, scratch, w, b.h);
  }
}

// Speed colour: cyan to 140 km/h, fading to amber by 170, amber to red from 200 to 215.
uint16_t speedColor(float v) {
  if (v < 140) return ACCENT;
  if (v < 170) return aa::mix(ACCENT, AMBER, (v - 140) * 255 / 30);
  if (v < 200) return AMBER;
  if (v < 215) return aa::mix(AMBER, RED, (v - 200) * 255 / 15);
  return RED;
}

// Value to dial angle (aa::dialAngle): 0 at the left, the maximum at the right.
inline float angleOf(float v, float maxV) { return PI + constrain(v, 0.0f, maxV) / maxV * PI; }

void panel(aa::Buf& b, int x, int y, int w, int h, float r = 8) {
  aa::roundRect(b, x, y, w, h, r, PANEL, PANEL_EDGE, 1);
}

// ABS warning-lamp symbol: a ring with "ABS" and a bracket arc either side.
void absSymbol(aa::Buf& b, float cx, float cy, float r, uint16_t c, const AaFont& font) {
  auto flat = [c](float) { return c; };
  aa::arc(b, cx, cy, r - 3, r, HALF_PI, HALF_PI + TWO_PI, flat);
  aa::arc(b, cx, cy, r + 5, r + 8, PI - 0.6f, PI + 0.6f, flat);
  aa::arc(b, cx, cy, r + 5, r + 8, TWO_PI - 0.6f, TWO_PI + 0.6f, flat);
  aa::textMid(b, font, "ABS", cx, cy, c);
}

// Dial face: track, ticks every `minor`, numbered majors every `major`.
void dialFace(aa::Buf& b, float cx, float cy, float r, float maxV, int minor, int major, float redFrom,
              const AaFont& numFont, float numInset, float tickScale) {
  auto flat = [](uint16_t c) { return [c](float) { return c; }; };
  aa::arc(b, cx, cy, 0, r - 10 * tickScale, PI, TWO_PI, flat(FACE));
  aa::arc(b, cx, cy, r + 3, r + 4, PI, TWO_PI, flat(PANEL_EDGE));
  aa::arc(b, cx, cy, r - 7 * tickScale, r, PI, TWO_PI, flat(TRACK));
  for (int v = 0; v <= maxV; v += minor) {
    const bool isMajor = v % major == 0, hot = v >= redFrom;
    const float a = angleOf(v, maxV), ca = cosf(a), sa = sinf(a);
    const float r0 = r - 10 * tickScale, r1 = r - (isMajor ? 20 : 15) * tickScale;
    const float w = (isMajor ? 2.2f : 1.2f) * min(1.0f, tickScale + 0.1f);
    aa::stroke(b, cx + ca * r0, cy + sa * r0, cx + ca * r1, cy + sa * r1, w, w, hot ? RED : isMajor ? TEXT : MUTED);
    if (isMajor) {
      // The end numbers sit a little above the horizontal, clear of a needle at 0 or the maximum.
      const float la = a + (v == 0 ? 0.2f : v == maxV ? -0.2f : 0);
      char label[6];
      snprintf(label, sizeof(label), "%d", v);
      aa::textMid(b, numFont, label, cx + cosf(la) * (r - numInset), cy + sinf(la) * (r - numInset), hot ? RED : TEXT);
    }
  }
}

// Needle from r0 to r1 with a soft glow.
void needle(aa::Buf& b, float cx, float cy, float a, float r0, float r1, float w0, float w1, uint16_t c, bool core) {
  const float ca = cosf(a), sa = sinf(a);
  const float x0 = cx + ca * r0, y0 = cy + sa * r0, x1 = cx + ca * r1, y1 = cy + sa * r1;
  aa::stroke(b, x0, y0, x1, y1, w0 + 4, w1 + 3, c, 45);
  aa::stroke(b, x0, y0, x1, y1, w0, w1, c);
  if (core) aa::stroke(b, x0, y0, x1 - ca * 3, y1 - sa * 3, w0 / 3, 0.6f, NEEDLE_CORE, 200);
}

// ---- Top row: REF1 | ABS, TCS lights | BAT; page dots ----

void drawTop(uint8_t i, float volts) {
  const int x = TOP_X[i];
  char value[10];
  snprintf(value, sizeof(value), "%.2f", constrain(volts, 0.0f, 99.99f));
  render(x, TOP_Y, TOP_W, TOP_H, [&](aa::Buf& b) {
    aa::fill(b, BG);
    panel(b, x, TOP_Y, TOP_W, TOP_H, 7);
    aa::text(b, aaLabel, TOP_LABELS[i], x + 9, TOP_Y + 20, TOP_ACCENTS[i], aa::LEFT, 1);
    const int unitX = x + TOP_W - 9 - aa::textWidth(aaLabel, "V");
    aa::text(b, aaLabel, "V", unitX, TOP_Y + 22, MUTED);
    aa::text(b, aaValue, value, unitX - 3, TOP_Y + 23, TEXT, aa::RIGHT);
  });
}

// Warning lights: a round light and its label ("(o) ABS"). Lit: a coloured
// light with a soft glow and bright text; off: a dark ringed light, dim text.
// Page 1: ABS / TCS in amber; page 2: LOW (yellow) / HIGH (green) logic level.
void drawLamps(const bool lit[2], const char* const labels[2] = LAMP_LABELS,
               const uint16_t colors[2] = LAMP_COLORS) {
  for (uint8_t i = 0; i < 2; ++i) {
    const int x = LAMP_X[i];
    const float cx = x + 8, cy = LAMP_Y + LAMP_H / 2.0f;
    const uint16_t c = colors[i];
    render(x - 1, LAMP_Y - 1, LAMP_W + 2, LAMP_H + 2, [&](aa::Buf& b) {
      aa::fill(b, BG);
      if (lit[i]) {
        aa::circle(b, cx, cy, 8.5f, aa::mix(BG, c, 70));  // Glow.
        aa::circle(b, cx, cy, 6, c);
        aa::circle(b, cx - 1.8f, cy - 1.8f, 1.8f, aa::mix(c, TEXT, 170));  // Highlight.
      } else {
        aa::circle(b, cx, cy, 6, OFF_EDGE);
        aa::circle(b, cx, cy, 5, OFF_FILL);
      }
      aa::text(b, aaLabel, labels[i], x + 19, LAMP_Y + 13, lit[i] ? TEXT : OFF_TEXT, aa::LEFT, 1);
    });
  }
}

// Page 2 logic lamps: the speed sensor's level shown at a visible rate.
// The real signal (up to 120 Hz) is far too fast for the LCD, so the lamps
// alternate at a display rate that rises with the input frequency, so faster
// signals blink faster:
//   up to 2 Hz:  the real rate (one LOW / HIGH cycle per input cycle);
//   2-120 Hz:    2 Hz rising to 8 Hz (2 + (Hz - 2) x 6 / 118).
// No pulses: stuck LOW (Nano D6 check 1) = LOW lamp steady, stuck HIGH (2) =
// HIGH lamp steady, unknown or no live data = both off.
inline float logicBlinkHz(float inHz) {
  if (inHz <= 2) return inHz;
  return 2 + (min(inHz, INPUT_MAX_HZ) - 2) * 6 / (INPUT_MAX_HZ - 2);
}
float logicPhase = 0;          // Display cycles, 0-1.
uint32_t logicLastMs = 0;

// level: -1 = both off, 0 = LOW (yellow), 1 = HIGH (green).
void logicLamps(bool live, float inHz, uint8_t fault) {
  const uint32_t now = millis();
  const float dt = (now - logicLastMs) / 1000.0f;
  logicLastMs = now;
  int8_t level = -1;
  if (live && inHz > 0) {
    logicPhase += logicBlinkHz(inHz) * min(dt, 0.2f);
    logicPhase -= floorf(logicPhase);
    level = logicPhase < 0.5f ? 1 : 0;
  } else if (live && fault == 1) level = 0;   // Stuck LOW.
  else if (live && fault == 2) level = 1;     // Stuck HIGH.
  char key[4];
  snprintf(key, sizeof(key), "%d", level);
  if (!changed(S_LAMPS, key)) return;
  const bool lit[2] = {level == 0, level == 1};
  drawLamps(lit, LOGIC_LABELS, LOGIC_COLORS);
}

// Page navigation in the footer: "PAGE 1/3" at the left, the page name at
// the right and one dot per page (the shown page in cyan). While the knob is
// selecting (sel 1-3), the left reads "SELECT 2/3" in amber, the right names
// the page that a press will lock, and that page's dot blinks amber.
// Locked by the encoder: the left reads "LOCKED 2/3" in cyan. Declog adjust
// (Nano encoder long press): "ADJUST 3/3" and DECLOG in amber.
void drawNav(uint8_t page, uint8_t sel, bool blinkOn, bool locked, bool adjust = false) {
  const bool selecting = sel != 0;
  const uint8_t target = selecting ? sel : page;
  char s[16];
  snprintf(s, sizeof(s), "%s %u/%u", selecting ? "SELECT" : adjust ? "ADJUST" : locked ? "LOCKED" : "PAGE", target, PAGE_COUNT);
  render(NAV_X, DOTS_Y, NAV_W, DOTS_H, [&](aa::Buf& b) {
    aa::fill(b, BG);
    aa::text(b, aaSmall, s, NAV_X, DOTS_Y + 12, selecting || adjust ? AMBER : locked ? ACCENT : MUTED, aa::LEFT, 1);
  });
  render(NAME_X, DOTS_Y, NAV_W, DOTS_H, [&](aa::Buf& b) {
    aa::fill(b, BG);
    aa::text(b, aaSmall, PAGE_NAMES[target - 1], NAME_X + NAV_W, DOTS_Y + 12, selecting || adjust ? AMBER : MUTED, aa::RIGHT, 1);
  });
  render(DOTS_X, DOTS_Y, DOTS_W, DOTS_H, [&](aa::Buf& b) {
    aa::fill(b, BG);
    const float cy = DOTS_Y + DOTS_H / 2.0f;
    for (uint8_t p = 1; p <= PAGE_COUNT; ++p) {
      const float cx = DOTS_X + DOTS_W / 2.0f + (p - (PAGE_COUNT + 1) / 2.0f) * 12;
      if (selecting && p == sel) {
        aa::circle(b, cx, cy, 5, blinkOn ? AMBER : aa::mix(BG, AMBER, 80));
        if (p == page) aa::circle(b, cx, cy, 2, ACCENT);
      } else aa::circle(b, cx, cy, 3, p == page ? ACCENT : DIM);
    }
  });
}

void navFooter(uint8_t page, uint8_t sel, bool locked, bool adjust) {
  const bool blinkOn = (millis() / 250) % 2;
  char key[16];
  snprintf(key, sizeof(key), "%u %u %d %d %d", page, sel, sel && blinkOn, locked, adjust);
  if (changed(S_DOTS, key)) drawNav(page, sel, blinkOn, locked, adjust);
}

// REF1 / BAT and the ABS / TCS lights (TCS blinks at 2 Hz; the self-test
// lights both). Without live data both lights stay off. lamps = false (pages
// 2 and 3): the lights are not drawn at all.
void topRow(const AbsData& d, bool live, bool selfTest, bool lamps) {
  char key[16];
  for (uint8_t i = 0; i < 2; ++i) {
    const float v = i ? d.batteryV : d.ref1V;
    snprintf(key, sizeof(key), "%.2f", v);
    if (changed(S_TOP0 + i, key)) drawTop(i, v);
  }
  if (!lamps) return;
  const bool blinkOn = (millis() / 250) % 2;
  const bool lit[2] = {live && (selfTest || d.absLamp), live && (selfTest || (tcsBlinking(d) && blinkOn))};
  snprintf(key, sizeof(key), "%d%d", lit[0], lit[1]);
  if (changed(S_LAMPS, key)) drawLamps(lit);
}

// ---- Pages 1-3: one big gauge with a bar panel below ----
// Page 1: FRONT speedometer (D8, green needle, 0-250 km/h, lit arc cyan ->
//         amber -> red, red from 210; "--" when the frame has no front
//         sensor), REAR speed bar below (bar and value in km/h).
// Page 2: SPEED SENSOR (Nano D6, or the ESP32's own GPIO34), 0-120 Hz shown
//         as 0-20 km/h (6 Hz per km/h, numbers 0 / 4 / 8 / 12 / 16 / 20, no
//         red), the SPEED text in km/h below (no bar), and the LOW (yellow) /
//         HIGH (green) logic lamps in the footer (logicLamps()).
//         No pulses = NO SIGNAL: the plate shows NO SIGNAL / --, the panel
//         below the error message in red.
// Page 3: the 0-100 Hz DECLOG gauge (the sender's D2), DECLOG bar (0-100 %) below.
// The LCD shows no pin names or raw input Hz (the serial log and web
// dashboard still do).
struct BigGauge {
  const char* title; const char* unit;
  float maxV; int minor, major; float redFrom;
  const char* barName; bool barPercent;  // Bar value: % of the range, or the gauge unit.
  uint16_t needleColor, dotColor;
};
enum : uint8_t { G_INPUT, G_DECLOG, G_FRONT };  // Page 2, page 3, page 1.
const BigGauge BIG_GAUGES[3] = {
    {"SPEED SENSOR", "km/h", INPUT_MAX_KMH, 1, 4, INPUT_RED_KMH, "SPEED", false, RED, ACCENT},  // 0, 4, ... 20.
    {"DECLOG", "Hz", DECLOG_MAX_HZ, 5, 25, DECLOG_MAX_HZ + 1, "DECLOG", true, RED, ACCENT},
    {"FRONT", "km/h", ABS_SPEED_MAX, 10, 50, SPEED_RED, "REAR", false, GREEN, GREEN}};
inline uint8_t pageOf(uint8_t which) { return which == G_FRONT ? 1 : which + 2; }

// which: G_FRONT (page 1), G_INPUT (page 2, speed sensor), G_DECLOG (page 3).
void drawDial2(aa::Buf& b, uint8_t which) {
  const BigGauge& g = BIG_GAUGES[which];
  aa::fill(b, BG);
  panel(b, P2X, P2Y, P2W, P2H, 10);
  dialFace(b, DCX2, DCY2, DR2, g.maxV, g.minor, g.major, g.redFrom, aaDial, 30, 1.0f);
  aa::circle(b, 16, P2Y + 13, 3, g.dotColor);
  aa::text(b, aaLabel, g.title, 24, P2Y + 18, TEXT, aa::LEFT, 1);
}

// v: the reading in the gauge's unit; signal false = NO SIGNAL on the plate.
// adjust (page 1): the Mega's speed encoder sets this wheel, shown by an
// amber title dot and ADJUST after the title.
// ecu (page 1): the real ABS ECU lamp at the top right of the gauge panel
// (EcuLamp below). Faults: red lamp + Offline here, the reason in the REAR bar panel (ecuOffline).
//   green lamp + Online (ECU OK), white + Checking (knob turned), amber +
//   Reset (GPIO13 relay), grey lamp alone (not checked yet), red + Offline for
//   ECU DISCONNECTED / REAR or FRONT NO RETURN.
//   -1 = no lamp (self-test, older Nano firmware). lit false = the dark
//   phase of a blink.
constexpr int8_t ECU_CHECK = 6;  // View only: the white CHECKING lamp.
constexpr uint16_t WHITE = rgb(0xff, 0xff, 0xff);
const uint16_t ECU_COLORS[7] = {MUTED, GREEN, RED, RED, RED, AMBER, WHITE};
inline bool ecuFault(int8_t ecu) {
  return ecu == AbsData::ECU_REAR || ecu == AbsData::ECU_FRONT || ecu == AbsData::ECU_LOST;
}

// Round warning light (as the ABS / TCS lights): lit with glow, or dark.
void ecuLight(aa::Buf& b, float cx, float cy, uint16_t c, bool lit) {
  if (lit) {
    aa::circle(b, cx, cy, 8.5f, aa::mix(PANEL, c, 70));  // Glow.
    aa::circle(b, cx, cy, 6, c);
    aa::circle(b, cx - 1.8f, cy - 1.8f, 1.8f, aa::mix(c, TEXT, 170));  // Highlight.
  } else {
    aa::circle(b, cx, cy, 6, OFF_EDGE);
    aa::circle(b, cx, cy, 5, OFF_FILL);
  }
}

// Knob lamp (page 1, under the top-right lamp while the ECU check fails):
// D9 / D10 are cut (no output), so this amber lamp and the kph beside it show
// the "isolated" speed the knob is set to (knob Hz / 6). 6-1500 Hz is far too fast to
// see, so it blinks at a scaled rate: 1 Hz at 6 Hz (1 km/h) rising to 8 Hz at
// 1500 Hz (250 km/h); knob at 0 = lamp off. knobHz < 0 = not shown.
constexpr float KNOB_BLINK_MIN = 1, KNOB_BLINK_MAX = 8, KNOB_MAX_HZ = 1500;
inline float knobBlinkHz(float hz) {
  if (hz <= 0) return 0;
  return KNOB_BLINK_MIN + (min(hz, KNOB_MAX_HZ) - 6) * (KNOB_BLINK_MAX - KNOB_BLINK_MIN) / (KNOB_MAX_HZ - 6);
}
float knobPhase = 0;      // Blink cycles, 0-1.
uint32_t knobLastMs = 0;
// Call every loop: true while the knob lamp is in its lit half.
bool knobBlink(uint16_t knobHz) {
  const uint32_t now = millis();
  const float dt = min((now - knobLastMs) / 1000.0f, 0.2f);
  knobLastMs = now;
  if (!knobHz) { knobPhase = 0; return false; }
  knobPhase += knobBlinkHz(knobHz) * dt;
  knobPhase -= floorf(knobPhase);
  return knobPhase < 0.5f;
}

// outKmh: the knob's output speed, shown as "Speed out N kph" while the knob
// was just turned (in place of the old "Checking").
void ecuBadge(aa::Buf& b, int8_t ecu, bool lit, int knobHz = -1, bool knobLit = false, int outKmh = 0) {
  if (ecu < 0) return;
  static const char* const LABELS[7] = {"", "Online", "Offline", "Offline", "Offline", "Reset", ""};
  const uint16_t c = ECU_COLORS[ecu];
  const float cx = P2X + P2W - 16, cy = P2Y + 13;  // Level with the gauge title dot.
  ecuLight(b, cx, cy, c, lit);
  // Same font and baseline as the FRONT title.
  if (ecu == ECU_CHECK) {  // Two lines: "Speed Out" beside the lamp, "100kph" under it.
    char kph[12];
    snprintf(kph, sizeof(kph), "%dkph", outKmh);
    aa::text(b, aaLabel, "Speed Out", cx - 13, P2Y + 18, c, aa::RIGHT, 1);
    aa::text(b, aaLabel, kph, cx - 13, P2Y + 38, c, aa::RIGHT, 1);
    return;  // The second line takes the knob row.
  }
  if (*LABELS[ecu]) aa::text(b, aaLabel, LABELS[ecu], cx - 13, P2Y + 18, lit ? c : aa::mix(PANEL, c, 120), aa::RIGHT, 1);
  if (knobHz < 0) return;
  // Knob lamp and speed (knob Hz / 6, whole km/h), one row below, lined up
  // with the lamp and text above.
  char kph[12];
  snprintf(kph, sizeof(kph), "%dkph", knobHz / (int)HZ_PER_KMH);
  ecuLight(b, cx, cy + 20, AMBER, knobLit);
  aa::text(b, aaLabel, kph, cx - 13, P2Y + 38, knobHz ? AMBER : DIM, aa::RIGHT, 1);
}

// REAR bar panel while the ECU check fails (page 1): red stripe and the
// reason, centred in the panel (the red lamp, Offline and the knob lamp are
// at the top right of the gauge panel).
void ecuOffline(int8_t ecu) {
  const char* reason = ecu == AbsData::ECU_REAR ? "No REAR return signal"
                     : ecu == AbsData::ECU_FRONT ? "No FRONT return signal" : "ABS Disconnected";
  constexpr int MID = P2X + P2W / 2;
  render(P2X, DY, P2W, DH, [&](aa::Buf& b) {
    aa::fill(b, BG);
    panel(b, P2X, DY, P2W, DH);
    aa::roundRect(b, P2X + 1, DY + 1, 4, DH - 2, 2, RED);
    aa::text(b, aaLabel, reason, MID, DY + 16, RED, aa::CENTER, 1);
    aa::text(b, aaSmall, "Turn the knob to check again", MID, DY + 29, MUTED, aa::CENTER);
  });
}

// ECU lamp state, run every loop (on every page, so coming back to page 1
// never blinks by itself): each status change blinks the lamp 5 times
// (200 ms off / 200 ms on), then it stays on. A knob turn shows the white
// CHECKING lamp for 3 s (steady); then the current status comes back,
// blinking 5 times if it differs from the one shown before.
constexpr uint32_t ECU_BLINK_MS = 200;
constexpr uint8_t ECU_BLINKS = 5;
constexpr uint32_t ABS_FLASH_MS = 400;  // Online lamp flash (400 ms on / 400 ms off) while the ABS light is on.
struct EcuView { int8_t status; bool lit; };
uint8_t ecuShown = AbsData::ECU_UNKNOWN;
uint32_t ecuBlinkStartMs = 0;
bool ecuBlinking = false;
EcuView ecuLamp(const AbsData& d, uint32_t now) {
  if (d.link != AbsData::LIVE || !d.ecuField) {
    ecuShown = AbsData::ECU_UNKNOWN;  // Link back: starts from "not checked", no blink.
    ecuBlinking = false;
    return {-1, false};
  }
  // Speed Out (knob just turned) only while no frequency comes back from the
  // testbench (D7 and D8 both 0); with a return signal the normal status shows.
  const bool noReturn = d.frequencyHz <= 0 && d.frontHz <= 0;
  if (d.ecuChecking && noReturn && d.ecu != AbsData::ECU_RESET) return {ECU_CHECK, true};
  if (d.ecu != ecuShown) {
    ecuShown = d.ecu;
    ecuBlinking = true;
    ecuBlinkStartMs = now;
  }
  bool lit = true;
  if (ecuBlinking) {
    const uint32_t phase = (now - ecuBlinkStartMs) / ECU_BLINK_MS;  // Even = off, odd = on.
    if (phase >= 2u * ECU_BLINKS) ecuBlinking = false;
    else lit = phase % 2;
  }
  // Online while the ABS light is on: flash like hazards until the light goes off.
  if (!ecuBlinking && ecuShown == AbsData::ECU_OK && d.absLamp) lit = (now / ABS_FLASH_MS) % 2 == 0;
  return {int8_t(ecuShown), lit};
}

void renderGauge2(float v, uint8_t which, bool signal, bool adjust = false, int8_t ecu = -1, bool ecuLit = true,
                  int knobHz = -1, bool knobLit = false, int outKmh = 0, bool braking = false) {
  const BigGauge& g = BIG_GAUGES[which];
  const uint8_t dialPage = pageOf(which);  // dialBg holds this page's face.
  char digits[6];
  if (signal) snprintf(digits, sizeof(digits), "%d", int(lroundf(constrain(v, 0.0f, 9999.0f))));
  else strlcpy(digits, "--", sizeof(digits));
  const int px = DCX2 - PLATE_W / 2;
  render(P2X, P2Y, P2W, P2H, [&](aa::Buf& b) {
    if (dialBg && dialFor == dialPage) memcpy(b.px, dialBg + (b.oy - P2Y) * P2W, b.w * b.h * 2);
    else drawDial2(b, which);
    if (adjust) {
      aa::circle(b, 16, P2Y + 13, 3.5f, AMBER);
      aa::text(b, aaSmall, "ADJUST", 24 + aa::textWidth(aaLabel, g.title) + 6, P2Y + 18, AMBER, aa::LEFT, 1);
    }
    ecuBadge(b, ecu, ecuLit, knobHz, knobLit, outKmh);
    if (braking) {  // Speed encoder held (brake): red BRAKING under the gauge title.
      aa::circle(b, 16, P2Y + 33, 3.5f, RED);
      aa::text(b, aaLabel, "BRAKING", 24, P2Y + 38, RED, aa::LEFT, 1);
    }
    const float a = angleOf(signal ? v : 0, g.maxV);
    const float redA = angleOf(g.redFrom, g.maxV);
    if (which == G_FRONT) {
      if (signal) aa::arc(b, DCX2, DCY2, DR2 - 7, DR2, PI, a,
                          [](float ang) { return speedColor((ang - PI) / PI * ABS_SPEED_MAX); });
    } else aa::arc(b, DCX2, DCY2, DR2 - 7, DR2, PI, a, [redA](float ang) { return ang >= redA ? RED : ACCENT; });
    if (signal || which != G_FRONT) needle(b, DCX2, DCY2, a, 26, DR2 - 3, 5.5f, 1.8f, g.needleColor, true);
    aa::roundRect(b, px - 3, PLATE_Y - 3, PLATE_W + 6, PLATE_H + 6, 11, FACE);  // Gap around the plate.
    aa::roundRect(b, px, PLATE_Y, PLATE_W, PLATE_H, 8, PLATE, signal ? PLATE_EDGE : RED, 1);
    aa::text(b, aaSmall, signal ? g.unit : "NO SIGNAL", DCX2, PLATE_Y + 12, signal ? MUTED : RED, aa::CENTER, 1);
    const bool hot = which == G_FRONT && signal && v >= SPEED_RED;
    aa::text(b, aaBig, digits, DCX2, PLATE_Y + 41, hot ? RED : signal ? TEXT : DIM, aa::CENTER, 1);
  });
}

// Bar panel under the big gauge: the name at the left, the bar, and the
// value at the right.
//   Page 2: SPEED, bar and value in km/h (0-200);
//           no pulses: NO SPEED SENSOR READING in red across the panel.
//   Page 3: DECLOG, bar and value as 0-100 % (NO LINK under the name without data).
//   Page 1: REAR, bar (cyan -> amber -> red) and value in km/h.
// adjust (page 1 REAR): the speed encoder sets this wheel: amber name with
// ADJUST under it.
// fault (page 2, Nano D6 check): 1 OPEN OR SHORT SENSOR, 2 NO PULSE (STUCK HIGH).
void drawBigBar(uint8_t which, float v, bool have, bool adjust = false, uint8_t fault = 0) {
  const BigGauge& g = BIG_GAUGES[which];
  const float frac = constrain(v / g.maxV, 0.0f, 1.0f);
  const char* unit = g.barPercent ? "%" : g.unit;
  if (!have && which == G_INPUT) {
    // Speed sensor with no pulses: the whole panel is the error message.
    render(P2X, DY, P2W, DH, [&](aa::Buf& b) {
      aa::fill(b, BG);
      panel(b, P2X, DY, P2W, DH);
      aa::roundRect(b, P2X + 1, DY + 1, 4, DH - 2, 2, RED);
      const char* msg = fault == 1 ? "OPEN OR SHORT SENSOR" : fault == 2 ? "NO PULSE (STUCK HIGH)" : "NO SPEED SENSOR READING";
      aa::text(b, aaLabel, msg, W / 2, DY + 16, RED, aa::CENTER, 1);
      aa::text(b, aaSmall, "Check the sensor and its wire",
               W / 2, DY + 29, MUTED, aa::CENTER);
    });
    return;
  }
  render(P2X, DY, P2W, DH, [&](aa::Buf& b) {
    aa::fill(b, BG);
    panel(b, P2X, DY, P2W, DH);
    // Name centred in the panel; without data it moves up for NO LINK below.
    const bool tag = !have || adjust;  // Second line under the name.
    if (adjust) aa::roundRect(b, P2X + 1, DY + 1, 4, DH - 2, 2, AMBER);
    aa::text(b, aaLabel, g.barName, P2X + 10, tag ? DY + 16 : DY + 23, adjust ? AMBER : TEXT, aa::LEFT, 1);
    if (!have) aa::text(b, aaSmall, "NO LINK", P2X + 10, DY + 29, RED, aa::LEFT);
    else if (adjust) aa::text(b, aaSmall, "ADJUST", P2X + 10, DY + 29, AMBER, aa::LEFT);
    char s[12];
    constexpr int X0 = P2X + 104, LEN = 128;
    const int cy = DY + DH / 2;
    if (which != G_INPUT) {  // Page 2: speed text only, no bar.
      aa::roundRect(b, X0, cy - 5, LEN, 10, 5, TRACK);
      const uint16_t fill = which == G_FRONT ? speedColor(v) : ACCENT;
      if (have && frac > 0.005f) aa::roundRect(b, X0, cy - 5, max(10.0f, frac * LEN), 10, 5, fill);
    }
    if (have) snprintf(s, sizeof(s), which == G_INPUT ? "%.1f" : "%.0f", g.barPercent ? frac * 100 : v);
    else strlcpy(s, "--", sizeof(s));
    const int unitX = P2X + P2W - 10 - aa::textWidth(aaLabel, unit);
    aa::text(b, aaLabel, unit, unitX, cy + 7, MUTED);
    aa::text(b, aaValue, s, unitX - 2, cy + 9, have ? TEXT : DIM, aa::RIGHT);
  });
}

// ---- Frames ----

void forget() {
  memset(shown, 0, sizeof(shown));
  shownGaugeKey[0] = shownGaugeKey[1] = -1;
}

// Draws the page's static dial into dialBg.
void cacheDial(uint8_t page) {
  if (!dialBg || dialFor == page) return;
  aa::Buf b{dialBg, P2W, P2H, P2X, P2Y};
  drawDial2(b, page == 1 ? G_FRONT : page == 3 ? G_DECLOG : G_INPUT);
  dialFor = page;
}

void clearScreen() {
  forget();
  render(0, 0, W, H, [](aa::Buf& b) { aa::fill(b, BG); });
}

void lockedText(const AbsData& d) {
  const bool live = d.link == AbsData::LIVE;
  // Before any frame the board is unknown: WAITING FOR DATA, and the hint
  // names both links. After that the screen names the board that was seen.
  const char* name = senderName(d.sender);
  char line[24], hint[40];
  if (d.link == AbsData::WAITING) strlcpy(line, "WAITING FOR DATA", sizeof(line));
  else if (d.link == AbsData::STALE) strlcpy(line, "NO LINK", sizeof(line));
  else snprintf(line, sizeof(line), "UPDATE %s", name);
  if (live) snprintf(hint, sizeof(hint), "Flash the new %s firmware",
                     d.sender == AbsData::MEGA ? "Mega" : "Nano");
  else if (d.sender == AbsData::MEGA) strlcpy(hint, "Check Mega D18 -> GPIO16, GND", sizeof(hint));
  else if (d.sender == AbsData::NANO) strlcpy(hint, "Check Nano D5 -> GPIO16, GND", sizeof(hint));
  else strlcpy(hint, "Nano D5 / Mega D18 -> GPIO16", sizeof(hint));
  const uint16_t color = live ? AMBER : RED;
  char key[32];  // Fits shown[] (39 chars); the hint follows from the line + sender.
  snprintf(key, sizeof(key), "%s|%u", line, unsigned(d.sender));
  if (!changed(S_LOCK, key)) return;
  render(30, 30, 260, 180, [&](aa::Buf& b) {
    aa::fill(b, BG);
    aa::roundRect(b, 30, 30, 260, 180, 14, PANEL, PANEL_EDGE, 1);
    absSymbol(b, W / 2, 84, 22, color, aaPill);
    aa::text(b, aaValue, line, W / 2, 144, color, aa::CENTER, 1);
    aa::text(b, aaLabel, hint, W / 2, 166, MUTED, aa::CENTER);
    aa::text(b, aaSmall, d.pagePot ? "Swipe or page pot A5: change page" : "Swipe: change page",
             W / 2, 186, DIM, aa::CENTER, 1);
    aa::text(b, aaSmall, "Wi-Fi Techroniks ABS Tester  |  192.168.4.1", W / 2, 201, DIM, aa::CENTER, 1);
  });
}

// Power-on intro, the only start-up animation (LOGO_MS in all): the TRN
// TECHRONIKS logo (LogoBitmap.h, 240 x 111, centred on black) with a glitch
// effect:
//   0.0-1.0 s  glitch in: the logo flickers in with shifted horizontal
//              slices, a red / blue colour split and noise lines, settling
//              down to a clean logo;
//   1.0-1.8 s  shine: a light band sweeps diagonally across the letters;
//   1.8-2.6 s  hold, with two short glitch bursts;
//   2.6-3.2 s  glitch out while fading to black.
// Glitch strength 0-1 per frame: slice shifts up to GLITCH_SHIFT px, colour
// split up to GLITCH_SPLIT px. The glitch is random, so every start looks a
// little different.
constexpr uint32_t LOGO_MS = 3200;
constexpr int LOGO_X = (W - LOGO_W) / 2, LOGO_Y = (H - LOGO_H) / 2;
constexpr int GLITCH_MARGIN = 16, GLITCH_SHIFT = 14, GLITCH_SPLIT = 6;

// Logo pixel, black outside the bitmap.
inline uint16_t logoPx(int x, int y) {
  if (x < 0 || x >= LOGO_W || y < 0 || y >= LOGO_H) return 0;
  return pgm_read_word(LOGO_PX + y * LOGO_W + x);
}

// Glitch strength at time p (seconds).
float glitchAt(float p) {
  if (p < 1.0f) return 1.0f - p;                                   // In: strong -> none.
  if (p >= 1.95f && p < 2.03f) return 0.6f;                         // Burst 1.
  if (p >= 2.30f && p < 2.36f) return 0.45f;                        // Burst 2.
  if (p >= 2.6f) return constrain((p - 2.6f) / 0.6f, 0.0f, 1.0f);  // Out: none -> strong.
  return 0;
}

void logoIntro() {
  constexpr uint16_t BLACK = 0x0000, WHITE = rgb(0xff, 0xff, 0xff);
  constexpr uint16_t NOISE[3] = {rgb(0x1e, 0x88, 0xff), rgb(0xe8, 0xf0, 0xff), rgb(0x6c, 0xc8, 0xff)};
  constexpr int RX = LOGO_X - GLITCH_MARGIN, RW = LOGO_W + 2 * GLITCH_MARGIN;
  static int8_t rowShift[LOGO_H];
  static uint8_t rowNoise[LOGO_H];  // 0 = none, 1-3 = noise line colour.
  render(0, 0, W, H, [](aa::Buf& b) { aa::fill(b, BLACK); });
  for (uint32_t start = millis(), t = 0; t <= LOGO_MS; t = millis() - start) {
    const float p = float(t) / LOGO_MS * 3.2f;  // Seconds.
    const float g = glitchAt(p);
    const float fade = p > 2.6f ? constrain((3.2f - p) / 0.6f, 0.0f, 1.0f) : 1.0f;
    const float shine = p >= 1.0f && p < 1.8f ? (p - 1.0f) / 0.8f * (LOGO_W + LOGO_H + 60) - 40 : -1000;
    // This frame's glitch: random bands of rows shifted sideways, a few noise
    // lines, a colour split, and flicker (some frames dimmed or blanked).
    const int split = int(g * GLITCH_SPLIT + 0.5f);
    float level = fade;
    if (g > 0) {
      for (int y = 0; y < LOGO_H;) {
        const int band = 2 + random(10);
        const int8_t dx = random(100) < g * 70 ? int8_t((random(2 * GLITCH_SHIFT + 1) - GLITCH_SHIFT) * g) : 0;
        for (int i = 0; i < band && y < LOGO_H; ++i, ++y) {
          rowShift[y] = dx;
          rowNoise[y] = random(1000) < g * 25 ? 1 + random(3) : 0;
        }
      }
      const long f = random(100);
      if (f < g * 18) level *= 0.0f;         // Blank frame.
      else if (f < g * 40) level *= 0.45f;   // Dim frame.
    } else {
      memset(rowShift, 0, sizeof(rowShift));
      memset(rowNoise, 0, sizeof(rowNoise));
    }
    render(RX, LOGO_Y, RW, LOGO_H, [&](aa::Buf& b) {
      for (int row = 0; row < b.h; ++row) {
        const int y = b.oy - LOGO_Y + row;
        uint16_t* out = b.px + row * b.w;
        const int shift = rowShift[y];
        for (int i = 0; i < RW; ++i) {
          const int x = i - GLITCH_MARGIN - shift;  // Logo x for this screen pixel.
          uint16_t c;
          if (rowNoise[y]) {
            // Noise line: a bright streak across part of the row.
            c = (i * 7 + y * 13 + int(p * 1000)) % 5 ? NOISE[rowNoise[y] - 1] : BLACK;
          } else if (split) {
            // Colour split: red from the right, blue from the left, green in place.
            c = (logoPx(x + split, y) & 0xF800) | (logoPx(x, y) & 0x07E0) | (logoPx(x - split, y) & 0x001F);
          } else {
            c = logoPx(x, y);
          }
          if (shine > -1000 && c) {
            const float s = fabsf(x + y * 0.6f - shine);
            if (s < 14) {
              // Only the letters shine (bright pixels), not the black background.
              const uint8_t lum = ((c >> 11) << 3) / 3 + (((c >> 5) & 0x3F) << 2) / 3 + ((c & 0x1F) << 3) / 3;
              if (lum > 40) c = aa::mix(c, WHITE, uint8_t((1 - s / 14) * 170));
            }
          }
          if (level < 1) c = aa::mix(BLACK, c, uint8_t(level * 255));
          out[i] = c;
        }
      }
    });
  }
  render(0, 0, W, H, [](aa::Buf& b) { aa::fill(b, BLACK); });
}

void begin() {
  // Deselect both devices on the shared bus before it starts.
  pinMode(TFT_CS, OUTPUT);   digitalWrite(TFT_CS, HIGH);
  pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  tft.begin();
  tft.setRotation(1);  // Landscape 320 x 240.
  dialBg = static_cast<uint16_t*>(malloc(P2W * P2H * 2));  // Big enough for either page.
  if (!dialBg) Serial.println(F("# Gauge: no heap for the dial cache, redrawing it each frame."));
  randomSeed(esp_random());  // A different glitch every start.
  logoIntro();  // TRN TECHRONIKS logo with a glitch effect, 3.2 s (the only start-up animation).
  clearScreen();
}

// ---- Touch: swipe left / right ----

struct TouchRead { bool down; int x, y; };

inline int median3(int a, int b, int c) { return max(min(a, b), min(max(a, b), c)); }

// XPT2046: pressure (Z1, Z2), then three X / Y readings (the first X after a
// pressure reading is noisy and dropped), then power down. x runs along the
// long side of the screen in landscape (the same axes as the XPT2046_Touchscreen
// library at rotation 1).
TouchRead readTouch() {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  freqInput::guardBegin();  // T_CS (GPIO33) couples into GPIO34: mask the read.
  digitalWrite(TOUCH_CS, LOW);
  SPI.transfer(0xB1);                        // Start Z1.
  const int z1 = SPI.transfer16(0xC1) >> 3;  // Read Z1, start Z2.
  const int z2 = SPI.transfer16(0x91) >> 3;  // Read Z2, start X.
  TouchRead t{z1 > 0 && z1 + 4095 - z2 >= TOUCH_PRESSURE, 0, 0};
  int d[6] = {0, 0, 0, 0, 0, 0};
  if (t.down) {
    SPI.transfer16(0x91);                    // Noisy first X, dropped.
    d[0] = SPI.transfer16(0xD1) >> 3;        // X, start Y.
    d[1] = SPI.transfer16(0x91) >> 3;        // Y, start X.
    d[2] = SPI.transfer16(0xD1) >> 3;
    d[3] = SPI.transfer16(0x91) >> 3;
  }
  d[4] = SPI.transfer16(0xD0) >> 3;          // X, then Y with power down.
  d[5] = SPI.transfer16(0) >> 3;
  digitalWrite(TOUCH_CS, HIGH);
  freqInput::guardEnd();
  SPI.endTransaction();
  if (t.down) {
    t.x = median3(d[0], d[2], d[4]);
    t.y = median3(d[1], d[3], d[5]);
  }
  return t;
}

int lastSwipeDx = 0, lastSwipeDy = 0;  // Raw movement of the last finished touch (for the serial log).

// Call every loop. Returns -1 for a swipe left, +1 for a swipe right, 0
// otherwise. A touch starts after two polls in a row with pressure (40 ms)
// and ends after two without, so noise doesn't make or end touches.
int8_t swipe() {
  static uint32_t lastPollMs = 0, downMs = 0;
  static bool down = false;
  static uint8_t agree = 0;
  static int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  const uint32_t now = millis();
  if (now - lastPollMs < TOUCH_POLL_MS) return 0;
  lastPollMs = now;
  const TouchRead t = readTouch();
  if (t.down == down) {
    agree = 0;
    if (down) { x1 = t.x; y1 = t.y; }
    return 0;
  }
  if (++agree < 2) return 0;
  agree = 0;
  down = t.down;
  if (down) {
    x0 = x1 = t.x;
    y0 = y1 = t.y;
    downMs = now;
    return 0;
  }
  lastSwipeDx = x1 - x0;
  lastSwipeDy = y1 - y0;
  if (now - downMs > SWIPE_MAX_MS || abs(lastSwipeDx) < SWIPE_MIN || abs(lastSwipeDx) < 2 * abs(lastSwipeDy)) return 0;
  const int8_t dir = lastSwipeDx > 0 ? 1 : -1;
  return SWIPE_FLIP ? -dir : dir;
}

// ---- Update ----

// Eases a needle toward its reading: about 1/3 of the way per 40 ms frame.
inline float ease(float& shownV, float target, float snap) {
  shownV += (target - shownV) * 0.35f;
  if (fabsf(target - shownV) < snap) shownV = target;
  return shownV;
}

// Call every loop. page: 1-3; sel: the page the knob is pointing at while
// selecting, 0 when not selecting; locked: the encoder holds the page.
void update(const AbsData& d, uint8_t page, uint8_t sel = 0, bool locked = false, bool declogAdj = false) {
  const uint32_t now = millis();
  const bool running = d.link == AbsData::LIVE && d.keyOn;
  const Mode want = page == 3 ? PAGE3 : page == 2 ? PAGE2 : running ? PAGE1 : LOCKED;
  if (want != mode) {
    // The switch-on self-test runs when page 1 comes up with live data after
    // the waiting / no link screen (not when coming back from page 2).
    if (want == PAGE1 && (mode == LOCKED || mode == NONE)) { selfTestActive = true; selfTestStartMs = now; }
    if (want != PAGE1) selfTestActive = false;
    clearScreen();
    if (want != LOCKED) cacheDial(want == PAGE1 ? 1 : want == PAGE2 ? 2 : 3);
    mode = want;
    lastGaugeMs = lastTextMs = 0;
  }
  const uint32_t testMs = now - selfTestStartMs;
  const EcuView ev = ecuLamp(d, now);  // Every loop, so the blink timing holds on any page.
  const bool knobLit = knobBlink(d.knobHz);  // Knob lamp blink, every loop so it stays even.
  if (selfTestActive && (!running || testMs >= SELF_TEST_MS)) selfTestActive = false;
  const bool selfTest = selfTestActive;

  if ((mode == PAGE1 || mode == PAGE2 || mode == PAGE3) && now - lastGaugeMs >= GAUGE_REFRESH_MS) {
    lastGaugeMs = now;
    if (mode == PAGE1) {
      const float sweep = sinf(PI * testMs / SELF_TEST_MS) * ABS_SPEED_MAX;  // Self-test: 0 -> 250 -> 0.
      // FRONT speedometer only; the rear speed is in the bar below.
      const float front = selfTest ? sweep : ease(easeFront, constrain(d.frontKmh, 0.0f, ABS_SPEED_MAX), 0.2f);
      const bool have = selfTest || d.front;
      const bool adjust = !selfTest && d.speedSel == 2;  // Speed encoder on FRONT.
      const int8_t ecu = selfTest ? -1 : ev.status;  // Real ECU lamp, top right.
      // Knob lamp under it while the ECU check fails (-1 = not shown).
      const int knob = ecu >= 0 && ecuFault(d.ecu) ? int(d.knobHz) : -1;
      const int outKmh = ecu == ECU_CHECK ? int(d.knobHz / HZ_PER_KMH) : 0;  // "Speed out N kph".
      const bool braking = !selfTest && d.brake;
      const int32_t key = lroundf(front * 4) | int32_t(have) << 16 | int32_t(adjust) << 17 | int32_t(ecu + 1) << 18 | int32_t(ev.lit) << 22 | int32_t(knob >= 0 && knobLit) << 23
                          | int32_t(braking) << 24 | int32_t(outKmh & 0x3f) << 25;
      if (key != shownGaugeKey[0] || knob != shownKnobHz || outKmh != shownOutKmh) {
        shownGaugeKey[0] = key;
        shownKnobHz = knob;
        shownOutKmh = outKmh;
        renderGauge2(front, G_FRONT, have, adjust, ecu, ev.lit, knob, knobLit, outKmh, braking);
      }
    } else if (mode == PAGE2) {
      // Speed sensor: Hz / 6 = km/h (0-20); no pulses = NO SIGNAL.
      const bool signal = d.inputHz > 0;
      const float kmh = ease(easeInput, constrain(d.inputHz / INPUT_HZ_PER_KMH, 0.0f, INPUT_MAX_KMH), 0.2f);
      const int32_t key = lroundf(kmh * 4) | int32_t(signal) << 16;
      if (key != shownGaugeKey[0]) {
        shownGaugeKey[0] = key;
        renderGauge2(kmh, G_INPUT, signal);
      }
    } else {
      // Page 3: Declog from the sender; drops to 0 without live data.
      const float hz = ease(easeDeclog, running ? constrain(d.declogHz, 0.0f, DECLOG_MAX_HZ) : 0.0f, 0.05f);
      const int32_t key = lroundf(hz * 20);
      if (key != shownGaugeKey[0]) {
        shownGaugeKey[0] = key;
        renderGauge2(hz, G_DECLOG, true);
      }
    }
  }
  // Page 2 logic lamps: checked every loop, so the blinking stays even.
  if (mode == PAGE2) logicLamps(running, d.inputHz, d.d6State);

  if (now - lastTextMs < TEXT_REFRESH_MS) return;
  lastTextMs = now;
  navFooter(page, sel, locked, declogAdj);
  if (mode == LOCKED) { lockedText(d); return; }
  topRow(d, running, selfTest, mode == PAGE1);  // ABS / TCS lights on page 1 only.
  if (mode == PAGE1) {
    // REAR speed bar (the self-test sweeps it with the needle).
    const float rear = selfTest ? sinf(PI * testMs / SELF_TEST_MS) * ABS_SPEED_MAX
                                : constrain(d.speedKmh, 0.0f, ABS_SPEED_MAX);
    const bool adjust = !selfTest && d.speedSel == 1;  // Speed encoder on REAR.
    char key[16];
    if (!selfTest && d.ecuField && ecuFault(d.ecu)) {
      // No return signal: the REAR panel shows the reason instead of the bar
      // (also while the white Checking lamp is on).
      snprintf(key, sizeof(key), "E%d", int(d.ecu));
      if (changed(S_BAR0, key)) ecuOffline(d.ecu);
    } else {
      snprintf(key, sizeof(key), "%.0f %d", rear, adjust);
      if (changed(S_BAR0, key)) drawBigBar(G_FRONT, rear, true, adjust);
    }
  } else if (mode == PAGE2) {
    const bool signal = d.inputHz > 0;
    const float kmh = constrain(d.inputHz / INPUT_HZ_PER_KMH, 0.0f, INPUT_MAX_KMH);
    char key[16];
    snprintf(key, sizeof(key), "%d %.1f %u", signal, kmh, unsigned(d.d6State));
    if (changed(S_INPUT, key)) drawBigBar(G_INPUT, kmh, signal, false, d.d6State);
  } else {
    char key[16];
    snprintf(key, sizeof(key), "%d %.0f", running, d.declogHz);
    if (changed(S_DECLOG, key)) drawBigBar(G_DECLOG, d.declogHz, running);
  }
}

}  // namespace absDisplay
