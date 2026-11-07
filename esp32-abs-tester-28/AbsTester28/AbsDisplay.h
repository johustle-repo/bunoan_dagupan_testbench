#pragma once
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "AaGraphics.h"

// ABS tester on the 2.8" ILI9341 TFT (landscape 320 x 240). Everything is
// drawn anti-aliased (AaGraphics.h) with the Barlow Semi Condensed fonts in
// AaFonts.h, into off-screen strips that are pushed in one SPI burst each, so
// nothing flickers. Two pages: swipe left for page 2, swipe right for page 1.
//   Header (both pages): REF1 | BAT.
//   Page 1: FRONT (D8, left) and REAR (D7, right) speedometers, 0-250 km/h,
//           each with its speed bar below.
//   Page 2: one 0-2000 Hz gauge for the ESP32's own frequency input (GPIO32)
//           and the DECLOG bar (the Nano's D2, 0-100 Hz shown as 0-100 %).
//   Footer (both pages): ABS light | page dots | TCS light.
// Page 1 needs the Nano (WAITING FOR MEGA / NO LINK until data arrives, then
// the switch-on self-test); page 2 works without it (DECLOG then shows --).
// Touch: the XPT2046 shares the display's SPI bus (T_CLK 18, T_DIN 23,
// T_DO 19) with its own chip select, T_CS on GPIO33. Only the direction of a
// swipe is used, so no calibration. T_IRQ is not used (unconnected or on
// GPIO35, which is then not used for anything else); it must not be wired
// to GPIO32 (that is the frequency input).
constexpr uint8_t TFT_SCK = 18, TFT_MISO = 19, TFT_MOSI = 23;
constexpr uint8_t TFT_CS = 27, TFT_DC = 25, TFT_RST = 26, TOUCH_CS = 33;
constexpr uint32_t GAUGE_REFRESH_MS = 40, TEXT_REFRESH_MS = 100;
constexpr uint32_t SPLASH_MS = 1800, SELF_TEST_MS = 1600;  // Power-on splash, switch-on sweep.
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
constexpr float INPUT_MAX_HZ = 2000;  // Page 2 gauge.
constexpr float ABS_CHECK_KMH = 10;   // ABS lamp: off above, on below (see AbsLamp).

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
  bool tcsSteady, absActive;     // The Nano's TCS (D13) and ABS (D12) LED states.
  float declogHz;                // D2, 0 with older firmware.
  bool gens;                     // Frame carries the Nano's pot generator settings.
  float genHz[3];                // Mega pots: rear A1 -> D44, front A0 -> D3 (0-1500 Hz), Declog A2 -> D11 (0-100 Hz).
  float inputHz;                 // ESP32 GPIO32 frequency input (page 2), measured here.
  bool absLamp;                  // ABS light, from AbsLamp.
};

// ABS light, like a real bike's start-up check, worked out from the REAR speed:
//   switched on (live data arrives): ON;
//   rear speed above 10 km/h: OFF (check passed);
//   after that: below 10 km/h ON again, 0 km/h OFF, above 10 km/h OFF.
// Losing the Nano link counts as switching off, so the next start begins ON.
struct AbsLamp {
  bool checked = false;  // Has been above 10 km/h since switch-on.
  bool on = true;
  void reset() { checked = false; on = true; }
  void update(float rearKmh) {
    if (!checked) {
      if (rearKmh > ABS_CHECK_KMH) { checked = true; on = false; }
      else on = true;
      return;
    }
    if (rearKmh < 0.5f) on = false;                 // Shows as 0 km/h.
    else if (rearKmh < ABS_CHECK_KMH) on = true;
    else if (rearKmh > ABS_CHECK_KMH) on = false;   // Exactly 10: unchanged.
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
// Header (both pages): REF1 | BAT.
constexpr int TOP_Y = 3, TOP_H = 30;
constexpr int TOP_X[2] = {4, 162}, TOP_W = 154;
// Page 1: two speedometers and their speed bars.
constexpr int GY = 37, GH = 140, GW = 154;
constexpr int GXS[2] = {162, 4};                 // REAR (right), FRONT (left).
constexpr float DCY1 = GY + 90, DR1 = 66;         // Dial centre height and radius.
constexpr int SBY = 181, SBH = 28;               // Speed bars.
// Page 2: the 0-2000 Hz gauge and the DECLOG bar.
constexpr int P2X = 4, P2Y = 37, P2W = 312, P2H = 142;
constexpr float DCX2 = 160, DCY2 = P2Y + 124, DR2 = 106;
constexpr int PLATE_W = 88, PLATE_H = 44, PLATE_Y = DCY2 - 46;
constexpr int DY = 183, DH = 36;                 // DECLOG panel.
// Footer (both pages): ABS light | page dots | TCS light.
constexpr int DOTS_X = 140, DOTS_Y = 222, DOTS_W = 40, DOTS_H = 16;
constexpr int LAMP_X[2] = {88, 188}, LAMP_W = 44, LAMP_H = 16, LAMP_Y = 222;

const char* const TOP_LABELS[2] = {"REF1", "BAT"};
const uint16_t TOP_ACCENTS[2] = {ACCENT, AMBER};
const char* const GAUGE_LABELS[2] = {"REAR", "FRONT"};
const uint16_t GAUGE_COLORS[2] = {RED, GREEN};
const char* const LAMP_LABELS[2] = {"ABS", "TCS"};

// Strips of the screen are drawn here, then pushed.
constexpr int SCRATCH_PX = 320 * 28;
uint16_t scratch[SCRATCH_PX];
// Static dial of the current page, drawn once per page change (page 1: one
// speedometer face, used for both). Null if the heap had no room; the dial is
// then drawn again with every gauge frame.
uint16_t* dialBg = nullptr;

enum Mode : uint8_t { NONE, LOCKED, PAGE1, PAGE2 } mode = NONE;
uint8_t dialFor = 0;  // Page whose dial is in dialBg (0 = none).
enum Slot : uint8_t { S_TOP0, S_TOP1, S_LAMPS, S_DOTS, S_BAR0, S_BAR1, S_DECLOG, S_LOCK, SLOT_COUNT };
char shown[SLOT_COUNT][40];
uint32_t lastGaugeMs = 0, lastTextMs = 0, selfTestStartMs = 0;
bool selfTestActive = false;
int32_t shownGaugeKey[2] = {-1, -1};
float easeRear = 0, easeFront = 0, easeInput = 0;  // Needle positions, eased toward the readings.

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

// Warning lights: a round light and its label ("(o) ABS"). Lit: an amber
// light with a soft glow and bright text; off: a dark ringed light, dim text.
void drawLamps(const bool lit[2]) {
  for (uint8_t i = 0; i < 2; ++i) {
    const int x = LAMP_X[i];
    const float cx = x + 8, cy = LAMP_Y + LAMP_H / 2.0f;
    render(x - 1, LAMP_Y - 1, LAMP_W + 2, LAMP_H + 2, [&](aa::Buf& b) {
      aa::fill(b, BG);
      if (lit[i]) {
        aa::circle(b, cx, cy, 8.5f, aa::mix(BG, AMBER, 70));  // Glow.
        aa::circle(b, cx, cy, 6, AMBER);
        aa::circle(b, cx - 1.8f, cy - 1.8f, 1.8f, aa::mix(AMBER, TEXT, 170));  // Highlight.
      } else {
        aa::circle(b, cx, cy, 6, OFF_EDGE);
        aa::circle(b, cx, cy, 5, OFF_FILL);
      }
      aa::text(b, aaLabel, LAMP_LABELS[i], x + 19, LAMP_Y + 13, lit[i] ? TEXT : OFF_TEXT, aa::LEFT, 1);
    });
  }
}

void drawDots(uint8_t page) {
  render(DOTS_X, DOTS_Y, DOTS_W, DOTS_H, [&](aa::Buf& b) {
    aa::fill(b, BG);
    const float cy = DOTS_Y + DOTS_H / 2.0f;
    aa::circle(b, 154, cy, 3, page == 1 ? ACCENT : DIM);
    aa::circle(b, 166, cy, 3, page == 2 ? ACCENT : DIM);
  });
}

// REF1 / BAT, the ABS / TCS lights (TCS blinks at 2 Hz; the self-test lights
// both) and the page dots. Without live data both lights stay off.
void topRow(const AbsData& d, uint8_t page, bool live, bool selfTest) {
  char key[16];
  for (uint8_t i = 0; i < 2; ++i) {
    const float v = i ? d.batteryV : d.ref1V;
    snprintf(key, sizeof(key), "%.2f", v);
    if (changed(S_TOP0 + i, key)) drawTop(i, v);
  }
  const bool blinkOn = (millis() / 250) % 2;
  const bool lit[2] = {live && (selfTest || d.absLamp), live && (selfTest || (tcsBlinking(d) && blinkOn))};
  snprintf(key, sizeof(key), "%d%d", lit[0], lit[1]);
  if (changed(S_LAMPS, key)) drawLamps(lit);
  snprintf(key, sizeof(key), "%u", page);
  if (changed(S_DOTS, key)) drawDots(page);
}

// ---- Page 1: REAR and FRONT speedometers ----

void drawDial1(aa::Buf& b, int gx) {
  aa::fill(b, BG);
  panel(b, gx, GY, GW, GH, 10);
  dialFace(b, gx + GW / 2.0f, DCY1, DR1, ABS_SPEED_MAX, 10, 50, SPEED_RED, aaSmall, 26, 0.75f);
}

// Lit arc to the speed, the needle (red rear, green front), the label in the
// corner and the digital speed under the centre. `have` false: no needle, "--".
void renderGauge1(uint8_t i, float kmh, bool have) {
  const int gx = GXS[i];
  const float cx = gx + GW / 2.0f;
  char digits[6];
  if (have) snprintf(digits, sizeof(digits), "%d", int(lroundf(kmh)));
  else strlcpy(digits, "--", sizeof(digits));
  render(gx, GY, GW, GH, [&](aa::Buf& b) {
    if (dialBg && dialFor == 1) memcpy(b.px, dialBg + (b.oy - GY) * GW, b.w * b.h * 2);
    else drawDial1(b, gx);
    aa::text(b, aaLabel, GAUGE_LABELS[i], gx + 10, GY + 17, GAUGE_COLORS[i], aa::LEFT, 1);
    if (have) {
      const float a = angleOf(kmh, ABS_SPEED_MAX);
      aa::arc(b, cx, DCY1, DR1 - 5, DR1, PI, a,
              [](float ang) { return speedColor((ang - PI) / PI * ABS_SPEED_MAX); });
      needle(b, cx, DCY1, a, 7, DR1 - 3, 4.0f, 1.4f, GAUGE_COLORS[i], i == 0);
    }
    aa::circle(b, cx, DCY1, 5, have ? GAUGE_COLORS[i] : DIM);
    aa::circle(b, cx, DCY1, 2, PLATE);
    const int wd = aa::textWidth(aaBig, digits), wu = aa::textWidth(aaSmall, "km/h");
    const int x = cx - (wd + 4 + wu) / 2;
    const int end = aa::text(b, aaBig, digits, x, DCY1 + 44, have && kmh >= SPEED_RED ? RED : TEXT);
    aa::text(b, aaSmall, "km/h", end + 4, DCY1 + 44, MUTED);
  });
}

void drawSpeedBar(uint8_t i, float kmh, bool have) {
  const int gx = GXS[i];
  render(gx, SBY, GW, SBH, [&](aa::Buf& b) {
    aa::fill(b, BG);
    panel(b, gx, SBY, GW, SBH, 7);
    constexpr int LEN = 82;
    const int x0 = gx + 10, cy = SBY + SBH / 2;
    aa::roundRect(b, x0, cy - 4, LEN, 8, 4, TRACK);
    const float f = have ? constrain(kmh / ABS_SPEED_MAX, 0.0f, 1.0f) : 0;
    if (f > 0.01f) aa::roundRect(b, x0, cy - 4, max(8.0f, f * LEN), 8, 4, speedColor(kmh));
    char s[12];
    if (have) snprintf(s, sizeof(s), "%.0f km/h", kmh);
    else strlcpy(s, "-- km/h", sizeof(s));
    aa::text(b, aaLabel, s, gx + GW - 10, cy + 5, TEXT, aa::RIGHT);
  });
}

void page1Text(const AbsData& d) {
  char key[16];
  const float speeds[2] = {d.speedKmh, d.frontKmh};
  const bool have[2] = {true, d.front};
  for (uint8_t i = 0; i < 2; ++i) {
    snprintf(key, sizeof(key), "%d %.0f", have[i], speeds[i]);
    if (changed(S_BAR0 + i, key)) drawSpeedBar(i, speeds[i], have[i]);
  }
}

// ---- Page 2: 0-2000 Hz input gauge and the DECLOG bar ----

void drawDial2(aa::Buf& b) {
  aa::fill(b, BG);
  panel(b, P2X, P2Y, P2W, P2H, 10);
  dialFace(b, DCX2, DCY2, DR2, INPUT_MAX_HZ, 100, 500, INPUT_MAX_HZ + 1, aaDial, 30, 1.0f);
  aa::circle(b, 16, P2Y + 13, 3, ACCENT);
  aa::text(b, aaLabel, "FREQUENCY", 24, P2Y + 18, TEXT, aa::LEFT, 1);
  aa::text(b, aaSmall, "GPIO32", P2X + P2W - 10, P2Y + 17, MUTED, aa::RIGHT, 1);
}

void renderGauge2(float hz) {
  char digits[6];
  snprintf(digits, sizeof(digits), "%d", int(lroundf(constrain(hz, 0.0f, 9999.0f))));
  const int px = DCX2 - PLATE_W / 2;
  render(P2X, P2Y, P2W, P2H, [&](aa::Buf& b) {
    if (dialBg && dialFor == 2) memcpy(b.px, dialBg + (b.oy - P2Y) * P2W, b.w * b.h * 2);
    else drawDial2(b);
    const float a = angleOf(hz, INPUT_MAX_HZ);
    aa::arc(b, DCX2, DCY2, DR2 - 7, DR2, PI, a, [](float) { return ACCENT; });
    needle(b, DCX2, DCY2, a, 26, DR2 - 3, 5.5f, 1.8f, RED, true);
    aa::roundRect(b, px - 3, PLATE_Y - 3, PLATE_W + 6, PLATE_H + 6, 11, FACE);  // Gap around the plate.
    aa::roundRect(b, px, PLATE_Y, PLATE_W, PLATE_H, 8, PLATE, PLATE_EDGE, 1);
    aa::text(b, aaSmall, "Hz", DCX2, PLATE_Y + 12, MUTED, aa::CENTER, 1);
    aa::text(b, aaBig, digits, DCX2, PLATE_Y + 41, TEXT, aa::CENTER, 1);
  });
}

// DECLOG: the Nano's D2 frequency, 0-100 Hz shown as 0-100 %.
void drawDeclog(float hz, bool have) {
  const float pct = constrain(hz / DECLOG_MAX_HZ * 100, 0.0f, 100.0f);
  render(P2X, DY, P2W, DH, [&](aa::Buf& b) {
    aa::fill(b, BG);
    panel(b, P2X, DY, P2W, DH);
    aa::text(b, aaLabel, "DECLOG", P2X + 10, DY + 16, TEXT, aa::LEFT, 1);
    char s[16];
    if (have) snprintf(s, sizeof(s), "D2  %.1f Hz", hz);
    else strlcpy(s, "NO MEGA", sizeof(s));
    aa::text(b, aaSmall, s, P2X + 10, DY + 29, MUTED, aa::LEFT);
    constexpr int X0 = P2X + 92, LEN = 150;
    const int cy = DY + DH / 2;
    aa::roundRect(b, X0, cy - 5, LEN, 10, 5, TRACK);
    if (have && pct > 0.5f) aa::roundRect(b, X0, cy - 5, max(10.0f, pct / 100 * LEN), 10, 5, ACCENT);
    if (have) snprintf(s, sizeof(s), "%.0f", pct);
    else strlcpy(s, "--", sizeof(s));
    const int unitX = P2X + P2W - 10 - aa::textWidth(aaLabel, "%");
    aa::text(b, aaLabel, "%", unitX, cy + 7, MUTED);
    aa::text(b, aaValue, s, unitX - 2, cy + 9, TEXT, aa::RIGHT);
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
  if (page == 1) {
    aa::Buf b{dialBg, GW, GH, GXS[0], GY};
    drawDial1(b, GXS[0]);
  } else {
    aa::Buf b{dialBg, P2W, P2H, P2X, P2Y};
    drawDial2(b);
  }
  dialFor = page;
}

void clearScreen() {
  forget();
  render(0, 0, W, H, [](aa::Buf& b) { aa::fill(b, BG); });
}

void lockedText(const AbsData& d) {
  const bool live = d.link == AbsData::LIVE;
  const char* line = d.link == AbsData::WAITING ? "WAITING FOR MEGA" : d.link == AbsData::STALE ? "NO LINK" : "UPDATE MEGA";
  const uint16_t color = live ? AMBER : RED;
  if (!changed(S_LOCK, line)) return;
  render(30, 30, 260, 180, [&](aa::Buf& b) {
    aa::fill(b, BG);
    aa::roundRect(b, 30, 30, 260, 180, 14, PANEL, PANEL_EDGE, 1);
    absSymbol(b, W / 2, 84, 22, color, aaPill);
    aa::text(b, aaValue, line, W / 2, 144, color, aa::CENTER, 1);
    aa::text(b, aaLabel, live ? "Flash the new Mega firmware" : "Check the Mega link", W / 2, 166, MUTED,
             aa::CENTER);
    aa::text(b, aaSmall, "Swipe left: page 2 (GPIO32 input)", W / 2, 186, DIM, aa::CENTER, 1);
    aa::text(b, aaSmall, "Wi-Fi ABS-Tester  |  192.168.4.1", W / 2, 201, DIM, aa::CENTER, 1);
  });
}

// Power-on splash: the ABS lamp ring sweeps round, the title fades in and a
// loading bar fills.
void splash() {
  constexpr int BAR_X = 100, BAR_Y = 186, BAR_W = 120;
  for (uint32_t start = millis(), t = 0; t <= SPLASH_MS; t = millis() - start) {
    const float p = float(t) / SPLASH_MS, fade = constrain((p - 0.15f) / 0.45f, 0.0f, 1.0f);
    render(0, 0, W, H, [&](aa::Buf& b) {
      aa::fill(b, BG);
      const float cx = W / 2, cy = 90, r = 30;
      aa::arc(b, cx, cy, r - 4, r, HALF_PI, HALF_PI + TWO_PI, [](float) { return TRACK; });
      const float sweep = 3 * HALF_PI + TWO_PI * min(1.0f, p / 0.6f);
      aa::arc(b, cx, cy, r - 4, r, 3 * HALF_PI, sweep, [](float) { return ACCENT; });
      const uint16_t amber = aa::mix(BG, AMBER, fade * 255);
      aa::arc(b, cx, cy, r + 6, r + 10, PI - 0.6f, PI + 0.6f, [amber](float) { return amber; });
      aa::arc(b, cx, cy, r + 6, r + 10, TWO_PI - 0.6f, TWO_PI + 0.6f, [amber](float) { return amber; });
      aa::textMid(b, aaPill, "ABS", cx, cy, amber, 1);
      aa::text(b, aaValue, "ABS TESTER", cx, 164, aa::mix(BG, TEXT, fade * 255), aa::CENTER, 3);
      aa::roundRect(b, BAR_X, BAR_Y, BAR_W, 6, 3, TRACK);
      aa::roundRect(b, BAR_X, BAR_Y, max(6.0f, BAR_W * min(1.0f, p)), 6, 3, ACCENT);
    });
    delay(10);
  }
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
  splash();
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

// Call every loop. page: 1 or 2.
void update(const AbsData& d, uint8_t page) {
  const uint32_t now = millis();
  const bool running = d.link == AbsData::LIVE && d.keyOn;
  const Mode want = page == 2 ? PAGE2 : running ? PAGE1 : LOCKED;
  if (want != mode) {
    // The switch-on self-test runs when page 1 comes up with live data after
    // the waiting / no link screen (not when coming back from page 2).
    if (want == PAGE1 && (mode == LOCKED || mode == NONE)) { selfTestActive = true; selfTestStartMs = now; }
    if (want != PAGE1) selfTestActive = false;
    clearScreen();
    if (want != LOCKED) cacheDial(want == PAGE1 ? 1 : 2);
    mode = want;
    lastGaugeMs = lastTextMs = 0;
  }
  const uint32_t testMs = now - selfTestStartMs;
  if (selfTestActive && (!running || testMs >= SELF_TEST_MS)) selfTestActive = false;
  const bool selfTest = selfTestActive;

  if (mode != LOCKED && now - lastGaugeMs >= GAUGE_REFRESH_MS) {
    lastGaugeMs = now;
    if (mode == PAGE1) {
      const float sweep = sinf(PI * testMs / SELF_TEST_MS) * ABS_SPEED_MAX;  // Self-test: 0 -> 250 -> 0.
      const float rear = selfTest ? sweep : ease(easeRear, constrain(d.speedKmh, 0.0f, ABS_SPEED_MAX), 0.2f);
      const float front = selfTest ? sweep : ease(easeFront, constrain(d.frontKmh, 0.0f, ABS_SPEED_MAX), 0.2f);
      const float v[2] = {rear, front};
      const bool have[2] = {true, selfTest || d.front};
      for (uint8_t i = 0; i < 2; ++i) {
        const int32_t key = lroundf(v[i] * 2) | int32_t(have[i]) << 12;
        if (key == shownGaugeKey[i]) continue;
        shownGaugeKey[i] = key;
        renderGauge1(i, v[i], have[i]);
      }
    } else {
      const float hz = ease(easeInput, max(0.0f, d.inputHz), 0.5f);
      const int32_t key = lroundf(hz * 2);
      if (key != shownGaugeKey[0]) {
        shownGaugeKey[0] = key;
        renderGauge2(hz);
      }
    }
  }

  if (now - lastTextMs < TEXT_REFRESH_MS) return;
  lastTextMs = now;
  if (mode == LOCKED) { lockedText(d); return; }
  topRow(d, page, running, selfTest);
  if (mode == PAGE1) {
    AbsData bars = d;
    if (selfTest) { bars.speedKmh = bars.frontKmh = sinf(PI * testMs / SELF_TEST_MS) * ABS_SPEED_MAX; bars.front = true; }
    page1Text(bars);
  } else {
    char key[16];
    snprintf(key, sizeof(key), "%d %.1f", running, d.declogHz);
    if (changed(S_DECLOG, key)) drawDeclog(d.declogHz, running);
  }
}

}  // namespace absDisplay
