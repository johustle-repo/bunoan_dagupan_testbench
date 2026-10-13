#pragma once
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// Local 2.8" ILI9341 TFT + XPT2046 touch panel (landscape 320 x 240) showing
// the instrument panel of the browser dashboard: the Classic display
// (speedometer, lamps, sensor column, bars) and the Race display (lamp strip,
// curved segmented RPM bar, tiles). Tap the screen to switch tabs.
// Display and touch share the VSPI bus with separate chip selects; none of
// these pins overlap the Uno link on GPIO16/17.
constexpr uint8_t TFT_SCK = 18, TFT_MISO = 19, TFT_MOSI = 23;
constexpr uint8_t TFT_CS = 27, TFT_DC = 25, TFT_RST = 26, TOUCH_CS = 33;
// Gauges are drawn off-screen and pushed in one SPI burst (about 10-13 ms),
// so needles never flicker. Text and lamps only redraw when they change.
constexpr uint32_t GAUGE_REFRESH_MS = 50, TEXT_REFRESH_MS = 100;
constexpr uint32_t SELF_TEST_MS = 2000;
constexpr float SPEED_MAX = 300, RPM_MAX = 15000;

// Snapshot of what the panel shows, filled by the sketch each loop.
struct DisplayData {
  enum Link : uint8_t { WAITING, LIVE, STALE } link;
  bool keyOn, brake, checkEngine, coolantWarning;
  uint8_t milBlink;        // Blink code of the first DTC, 0 = none.
  float speedKmh, rpm, frequencyHz;
  uint8_t gear;            // 0 = neutral.
  const char* dtcCode;     // First active DTC, or nullptr.
  const char* dtcSensor;
  SensorReading ect, iat, map, tps;
  OxygenReading o2;
};

namespace localDisplay {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// Browser dashboard palette (style.css).
constexpr uint16_t BG = rgb(0x0d, 0x14, 0x1e), PANEL = rgb(0x13, 0x1d, 0x29), EDGE = rgb(0x43, 0x54, 0x6b);
constexpr uint16_t TEXT = rgb(0xf2, 0xf7, 0xfc), MUTED = rgb(0xac, 0xbd, 0xd1), ACCENT = rgb(0x66, 0xd4, 0xf1);
constexpr uint16_t ARC = rgb(0x33, 0x46, 0x5c), TICK = rgb(0x81, 0x99, 0xb5), TICK_LABEL = rgb(0xae, 0xbd, 0xd0);
constexpr uint16_t DIGITS = rgb(0xc7, 0xf3, 0xff), GEAR_COLOR = rgb(0xff, 0xbd, 0x64);
constexpr uint16_t AMBER = rgb(0xff, 0xbd, 0x64), ORANGE = rgb(0xff, 0x9b, 0x36), RED = rgb(0xff, 0x4d, 0x4d);
constexpr uint16_t GREEN = rgb(0x42, 0xd9, 0x7c), LAMP_OFF = rgb(0x23, 0x30, 0x44), LAMP_RING = rgb(0x63, 0x73, 0x8a);
constexpr uint16_t BAR_TRACK = rgb(0x25, 0x34, 0x48), BAR_GAP = rgb(0x11, 0x1b, 0x28), BAR_EDGE = rgb(0x4f, 0x63, 0x7b);
constexpr uint16_t RACE_BG = rgb(0x06, 0x09, 0x0e), RACE_TRACK = rgb(0x1b, 0x24, 0x30), RACE_DIM = rgb(0x5d, 0x6b, 0x7c);
constexpr uint16_t RACE_NA = rgb(0x2f, 0x3c, 0x4c), RACE_LABEL = rgb(0x5f, 0xe0, 0x8a);
constexpr uint16_t TILE_BG = rgb(0x11, 0x18, 0x21), TILE_EDGE = rgb(0x2a, 0x36, 0x46), TILE_INK = rgb(0x04, 0x21, 0x0e);
constexpr uint16_t TP_TRACK = rgb(0x1d, 0x2a, 0x3a);
constexpr uint16_t MIX_RICH = rgb(0xff, 0x7a, 0x45), MIX_STOICH = rgb(0x42, 0xd9, 0x7c), MIX_LEAN = rgb(0x4e, 0xa8, 0xff);

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen touch(TOUCH_CS);

enum View : uint8_t { CLASSIC, RACE } view = CLASSIC;
GFXcanvas16* gauge = nullptr;   // Off-screen buffer for the active view's gauge.
GFXcanvas16* barCanvas = nullptr;
GFXcanvas16* throttleCanvas = nullptr;
bool wasTouched = false, wasRunning = false, selfTestActive = false;
uint32_t lastTapMs = 0, lastGaugeMs = 0, lastTextMs = 0, lastAnimMs = 0, selfTestStartMs = 0;
float animatedRpm = 0;
int32_t shownGaugeKey = -1;

// Last text / colour drawn in each slot; a slot is redrawn only when it changes.
constexpr uint8_t SLOT_COUNT = 40;
char shown[SLOT_COUNT][24];
uint16_t shownColor[SLOT_COUNT];
enum Slot : uint8_t { S_STATUS, S_DTC, S_BAR0, S_BAR1, S_BAR2, S_LAMP0 = 8, S_VALUE0 = 16, S_TP_ICON = 30, S_TP_ANGLE };

bool changed(uint8_t slot, const char* text, uint16_t color) {
  if (!strcmp(shown[slot], text) && shownColor[slot] == color) return false;
  strlcpy(shown[slot], text, sizeof(shown[slot]));
  shownColor[slot] = color;
  return true;
}

// Fixed-width text with an opaque background, so nothing needs clearing.
void field(uint8_t slot, int x, int y, uint8_t size, uint16_t color, uint16_t bg, const char* text, uint8_t width) {
  char padded[24];
  snprintf(padded, sizeof(padded), "%*s", width, text);
  if (!changed(slot, padded, color)) return;
  tft.setFont(nullptr);
  tft.setTextSize(size);
  tft.setTextColor(color, bg);
  tft.setCursor(x, y);
  tft.print(padded);
}

void centerText(Adafruit_GFX& g, const char* text, int cx, int baseline, const GFXfont* font, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  g.setFont(font);
  g.setTextSize(1);
  g.getTextBounds(text, 0, baseline, &x1, &y1, &w, &h);
  g.setTextColor(color);
  g.setCursor(cx - int(w) / 2 - x1, baseline);
  g.print(text);
  g.setFont(nullptr);
}

void smallText(Adafruit_GFX& g, const char* text, int x, int y, uint16_t color, int align = -1) {
  g.setFont(nullptr);
  g.setTextSize(1);
  g.setTextColor(color);
  const int w = strlen(text) * 6 - 1;
  g.setCursor(align < 0 ? x : align == 0 ? x - w / 2 : x - w, y);
  g.print(text);
}

// Sensor value with the browser's formatting; returns the colour to use.
uint16_t sensorText(const SensorReading& r, uint8_t decimals, char* out, size_t size) {
  if (!r.available) { strlcpy(out, "--", size); return RED; }
  snprintf(out, size, "%.*f", decimals, r.value);
  if (strlen(out) > 5) snprintf(out, size, "%.0f", r.value);
  return r.inSpec ? TEXT : AMBER;
}

uint16_t mixtureColor(const OxygenReading& o2) {
  return !strcmp(o2.mixture, "rich") ? MIX_RICH : !strcmp(o2.mixture, "stoich") ? MIX_STOICH : MIX_LEAN;
}

// ---- Lamps -----------------------------------------------------------------
// Order matches the browser: smart key, brake, winker, check engine, coolant,
// ABS, TCS. Winker and TCS have no input on this bench.
constexpr uint8_t LAMP_COUNT = 7;
const char* const CLASSIC_LAMPS[LAMP_COUNT] = {"KEY", "BRAKE", "WINKER", "CHECK", "TEMP", "ABS", "TCS"};
const char* const RACE_LAMPS[LAMP_COUNT] = {"KEY", "BRAKE", "WINKER", "CHECK", "TEMP", "ABS", "TCS"};
constexpr bool LAMP_WIRED[LAMP_COUNT] = {true, true, false, true, true, true, false};

// Honda-style blink code: long flashes count tens, short flashes units, then a
// pause. Steady while moving or when no blink code is known.
bool milLit(const DisplayData& d, float speed, uint32_t now) {
  if (!d.checkEngine) return false;
  if (!d.milBlink || speed >= 1) return true;
  const uint8_t tens = d.milBlink / 10, units = d.milBlink % 10;
  const uint32_t cycle = tens * 1800 + units * 800 + 2000;
  uint32_t t = now % cycle;
  for (uint8_t i = 0; i < tens; ++i) { if (t < 1200) return true; if (t < 1800) return false; t -= 1800; }
  for (uint8_t i = 0; i < units; ++i) { if (t < 400) return true; if (t < 800) return false; t -= 800; }
  return false;
}

// Lamp colours for the active view; 0 = off.
void lampColors(const DisplayData& d, bool selfTest, float speed, uint32_t now, uint16_t out[LAMP_COUNT]) {
  const bool fresh = d.link == DisplayData::LIVE, running = fresh && d.keyOn;
  const bool race = view == RACE;
  for (uint8_t i = 0; i < LAMP_COUNT; ++i) out[i] = selfTest ? ORANGE : 0;
  if (selfTest) return;
  if (fresh && d.keyOn) out[0] = race ? GREEN : ACCENT;
  if (fresh && d.brake) out[1] = race ? ORANGE : AMBER;
  if (running && milLit(d, speed, now)) out[3] = ORANGE;
  if (running && d.coolantWarning) out[4] = RED;
  if (running) out[5] = d.brake ? ORANGE : GREEN;
}

void drawClassicLamp(uint8_t i, uint16_t color) {
  const int y = 28 + i * 30;
  const char* key = color ? "1" : "0";
  char tag[8];
  snprintf(tag, sizeof(tag), "%s%u", key, color);
  if (!changed(S_LAMP0 + i, tag, color)) return;
  tft.fillRect(1, y, 59, 28, BG);
  if (color) tft.fillCircle(12, y + 13, 7, color);
  else if (LAMP_WIRED[i]) { tft.fillCircle(12, y + 13, 7, LAMP_OFF); tft.drawCircle(12, y + 13, 7, LAMP_RING); }
  else for (int a = 0; a < 360; a += 30) {  // Dashed ring: not wired on this bench.
    const float r = a * DEG_TO_RAD;
    tft.drawPixel(12 + lroundf(7 * cosf(r)), y + 13 + lroundf(7 * sinf(r)), LAMP_RING);
    tft.drawPixel(12 + lroundf(7 * cosf(r + 0.15f)), y + 13 + lroundf(7 * sinf(r + 0.15f)), LAMP_RING);
  }
  tft.setFont(nullptr);
  tft.setTextSize(1);
  tft.setTextColor(color ? color : LAMP_WIRED[i] ? MUTED : LAMP_RING);
  tft.setCursor(23, y + 10);
  tft.print(CLASSIC_LAMPS[i]);
}

void drawRaceLamp(uint8_t i, uint16_t color) {
  char tag[8];
  snprintf(tag, sizeof(tag), "%u", color);
  if (!changed(S_LAMP0 + i, tag, color)) return;
  const int x = 2 + i * 45, y = 26, w = 43, h = 16;
  tft.fillRect(x, y, w, h, RACE_BG);
  if (color) tft.fillRoundRect(x, y, w, h, 3, color);
  else if (LAMP_WIRED[i]) tft.fillRoundRect(x, y, w, h, 3, RACE_TRACK);
  else for (int dx = 0; dx < w; dx += 4) {  // Dashed outline: not wired.
    tft.drawFastHLine(x + dx, y, 2, RACE_NA);
    tft.drawFastHLine(x + dx, y + h - 1, 2, RACE_NA);
  }
  tft.setFont(nullptr);
  tft.setTextSize(1);
  tft.setTextColor(color ? RACE_BG : RACE_DIM);
  tft.setCursor(x + (w - int(strlen(RACE_LAMPS[i])) * 6 + 1) / 2, y + 4);
  tft.print(RACE_LAMPS[i]);
}

// ---- Throttle angle --------------------------------------------------------
// Side view of the throttle body, as in the browser: TPS 0-100 % turns the
// butterfly plate 0-THROTTLE_MAX_DEG. Closed, the plate is vertical (ends at
// 0 and 180 deg); TP_DIR -1 turns it counter-clockwise, so wide open ends at
// 270 and 90 deg. Orange shows the angle the plate has turned.
constexpr float THROTTLE_MAX_DEG = 90;
constexpr int TP_DIR = -1;
constexpr int TP_SIZE = 26, TP_C = 13, TP_R = 12;

inline float throttleDeg(const SensorReading& tps) { return tps.value * THROTTLE_MAX_DEG / 100; }

// Plate end angles, e.g. "0-180" closed, "270-90" wide open.
void throttleEnds(float deg, char* out, size_t size) {
  const int end1 = (int(lroundf(TP_DIR * deg)) + 360) % 360;
  snprintf(out, size, "%d-%d", end1, (end1 + 180) % 360);
}

inline void tpPoint(float deg, float radius, int& x, int& y) {
  x = TP_C + lroundf(sinf(deg * DEG_TO_RAD) * radius);
  y = TP_C - lroundf(cosf(deg * DEG_TO_RAD) * radius);
}

void drawThrottle(int x, int y, uint16_t bg, bool has, float deg) {
  char tag[12];
  snprintf(tag, sizeof(tag), has ? "%d" : "-", int(lroundf(deg)));
  if (!changed(S_TP_ICON, tag, bg)) return;
  GFXcanvas16& c = *throttleCanvas;
  c.fillScreen(bg);
  c.fillCircle(TP_C, TP_C, TP_R, TP_TRACK);
  if (has && deg > 0.2f) {
    // Arcs run clockwise, so a counter-clockwise turn spans [-deg, 0].
    const float a0 = TP_DIR > 0 ? 0 : -deg, a1 = TP_DIR > 0 ? deg : 0;
    for (int half = 0; half < 360; half += 180)
      for (float a = a0; a < a1; a += 5) {
        int x1, y1, x2, y2;
        tpPoint(half + a, TP_R, x1, y1);
        tpPoint(half + min(a + 5, a1), TP_R, x2, y2);
        c.fillTriangle(TP_C, TP_C, x1, y1, x2, y2, ORANGE);
      }
  }
  c.drawCircle(TP_C, TP_C, TP_R, BAR_EDGE);
  for (int a = 0; a < 360; a += 90) {
    int x1, y1, x2, y2;
    tpPoint(a, TP_R, x1, y1);
    tpPoint(a, TP_R - 3, x2, y2);
    c.drawLine(x1, y1, x2, y2, MUTED);
  }
  if (has) {
    const float turn = TP_DIR * deg;
    int x1, y1, x2, y2;
    tpPoint(turn, TP_R - 1, x1, y1);
    tpPoint(turn + 180, TP_R - 1, x2, y2);
    const int ox = lroundf(cosf(turn * DEG_TO_RAD)), oy = lroundf(sinf(turn * DEG_TO_RAD));
    c.drawLine(x1, y1, x2, y2, TEXT);
    c.drawLine(x1 + ox, y1 + oy, x2 + ox, y2 + oy, TEXT);
  }
  c.fillCircle(TP_C, TP_C, 2, bg);
  c.drawCircle(TP_C, TP_C, 2, TEXT);
  tft.drawRGBBitmap(x, y, c.getBuffer(), TP_SIZE, TP_SIZE);
}

// ---- Classic display -------------------------------------------------------
constexpr int CG_X = 62, CG_Y = 38, CG_W = 192, CG_H = 128;  // Speedometer canvas.
constexpr int CG_CX = 96, CG_CY = 120, CG_R = 88;
const char* const CLASSIC_SENSORS[5] = {"AIT C", "ECT C", "O2 lambda", "MAP kPa", "TPS %"};

inline void polar(float value, float r, int& x, int& y) {
  const float a = PI + value / SPEED_MAX * PI;
  x = CG_CX + lroundf(cosf(a) * r);
  y = CG_CY + lroundf(sinf(a) * r);
}

void renderClassicGauge(float speed, bool showNeedle, const char* digits, const char* gearText) {
  GFXcanvas16& c = *gauge;
  c.fillScreen(BG);
  // 0-300 arc, ticks every 10 km/h, labels every 50.
  for (float r = CG_R - 1; r <= CG_R + 1; r += 1) {
    int px, py; polar(0, r, px, py);
    for (int v = 3; v <= 300; v += 3) { int x, y; polar(v, r, x, y); c.drawLine(px, py, x, y, ARC); px = x; py = y; }
  }
  for (int i = 0; i <= 30; ++i) {
    int x1, y1, x2, y2;
    polar(i * 10, CG_R - 2, x1, y1);
    polar(i * 10, i % 5 ? CG_R - 7 : CG_R - 11, x2, y2);
    c.drawLine(x1, y1, x2, y2, TICK);
    if (i % 5 == 0) {
      int tx, ty; polar(i * 10, CG_R - 22, tx, ty);
      char label[4]; snprintf(label, sizeof(label), "%d", i * 10);
      smallText(c, label, tx, ty - 3, TICK_LABEL, 0);
    }
  }
  if (showNeedle) {
    const float a = PI + constrain(speed, 0.0f, SPEED_MAX) / SPEED_MAX * PI;
    const int tx = CG_CX + lroundf(cosf(a) * (CG_R - 10)), ty = CG_CY + lroundf(sinf(a) * (CG_R - 10));
    const int ox = lroundf(-sinf(a)), oy = lroundf(cosf(a));  // 1 px perpendicular offset.
    c.drawLine(CG_CX, CG_CY, tx, ty, ACCENT);
    c.drawLine(CG_CX + ox, CG_CY + oy, tx + ox, ty + oy, ACCENT);
    c.fillCircle(CG_CX, CG_CY, 5, ACCENT);
  }
  centerText(c, digits, CG_CX, 98, &FreeSansBold18pt7b, DIGITS);
  smallText(c, "km/h", CG_CX, 104, MUTED, 0);
  // Gear indicator, top right as in the browser.
  c.fillRoundRect(156, 0, 36, 36, 5, BG);
  c.drawRoundRect(156, 0, 36, 36, 5, EDGE);
  smallText(c, "GEAR", 174, 3, MUTED, 0);
  centerText(c, gearText, 174, 32, &FreeSansBold12pt7b, GEAR_COLOR);
  tft.drawRGBBitmap(CG_X, CG_Y, c.getBuffer(), CG_W, CG_H);
}

// Segmented bar like the browser's .bar (9 px fill, 3 px gap).
void drawBar(int y, float fraction, uint16_t color) {
  GFXcanvas16& b = *barCanvas;
  const int w = b.width(), h = b.height();
  b.fillScreen(BAR_TRACK);
  b.fillRect(0, 0, lroundf(constrain(fraction, 0.0f, 1.0f) * w), h, color);
  for (int x = 9; x < w; x += 12) b.fillRect(x, 0, 3, h, BAR_GAP);
  b.drawRect(0, 0, w, h, BAR_EDGE);
  tft.drawRGBBitmap(102, y, b.getBuffer(), w, h);
}

void drawClassicFrame() {
  tft.drawFastVLine(61, 24, 216, EDGE);
  tft.drawFastVLine(255, 24, 216, EDGE);
  smallText(tft, "SIMULATED SPEED 0-300", 158, 28, MUTED, 0);
  const char* const bars[3] = {"km/h", "RPM", "DC Log"};
  for (uint8_t i = 0; i < 3; ++i) smallText(tft, bars[i], 64, 174 + i * 16, MUTED);
  for (uint8_t i = 0; i < 5; ++i) {
    const int y = 24 + i * 43;
    smallText(tft, CLASSIC_SENSORS[i], 261, y + 6, MUTED);
    if (i < 4) tft.drawFastHLine(256, y + 42, 64, EDGE);
  }
}

void classicText(const DisplayData& d, bool running, float speed, float rpm) {
  char value[16];
  const SensorReading* temps[2] = {&d.iat, &d.ect};
  for (uint8_t i = 0; i < 4; ++i) {
    const int y = 24 + i * 43 + 20;
    uint16_t color = MUTED;
    if (!running) strcpy(value, "--");
    else if (i < 2) color = sensorText(*temps[i], 1, value, sizeof(value));
    else if (i == 2) { snprintf(value, sizeof(value), "%.2f", d.o2.lambda); color = mixtureColor(d.o2); }
    else color = sensorText(d.map, 1, value, sizeof(value));
    field(S_VALUE0 + i, 259, y, 2, color, BG, value, 5);
  }
  // TPS row: throttle-body symbol, whole percent and plate end angles.
  const bool tpsOk = running && d.tps.available;
  char angle[12] = "";
  if (tpsOk) {
    snprintf(value, sizeof(value), "%.0f", d.tps.value);
    throttleEnds(throttleDeg(d.tps), angle, sizeof(angle));
  } else strcpy(value, "--");
  drawThrottle(257, 24 + 4 * 43 + 15, BG, tpsOk, tpsOk ? throttleDeg(d.tps) : 0);
  field(S_VALUE0 + 4, 284, 24 + 4 * 43 + 15, 2, !running ? MUTED : !tpsOk ? RED : d.tps.inSpec ? TEXT : AMBER, BG, value, 3);
  field(S_TP_ANGLE, 284, 24 + 4 * 43 + 34, 1, ORANGE, BG, angle, 6);
  snprintf(value, sizeof(value), running ? "%.1f" : "--", speed);
  field(S_BAR0, 204, 175, 1, TEXT, BG, value, 8);
  snprintf(value, sizeof(value), running ? "%.0f" : "--", rpm);
  field(S_BAR1, 204, 191, 1, TEXT, BG, value, 8);
  snprintf(value, sizeof(value), running ? "%.1f%%" : "-- %%", constrain(d.frequencyHz / 1639 * 100, 0.0f, 100.0f));
  field(S_BAR2, 204, 207, 1, TEXT, BG, value, 8);
  char dtc[32] = "";
  if (running) {
    if (d.dtcCode) snprintf(dtc, sizeof(dtc), "CHECK ENGINE %s %s", d.dtcCode, d.dtcSensor);
    else strcpy(dtc, "NO DTC");
  }
  field(S_DTC, 64, 226, 1, running && d.dtcCode ? ORANGE : MUTED, BG, dtc, -31);  // Negative width: left-aligned.
}

// ---- Race display ----------------------------------------------------------
// The browser's bar path in its 600 x 150 viewBox:
// M24 140 L78 62 Q96 36 128 36 L588 36, stroke 30, 50 segments.
constexpr int RG_Y = 46, RG_W = 320, RG_H = 100;
constexpr float RG_SCALE = 0.52f, RG_OX = 4, RG_OY = 4, RG_HALF = 15 * RG_SCALE;
constexpr uint8_t PATH_POINTS = 27, RACE_SEGMENTS = 50;
float pathX[PATH_POINTS], pathY[PATH_POINTS], pathLen[PATH_POINTS];

void buildRacePath() {
  uint8_t n = 0;
  auto add = [&](float x, float y) { pathX[n] = x; pathY[n] = y; ++n; };
  add(24, 140);
  for (uint8_t i = 0; i <= 24; ++i) {  // Quadratic curve 78,62 -> 96,36 -> 128,36.
    const float t = i / 24.0f, u = 1 - t;
    add(u * u * 78 + 2 * u * t * 96 + t * t * 128, u * u * 62 + 2 * u * t * 36 + t * t * 36);
  }
  add(588, 36);
  pathLen[0] = 0;
  for (uint8_t i = 1; i < PATH_POINTS; ++i)
    pathLen[i] = pathLen[i - 1] + hypotf(pathX[i] - pathX[i - 1], pathY[i] - pathY[i - 1]);
}

// Point and unit direction at arc length s, in viewBox units.
void pathAt(float s, float& x, float& y, float& dx, float& dy) {
  uint8_t i = 1;
  while (i < PATH_POINTS - 1 && pathLen[i] < s) ++i;
  const float seg = pathLen[i] - pathLen[i - 1];
  const float t = seg > 0 ? constrain((s - pathLen[i - 1]) / seg, 0.0f, 1.0f) : 0;
  dx = (pathX[i] - pathX[i - 1]) / seg;
  dy = (pathY[i] - pathY[i - 1]) / seg;
  x = pathX[i - 1] + t * (pathX[i] - pathX[i - 1]);
  y = pathY[i - 1] + t * (pathY[i] - pathY[i - 1]);
}

uint16_t lerpColor(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int shift) { return uint8_t(((a >> shift) & 0xFF) + t * (int((b >> shift) & 0xFF) - int((a >> shift) & 0xFF))); };
  return rgb(ch(16), ch(8), ch(0));
}

// Browser gradient along x: red at the foot, orange, then cyan.
uint16_t raceGradient(float vbX) {
  const float t = (vbX - 24) / (588 - 24);
  if (t < 0.2f) return lerpColor(0xff3b3b, 0xff9b36, t / 0.2f);
  if (t < 0.45f) return lerpColor(0xff9b36, 0x66d4f1, (t - 0.2f) / 0.25f);
  return ACCENT;
}

void renderRaceGauge(float rpm, bool showRpm, const char* speedText, const char* gearText) {
  GFXcanvas16& c = *gauge;
  c.fillScreen(RACE_BG);
  const float total = pathLen[PATH_POINTS - 1], step = total / RACE_SEGMENTS;
  const int lit = lroundf(constrain(rpm, 0.0f, RPM_MAX) / RPM_MAX * RACE_SEGMENTS);
  for (uint8_t i = 0; i < RACE_SEGMENTS; ++i) {
    float x0, y0, dx0, dy0, x1, y1, dx1, dy1;
    pathAt(i * step, x0, y0, dx0, dy0);
    pathAt(i * step + step * 5 / 6, x1, y1, dx1, dy1);
    auto px = [](float x) { return int(lroundf(RG_OX + x * RG_SCALE)); };
    auto py = [](float y) { return int(lroundf(RG_OY + y * RG_SCALE)); };
    // Perpendicular offsets (dy, -dx) give the stroke width.
    const float h = RG_HALF / RG_SCALE;
    const int ax = px(x0 + dy0 * h), ay = py(y0 - dx0 * h), bx = px(x0 - dy0 * h), by = py(y0 + dx0 * h);
    const int cx = px(x1 + dy1 * h), cy = py(y1 - dx1 * h), ex = px(x1 - dy1 * h), ey = py(y1 + dx1 * h);
    const uint16_t color = i < lit ? raceGradient((x0 + x1) / 2) : RACE_TRACK;
    c.fillTriangle(ax, ay, bx, by, cx, cy, color);
    c.fillTriangle(bx, by, cx, cy, ex, ey, color);
  }
  // 0-15 (x1000 RPM) labels above the bar; 0 sits beside the foot.
  for (int v = 0; v <= 15; v += 3) {
    float x, y, dx, dy;
    pathAt(v / 15.0f * total, x, y, dx, dy);
    char label[4]; snprintf(label, sizeof(label), "%d", v);
    if (v == 0) smallText(c, label, RG_OX + (x + 34) * RG_SCALE, RG_OY + (y + 4) * RG_SCALE - 7, TEXT);
    else smallText(c, label, RG_OX + (v == 15 ? x + 14 : x) * RG_SCALE, RG_OY + (y - 21) * RG_SCALE - 7, TEXT, v == 15 ? 1 : 0);
  }
  smallText(c, "x1000 RPM", RG_OX + 588 * RG_SCALE, 36, MUTED, 1);
  // Readouts: speed, gear, RPM.
  char rpmText[8];
  if (showRpm) snprintf(rpmText, sizeof(rpmText), "%d", int(lroundf(rpm / 10) * 10));
  else strcpy(rpmText, "-");
  centerText(c, speedText, 128, 84, &FreeSansBold24pt7b, TEXT);
  centerText(c, gearText, 204, 84, &FreeSansBold18pt7b, GEAR_COLOR);
  centerText(c, rpmText, 272, 84, &FreeSansBold12pt7b, DIGITS);
  smallText(c, "km/h", 128, 90, RACE_LABEL, 0);
  smallText(c, "GEAR", 204, 90, RACE_LABEL, 0);
  smallText(c, "RPM", 272, 90, RACE_LABEL, 0);
  tft.drawRGBBitmap(0, RG_Y, c.getBuffer(), RG_W, RG_H);
}

constexpr uint8_t TILE_COUNT = 8;
const char* const TILE_LABELS[TILE_COUNT] = {"Bat", "D7", "TEng", "TAir", "MAP", "O2 lambda", "AFR", "TPS %"};
const char* const TILE_UNITS[TILE_COUNT] = {"V", "Hz", "C", "C", "kPa", "", ":1", ""};
inline int tileX(uint8_t i) { return 2 + (i % 4) * 80; }
inline int tileY(uint8_t i) { return 150 + (i / 4) * 46; }

void drawRaceFrame() {
  for (uint8_t i = 0; i < TILE_COUNT; ++i) {
    const int x = tileX(i), y = tileY(i), w = 76, h = 42;
    tft.fillRoundRect(x, y, w, h, 5, TILE_BG);
    tft.drawRoundRect(x, y, w, h, 5, TILE_EDGE);
    for (int r = 0; r < 11; ++r)  // Green header, #6be594 -> #2c9a52 like the browser.
      tft.drawFastHLine(x + (r ? 1 : 3), y + 1 + r, w - (r ? 2 : 6), lerpColor(0x6be594, 0x2c9a52, r / 10.0f));
    tft.setFont(nullptr);
    tft.setTextSize(1);
    tft.setTextColor(TILE_INK);
    tft.setCursor(x + (w - int(strlen(TILE_LABELS[i])) * 6 + 1) / 2, y + 3);
    tft.print(TILE_LABELS[i]);
    smallText(tft, TILE_UNITS[i], x + w - 5, y + 32, MUTED, 1);
  }
}

void raceText(const DisplayData& d, bool running) {
  char value[16];
  for (uint8_t i = 0; i < TILE_COUNT - 1; ++i) {
    uint16_t color = TEXT;
    if (i == 0 || !running) { strcpy(value, "--"); color = MUTED; }  // No battery input.
    else if (i == 1) snprintf(value, sizeof(value), "%.0f", d.frequencyHz);
    else if (i == 2) color = sensorText(d.ect, 1, value, sizeof(value));
    else if (i == 3) color = sensorText(d.iat, 1, value, sizeof(value));
    else if (i == 4) color = sensorText(d.map, 1, value, sizeof(value));
    else if (i == 5) { snprintf(value, sizeof(value), "%.2f", d.o2.lambda); color = mixtureColor(d.o2); }
    else if (i == 6) { if (d.o2.afrAvailable) snprintf(value, sizeof(value), "%.1f", d.o2.afr); else strcpy(value, "--"); }
    // Left-aligned, padded right so a shorter value erases the old one.
    char padded[8];
    snprintf(padded, sizeof(padded), "%-5s", value);
    if (changed(S_VALUE0 + i, padded, color)) {
      tft.setFont(nullptr);
      tft.setTextSize(2);
      tft.setTextColor(color, TILE_BG);
      tft.setCursor(tileX(i) + 6, tileY(i) + 16);
      tft.print(padded);
    }
  }
  // TPS tile: whole percent and end angles on the left, throttle body on the right.
  const uint8_t t = TILE_COUNT - 1;
  const bool tpsOk = running && d.tps.available;
  char angle[12] = "";
  if (tpsOk) {
    snprintf(value, sizeof(value), "%.0f", d.tps.value);
    throttleEnds(throttleDeg(d.tps), angle, sizeof(angle));
  } else strcpy(value, "--");
  char padded[8];
  snprintf(padded, sizeof(padded), "%-3s", value);
  field(S_VALUE0 + t, tileX(t) + 6, tileY(t) + 15, 2, !running ? MUTED : !tpsOk ? RED : d.tps.inSpec ? TEXT : AMBER, TILE_BG, padded, 3);
  field(S_TP_ANGLE, tileX(t) + 6, tileY(t) + 32, 1, ORANGE, TILE_BG, angle, -6);
  drawThrottle(tileX(t) + 48, tileY(t) + 13, TILE_BG, tpsOk, tpsOk ? throttleDeg(d.tps) : 0);
}

// ---- Shared frame ----------------------------------------------------------
void drawTabs() {
  const uint16_t bg = view == RACE ? RACE_BG : BG;
  const char* const names[2] = {"CLASSIC", "RACE"};
  for (uint8_t i = 0; i < 2; ++i) {
    const bool active = i == view;
    const int x = 4 + i * 84;
    tft.fillRoundRect(x, 3, 80, 18, 4, active ? PANEL : bg);
    tft.drawRoundRect(x, 3, 80, 18, 4, active ? ACCENT : EDGE);
    smallText(tft, names[i], x + 40, 8, active ? ACCENT : MUTED, 0);
  }
}

void drawFrame() {
  tft.fillScreen(view == RACE ? RACE_BG : BG);
  drawTabs();
  tft.drawFastHLine(0, 23, 320, EDGE);
  delete gauge;
  gauge = view == RACE ? new GFXcanvas16(RG_W, RG_H) : new GFXcanvas16(CG_W, CG_H);
  if (view == RACE) drawRaceFrame(); else drawClassicFrame();
  memset(shown, 0, sizeof(shown));
  memset(shownColor, 0, sizeof(shownColor));
  shownGaugeKey = -1;
}

void begin() {
  // Deselect both devices before the bus starts.
  pinMode(TFT_CS, OUTPUT);   digitalWrite(TFT_CS, HIGH);
  pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  tft.begin();
  tft.setRotation(1);
  touch.begin();
  touch.setRotation(1);
  barCanvas = new GFXcanvas16(96, 10);
  throttleCanvas = new GFXcanvas16(TP_SIZE, TP_SIZE);
  buildRacePath();
  drawFrame();
}

void drawStatus(const DisplayData& d) {
  const bool live = d.link == DisplayData::LIVE;
  const char* status = d.link == DisplayData::WAITING ? "WAITING" : !live ? "NO LINK"
                     : !d.keyOn ? "KEY OFF" : "LIVE";
  const uint16_t color = !live ? RED : d.keyOn ? GREEN : AMBER;
  if (!changed(S_STATUS, status, color)) return;
  tft.fillRoundRect(236, 3, 80, 18, 5, color);
  tft.setFont(nullptr);
  tft.setTextSize(1);
  tft.setTextColor(view == RACE ? RACE_BG : BG);
  tft.setCursor(276 - strlen(status) * 3, 8);
  tft.print(status);
}

// Call every loop. Tapping anywhere switches between the Classic and Race tabs.
void update(const DisplayData& d) {
  const uint32_t now = millis();
  const bool pressed = touch.touched();
  if (pressed && !wasTouched && now - lastTapMs >= 300) {
    lastTapMs = now;
    view = view == CLASSIC ? RACE : CLASSIC;
    drawFrame();
  }
  wasTouched = pressed;

  const bool live = d.link == DisplayData::LIVE, running = live && d.keyOn;
  // Key-on self-test: every lamp lit and a full needle / bar sweep.
  if (running && !wasRunning) { selfTestActive = true; selfTestStartMs = now; }
  wasRunning = running;
  const uint32_t testMs = now - selfTestStartMs;
  if (selfTestActive && testMs >= SELF_TEST_MS) selfTestActive = false;
  const bool selfTest = selfTestActive;
  const float sweep = selfTest ? SPEED_MAX * (testMs < SELF_TEST_MS / 2 ? testMs : SELF_TEST_MS - testMs) / (SELF_TEST_MS / 2) : 0;

  // Engine RPM eases at 20,000 rpm/s so gear changes read as a quick drop.
  const float dt = min(0.1f, (now - lastAnimMs) / 1000.0f);
  lastAnimMs = now;
  const float targetRpm = running ? d.rpm : 0;
  animatedRpm += constrain(targetRpm - animatedRpm, -20000 * dt, 20000 * dt);

  const float speed = selfTest ? sweep : running ? d.speedKmh : 0;
  const float rpm = selfTest ? sweep / SPEED_MAX * RPM_MAX : animatedRpm;

  if (now - lastGaugeMs >= GAUGE_REFRESH_MS) {
    lastGaugeMs = now;
    const bool locked = live && !d.keyOn;
    const uint8_t gearCode = running ? d.gear : locked ? 0 : 0xFF;
    const int32_t key = (int32_t(lroundf(speed * 2)) << 16) ^ (int32_t(rpm / 50) << 4) ^ gearCode
                      ^ (selfTest << 30) ^ (running << 29) ^ (locked << 28);
    if (key != shownGaugeKey) {
      shownGaugeKey = key;
      char gearText[4];
      if (gearCode == 0xFF) strcpy(gearText, "-");
      else if (gearCode == 0) strcpy(gearText, "N");
      else snprintf(gearText, sizeof(gearText), "%u", gearCode);
      char speedText[8];
      const bool show = running || selfTest;
      if (view == CLASSIC) {
        if (show) snprintf(speedText, sizeof(speedText), "%03d", int(lroundf(speed)));
        else strcpy(speedText, locked ? "000" : "---");
        renderClassicGauge(speed, show, speedText, gearText);
        drawBar(173, speed / SPEED_MAX, ACCENT);
        drawBar(189, rpm / RPM_MAX, ACCENT);
        drawBar(205, running ? d.frequencyHz / 1639 : 0, ACCENT);
      } else {
        if (show) snprintf(speedText, sizeof(speedText), "%d", int(lroundf(speed)));
        else strcpy(speedText, locked ? "0" : "-");
        renderRaceGauge(rpm, show || locked, speedText, gearText);
      }
    }
  }

  if (now - lastTextMs < TEXT_REFRESH_MS) return;
  lastTextMs = now;
  drawStatus(d);
  uint16_t lamps[LAMP_COUNT];
  lampColors(d, selfTest, speed, now, lamps);
  for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
    if (view == RACE) drawRaceLamp(i, lamps[i]); else drawClassicLamp(i, lamps[i]);
  }
  if (view == RACE) raceText(d, running); else classicText(d, running, speed, rpm);
}

} // namespace localDisplay
