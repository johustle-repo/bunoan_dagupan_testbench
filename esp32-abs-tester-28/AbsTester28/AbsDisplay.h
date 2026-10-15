#pragma once
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ABS tester on the 2.8" ILI9341 TFT (landscape 320 x 240), drawn with the
// Adafruit GFX / ILI9341 libraries like esp32-universal/Receiver:
//   title | 0-15,000 RPM gauge, REAR speed bar and DC LOG bar | REF1, REF2, CURRENT,
//   BATTERY tiles | key, brake and D7 frequency along the bottom.
// With the key off (starter D8 not pressed) only a "press START" screen shows.
// The touch controller is not used: the testbench buttons are the controls.
constexpr uint8_t TFT_SCK = 18, TFT_MISO = 19, TFT_MOSI = 23;
constexpr uint8_t TFT_CS = 27, TFT_DC = 25, TFT_RST = 26, TOUCH_CS = 33;
constexpr uint32_t GAUGE_REFRESH_MS = 50, TEXT_REFRESH_MS = 100;
constexpr float ABS_SPEED_MAX = 200;
// REAR bar: the wheel speed, 0-200 km/h. DC LOG bar: the D7 frequency as a
// duty percentage, 0-1639 Hz = 0-100 %. The gauge shows RPM, also from the
// D7 frequency: 0-1639 Hz = 0-15,000 rpm.
constexpr float FREQUENCY_MAX_HZ = 1639, RPM_MAX = 15000, RPM_REDLINE = 12000;

// What the screen shows, filled by the sketch each loop.
struct AbsData {
  enum Link : uint8_t { WAITING, LIVE, STALE } link;
  bool keyOn, brake;
  float speedKmh, frequencyHz;
  float ref1V, ref2V, currentA, batteryV;
};

namespace absDisplay {

constexpr int W = 320, H = 240;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t BG = rgb(0x06, 0x09, 0x0e), EDGE = rgb(0x43, 0x54, 0x6b), TRACK = rgb(0x1b, 0x24, 0x30);
constexpr uint16_t TEXT = rgb(0xf2, 0xf7, 0xfc), MUTED = rgb(0xac, 0xbd, 0xd1), ACCENT = rgb(0x66, 0xd4, 0xf1);
constexpr uint16_t AMBER = rgb(0xff, 0xbd, 0x64), RED = rgb(0xff, 0x4d, 0x4d), GREEN = rgb(0x42, 0xd9, 0x7c);
constexpr uint16_t LAMP_OFF = rgb(0x23, 0x30, 0x44), LAMP_RING = rgb(0x63, 0x73, 0x8a), GAP = rgb(0x0b, 0x11, 0x19);
// Gauge face: steel bezel, slightly lighter dial, dim track under the lit arc.
constexpr uint16_t BEZEL = rgb(0x5a, 0x6a, 0x7e), BEZEL_DARK = rgb(0x26, 0x31, 0x3f), FACE = rgb(0x13, 0x1d, 0x2a);
// Every panel: dark face, steel frame with a highlight along the top edge.
constexpr uint16_t PANEL_FACE = rgb(0x0b, 0x12, 0x1b);
constexpr uint16_t FACE_INNER = rgb(0x0f, 0x17, 0x22), LCD_BG = rgb(0x05, 0x0a, 0x10);

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// Layout. The gauge and the bars are drawn off-screen and pushed in one SPI
// burst each, so nothing flickers. Text only redraws when it changes.
constexpr int TITLE_H = 19;
constexpr int GX = 4, GY = 24, GW = 214, GH = 123;     // Gauge canvas.
constexpr int GCX = 107, GCY = 89, GR = 80;           // In canvas coordinates.
constexpr int BARS_Y = 152, BAR_ROW = 31;             // REAR and DC LOG rows under the gauge.
constexpr int BAR_X = 50, BAR_W = 84, BAR_H = 10;     // Bar canvas; readout to its right.
constexpr int TX = 224, TW = 94, TY0 = 22, TH = 46, TSTEP = 49;   // Right tiles.
constexpr int RY = 218, RH = 20;                      // Bottom row.
const char* const TILES[4] = {"REF1", "REF2", "CURRENT", "BATTERY"};
const uint16_t TILE_ACCENTS[4] = {ACCENT, ACCENT, AMBER, GREEN};
const char* const BAR_LABELS[2] = {"REAR", "DC LOG"};

GFXcanvas16* gauge = nullptr;
GFXcanvas16* bar = nullptr;

enum Mode : uint8_t { NONE, LOCKED, TESTER } mode = NONE;
enum Slot : uint8_t { S_TILE0, S_BAR0 = 4, S_KEY = 6, S_BRAKE, S_FREQ, S_LOCK, SLOT_COUNT };
char shown[SLOT_COUNT][24];
uint16_t shownColor[SLOT_COUNT];
uint32_t lastGaugeMs = 0, lastTextMs = 0;
int32_t shownGaugeKey = -1, shownBarKey = -1;

bool changed(uint8_t slot, const char* text, uint16_t color) {
  if (!strcmp(shown[slot], text) && shownColor[slot] == color) return false;
  strlcpy(shown[slot], text, sizeof(shown[slot]));
  shownColor[slot] = color;
  return true;
}

enum Align : uint8_t { LEFT, CENTER, RIGHT };

// Text with its baseline at y (top for the built-in font), aligned on x.
// Custom fonts draw transparent.
void text(Adafruit_GFX& g, const char* s, int x, int y, uint16_t color, const GFXfont* font, Align align = LEFT) {
  g.setFont(font);
  g.setTextSize(1);
  g.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  g.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  const int left = align == LEFT ? x : align == CENTER ? x - int(w) / 2 : x - int(w);
  g.setCursor(left - x1, y);
  g.print(s);
}

// Text centred on (x, y) both ways.
void centered(Adafruit_GFX& g, const char* s, int x, int y, uint16_t color, const GFXfont* font) {
  g.setFont(font);
  g.setTextSize(1);
  g.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  g.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  g.setCursor(x - int(w) / 2 - x1, y - int(h) / 2 - y1);
  g.print(s);
}

// Redraws a text field inside its box only when it changes.
void field(uint8_t slot, int bx, int by, int bw, int bh, int baseline, Align align,
           const GFXfont* font, uint16_t color, const char* s) {
  if (!changed(slot, s, color)) return;
  tft.fillRect(bx, by, bw, bh, PANEL_FACE);  // Every field sits on a panel.
  const int x = align == LEFT ? bx : align == CENTER ? bx + bw / 2 : bx + bw;
  text(tft, s, x, baseline, color, font, align);
}

// Speed to screen angle: 0 km/h at the left (180 deg), 200 km/h at the right.
inline float angleOf(float kmh) { return PI + constrain(kmh, 0.0f, ABS_SPEED_MAX) / ABS_SPEED_MAX * PI; }

// Filled ring segment from a0 to a1 (radians), made of small triangles.
void ring(GFXcanvas16& c, float a0, float a1, int r0, int r1, uint16_t color) {
  const float step = 2 * DEG_TO_RAD;
  for (float a = a0; a < a1; a += step) {
    const float b = min(a + step, a1);
    const float ox0 = cosf(a), oy0 = sinf(a), ox1 = cosf(b), oy1 = sinf(b);
    c.fillTriangle(GCX + ox0 * r1, GCY + oy0 * r1, GCX + ox1 * r1, GCY + oy1 * r1, GCX + ox0 * r0, GCY + oy0 * r0, color);
    c.fillTriangle(GCX + ox1 * r1, GCY + oy1 * r1, GCX + ox1 * r0, GCY + oy1 * r0, GCX + ox0 * r0, GCY + oy0 * r0, color);
  }
}

// Lit-arc colour: cyan, then amber from 120 km/h, red from 170 km/h.
uint16_t speedColor(float kmh) { return kmh >= 170 ? RED : kmh >= 120 ? AMBER : ACCENT; }

// Brighter version of a colour, for the lit arc's edge highlight.
uint16_t lighter(uint16_t c) {
  const uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return (min(31, r + 6) << 11) | (min(63, g + 12) << 5) | min(31, b + 6);
}

// RPM colour: cyan, amber from 9,000 rpm, red from the 12,000 redline.
uint16_t rpmColor(float rpm) { return rpm >= RPM_REDLINE ? RED : rpm >= 9000 ? AMBER : ACCENT; }

// RPM to the same 0-200 angle scale the dial uses.
inline float rpmAngle(float rpm) { return angleOf(constrain(rpm, 0.0f, RPM_MAX) / RPM_MAX * ABS_SPEED_MAX); }

// RPM gauge, 0-15,000 rpm (scale in x1000). The wheel speed is on the REAR bar.
void renderGauge(float rpm) {
  GFXcanvas16& c = *gauge;
  c.fillScreen(PANEL_FACE);
  // Dial: steel bezel, two-tone face for depth.
  c.fillCircle(GCX, GCY, GR + 4, BEZEL);
  c.fillCircle(GCX, GCY, GR + 3, BEZEL_DARK);
  c.fillCircle(GCX, GCY, GR + 1, FACE);
  c.fillCircle(GCX, GCY, GR - 36, FACE_INNER);
  // Dim track, arc lit up to the current RPM with a bright outer edge.
  ring(c, rpmAngle(0), rpmAngle(RPM_MAX), GR - 5, GR, TRACK);
  const float shown = constrain(rpm, 0.0f, RPM_MAX);
  for (float r = 0; r < shown; r += 150) {
    const float a0 = rpmAngle(r), a1 = rpmAngle(min(r + 150, shown));
    ring(c, a0, a1, GR - 5, GR - 1, rpmColor(r));
    ring(c, a0, a1, GR - 1, GR, lighter(rpmColor(r)));
  }
  // Red redline band just inside the bezel.
  ring(c, rpmAngle(RPM_REDLINE), rpmAngle(RPM_MAX), GR + 1, GR + 2, RED);
  // Ticks: majors every 3,000 rpm, minors every 1,000, dots every 500.
  for (int i = 0; i <= 30; ++i) {
    const float r = i * 500, a = rpmAngle(r), ca = cosf(a), sa = sinf(a);
    const uint16_t color = r >= RPM_REDLINE ? RED : TEXT;
    if (i % 2) { c.drawPixel(GCX + ca * (GR - 9), GCY + sa * (GR - 9), MUTED); continue; }
    const bool major = i % 6 == 0;
    const int r0 = GR - 8, r1 = major ? GR - 17 : GR - 12;
    for (int w = major ? -1 : 0; w <= (major ? 1 : 0); ++w)  // Majors are 3 px wide.
      c.drawLine(GCX + ca * r0 - sa * w, GCY + sa * r0 + ca * w, GCX + ca * r1 - sa * w, GCY + sa * r1 + ca * w, color);
    if (major) {
      char label[4];
      snprintf(label, sizeof(label), "%d", int(r / 1000));
      centered(c, label, GCX + ca * (GR - 28), GCY + sa * (GR - 28), color, &FreeSans9pt7b);
    }
  }
  centered(c, "x1000", GCX + 62, GCY + 14, MUTED, nullptr);  // Scale unit, clear of the needle.
  // RPM in an LCD-style window below the hub.
  char digits[8];
  snprintf(digits, sizeof(digits), "%d", int(lroundf(rpm)));
  int16_t x1, y1;
  uint16_t dw, uw, h;
  c.setFont(&FreeSansBold12pt7b);
  c.getTextBounds(digits, 0, 0, &x1, &y1, &dw, &h);
  c.setFont(nullptr);
  c.getTextBounds("RPM", 0, 0, &x1, &y1, &uw, &h);
  const int gx = GCX - int(dw + 4 + uw) / 2;
  c.fillRoundRect(GCX - 44, GCY + 8, 88, 25, 4, LCD_BG);
  c.drawRoundRect(GCX - 44, GCY + 8, 88, 25, 4, BEZEL_DARK);
  text(c, digits, gx, GCY + 29, rpm >= RPM_REDLINE ? RED : TEXT, &FreeSansBold12pt7b);
  text(c, "RPM", gx + dw + 4, GCY + 22, MUTED, nullptr);  // Built-in font: y is the top.
  // Red needle with a short tail and a drop shadow, on a steel hub.
  const float a = rpmAngle(rpm), ca = cosf(a), sa = sinf(a);
  const float tipX = GCX + ca * (GR - 10), tipY = GCY + sa * (GR - 10);
  const float tailX = GCX - ca * 12, tailY = GCY - sa * 12;
  c.fillTriangle(GCX - sa * 3 + 2, GCY + ca * 3 + 2, GCX + sa * 3 + 2, GCY - ca * 3 + 2, tipX + 2, tipY + 2, LCD_BG);
  c.fillTriangle(GCX - sa * 3, GCY + ca * 3, GCX + sa * 3, GCY - ca * 3, tipX, tipY, RED);
  c.fillTriangle(GCX - sa * 3, GCY + ca * 3, GCX + sa * 3, GCY - ca * 3, tailX, tailY, RED);
  c.drawLine(GCX, GCY, tipX, tipY, TEXT);  // Bright centre line.
  c.fillCircle(GCX, GCY, 7, BEZEL);
  c.fillCircle(GCX, GCY, 5, BEZEL_DARK);
  c.fillCircle(GCX, GCY, 2, RED);
  tft.drawRGBBitmap(GX, GY, c.getBuffer(), GW, GH);
}

// Lit-arc colour: cyan, then amber from 120 km/h, red from 170 km/h.
uint16_t speedColor(float kmh);

// Segmented bar. The REAR bar is coloured by speed like the gauge arc.
void renderBar(uint8_t row, float fraction, bool speed) {
  GFXcanvas16& c = *bar;
  c.fillScreen(TRACK);
  constexpr int SEG = 4;  // Segment pitch: 3 px lit, 1 px gap.
  const int lit = lroundf(constrain(fraction, 0.0f, 1.0f) * BAR_W);
  for (int x = 0; x < lit; x += SEG) {
    const float at = float(x) / BAR_W;
    const uint16_t color = speed ? speedColor(at * ABS_SPEED_MAX) : ACCENT;
    c.fillRect(x, 0, min(SEG - 1, lit - x), BAR_H, color);
  }
  for (int x = SEG - 1; x < BAR_W; x += SEG) c.drawFastVLine(x, 0, BAR_H, GAP);
  tft.drawRGBBitmap(BAR_X, BARS_Y + row * BAR_ROW + (BAR_ROW - BAR_H) / 2, c.getBuffer(), BAR_W, BAR_H);
}

void forget() {
  memset(shown, 0, sizeof(shown));
  memset(shownColor, 0, sizeof(shownColor));
  shownGaugeKey = shownBarKey = -1;
}

// Beveled panel: dark face, steel frame, highlight line along the top.
void panel(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 5, PANEL_FACE);
  tft.drawRoundRect(x, y, w, h, 5, BEZEL_DARK);
  tft.drawFastHLine(x + 5, y + 1, w - 10, BEZEL);
}

// Title bar: vertical gradient, title, decorative slashes.
void drawTitle() {
  for (int r = 0; r < TITLE_H; ++r) {
    const float t = float(r) / (TITLE_H - 1);
    tft.drawFastHLine(4, 1 + r, W - 8, rgb(0x1c - 0x12 * t, 0x2c - 0x1a * t, 0x40 - 0x26 * t));
  }
  tft.drawRoundRect(2, 1, W - 4, TITLE_H, 4, BEZEL_DARK);
  tft.drawFastHLine(7, 2, W - 14, BEZEL);
  text(tft, "ABS TESTER", 10, 16, ACCENT, &FreeSansBold9pt7b);
  for (int k = 0; k < 5; ++k)  // "/////" between the title and the status pill.
    for (int w = 0; w < 2; ++w)
      tft.drawLine(196 + k * 9 + w, TITLE_H - 3, 202 + k * 9 + w, 5, BEZEL_DARK);
}

// Status pill on the right of the title bar.
void statusPill(const char* label, uint16_t color) {
  tft.fillRoundRect(262, 4, 52, TITLE_H - 6, 6, color);
  centered(tft, label, 288, 4 + (TITLE_H - 6) / 2, BG, nullptr);
}

// Key icon for the key-off screen: ring, shaft and two teeth.
void keyIcon(int cx, int cy, uint16_t color) {
  tft.fillCircle(cx - 12, cy, 9, color);
  tft.fillCircle(cx - 12, cy, 4, PANEL_FACE);
  tft.fillRect(cx - 4, cy - 2, 26, 5, color);
  tft.fillRect(cx + 12, cy + 3, 4, 6, color);
  tft.fillRect(cx + 18, cy + 3, 4, 8, color);
}

// Key off or no link: only the title and a framed prompt, like a bike
// dashboard that stays dark until the key is turned.
void drawLockedFrame() {
  tft.fillScreen(BG);
  drawTitle();
  panel(40, 60, W - 80, 130);
  forget();
  mode = LOCKED;
}

void drawTesterFrame() {
  tft.fillScreen(BG);
  drawTitle();
  statusPill("LIVE", GREEN);
  panel(GX - 2, GY - 2, GW + 4, GH + 4);
  // REAR and DC LOG rows: label tags, a divider between the rows.
  panel(GX - 2, BARS_Y - 2, GW + 4, 2 * BAR_ROW + 3);
  tft.drawFastHLine(GX + 4, BARS_Y + BAR_ROW, GW - 8, BEZEL_DARK);
  for (uint8_t i = 0; i < 2; ++i) {
    const int y = BARS_Y + i * BAR_ROW;
    tft.fillRoundRect(6, y + (BAR_ROW - 13) / 2, 40, 13, 3, BEZEL_DARK);
    centered(tft, BAR_LABELS[i], 26, y + BAR_ROW / 2, MUTED, nullptr);
  }
  // Tiles: coloured accent bar and label.
  for (uint8_t i = 0; i < 4; ++i) {
    const int y = TY0 + i * TSTEP;
    panel(TX, y, TW, TH);
    tft.fillRoundRect(TX + 3, y + 5, 3, TH - 10, 1, TILE_ACCENTS[i]);
    text(tft, TILES[i], TX + 10, y + 5, TILE_ACCENTS[i], nullptr);
  }
  // Bottom row: key | brake | frequency, with separators.
  panel(2, RY, W - 4, RH);
  tft.drawFastVLine(96, RY + 4, RH - 8, BEZEL_DARK);
  tft.drawFastVLine(222, RY + 4, RH - 8, BEZEL_DARK);
  forget();
  mode = TESTER;
}

void lockedText(const AbsData& d) {
  const char* line = d.link == AbsData::WAITING ? "WAITING FOR NANO"
                   : d.link == AbsData::STALE ? "NO LINK" : "PRESS START";
  const uint16_t color = d.link == AbsData::LIVE ? AMBER : RED;
  if (!changed(S_LOCK, line, color)) return;
  tft.fillRect(44, 66, W - 88, 120, PANEL_FACE);
  keyIcon(W / 2, 92, color);
  centered(tft, line, W / 2, 130, color, &FreeSansBold12pt7b);
  if (d.link == AbsData::LIVE) centered(tft, "Press the starter button (D8)", W / 2, 162, MUTED, &FreeSans9pt7b);
  else centered(tft, "Check Nano D6 -> GPIO17, GND", W / 2, 162, MUTED, &FreeSans9pt7b);
}

void lamp(int x, int y, bool on, uint16_t color) {
  tft.fillCircle(x, y, 4, on ? color : LAMP_OFF);
  tft.drawCircle(x, y, 4, on ? color : LAMP_RING);
}

void testerText(const AbsData& d) {
  char s[16];
  const float values[4] = {d.ref1V, d.ref2V, d.currentA, d.batteryV};
  for (uint8_t i = 0; i < 4; ++i) {
    snprintf(s, sizeof(s), i == 2 ? "%.2f A" : "%.2f V", values[i]);
    const int y = TY0 + i * TSTEP;
    field(S_TILE0 + i, TX + 9, y + 14, TW - 15, TH - 17, y + TH - 8, RIGHT, &FreeSansBold9pt7b, TEXT, s);
  }
  // Bar readouts.
  const float percent = constrain(d.frequencyHz / FREQUENCY_MAX_HZ * 100, 0.0f, 100.0f);
  for (uint8_t i = 0; i < 2; ++i) {
    snprintf(s, sizeof(s), i ? "%.0f%%" : "%.0f km/h", i ? percent : d.speedKmh);
    const uint16_t color = !i && d.speedKmh >= 170 ? RED : TEXT;
    const int y = BARS_Y + i * BAR_ROW;
    field(S_BAR0 + i, BAR_X + BAR_W + 2, y + 2, GX + GW - BAR_X - BAR_W - 4, BAR_ROW - 4, y + BAR_ROW / 2 + 6,
          RIGHT, &FreeSans9pt7b, color, s);
  }
  // Bottom row: key | brake | D7 frequency, each in its own box so they can't overlap.
  if (changed(S_KEY, "1", GREEN)) {
    lamp(14, RY + RH / 2, true, GREEN);
    text(tft, "KEY ON", 24, RY + 6, GREEN, nullptr);  // Built-in font: y is the top.
  }
  if (changed(S_BRAKE, d.brake ? "ON" : "OFF", d.brake ? RED : MUTED)) {
    tft.fillRect(100, RY + 3, 118, RH - 6, PANEL_FACE);
    lamp(110, RY + RH / 2, d.brake, RED);
    text(tft, d.brake ? "BRAKE ON" : "BRAKE OFF", 120, RY + 6, d.brake ? RED : MUTED, nullptr);
  }
  snprintf(s, sizeof(s), "%.0f Hz", d.frequencyHz);
  field(S_FREQ, 226, RY + 3, 86, RH - 6, RY + 6, RIGHT, nullptr, MUTED, s);
}

void begin() {
  // Deselect both devices on the shared bus before it starts.
  pinMode(TFT_CS, OUTPUT);   digitalWrite(TFT_CS, HIGH);
  pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  tft.begin();
  tft.setRotation(1);  // Landscape 320 x 240.
  tft.setTextWrap(false);
  gauge = new GFXcanvas16(GW, GH);
  bar = new GFXcanvas16(BAR_W, BAR_H);
  gauge->setTextWrap(false);
  drawLockedFrame();
}

// Call every loop.
void update(const AbsData& d) {
  const uint32_t now = millis();
  const bool running = d.link == AbsData::LIVE && d.keyOn;
  if (running && mode != TESTER) drawTesterFrame();
  else if (!running && mode != LOCKED) drawLockedFrame();
  if (running && now - lastGaugeMs >= GAUGE_REFRESH_MS) {
    lastGaugeMs = now;
    const float rpm = constrain(d.frequencyHz / FREQUENCY_MAX_HZ, 0.0f, 1.0f) * RPM_MAX;
    const int32_t key = lroundf(rpm / 20);
    if (key != shownGaugeKey) {
      shownGaugeKey = key;
      renderGauge(rpm);
    }
    const float rear = constrain(d.speedKmh / ABS_SPEED_MAX, 0.0f, 1.0f);
    const float dcLog = constrain(d.frequencyHz / FREQUENCY_MAX_HZ, 0.0f, 1.0f);
    const int32_t barKey = lroundf(rear * BAR_W) * 1000 + lroundf(dcLog * BAR_W);
    if (barKey != shownBarKey) {
      shownBarKey = barKey;
      renderBar(0, rear, true);
      renderBar(1, dcLog, false);
    }
  }
  if (now - lastTextMs < TEXT_REFRESH_MS) return;
  lastTextMs = now;
  if (running) testerText(d); else lockedText(d);
}

}  // namespace absDisplay
