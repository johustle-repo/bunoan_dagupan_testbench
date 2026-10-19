#pragma once
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "AaGraphics.h"

// ABS tester on the 2.8" ILI9341 TFT (landscape 320 x 240). Everything is
// drawn anti-aliased (AaGraphics.h) with the Barlow Semi Condensed fonts in
// AaFonts.h, into off-screen strips that are pushed in one SPI burst each, so
// nothing flickers:
//   header | 0-250 km/h speedometer (rear + front needles), ABS / TCS /
//   SENSOR warning lights and REAR / FRONT bars | REF1, REAR, FRONT, BATTERY
//   tiles | key, RPM and DECLOG along the bottom.
// The Nano has no key button any more (D8 is the front sensor), so every
// power-up plays a splash and the switch-on self-test runs when live data
// arrives. WAITING FOR NANO / NO LINK show until then.
// The touch controller is not used: the testbench buttons are the controls.
constexpr uint8_t TFT_SCK = 18, TFT_MISO = 19, TFT_MOSI = 23;
constexpr uint8_t TFT_CS = 27, TFT_DC = 25, TFT_RST = 26, TOUCH_CS = 33;
constexpr uint32_t GAUGE_REFRESH_MS = 40, TEXT_REFRESH_MS = 100;
constexpr uint32_t SPLASH_MS = 1800, SELF_TEST_MS = 1600;  // Power-on splash, switch-on sweep.
// REAR (D7) and FRONT (D8): wheel speed = Hz / 6 km/h, 0-250 km/h, the same
// formula as the Nano. DECLOG: the D7 frequency as a duty percentage, 0-1639 Hz = 0-100 %.
constexpr float ABS_SPEED_MAX = 250, SPEED_RED = 210, HZ_PER_KMH = 6;
constexpr float FREQUENCY_MAX_HZ = 1639;

// What the screen shows, filled by the sketch each loop.
struct AbsData {
  enum Link : uint8_t { WAITING, LIVE, STALE } link;
  bool keyOn, brake;
  float speedKmh, frequencyHz;   // Rear, D7.
  bool front;                    // Frame carries the D8 front sensor.
  float frontKmh, frontHz;
  float ref1V, ref2V, currentA, batteryV;  // REF2 and current are sent but not shown.
  bool nanoLamps;                // Frame carries the Nano's RPM, TCS and ABS.
  uint16_t rpm;                  // D7 Hz x 15000 / 1560, from the Nano.
  bool tcsSteady, absActive;     // The Nano's TCS (D13) and ABS (D12) LED states.
};

// Warning lights, the same as the Nano's LEDs:
//   ABS    amber, on: D7 (rear) is at least 10 % and 5 km/h slower than D8
//          (front). Worked out here with the Nano's rule, copied below.
//   TCS    amber. Steady: both speeds match to the whole km/h. Flashing: they
//          differ. From the Nano's frame (same rule here for older firmware).
//   SENSOR green: both wheel sensors give pulses. Amber: only one does.
//          Off: no pulses (wheels stopped).
// All three light for the switch-on bulb check.
enum class Lamp : uint8_t { OFF, ON, FLASH, OK };  // OK: SENSOR green.
struct Lamps { Lamp abs, tcs, sensor; };

inline Lamps warningLamps(const AbsData& d) {
  const float rawRear = d.frequencyHz / HZ_PER_KMH, rawFront = d.front ? d.frontHz / HZ_PER_KMH : 0;
  const float rear = constrain(rawRear, 0.0f, ABS_SPEED_MAX), front = constrain(rawFront, 0.0f, ABS_SPEED_MAX);
  const bool rearPulses = d.frequencyHz > 0, frontPulses = d.front && d.frontHz > 0;
  Lamps l;
  // Simulated ABS (the Nano's rule):
  // D7 wheel is at least 10% slower than D8 reference,
  // with a minimum difference of 5 km/h.
  const float rawSpeed1 = rawRear, rawSpeed2 = rawFront;
  const bool absOn =
    (rawSpeed2 > 0.0) &&
    (rawSpeed1 <= rawSpeed2 * 0.90) &&
    ((rawSpeed2 - rawSpeed1) >= 5.0);
  const bool tcsSteady = d.nanoLamps ? d.tcsSteady : lroundf(rear) == lroundf(front);
  l.abs = absOn ? Lamp::ON : Lamp::OFF;
  l.tcs = tcsSteady ? Lamp::ON : Lamp::FLASH;
  l.sensor = rearPulses && frontPulses ? Lamp::OK : rearPulses || frontPulses ? Lamp::ON : Lamp::OFF;
  return l;
}

inline const char* lampName(Lamp l) {
  return l == Lamp::ON ? "on" : l == Lamp::FLASH ? "flash" : l == Lamp::OK ? "ok" : "off";
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
constexpr uint16_t HEAD_TOP = rgb(0x13, 0x20, 0x2f), HEAD_BOTTOM = rgb(0x0a, 0x12, 0x1b);
constexpr uint16_t NEEDLE_CORE = rgb(0xff, 0xb4, 0xb4);
const uint16_t WHEEL_COLOR[2] = {RED, GREEN};  // Rear needle red, front green.

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// Layout.
constexpr int HEAD_H = 22;
constexpr int GX = 4, GY = 26, GW = 212, GH = 122;            // Speedometer panel.
constexpr float DCX = GX + 106, DCY = GY + 106, DR = 96;      // Dial centre and radius.
constexpr int PLATE_W = 72, PLATE_H = 46, PLATE_Y = DCY - 38;  // Digital readout.
constexpr int LY = 151, LH = 24, LW = 68, LGAP = 4;          // Warning lights row.
constexpr int BX = 4, BY = 178, BW = 212, BH = 36;            // REAR / FRONT bars panel.
constexpr int TX = 220, TW = 96, TY0 = 26, TH = 44, TSTEP = 48;  // Right tiles.
constexpr int FY = 218, FH = 22;                              // Footer.
enum Tile : uint8_t { T_REF1, T_REAR, T_FRONT, T_BATTERY };
const char* const TILES[4] = {"REF1", "REAR", "FRONT", "BATTERY"};
const uint16_t TILE_ACCENTS[4] = {ACCENT, RED, GREEN, AMBER};
const char* const LAMP_LABELS[3] = {"(ABS)", "TCS", "SENSOR"};

// Strips of the screen are drawn here, then pushed.
constexpr int SCRATCH_PX = 320 * 28;
uint16_t scratch[SCRATCH_PX];
// Speedometer: the static dial, drawn once. Null if the heap had no room;
// the dial is then drawn again with every gauge frame.
uint16_t* dialBg = nullptr;

enum Mode : uint8_t { NONE, LOCKED, TESTER } mode = NONE;
enum Slot : uint8_t { S_TILE0, S_LIGHTS = 4, S_BARS, S_FOOTER, S_HEAD, S_LOCK, SLOT_COUNT };
char shown[SLOT_COUNT][40];
uint32_t lastGaugeMs = 0, lastTextMs = 0, selfTestStartMs = 0;
bool selfTestActive = false;
int32_t shownGaugeKey = -1;

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

// Speed to dial angle (aa::dialAngle): 0 km/h at the left, 250 at the right.
inline float angleOf(float kmh) { return PI + constrain(kmh, 0.0f, ABS_SPEED_MAX) / ABS_SPEED_MAX * PI; }
inline float speedOf(float angle) { return (angle - PI) / PI * ABS_SPEED_MAX; }

// Panel: rounded, slightly lifted from the background, with a fine edge.
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

// ---- Header ----

void drawHeader(const char* pill, uint16_t pillColor) {
  render(0, 0, W, HEAD_H, [&](aa::Buf& b) {
    aa::gradient(b, 0, 0, W, HEAD_H, HEAD_TOP, HEAD_BOTTOM);
    aa::fillRect(b, 0, HEAD_H - 1, W, 1, PANEL_EDGE);
    const int x = aa::text(b, aaTitle, "ABS", 10, 17, ACCENT, aa::LEFT, 1);
    aa::text(b, aaTitle, "TESTER", x + 5, 17, TEXT, aa::LEFT, 1);
    aa::text(b, aaSmall, "REAR D7  |  FRONT D8", 120, 15, DIM, aa::LEFT, 1);
    aa::roundRect(b, 256, 4, 58, 14, 7, pillColor);
    aa::textMid(b, aaSmall, pill, 285, 11, BG, 1);
  });
}

void header(const char* pill, uint16_t pillColor) {
  char key[24];
  snprintf(key, sizeof(key), "%s%u", pill, pillColor);
  if (changed(S_HEAD, key)) drawHeader(pill, pillColor);
}

// ---- Speedometer ----

// The static part of the dial, into the whole gauge panel or a strip of it.
void drawDial(aa::Buf& b) {
  aa::fill(b, BG);
  panel(b, GX, GY, GW, GH, 10);
  auto flat = [](uint16_t c) { return [c](float) { return c; }; };
  aa::arc(b, DCX, DCY, 0, DR - 12, PI, TWO_PI, flat(FACE));        // Face.
  aa::arc(b, DCX, DCY, DR + 3, DR + 4, PI, TWO_PI, flat(PANEL_EDGE));  // Outer ring.
  aa::arc(b, DCX, DCY, DR - 8, DR, PI, TWO_PI, flat(TRACK));        // Unlit track.
  // Ticks every 10 km/h, majors every 50 with their numbers; red from 210.
  for (int v = 0; v <= ABS_SPEED_MAX; v += 10) {
    const bool major = v % 50 == 0, hot = v >= SPEED_RED;
    const float a = angleOf(v), ca = cosf(a), sa = sinf(a);
    const float r0 = DR - 12, r1 = major ? DR - 24 : DR - 18;
    aa::stroke(b, DCX + ca * r0, DCY + sa * r0, DCX + ca * r1, DCY + sa * r1, major ? 2.4f : 1.3f, major ? 2.4f : 1.3f,
               hot ? RED : major ? TEXT : MUTED);
    if (major) {
      // The end numbers sit a little above the horizontal, clear of a needle at 0 or 250.
      const float la = a + (v == 0 ? 0.17f : v == ABS_SPEED_MAX ? -0.17f : 0);
      char label[4];
      snprintf(label, sizeof(label), "%d", v);
      aa::textMid(b, aaDial, label, DCX + cosf(la) * (DR - 37), DCY + sinf(la) * (DR - 37), hot ? RED : TEXT);
    }
  }
}

// Needle "floating" from behind the readout to the scale, with a soft glow.
void needle(aa::Buf& b, float kmh, float r0, float r1, float w0, float w1, uint16_t c, bool core) {
  const float a = angleOf(kmh), ca = cosf(a), sa = sinf(a);
  const float x0 = DCX + ca * r0, y0 = DCY + sa * r0, x1 = DCX + ca * r1, y1 = DCY + sa * r1;
  aa::stroke(b, x0, y0, x1, y1, w0 + 5, w1 + 3, c, 45);
  aa::stroke(b, x0, y0, x1, y1, w0, w1, c);
  if (core) aa::stroke(b, x0, y0, x1 - ca * 3, y1 - sa * 3, w0 / 3, 0.6f, NEEDLE_CORE, 200);
}

// Lit arc to the rear speed, front (green) and rear (red) needles, then the
// readout plate with the rear speed on top of the needles' inner ends.
// Built strip by strip in scratch on a copy of the static dial.
void renderGauge(float rear, float front, bool haveFront) {
  char digits[6];
  snprintf(digits, sizeof(digits), "%d", int(lroundf(rear)));
  const int px = DCX - PLATE_W / 2;
  render(GX, GY, GW, GH, [&](aa::Buf& b) {
    if (dialBg) memcpy(b.px, dialBg + (b.oy - GY) * GW, b.w * b.h * 2);
    else drawDial(b);
    aa::arc(b, DCX, DCY, DR - 8, DR, PI, angleOf(rear), [](float a) { return speedColor(speedOf(a)); });
    if (haveFront) needle(b, front, 30, DR - 6, 3.2f, 1.4f, GREEN, false);
    needle(b, rear, 26, DR - 3, 5.5f, 1.8f, RED, true);
    aa::roundRect(b, px - 3, PLATE_Y - 3, PLATE_W + 6, PLATE_H + 6, 11, FACE);  // Gap around the plate.
    aa::roundRect(b, px, PLATE_Y, PLATE_W, PLATE_H, 8, PLATE, PLATE_EDGE, 1);
    aa::text(b, aaSmall, "km/h", DCX, PLATE_Y + 13, MUTED, aa::CENTER, 1);
    aa::text(b, aaBig, digits, DCX, PLATE_Y + 42, rear >= SPEED_RED ? RED : TEXT, aa::CENTER, 1);
  });
}

// ---- Warning lights, bars, tiles, footer ----

void drawLights(const bool lit[3], const uint16_t colors[3]) {
  render(GX, LY - 2, GW, LH + 4, [&](aa::Buf& b) {
    aa::fill(b, BG);
    for (uint8_t i = 0; i < 3; ++i) {
      const int x = GX + i * (LW + LGAP);
      if (lit[i]) {
        aa::roundRect(b, x - 1, LY - 1, LW + 2, LH + 2, 8, aa::mix(BG, colors[i], 90));  // Glow.
        aa::roundRect(b, x, LY, LW, LH, 7, colors[i]);
        aa::textMid(b, aaPill, LAMP_LABELS[i], x + LW / 2, LY + LH / 2, BG, 1);
      } else {
        aa::roundRect(b, x, LY, LW, LH, 7, OFF_FILL, OFF_EDGE, 1);
        aa::textMid(b, aaPill, LAMP_LABELS[i], x + LW / 2, LY + LH / 2, OFF_TEXT, 1);
      }
    }
  });
}

// REAR and FRONT rows: label in the needle colour, a bar to the speed and the
// speed in km/h.
void drawBars(const float kmh[2], bool haveFront) {
  render(BX, BY, BW, BH, [&](aa::Buf& b) {
    aa::fill(b, BG);
    panel(b, BX, BY, BW, BH);
    for (uint8_t w = 0; w < 2; ++w) {
      const int cy = BY + 11 + w * 14;
      const bool on = w == 0 || haveFront;
      aa::text(b, aaLabel, w ? "FRONT" : "REAR", BX + 10, cy + 4, on ? WHEEL_COLOR[w] : DIM, aa::LEFT, 1);
      constexpr int X0 = BX + 52, LEN = 100;
      aa::roundRect(b, X0, cy - 3, LEN, 6, 3, TRACK);
      const float f = on ? constrain(kmh[w] / ABS_SPEED_MAX, 0.0f, 1.0f) : 0;
      if (f > 0.01f) aa::roundRect(b, X0, cy - 3, max(6.0f, f * LEN), 6, 3, speedColor(kmh[w]));
      char s[16];
      if (on) snprintf(s, sizeof(s), "%.0f km/h", kmh[w]);
      else strcpy(s, "-- km/h");
      aa::text(b, aaLabel, s, BX + BW - 10, cy + 4, on ? MUTED : DIM, aa::RIGHT);
    }
  });
}

// Tile: accent mark, label, then the value with its unit right-aligned.
void drawTile(uint8_t i, const char* value, const char* unit, uint16_t color) {
  const int y = TY0 + i * TSTEP;
  render(TX, y, TW, TH, [&](aa::Buf& b) {
    aa::fill(b, BG);
    panel(b, TX, y, TW, TH);
    aa::roundRect(b, TX + 8, y + 8, 3, 10, 1.5f, TILE_ACCENTS[i]);
    aa::text(b, aaLabel, TILES[i], TX + 16, y + 17, MUTED, aa::LEFT, 1);
    const int unitW = aa::textWidth(aaSmall, unit);
    aa::text(b, aaSmall, unit, TX + TW - 9, y + 37, MUTED, aa::RIGHT);
    aa::text(b, aaValue, value, TX + TW - 12 - unitW, y + 38, color, aa::RIGHT);
  });
}

// KEY ON | RPM (LAMP CHECK during the self-test) | DECLOG.
void drawFooter(bool selfTest, uint16_t rpm, float percent) {
  render(GX, FY, W - 2 * GX, FH, [&](aa::Buf& b) {
    aa::fill(b, BG);
    panel(b, GX, FY, W - 2 * GX, FH, 7);
    const int cy = FY + FH / 2;
    aa::circle(b, GX + 13, cy, 3.5f, GREEN);
    aa::text(b, aaLabel, "KEY ON", GX + 22, cy + 5, TEXT, aa::LEFT, 1);
    aa::fillRect(b, 100, FY + 5, 1, FH - 10, PANEL_EDGE);
    char r[16];
    snprintf(r, sizeof(r), "%u RPM", rpm);
    aa::circle(b, 113, cy, 3.5f, selfTest ? AMBER : rpm ? ACCENT : DIM);
    aa::text(b, aaLabel, selfTest ? "LAMP CHECK" : r, 122, cy + 5, selfTest ? AMBER : rpm ? TEXT : MUTED, aa::LEFT, 1);
    aa::fillRect(b, 214, FY + 5, 1, FH - 10, PANEL_EDGE);
    char s[8];
    snprintf(s, sizeof(s), "%.0f%%", percent);
    aa::text(b, aaLabel, s, W - GX - 10, cy + 5, TEXT, aa::RIGHT);
    aa::text(b, aaLabel, "DECLOG", 226, cy + 5, MUTED, aa::LEFT, 1);
  });
}

void testerText(const AbsData& d, bool selfTest) {
  char key[40], value[12];
  header(selfTest ? "TEST" : "LIVE", selfTest ? ACCENT : GREEN);
  // Tiles: REF1 and battery volts, rear and front km/h ("--" without D8).
  for (uint8_t i = 0; i < 4; ++i) {
    uint16_t color = TEXT;
    const char* unit = "km/h";
    if (i == T_REF1 || i == T_BATTERY) {
      snprintf(value, sizeof(value), "%.2f", i == T_REF1 ? d.ref1V : d.batteryV);
      unit = "V";
    } else if (i == T_FRONT && !d.front) {
      strcpy(value, "--");
      color = DIM;
    } else {
      const float kmh = i == T_REAR ? d.speedKmh : d.frontKmh;
      snprintf(value, sizeof(value), "%.0f", kmh);
      if (kmh >= SPEED_RED) color = RED;
    }
    snprintf(key, sizeof(key), "%s%u", value, color);
    if (changed(S_TILE0 + i, key)) drawTile(i, value, unit, color);
  }
  // Warning lights; FLASH blinks at 2 Hz and the self-test lights them all.
  const Lamps lamps = warningLamps(d);
  const bool blinkOn = (millis() / 250) % 2;
  const Lamp states[3] = {lamps.abs, lamps.tcs, lamps.sensor};
  bool lit[3];
  uint16_t colors[3];
  for (uint8_t i = 0; i < 3; ++i) {
    const Lamp l = selfTest ? (i == 2 ? Lamp::OK : Lamp::ON) : states[i];
    lit[i] = l == Lamp::ON || l == Lamp::OK || (l == Lamp::FLASH && blinkOn);
    colors[i] = l == Lamp::OK ? GREEN : AMBER;
  }
  snprintf(key, sizeof(key), "%d%d%d%u%u%u", lit[0], lit[1], lit[2], colors[0], colors[1], colors[2]);
  if (changed(S_LIGHTS, key)) drawLights(lit, colors);
  // Bars.
  const float kmh[2] = {d.speedKmh, d.frontKmh};
  snprintf(key, sizeof(key), "%.0f %.0f %d", kmh[0], kmh[1], d.front);
  if (changed(S_BARS, key)) drawBars(kmh, d.front);
  // Footer.
  const float percent = constrain(d.frequencyHz / FREQUENCY_MAX_HZ * 100, 0.0f, 100.0f);
  snprintf(key, sizeof(key), "%d %u %.0f", selfTest, d.rpm, percent);
  if (changed(S_FOOTER, key)) drawFooter(selfTest, d.rpm, percent);
}

// ---- Locked (waiting / no link) and the tester frame ----

void forget() {
  memset(shown, 0, sizeof(shown));
  shownGaugeKey = -1;
}

void drawLockedFrame() {
  forget();
  render(0, HEAD_H, W, H - HEAD_H, [](aa::Buf& b) { aa::fill(b, BG); });
  mode = LOCKED;
}

void lockedText(const AbsData& d) {
  const bool live = d.link == AbsData::LIVE;
  const char* line = d.link == AbsData::WAITING ? "WAITING FOR NANO" : d.link == AbsData::STALE ? "NO LINK" : "UPDATE NANO";
  const uint16_t color = live ? AMBER : RED;
  header(d.link == AbsData::WAITING ? "WAIT" : live ? "KEY OFF" : "NO LINK", color);
  if (!changed(S_LOCK, line)) return;
  render(30, 44, 260, 164, [&](aa::Buf& b) {
    aa::fill(b, BG);
    aa::roundRect(b, 30, 44, 260, 164, 14, PANEL, PANEL_EDGE, 1);
    absSymbol(b, W / 2, 92, 22, color, aaPill);
    aa::text(b, aaValue, line, W / 2, 152, color, aa::CENTER, 1);
    aa::text(b, aaLabel, live ? "Flash the new Nano firmware" : "Check Nano D5 -> GPIO16 and GND", W / 2, 174, MUTED,
             aa::CENTER);
    aa::text(b, aaSmall, "Wi-Fi ABS-Tester  |  192.168.4.1", W / 2, 194, DIM, aa::CENTER, 1);
  });
}

void drawTesterFrame() {
  forget();
  render(0, HEAD_H, W, H - HEAD_H, [](aa::Buf& b) { aa::fill(b, BG); });
  mode = TESTER;
}

// Power-on splash: the ABS lamp ring sweeps round, the title fades in and a
// loading bar fills.
void splash() {
  constexpr int BAR_X = 80, BAR_Y = 200, BAR_W = 160;
  for (uint32_t start = millis(), t = 0; t <= SPLASH_MS; t = millis() - start) {
    const float p = float(t) / SPLASH_MS, fade = constrain((p - 0.15f) / 0.45f, 0.0f, 1.0f);
    render(0, 0, W, H, [&](aa::Buf& b) {
      aa::fill(b, BG);
      const float cx = W / 2, cy = 84, r = 30;
      aa::arc(b, cx, cy, r - 4, r, HALF_PI, HALF_PI + TWO_PI, [](float) { return TRACK; });
      const float sweep = 3 * HALF_PI + TWO_PI * min(1.0f, p / 0.6f);
      aa::arc(b, cx, cy, r - 4, r, 3 * HALF_PI, sweep, [](float) { return ACCENT; });
      const uint16_t amber = aa::mix(BG, AMBER, fade * 255);
      aa::arc(b, cx, cy, r + 6, r + 10, PI - 0.6f, PI + 0.6f, [amber](float) { return amber; });
      aa::arc(b, cx, cy, r + 6, r + 10, TWO_PI - 0.6f, TWO_PI + 0.6f, [amber](float) { return amber; });
      aa::textMid(b, aaPill, "ABS", cx, cy, amber, 1);
      aa::text(b, aaValue, "ABS TESTER", cx, 158, aa::mix(BG, TEXT, fade * 255), aa::CENTER, 3);
      aa::text(b, aaLabel, "REAR D7   |   FRONT D8   |   SPEED = Hz / 6", cx, 180, aa::mix(BG, MUTED, fade * 255),
               aa::CENTER, 1);
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
  dialBg = static_cast<uint16_t*>(malloc(GW * GH * 2));
  if (dialBg) {
    aa::Buf b{dialBg, GW, GH, GX, GY};
    drawDial(b);
  } else {
    Serial.println(F("# Gauge: no heap for the dial cache, redrawing it each frame."));
  }
  splash();
  drawLockedFrame();
}

// Call every loop.
void update(const AbsData& d) {
  const uint32_t now = millis();
  const bool running = d.link == AbsData::LIVE && d.keyOn;
  // Switch-on self-test when live data arrives: both needles and bars sweep
  // up and back with LAMP CHECK and every warning light lit.
  if (running && mode != TESTER) { drawTesterFrame(); selfTestActive = true; selfTestStartMs = now; }
  else if (!running && mode != LOCKED) drawLockedFrame();
  const uint32_t testMs = now - selfTestStartMs;
  if (selfTestActive && (!running || testMs >= SELF_TEST_MS)) selfTestActive = false;
  const bool selfTest = selfTestActive;
  AbsData shownData = d;
  if (selfTest) {
    const float sweep = sinf(PI * testMs / SELF_TEST_MS) * ABS_SPEED_MAX;  // 0 -> 250 -> 0, eased.
    shownData.speedKmh = shownData.frontKmh = sweep;
    shownData.front = true;
  }
  if (running && (now - lastGaugeMs >= GAUGE_REFRESH_MS)) {
    lastGaugeMs = now;
    const int32_t key = lroundf(shownData.speedKmh * 2) | lroundf(shownData.frontKmh * 2) << 10 |
                        int32_t(shownData.front) << 20;
    if (key != shownGaugeKey) {
      shownGaugeKey = key;
      renderGauge(shownData.speedKmh, shownData.frontKmh, shownData.front);
    }
  }
  if (now - lastTextMs < TEXT_REFRESH_MS) return;
  lastTextMs = now;
  if (running) testerText(shownData, selfTest); else lockedText(d);
}

}  // namespace absDisplay
