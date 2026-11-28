#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Preferences.h>

// Local 4.0" ST7796S TFT + XPT2046 touch panel (landscape 480 x 320) showing
// the instrument panel of the browser dashboard in three screens:
//   CLASSIC - tachometer (RPM), lamps, sensor column, readout grid and DTC line
//   RACE    - REF 1 / REF 2, LED indicator strip, curved RPM bar, speed / RPM, tiles
//   SMART   - neon REF1 / REF2 / BAT boxes, RPM and speed dials, six sensor tiles
// There are no tabs: double-tap anywhere on the screen for the next screen.
// Display and touch share the VSPI bus with separate chip selects; none of
// these pins overlap the Uno/Nano/Mega link on GPIO16/17.
constexpr int8_t PIN_TFT_SCK = 18, PIN_TFT_MISO = 19, PIN_TFT_MOSI = 23;
constexpr int8_t PIN_TFT_CS = 27, PIN_TFT_DC = 25, PIN_TFT_RST = 26;
// T_IRQ (GPIO32) is not used: a press is detected from the XPT2046 pressure
// reading, so a missing or loose IRQ wire can't block touch.
constexpr int8_t PIN_TOUCH_CS = 33;
// 27 MHz is what ESP32_Touch_Games was tested with; most ST7796S modules also
// run at 40 MHz, which makes the gauges about a third quicker to push.
constexpr uint32_t TFT_SPI_HZ = 27000000;
// Gauges are drawn off-screen in horizontal bands and pushed band by band, so
// needles never flicker and no full-size frame buffer is needed (the ESP32
// has no PSRAM). Text and lamps only redraw when they change.
// pushSprite() sends with DMA in the background, so a sprite must not be
// redrawn until its transfer has finished: the bands alternate between two
// buffers (one is drawn while the other is sent) and every reuse of a sprite
// waits for the DMA first. Without this the panel shows half-drawn bands.
constexpr uint32_t GAUGE_REFRESH_MS = 50, TEXT_REFRESH_MS = 100, TOUCH_POLL_MS = 20;
constexpr uint32_t SELF_TEST_MS = 2000;
constexpr float SPEED_MAX = 250, RPM_MAX = 15000;

// Snapshot of what the panel shows, filled by the sketch each loop.
struct DisplayData {
  enum Link : uint8_t { WAITING, LIVE, STALE } link;
  bool keyOn, brake, checkEngine, coolantWarning;  // brake: the D9 ABS input on U6.
  bool absLamp;            // ABS lamp from the speed (AbsLamp in ReceiverV2.ino).
  bool declog;             // Mega 2560 Pro (U6) Declog lamp.
  bool declogHzAvailable;  // Newer U6 frames: Declog frequency from the Mega's A9 pot.
  float declogHz;          // 0-100 Hz.
  uint8_t milBlink;        // Blink code of the first DTC, 0 = none.
  float speedKmh, rpm, frequencyHz;
  const char* dtcCode;     // First active DTC, or nullptr.
  const char* dtcSensor;
  SensorReading ect, iat, map, tps;
  OxygenReading o2;
  bool batteryAvailable;   // Mega frame with the A7 battery input.
  float batteryV;
  bool currentAvailable;   // Mega U5 frame with the A8 current input.
  float currentA;          // Mega A8, 0-10 A.
  // Board and link details (the old SENSORS screen; also used by the dashboard).
  bool references;         // Board sends Reference 1/2 (Nano U4: 0-5 V, Mega U5: 0-30 V).
  const char* board;       // "Uno", "Nano" or "Mega".
  const char* boardFrame;  // "U3", "U4", "U5" or "U6".
  float volts[5];          // IAT, ECT, O2, MAP, TPS at the ADC pin.
  float reference[2];
  uint8_t injectedMask, autoMask, highMask;
  uint32_t packetAgeMs, badFrames;
  float tpsClosedRaw, tpsWotRaw;
};

namespace localDisplay {

class Panel : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Touch_XPT2046 _touch;

 public:
  Panel() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI3_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = TFT_SPI_HZ;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_TFT_SCK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = PIN_TFT_MISO;
      cfg.pin_dc = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_TFT_CS;
      cfg.pin_rst = PIN_TFT_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true;       // The V2.2 4.0" panel needs inversion.
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel.config(cfg);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = -1;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI3_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = PIN_TFT_SCK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = PIN_TFT_MISO;
      cfg.pin_cs = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

constexpr int W = 480, H = 320, HEADER_H = 36;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// Browser dashboard palette (style.css).
constexpr uint16_t BG = rgb(0x0d, 0x14, 0x1e), PANEL = rgb(0x13, 0x1d, 0x29), EDGE = rgb(0x43, 0x54, 0x6b);
constexpr uint16_t TEXT = rgb(0xf2, 0xf7, 0xfc), MUTED = rgb(0xac, 0xbd, 0xd1), ACCENT = rgb(0x66, 0xd4, 0xf1);
constexpr uint16_t ARC = rgb(0x33, 0x46, 0x5c), TICK = rgb(0x81, 0x99, 0xb5), TICK_LABEL = rgb(0xae, 0xbd, 0xd0);
constexpr uint16_t DIGITS = rgb(0xc7, 0xf3, 0xff);
constexpr uint16_t AMBER = rgb(0xff, 0xbd, 0x64), ORANGE = rgb(0xff, 0x9b, 0x36), RED = rgb(0xff, 0x4d, 0x4d);
constexpr uint16_t GREEN = rgb(0x42, 0xd9, 0x7c), LAMP_OFF = rgb(0x23, 0x30, 0x44), LAMP_RING = rgb(0x63, 0x73, 0x8a);
constexpr uint16_t BAR_TRACK = rgb(0x25, 0x34, 0x48), BAR_GAP = rgb(0x11, 0x1b, 0x28), BAR_EDGE = rgb(0x4f, 0x63, 0x7b);
constexpr uint16_t RACE_BG = rgb(0x06, 0x09, 0x0e), RACE_TRACK = rgb(0x1b, 0x24, 0x30), RACE_DIM = rgb(0x5d, 0x6b, 0x7c);
constexpr uint16_t RACE_NA = rgb(0x2f, 0x3c, 0x4c), RACE_LABEL = rgb(0x5f, 0xe0, 0x8a);
constexpr uint16_t TILE_BG = rgb(0x11, 0x18, 0x21), TILE_EDGE = rgb(0x2a, 0x36, 0x46), TILE_INK = rgb(0x04, 0x21, 0x0e);
constexpr uint16_t TP_TRACK = rgb(0x1d, 0x2a, 0x3a);
constexpr uint16_t MIX_RICH = rgb(0xff, 0x7a, 0x45), MIX_STOICH = rgb(0x42, 0xd9, 0x7c), MIX_LEAN = rgb(0x4e, 0xa8, 0xff);

Panel lcd;
LGFX_Sprite gaugeBand[2] = {LGFX_Sprite(&lcd), LGFX_Sprite(&lcd)};  // Ping-pong band buffers.
LGFX_Sprite throttleSprite(&lcd);

constexpr uint8_t VIEW_COUNT = 3;
enum View : uint8_t { CLASSIC, RACE, SMART } view = CLASSIC;
const char* const VIEW_NAMES[VIEW_COUNT] = {"CLASSIC", "RACE", "SMART"};
constexpr int TAB_Y = 4, TAB_H = 28;           // Header row: status pill, board, REF boxes.
constexpr uint32_t DOUBLE_TAP_MS = 400;        // Second tap within this time switches screen.

bool wasTouched = false, wasRunning = false, selfTestActive = false;
uint32_t lastTapMs = 0, lastPressMs = 0, lastTouchPollMs = 0;
uint32_t lastGaugeMs = 0, lastTextMs = 0, lastAnimMs = 0, selfTestStartMs = 0;
float animatedRpm = 0;
int32_t shownGaugeKey = -1, shownSpeedKey = -1;  // SMART: RPM dial, speed dial.

// Last text / colour drawn in each slot; a slot is redrawn only when it changes.
// Slots are reused between tabs: drawFrame() forgets them all.
constexpr uint8_t SLOT_COUNT = 64;
char shown[SLOT_COUNT][40];
uint16_t shownColor[SLOT_COUNT];
enum Slot : uint8_t {
  S_STATUS, S_DTC, S_BOARD, S_TP_ICON, S_TP_ANGLE, S_REF1, S_REF2, S_GRID_LABEL,
  S_LAMP0 = 8, S_INFO_LABEL = 15, S_VALUE0 = 16
};

bool changed(uint8_t slot, const char* text, uint16_t color) {
  if (!strcmp(shown[slot], text) && shownColor[slot] == color) return false;
  strlcpy(shown[slot], text, sizeof(shown[slot]));
  shownColor[slot] = color;
  return true;
}

// Transparent text, for frames and sprites.
void label(LovyanGFX& g, const char* text, int x, int y, uint16_t color,
           textdatum_t datum = textdatum_t::top_left, const lgfx::IFont* font = &fonts::Font2) {
  g.setFont(font);
  g.setTextSize(1);
  g.setTextColor(color);
  g.setTextDatum(datum);
  g.drawString(text, x, y);
}

// Opaque text padded to padPx, so a shorter value erases the longer old one.
void field(uint8_t slot, int x, int y, textdatum_t datum, const lgfx::IFont* font,
           uint16_t color, uint16_t bg, const char* text, int padPx) {
  if (!changed(slot, text, color)) return;
  lcd.setFont(font);
  lcd.setTextSize(1);
  lcd.setTextColor(color, bg);
  lcd.setTextDatum(datum);
  lcd.setTextPadding(padPx);
  lcd.drawString(text, x, y);
  lcd.setTextPadding(0);
}

// Sensor value with the browser's formatting; returns the colour to use.
uint16_t sensorText(const SensorReading& r, uint8_t decimals, char* out, size_t size) {
  if (!r.available) { strlcpy(out, "ERR", size); return RED; }  // Fault, as the Mega prints it.
  snprintf(out, size, "%.*f", decimals, r.value);
  if (strlen(out) > 5) snprintf(out, size, "%.0f", r.value);
  return r.inSpec ? TEXT : AMBER;
}

uint16_t mixtureColor(const OxygenReading& o2) {
  return !strcmp(o2.mixture, "rich") ? MIX_RICH : !strcmp(o2.mixture, "stoich") ? MIX_STOICH : MIX_LEAN;
}

uint16_t lerpColor(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int shift) { return uint8_t(((a >> shift) & 0xFF) + t * (int((b >> shift) & 0xFF) - int((a >> shift) & 0xFF))); };
  return rgb(ch(16), ch(8), ch(0));
}

inline uint32_t to888(uint16_t c) {
  return uint32_t(c >> 11) << 19 | uint32_t((c >> 5) & 0x3F) << 10 | uint32_t(c & 0x1F) << 3;
}

// ---- Lamps -----------------------------------------------------------------
// Smart key, winker, check engine, ABS, TCS (no Declog or Temp lamp). Winker
// and TCS have no input on this bench. ABS follows the speed (AbsLamp in
// ReceiverV2.ino), not the D9 input.
constexpr uint8_t LAMP_COUNT = 5;
enum Lamp : uint8_t { L_KEY, L_WINKER, L_CHECK, L_ABS, L_TCS };
const char* const LAMP_NAMES[LAMP_COUNT] = {"KEY", "WINKER", "CHECK ENGINE", "ABS", "TCS"};
constexpr bool LAMP_WIRED[LAMP_COUNT] = {true, false, true, true, false};

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
  if (fresh && d.keyOn) out[L_KEY] = race ? GREEN : ACCENT;
  if (running && milLit(d, speed, now)) out[L_CHECK] = ORANGE;
  // ABS: lit from power-on until the speed passes 10 km/h, then lit at
  // 10 km/h or less and off at 0 (AbsLamp); shown with or without a link.
  if (d.absLamp) out[L_ABS] = ORANGE;
}

void drawClassicLamp(uint8_t i, uint16_t color) {
  char tag[8];
  snprintf(tag, sizeof(tag), "%u", color);
  if (!changed(S_LAMP0 + i, tag, color)) return;
  constexpr int ROW = 56;  // 5 lamps fill the old 7 x 40 px column (y 40-320).
  const int y = 40 + i * ROW, cx = 17, cy = y + ROW / 2;
  lcd.fillRect(0, y, 87, ROW, BG);
  if (color) {
    lcd.fillCircle(cx, cy, 13, lerpColor(0x0d141e, to888(color), 0.35f));  // Soft glow.
    lcd.fillCircle(cx, cy, 10, color);
  } else if (LAMP_WIRED[i]) {
    lcd.fillCircle(cx, cy, 10, LAMP_OFF);
    lcd.drawCircle(cx, cy, 10, LAMP_RING);
  } else {
    for (int a = 0; a < 360; a += 30) lcd.fillArc(cx, cy, 10, 9, a, a + 12, LAMP_RING);  // Dashed: not wired.
  }
  const uint16_t text = color ? color : LAMP_WIRED[i] ? MUTED : LAMP_RING;
  if (i == L_CHECK) {  // Two lines: the column is too narrow for "CHECK ENGINE".
    label(lcd, "CHECK", 34, cy - 8, text, textdatum_t::middle_left);
    label(lcd, "ENGINE", 34, cy + 8, text, textdatum_t::middle_left);
  } else label(lcd, LAMP_NAMES[i], 34, cy, text, textdatum_t::middle_left);
}

// RACE indicator strip: one LED per lamp with its name beside it.
void drawRaceLamp(uint8_t i, uint16_t color) {
  char tag[8];
  snprintf(tag, sizeof(tag), "%u", color);
  if (!changed(S_LAMP0 + i, tag, color)) return;
  const int x = 3 + i * 95, y = 41, w = 93, h = 24, cx = x + 10, cy = y + h / 2;  // 5 lamps across 480 px.
  lcd.fillRect(x, y, w, h, RACE_BG);
  if (color) {
    lcd.fillCircle(cx, cy, 9, lerpColor(0x06090e, to888(color), 0.35f));  // Glow.
    lcd.fillCircle(cx, cy, 6, color);
    lcd.fillCircle(cx - 2, cy - 2, 2, TEXT);                              // Highlight.
  } else if (LAMP_WIRED[i]) {
    lcd.fillCircle(cx, cy, 6, RACE_TRACK);
    lcd.drawCircle(cx, cy, 6, RACE_DIM);
  } else {
    for (int a = 0; a < 360; a += 45) lcd.fillArc(cx, cy, 6, 5, a, a + 20, RACE_NA);  // Dashed: not wired.
  }
  // "CHECK ENGINE" in the small font so it fits the 73 px beside the LED.
  label(lcd, LAMP_NAMES[i], x + 20, cy, color ? color : LAMP_WIRED[i] ? RACE_DIM : RACE_NA, textdatum_t::middle_left,
        i == L_CHECK ? static_cast<const lgfx::IFont*>(&fonts::Font0) : &fonts::Font2);
}

// ---- Throttle angle --------------------------------------------------------
// Side view of the throttle body, as in the browser: TPS 0-100 % turns the
// butterfly plate 0-THROTTLE_MAX_DEG. Closed, the plate is vertical (ends at
// 0 and 180 deg); TP_DIR -1 turns it counter-clockwise, so wide open ends at
// 270 and 90 deg. Orange shows the angle the plate has turned.
constexpr float THROTTLE_MAX_DEG = 90;
constexpr int TP_DIR = -1;
constexpr int TP_SIZE = 30, TP_C = 15, TP_R = 14;

inline float throttleDeg(const SensorReading& tps) { return tps.value * THROTTLE_MAX_DEG / 100; }

// Plate end angles, e.g. "0-180" closed, "270-90" wide open.
void throttleEnds(float deg, char* out, size_t size) {
  const int end1 = (int(lroundf(TP_DIR * deg)) + 360) % 360;
  snprintf(out, size, "%d-%d", end1, (end1 + 180) % 360);
}

inline void tpPoint(float deg, float radius, float& x, float& y) {
  x = TP_C + sinf(deg * DEG_TO_RAD) * radius;
  y = TP_C - cosf(deg * DEG_TO_RAD) * radius;
}

void drawThrottle(int x, int y, uint16_t bg, bool has, float deg) {
  char tag[12];
  snprintf(tag, sizeof(tag), has ? "%d" : "-", int(lroundf(deg)));
  if (!changed(S_TP_ICON, tag, bg) || !throttleSprite.getBuffer()) return;
  LGFX_Sprite& c = throttleSprite;
  lcd.waitDMA();
  c.fillScreen(bg);
  c.fillCircle(TP_C, TP_C, TP_R, TP_TRACK);
  if (has && deg > 0.2f) {
    // Screen angles: 0 = right, clockwise. Plate angle 0 (up) is 270 there.
    const int sweep = lroundf(deg);
    for (int half = 0; half < 360; half += 180) {
      const int base = (270 + half) % 360;  // 270 or 90; the sweep is at most 90.
      if (TP_DIR < 0) c.fillArc(TP_C, TP_C, TP_R, 0, base - sweep, base, ORANGE);
      else c.fillArc(TP_C, TP_C, TP_R, 0, base, base + sweep, ORANGE);
    }
  }
  c.drawCircle(TP_C, TP_C, TP_R, BAR_EDGE);
  for (int a = 0; a < 360; a += 90) {
    float x1, y1, x2, y2;
    tpPoint(a, TP_R, x1, y1);
    tpPoint(a, TP_R - 4, x2, y2);
    c.drawLine(x1, y1, x2, y2, MUTED);
  }
  if (has) {
    const float turn = TP_DIR * deg;
    float x1, y1, x2, y2;
    tpPoint(turn, TP_R - 1, x1, y1);
    tpPoint(turn + 180, TP_R - 1, x2, y2);
    c.drawWideLine(x1, y1, x2, y2, 1.3f, TEXT);
  }
  c.fillCircle(TP_C, TP_C, 3, bg);
  c.drawCircle(TP_C, TP_C, 3, TEXT);
  c.pushSprite(&lcd, x, y);
}

// ---- Classic display -------------------------------------------------------
// Lamps 0-86 | tachometer and readout grid 88-379 | sensor column 381-479.
constexpr int CG_X = 90, CG_Y = 40, CG_W = 288, CG_H = 186, CG_BAND = 31;  // Tachometer.
constexpr int CG_CX = 144, CG_CY = 176, CG_R = 134;
// Readout grid under the tachometer: km/h, RPM, Current / Ref 1, Ref 2, Battery.
constexpr int GRID_X = 88, GRID_Y = 228, GRID_W = 292, GRID_ROW1 = 34, GRID_ROW2 = 33;
constexpr int GRID_COL_X[4] = {GRID_X, GRID_X + 97, GRID_X + 194, GRID_X + GRID_W};
const char* const GRID_LABELS[6] = {"km/h", "RPM", "CURRENT", "REFERENCE 1", "REFERENCE 2", "BATTERY"};
enum : uint8_t { CL_GRID0 = S_VALUE0 + 8 };  // 6 slots after the sensor column's.
constexpr int SC_X = 381, SC_ROW = 56;  // Sensor column.
const char* const CLASSIC_SENSORS[5] = {"AIT C", "ECT C", "O2 V", "MAP kPa", "TPS %"};

inline void polar(float rpm, float r, int oy, float& x, float& y) {
  const float a = PI + rpm / RPM_MAX * PI;
  x = CG_CX + cosf(a) * r;
  y = CG_CY - oy + sinf(a) * r;
}

// Tachometer: a plain dial-face background, the arc with a red zone above
// 12,000 rpm, ticks every 1,000 rpm and labels 0, 3, 6, 9, 12, 15 (x1000).
constexpr uint16_t CG_FACE = rgb(0x16, 0x22, 0x30), CG_FACE_INNER = rgb(0x11, 0x1a, 0x26);
constexpr float CG_REDLINE_RPM = 12000;

void renderClassicGauge(float rpm, bool showNeedle, const char* digits) {
  if (!gaugeBand[0].getBuffer() || !gaugeBand[1].getBuffer()) return;
  for (int oy = 0, band = 0; oy < CG_H; oy += CG_BAND, ++band) {
    LGFX_Sprite& c = gaugeBand[band & 1];
    c.fillScreen(BG);
    const int cy = CG_CY - oy;
    // Background: dial face with a darker centre and a thin edge.
    c.fillArc(CG_CX, cy, CG_R + 6, 0, 180, 360, CG_FACE);
    c.fillArc(CG_CX, cy, CG_R - 50, 0, 180, 360, CG_FACE_INNER);
    c.fillArc(CG_CX, cy, CG_R + 6, CG_R + 5, 180, 360, EDGE);
    c.fillArc(CG_CX, cy, CG_R + 2, CG_R - 1, 180, 324, ARC);
    c.fillArc(CG_CX, cy, CG_R + 2, CG_R - 1, 324, 360, RED);
    for (int i = 0; i <= 15; ++i) {
      const bool major = i % 3 == 0;
      const uint16_t color = i * 1000 >= CG_REDLINE_RPM ? RED : TICK;
      float x1, y1, x2, y2;
      polar(i * 1000, CG_R - 3, oy, x1, y1);
      polar(i * 1000, major ? CG_R - 16 : CG_R - 10, oy, x2, y2);
      c.drawWideLine(x1, y1, x2, y2, major ? 1.6f : 0.8f, color);
      if (major) {
        float tx, ty;
        polar(i * 1000, CG_R - 32, oy, tx, ty);
        char text[4];
        snprintf(text, sizeof(text), "%d", i);
        label(c, text, tx, ty, i * 1000 >= CG_REDLINE_RPM ? RED : TICK_LABEL, textdatum_t::middle_center,
              &fonts::FreeSansBold12pt7b);
      }
    }
    label(c, "x1000 r/min", CG_CX, cy - 76, MUTED, textdatum_t::middle_center, &fonts::Font0);
    label(c, digits, CG_CX, cy - 44, DIGITS, textdatum_t::middle_center, &fonts::FreeSansBold18pt7b);
    label(c, "RPM", CG_CX, cy - 20, MUTED, textdatum_t::middle_center);
    if (showNeedle) {
      float tx, ty, bx, by;
      const float v = constrain(rpm, 0.0f, RPM_MAX);
      polar(v, CG_R - 14, oy, tx, ty);
      polar(v, -12, oy, bx, by);  // Short tail behind the hub.
      c.drawWideLine(bx, by, tx, ty, 2.2f, ACCENT);
      c.fillCircle(CG_CX, cy, 8, ACCENT);
      c.fillCircle(CG_CX, cy, 3, BG);
    }
    lcd.waitDMA();  // The other buffer's transfer must end before this one starts.
    c.pushSprite(&lcd, CG_X, CG_Y + oy);
  }
}

void drawClassicFrame() {
  lcd.drawFastVLine(87, HEADER_H + 2, H - HEADER_H - 2, EDGE);
  lcd.drawFastVLine(380, HEADER_H + 2, H - HEADER_H - 2, EDGE);
  // Readout grid: dark cells with a label in the top-left corner.
  const int gridH = GRID_ROW1 + GRID_ROW2;
  lcd.fillRect(GRID_X, GRID_Y, GRID_W, gridH, PANEL);
  lcd.drawRect(GRID_X, GRID_Y, GRID_W, gridH, EDGE);
  lcd.drawFastHLine(GRID_X, GRID_Y + GRID_ROW1, GRID_W, EDGE);
  for (uint8_t c = 1; c < 3; ++c) lcd.drawFastVLine(GRID_COL_X[c], GRID_Y, gridH, EDGE);
  for (uint8_t i = 0; i < 6; ++i)
    label(lcd, GRID_LABELS[i], GRID_COL_X[i % 3] + 5, GRID_Y + (i < 3 ? 0 : GRID_ROW1) + 4, MUTED,
          textdatum_t::top_left, &fonts::Font0);
  lcd.drawFastHLine(88, 296, 292, EDGE);
  for (uint8_t i = 0; i < 5; ++i) {
    const int y = 40 + i * SC_ROW;
    label(lcd, CLASSIC_SENSORS[i], SC_X + 6, y + 4, MUTED);
    if (i < 4) lcd.drawFastHLine(SC_X, y + SC_ROW - 1, W - SC_X, EDGE);
  }
}

void classicText(const DisplayData& d, bool running, float speed, float rpm) {
  char value[16];
  const SensorReading* temps[2] = {&d.iat, &d.ect};
  for (uint8_t i = 0; i < 4; ++i) {
    uint16_t color = MUTED;
    if (!running) strcpy(value, "--");
    else if (i < 2) color = sensorText(*temps[i], 1, value, sizeof(value));
    else if (i == 2) {
      if (d.o2.afrAvailable) { snprintf(value, sizeof(value), "%.2f", d.o2.volts); color = mixtureColor(d.o2); }
      else { strcpy(value, "ERR"); color = RED; }
    } else color = sensorText(d.map, 1, value, sizeof(value));
    field(S_VALUE0 + i, SC_X + 6, 40 + i * SC_ROW + 24, textdatum_t::top_left, &fonts::Font4, color, BG, value, 92);
  }
  // TPS row: throttle-body symbol, whole percent and plate end angles.
  const int ty = 40 + 4 * SC_ROW;
  const bool tpsOk = running && d.tps.available;
  char angle[12] = "";
  if (tpsOk) {
    snprintf(value, sizeof(value), "%.0f", d.tps.value);
    throttleEnds(throttleDeg(d.tps), angle, sizeof(angle));
  } else strcpy(value, "--");
  drawThrottle(SC_X + 4, ty + 22, BG, tpsOk, tpsOk ? throttleDeg(d.tps) : 0);
  field(S_VALUE0 + 4, SC_X + 40, ty + 20, textdatum_t::top_left, &fonts::Font4,
        !running ? MUTED : !tpsOk ? RED : d.tps.inSpec ? TEXT : AMBER, BG, value, 56);
  field(S_TP_ANGLE, SC_X + 40, ty + 46, textdatum_t::top_left, &fonts::Font0, ORANGE, BG, angle, 56);
  // Readout grid. Top row in a larger font; values sit bottom-right of each cell.
  const bool show = running || selfTestActive;
  const bool havePacket = d.link != DisplayData::WAITING;
  for (uint8_t i = 0; i < 6; ++i) {
    uint16_t color = TEXT;
    switch (i) {
      case 0: if (show) snprintf(value, sizeof(value), "%.0f", speed); else { strcpy(value, "--"); color = MUTED; } break;
      case 1: if (show) snprintf(value, sizeof(value), "%.0f", rpm); else { strcpy(value, "--"); color = MUTED; } break;
      case 2:  // DECLOG Hz on the Mega 2560 Pro, otherwise the Mega A8 current (live with the key off).
        field(S_GRID_LABEL, GRID_COL_X[2] + 5, GRID_Y + 4, textdatum_t::top_left, &fonts::Font0, MUTED, PANEL,
              d.declogHzAvailable ? "DECLOG" : "CURRENT", 60);
        if (d.declogHzAvailable) {
          if (running) snprintf(value, sizeof(value), "%.1f Hz", d.declogHz); else { strcpy(value, "-- Hz"); color = MUTED; }
          if (running && d.declogHz > 0) color = AMBER;
        } else if (d.currentAvailable) snprintf(value, sizeof(value), "%.2f A", d.currentA);
        else { strcpy(value, havePacket ? "n/a" : "-- A"); color = MUTED; }
        break;
      case 3: case 4:
        if (havePacket && !d.references) { strcpy(value, "n/a"); color = MUTED; }
        else if (running && isnan(d.reference[i - 3])) { strcpy(value, "ERR"); color = RED; }  // Nothing on the pin.
        else if (running) snprintf(value, sizeof(value), "%.2f V", d.reference[i - 3]);
        else { strcpy(value, "-- V"); color = MUTED; }
        break;
      case 5:  // Live with the key off, like the RACE battery tile.
        if (d.batteryAvailable && isnan(d.batteryV)) { strcpy(value, "ERR"); color = RED; }  // Nothing on A7.
        else if (d.batteryAvailable) {
          snprintf(value, sizeof(value), "%.2f V", d.batteryV);
          color = d.batteryV < 11.8f || d.batteryV > 15.0f ? ORANGE : d.batteryV >= 13.2f ? GREEN : TEXT;
        } else { strcpy(value, havePacket ? "n/a" : "-- V"); color = MUTED; }
        break;
    }
    const bool top = i < 3;
    const int x = GRID_COL_X[i % 3], w = GRID_COL_X[i % 3 + 1] - x;
    const int bottom = GRID_Y + (top ? GRID_ROW1 : GRID_ROW1 + GRID_ROW2) - 2;
    field(CL_GRID0 + i, x + w - 6, bottom, textdatum_t::bottom_right, top ? static_cast<const lgfx::IFont*>(&fonts::DejaVu18) : &fonts::Font2,
          color, PANEL, value, w - 12);
  }
  char dtc[40] = "";
  if (running) {
    if (d.dtcCode) snprintf(dtc, sizeof(dtc), "CHECK ENGINE  %s  %s", d.dtcCode, d.dtcSensor);
    else strcpy(dtc, "NO DTC");
  }
  field(S_DTC, 94, 308, textdatum_t::middle_left, &fonts::Font2, running && d.dtcCode ? ORANGE : MUTED, BG, dtc, 282);
}

// ---- Race display ----------------------------------------------------------
// The browser's bar path in its 600 x 150 viewBox:
// M24 140 L78 62 Q96 36 128 36 L588 36, stroke 30, 50 segments.
constexpr int RG_Y = 68, RG_W = 480, RG_H = 150, RG_BAND = 30;
constexpr float RG_SCALE = 0.78f, RG_OX = 6, RG_OY = 6, RG_HALF = 15 * RG_SCALE;
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

// Gradient along x: green at the foot (low RPM), yellow at mid-range, red at
// the redline (same stops as the browser's race bar).
uint16_t raceGradient(float vbX) {
  const float t = constrain((vbX - 24) / (588 - 24), 0.0f, 1.0f);
  return t < 0.55f ? lerpColor(0x3ddc84, 0xffd23f, t / 0.55f) : lerpColor(0xffd23f, 0xff3b3b, (t - 0.55f) / 0.45f);
}

// Readout centres: speed | RPM.
constexpr int RD_SPEED_X = 180, RD_RPM_X = 372, RD_Y = 92;
// Each segment fills this share of its slot; the rest is the dark gap.
constexpr float RACE_FILL = 0.75f;

void renderRaceGauge(float rpm, bool showRpm, const char* speedText) {
  if (!gaugeBand[0].getBuffer() || !gaugeBand[1].getBuffer()) return;
  const float total = pathLen[PATH_POINTS - 1], step = total / RACE_SEGMENTS;
  const int lit = lroundf(constrain(rpm, 0.0f, RPM_MAX) / RPM_MAX * RACE_SEGMENTS);
  char rpmText[8];
  if (showRpm) snprintf(rpmText, sizeof(rpmText), "%d", int(lroundf(rpm / 10) * 10));
  else strcpy(rpmText, "-");
  for (int oy = 0, band = 0; oy < RG_H; oy += RG_BAND, ++band) {
    LGFX_Sprite& c = gaugeBand[band & 1];
    c.fillScreen(RACE_BG);
    auto px = [](float x) { return RG_OX + x * RG_SCALE; };
    auto py = [oy](float y) { return RG_OY + y * RG_SCALE - oy; };
    const float h = RG_HALF / RG_SCALE;
    for (uint8_t i = 0; i < RACE_SEGMENTS; ++i) {
      float x0, y0, dx0, dy0, x1, y1, dx1, dy1;
      pathAt(i * step, x0, y0, dx0, dy0);
      pathAt(i * step + step * RACE_FILL, x1, y1, dx1, dy1);
      // Perpendicular offsets (dy, -dx) give the stroke width.
      const int ax = px(x0 + dy0 * h), ay = py(y0 - dx0 * h), bx = px(x0 - dy0 * h), by = py(y0 + dx0 * h);
      const int cx = px(x1 + dy1 * h), cy = py(y1 - dx1 * h), ex = px(x1 - dy1 * h), ey = py(y1 + dx1 * h);
      if (max(max(ay, by), max(cy, ey)) < 0 || min(min(ay, by), min(cy, ey)) >= RG_BAND) continue;
      const uint16_t color = i < lit ? raceGradient((x0 + x1) / 2) : RACE_TRACK;
      // Two triangles make the quad; the line covers the seam along their
      // shared diagonal, which the rasteriser can leave unfilled.
      c.fillTriangle(ax, ay, bx, by, cx, cy, color);
      c.fillTriangle(bx, by, cx, cy, ex, ey, color);
      c.drawLine(bx, by, cx, cy, color);
    }
    // 0-15 (x1000 RPM) labels above the bar; 0 sits beside the foot.
    for (int v = 0; v <= 15; v += 3) {
      float x, y, dx, dy;
      pathAt(v / 15.0f * total, x, y, dx, dy);
      char text[4];
      snprintf(text, sizeof(text), "%d", v);
      if (v == 0) label(c, text, px(x + 34), py(y + 4), TEXT, textdatum_t::middle_left);
      else if (v == 15) label(c, text, W - 4, py(y - 26), TEXT, textdatum_t::middle_right);
      else label(c, text, px(x), py(y - 26), TEXT, textdatum_t::middle_center);
    }
    label(c, "x1000 RPM", px(588), 52 - oy, MUTED, textdatum_t::top_right, &fonts::Font0);
    // Readouts: speed and RPM.
    label(c, speedText, RD_SPEED_X, RD_Y - oy, TEXT, textdatum_t::middle_center, &fonts::Font7);
    label(c, rpmText, RD_RPM_X, RD_Y - oy, DIGITS, textdatum_t::middle_center, &fonts::Font7);
    label(c, "km/h", RD_SPEED_X, 136 - oy, RACE_LABEL, textdatum_t::middle_center);
    label(c, "RPM", RD_RPM_X, 136 - oy, RACE_LABEL, textdatum_t::middle_center);
    lcd.waitDMA();
    c.pushSprite(&lcd, 0, RG_Y + oy);
  }
}

// Tiles: four on the top row, three wider ones below.
constexpr uint8_t TILE_COUNT = 7;
const char* const TILE_LABELS[TILE_COUNT] = {"Bat", "ECT", "IAT", "MAP", "O2 V", "AFR", "TPS %"};
const char* const TILE_UNITS[TILE_COUNT] = {"V", "C", "C", "kPa", "", ":1", ""};
constexpr int TILE_H = 46, TILE_HEAD = 15;
inline int tileW(uint8_t i) { return i < 4 ? 116 : 156; }
inline int tileX(uint8_t i) { return i < 4 ? 2 + i * 120 : 2 + (i - 4) * 160; }
inline int tileY(uint8_t i) { return 222 + (i < 4 ? 0 : 49); }

void drawRaceFrame() {
  for (uint8_t i = 0; i < TILE_COUNT; ++i) {
    const int x = tileX(i), y = tileY(i), w = tileW(i);
    lcd.fillRoundRect(x, y, w, TILE_H, 6, TILE_BG);
    lcd.drawRoundRect(x, y, w, TILE_H, 6, TILE_EDGE);
    for (int r = 0; r < TILE_HEAD; ++r)  // Green header, #6be594 -> #2c9a52 like the browser.
      lcd.drawFastHLine(x + (r ? 1 : 4), y + 1 + r, w - (r ? 2 : 8), lerpColor(0x6be594, 0x2c9a52, r / float(TILE_HEAD - 1)));
    label(lcd, TILE_LABELS[i], x + w / 2, y + 1 + TILE_HEAD / 2, TILE_INK, textdatum_t::middle_center);
    label(lcd, TILE_UNITS[i], x + w - 6, y + TILE_H - 4, MUTED, textdatum_t::bottom_right);
  }
}

void raceText(const DisplayData& d, bool running) {
  char value[16];
  for (uint8_t i = 0; i < TILE_COUNT - 1; ++i) {
    uint16_t color = TEXT;
    if (i == 0) {  // Battery is live with the key off, like a real bike.
      if (d.batteryAvailable && isnan(d.batteryV)) { strcpy(value, "ERR"); color = RED; }
      else if (d.batteryAvailable) {
        snprintf(value, sizeof(value), "%.1f", d.batteryV);
        color = d.batteryV < 11.8f || d.batteryV > 15.0f ? ORANGE : d.batteryV >= 13.2f ? GREEN : TEXT;
      } else { strcpy(value, "--"); color = MUTED; }
    }
    else if (!running) { strcpy(value, "--"); color = MUTED; }
    else if (i == 1) color = sensorText(d.ect, 1, value, sizeof(value));
    else if (i == 2) color = sensorText(d.iat, 1, value, sizeof(value));
    else if (i == 3) color = sensorText(d.map, 1, value, sizeof(value));
    else if (i == 4) {
      if (d.o2.afrAvailable) { snprintf(value, sizeof(value), "%.2f", d.o2.volts); color = mixtureColor(d.o2); }
      else { strcpy(value, "ERR"); color = RED; }
    } else if (i == 5) { if (d.o2.afrAvailable) snprintf(value, sizeof(value), "%.1f", d.o2.afr); else { strcpy(value, "ERR"); color = RED; } }
    field(S_VALUE0 + i, tileX(i) + 8, tileY(i) + 18, textdatum_t::top_left, &fonts::Font4, color, TILE_BG, value, 74);
  }
  // TPS tile: whole percent and end angles on the left, throttle body on the right.
  const uint8_t t = TILE_COUNT - 1;
  const bool tpsOk = running && d.tps.available;
  char angle[12] = "";
  if (tpsOk) {
    snprintf(value, sizeof(value), "%.0f", d.tps.value);
    throttleEnds(throttleDeg(d.tps), angle, sizeof(angle));
  } else strcpy(value, "--");
  field(S_VALUE0 + t, tileX(t) + 8, tileY(t) + 18, textdatum_t::top_left, &fonts::Font4,
        !running ? MUTED : !tpsOk ? RED : d.tps.inSpec ? TEXT : AMBER, TILE_BG, value, 44);
  // Only the first plate end fits beside the icon ("293" of "293-113").
  if (char* dash = strchr(angle, '-')) *dash = '\0';
  field(S_TP_ANGLE, tileX(t) + 52, tileY(t) + 34, textdatum_t::top_left, &fonts::Font0, ORANGE, TILE_BG, angle, 30);
  drawThrottle(tileX(t) + tileW(t) - TP_SIZE - 2, tileY(t) + TILE_HEAD + 1, TILE_BG, tpsOk, tpsOk ? throttleDeg(d.tps) : 0);
}

// ---- Smart gauge display ---------------------------------------------------
// Neon "SMART GAUGE" screen: REF1 / REF2 / BAT boxes on top, an RPM dial and
// a speed dial with the title between them, and six sensor tiles below
// (ECT, AIT, MAP, O2, AFR, TPS).
constexpr uint32_t SM_BG888 = 0x040710;
constexpr uint16_t SM_BG = rgb(0x04, 0x07, 0x10), SM_FACE = rgb(0x01, 0x02, 0x06);
constexpr uint16_t SM_CHROME = rgb(0xc4, 0xcc, 0xd6), SM_CHROME_DK = rgb(0x4a, 0x55, 0x63), SM_GLOW = rgb(0x12, 0x3a, 0x8a);
constexpr uint16_t SM_NEEDLE = rgb(0xff, 0x2a, 0x2a), SM_NEEDLE_GLOW = rgb(0x5a, 0x0a, 0x0a);
constexpr uint16_t SM_TICK = rgb(0xf2, 0xf7, 0xfc), SM_TICK_MINOR = rgb(0x8a, 0x97, 0xab);
constexpr uint16_t SM_GOLD = rgb(0xff, 0xc4, 0x1a), SM_SILVER = rgb(0xdd, 0xe3, 0xea);

constexpr uint16_t hex(uint32_t c) { return rgb(c >> 16, (c >> 8) & 0xFF, c & 0xFF); }
inline uint16_t neonFill(uint32_t c) { return lerpColor(SM_BG888, c, 0.12f); }

// Rounded box with a soft outer glow, a 2 px neon edge and a dark tinted fill.
void neonBox(int x, int y, int w, int h, uint32_t c) {
  lcd.drawRoundRect(x, y, w, h, 9, lerpColor(SM_BG888, c, 0.4f));
  lcd.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 8, hex(c));
  lcd.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 7, hex(c));
  lcd.fillRoundRect(x + 3, y + 3, w - 6, h - 6, 6, neonFill(c));
}

// Clears a rectangle and draws the text on it, only when the text changes.
// (Free fonts are taller than their digits, so an opaque string would spill
// over the neon edges; the clear rectangle keeps it inside.)
void boxText(uint8_t slot, int cx, int cy, int cw, int ch, uint16_t bg, const char* text, uint16_t color,
             int tx, int ty, textdatum_t datum, const lgfx::IFont* font) {
  if (!changed(slot, text, color)) return;
  lcd.fillRect(cx, cy, cw, ch, bg);
  label(lcd, text, tx, ty, color, datum, font);
}

// -- Dials --
constexpr int DG_W = 180, DG_H = 156, DG_BAND = 26, DG_CX = 90, DG_CY = 90, DG_Y = 58;
constexpr int DG_X[2] = {4, 296};
constexpr float DG_START = 150, DG_SWEEP = 240, DG_STEP = 4;  // Screen degrees, clockwise from 3 o'clock.
constexpr int DG_BAND_IN = 71, DG_BAND_OUT = 79;              // Coloured scale band.

struct Dial {
  const char* title;
  const char* unit;
  float max, minor, major, labelDiv, red;
  const lgfx::IFont* digits;
};
const Dial DIALS[2] = {
  {"RPM", "rpm", RPM_MAX, 1000, 3000, 1000, 12000, &fonts::FreeSansBold18pt7b},  // Labels x1000, red from 12,000.
  {"SPEED", "km/h", 240, 20, 40, 1, 1e9f, &fonts::FreeSansBold24pt7b}};

inline float dialAngle(const Dial& g, float v) { return DG_START + constrain(v / g.max, 0.0f, 1.0f) * DG_SWEEP; }

inline void dialPoint(float deg, float r, int cy, float& x, float& y) {
  x = DG_CX + cosf(deg * DEG_TO_RAD) * r;
  y = cy + sinf(deg * DEG_TO_RAD) * r;
}

// fillArc with angles past 360 split in two.
void arcDeg(LovyanGFX& c, int cx, int cy, int r0, int r1, float a0, float a1, uint16_t color) {
  if (a1 <= a0) return;
  if (a0 >= 360) c.fillArc(cx, cy, r0, r1, a0 - 360, a1 - 360, color);
  else if (a1 > 360) { c.fillArc(cx, cy, r0, r1, a0, 360, color); c.fillArc(cx, cy, r0, r1, 0, a1 - 360, color); }
  else c.fillArc(cx, cy, r0, r1, a0, a1, color);
}

// Does the piece of ring between a0 and a1 (r0-r1) touch this band?
bool arcInBand(int cy, int r0, int r1, float a0, float a1) {
  const float s0 = sinf(a0 * DEG_TO_RAD), s1 = sinf(a1 * DEG_TO_RAD);
  const float lo = cy + min(min(s0 * r0, s0 * r1), min(s1 * r0, s1 * r1)) - 2;
  const float hi = cy + max(max(s0 * r0, s0 * r1), max(s1 * r0, s1 * r1)) + 2;
  return hi >= 0 && lo < DG_BAND;
}

// Scale colour: blue -> cyan along the dial, red in the red zone. Unlit
// parts of the band are a dim version of the same colour.
uint16_t dialColor(const Dial& g, float v, bool lit) {
  const uint16_t c = v >= g.red ? RED : lerpColor(0x1f5bff, 0x2ee6ff, constrain(v / g.max, 0.0f, 1.0f));
  return lit ? c : lerpColor(0x010206, to888(c), 0.32f);
}

void renderDial(uint8_t which, float value, bool needle, const char* digits) {
  if (!gaugeBand[0].getBuffer() || !gaugeBand[1].getBuffer()) return;
  const Dial& g = DIALS[which];
  const float aValue = needle ? dialAngle(g, value) : DG_START;
  const int segments = int(DG_SWEEP / DG_STEP);
  for (int oy = 0, band = 0; oy < DG_H; oy += DG_BAND, ++band) {
    LGFX_Sprite& c = gaugeBand[band & 1];
    c.fillScreen(SM_BG);
    const int cy = DG_CY - oy;
    // Face, cut flat at the bottom, with a chrome bezel and a blue glow.
    c.fillCircle(DG_CX, cy, 82, SM_FACE);
    arcDeg(c, DG_CX, cy, 88, 90, 134, 406, SM_GLOW);
    arcDeg(c, DG_CX, cy, 84, 87, 134, 406, SM_CHROME);
    arcDeg(c, DG_CX, cy, 82, 83, 134, 406, SM_CHROME_DK);
    c.fillRect(0, cy + 63, DG_W, DG_H, SM_BG);
    c.fillRect(DG_CX - 61, cy + 60, 122, 3, SM_CHROME);
    // Scale band: lit up to the needle.
    for (int i = 0; i < segments; ++i) {
      const float a0 = DG_START + i * DG_STEP, a1 = a0 + DG_STEP;
      if (!arcInBand(cy, DG_BAND_IN, DG_BAND_OUT, a0, a1)) continue;
      const float v = (i + 0.5f) / segments * g.max;
      if (a1 <= aValue) arcDeg(c, DG_CX, cy, DG_BAND_IN, DG_BAND_OUT, a0, a1, dialColor(g, v, true));
      else if (a0 >= aValue) arcDeg(c, DG_CX, cy, DG_BAND_IN, DG_BAND_OUT, a0, a1, dialColor(g, v, false));
      else {
        arcDeg(c, DG_CX, cy, DG_BAND_IN, DG_BAND_OUT, a0, aValue, dialColor(g, v, true));
        arcDeg(c, DG_CX, cy, DG_BAND_IN, DG_BAND_OUT, aValue, a1, dialColor(g, v, false));
      }
    }
    // Ticks and labels.
    for (float v = 0; v <= g.max + 0.5f; v += g.minor) {
      const bool major = fmodf(v + 0.5f, g.major) < 1;
      const float a = dialAngle(g, v);
      float x1, y1, x2, y2;
      dialPoint(a, 67, cy, x1, y1);
      dialPoint(a, major ? 57 : 62, cy, x2, y2);
      if (max(y1, y2) < -2 || min(y1, y2) > DG_BAND + 2) continue;
      c.drawWideLine(x1, y1, x2, y2, major ? 1.5f : 0.8f, v >= g.red ? RED : major ? SM_TICK : SM_TICK_MINOR);
      if (major) {
        float tx, ty;
        dialPoint(a, 46, cy, tx, ty);
        char text[6];
        snprintf(text, sizeof(text), "%d", int(lroundf(v / g.labelDiv)));
        label(c, text, tx, ty, v >= g.red ? RED : SM_TICK, textdatum_t::middle_center, &fonts::FreeSansBold9pt7b);
      }
    }
    // Needle (under the readout so the digits stay readable).
    if (needle) {
      float x1, y1, x2, y2;
      dialPoint(aValue, 24, cy, x1, y1);
      dialPoint(aValue, 72, cy, x2, y2);
      c.drawWideLine(x1, y1, x2, y2, 3.0f, SM_NEEDLE_GLOW);
      c.drawWideLine(x1, y1, x2, y2, 1.4f, SM_NEEDLE);
    }
    label(c, g.title, DG_CX, cy - 26, SM_TICK, textdatum_t::middle_center, &fonts::FreeSansBold9pt7b);
    label(c, digits, DG_CX, cy + 4, TEXT, textdatum_t::middle_center, g.digits);
    label(c, g.unit, DG_CX, cy + 44, MUTED, textdatum_t::middle_center, &fonts::FreeSansBold9pt7b);
    lcd.waitDMA();  // The other buffer's transfer must end before this one starts.
    c.pushSprite(&lcd, DG_X[which], DG_Y + oy);
  }
}

// -- Top boxes: REF1, REF2, BAT --
constexpr int BX_Y = 2, BX_H = 50, BX_W = 154;
constexpr int BX_X[3] = {4, 163, 322};
const char* const BX_NAMES[3] = {"REF1", "REF2", "BAT"};
constexpr uint32_t BX_EDGE[3] = {0x39ff5a, 0x1e9bff, 0xffe11a};
constexpr uint32_t BX_VALUE[3] = {0x5cff7a, 0x5cff7a, 0xffe53b};

void drawBoxIcon(uint8_t i, int ix, int iy, uint16_t c) {
  if (i == 0) {  // Lightning bolt.
    lcd.fillTriangle(ix + 16, iy, ix + 4, iy + 18, ix + 13, iy + 17, c);
    lcd.fillTriangle(ix + 10, iy + 14, ix + 21, iy + 13, ix + 7, iy + 32, c);
    lcd.fillTriangle(ix + 16, iy, ix + 10, iy + 14, ix + 13, iy + 17, c);
  } else if (i == 1) {  // Crosshair.
    const int cx = ix + 12, cy = iy + 16;
    lcd.drawCircle(cx, cy, 10, c);
    lcd.drawCircle(cx, cy, 9, c);
    lcd.fillCircle(cx, cy, 3, c);
    lcd.fillRect(cx - 1, cy - 15, 3, 7, c);
    lcd.fillRect(cx - 1, cy + 9, 3, 7, c);
    lcd.fillRect(cx - 15, cy - 1, 7, 3, c);
    lcd.fillRect(cx + 9, cy - 1, 7, 3, c);
  } else {  // Battery.
    lcd.fillRect(ix + 8, iy + 1, 8, 4, c);
    lcd.drawRoundRect(ix + 3, iy + 5, 18, 27, 3, c);
    lcd.drawRoundRect(ix + 4, iy + 6, 16, 25, 2, c);
    for (int k = 0; k < 3; ++k) lcd.fillRect(ix + 7, iy + 10 + k * 7, 10, 4, c);
  }
}

// -- Bottom tiles: ECT, AIT, MAP, O2, AFR, TPS --
constexpr int TL_Y = 222, TL_H = 96, TL_W = 76;
inline int tlX(uint8_t i) { return 2 + i * 80; }
const char* const TL_NAMES[6] = {"ECT", "AIT", "MAP", "O2", "AFR", "TPS"};
const char* const TL_UNITS[6] = {"C", "C", "kPa", "V", ":1", "%"};
constexpr uint32_t TL_EDGE[6] = {0x22d3ee, 0x7cff3a, 0xe03cff, 0xff7a1a, 0x2ee6c0, 0x9b5cff};
// Value area: right of the icon (the TPS throttle symbol is wider).
inline int tlValueX(uint8_t i) { return tlX(i) + (i == 5 ? 36 : 24); }

void drawThermometer(int ix, int iy, int len, uint16_t c, uint16_t fill) {
  lcd.fillRoundRect(ix + 6, iy, 7, len, 3, c);
  lcd.fillRoundRect(ix + 8, iy + 2, 3, len - 4, 1, fill);
  lcd.fillRect(ix + 8, iy + len / 3, 3, len - len / 3, c);  // Mercury.
  lcd.fillCircle(ix + 9, iy + len + 3, 5, c);
  for (int k = 0; k < 3; ++k) lcd.drawFastHLine(ix + 14, iy + 3 + k * 5, 4, c);
}

void drawTileIcon(uint8_t i, int ix, int iy, uint16_t c, uint16_t fill) {
  switch (i) {
    case 0:  // Coolant: thermometer over waves.
      drawThermometer(ix, iy, 20, c, fill);
      for (int row = 0; row < 2; ++row)
        for (int k = 0; k < 3; ++k) {
          const int x = ix + k * 6, y = iy + 33 + row * 5;
          lcd.drawLine(x, y, x + 3, y - 2, c);
          lcd.drawLine(x + 3, y - 2, x + 6, y, c);
        }
      break;
    case 1: drawThermometer(ix, iy + 2, 26, c, fill); break;  // Air temperature.
    case 2:  // Intake manifold: three runners on a plenum.
      for (int k = 0; k < 3; ++k) lcd.fillRoundRect(ix + 1 + k * 6, iy + 2, 4, 15, 2, c);
      lcd.drawRoundRect(ix, iy + 16, 18, 18, 3, c);
      lcd.drawRoundRect(ix + 1, iy + 17, 16, 16, 3, c);
      lcd.drawFastHLine(ix + 4, iy + 25, 10, c);
      break;
    case 3:  // Oxygen sensor.
      lcd.drawCircle(ix + 9, iy + 18, 10, c);
      lcd.drawCircle(ix + 9, iy + 18, 9, c);
      label(lcd, "O2", ix + 10, iy + 18, c, textdatum_t::middle_center, &fonts::Font0);
      break;
    case 4:  // Fuel pump.
      lcd.drawRoundRect(ix, iy + 6, 12, 28, 2, c);
      lcd.drawRoundRect(ix + 1, iy + 7, 10, 26, 2, c);
      lcd.fillRect(ix + 3, iy + 10, 6, 6, c);
      lcd.fillRect(ix - 1, iy + 34, 14, 3, c);
      lcd.fillRect(ix + 12, iy + 8, 3, 3, c);
      lcd.drawLine(ix + 14, iy + 10, ix + 17, iy + 14, c);
      lcd.drawFastVLine(ix + 17, iy + 14, 14, c);
      lcd.drawLine(ix + 17, iy + 28, ix + 14, iy + 31, c);
      break;
    default: break;  // TPS: the live throttle symbol, drawn by smartText().
  }
}

// Simple coupe outline for the centre column, around (cx, cy).
void drawCar(int cx, int cy, uint16_t c, float w) {
  static const int8_t BODY[][2] = {{-46, 14}, {-45, 6}, {-34, 2}, {-15, -6}, {4, -9}, {19, -8}, {32, -1}, {44, 2}, {47, 9}, {46, 14}};
  static const int8_t ROOF[][2] = {{-14, 0}, {-5, -5}, {13, -5}, {21, 1}, {-14, 0}};
  for (uint8_t k = 1; k < sizeof(BODY) / sizeof(BODY[0]); ++k)
    lcd.drawWideLine(cx + BODY[k - 1][0], cy + BODY[k - 1][1], cx + BODY[k][0], cy + BODY[k][1], w, c);
  for (uint8_t k = 1; k < sizeof(ROOF) / sizeof(ROOF[0]); ++k)
    lcd.drawWideLine(cx + ROOF[k - 1][0], cy + ROOF[k - 1][1], cx + ROOF[k][0], cy + ROOF[k][1], w * 0.7f, c);
  lcd.drawWideLine(cx - 46, cy + 14, cx - 36, cy + 14, w, c);
  lcd.drawWideLine(cx - 20, cy + 14, cx + 20, cy + 14, w, c);
  lcd.drawWideLine(cx + 36, cy + 14, cx + 46, cy + 14, w, c);
  for (int s = -1; s <= 1; s += 2) lcd.fillArc(cx + s * 28, cy + 14, 8 + int(w), 7 - int(w / 2), 180, 360, c);
}

enum : uint8_t { SM_TILE0 = S_VALUE0, SM_BOX0 = S_VALUE0 + 6 };

void drawSmartFrame() {
  for (uint8_t i = 0; i < 3; ++i) {
    const int x = BX_X[i];
    neonBox(x, BX_Y, BX_W, BX_H, BX_EDGE[i]);
    drawBoxIcon(i, x + 9, BX_Y + 9, hex(BX_EDGE[i]));
    label(lcd, BX_NAMES[i], (x + 36 + x + BX_W - 8) / 2, BX_Y + 18, TEXT, textdatum_t::baseline_center, &fonts::FreeSansBold9pt7b);
    label(lcd, "V", x + BX_W - 9, BX_Y + 45, hex(BX_VALUE[i]), textdatum_t::baseline_right, &fonts::FreeSansBold12pt7b);
  }
  for (uint8_t i = 0; i < 6; ++i) {
    const int x = tlX(i);
    neonBox(x, TL_Y, TL_W, TL_H, TL_EDGE[i]);
    const uint16_t fill = neonFill(TL_EDGE[i]);
    label(lcd, TL_NAMES[i], x + TL_W / 2, TL_Y + 22, TEXT, textdatum_t::baseline_center, &fonts::FreeSansBold9pt7b);
    drawTileIcon(i, x + 6, TL_Y + 33, hex(TL_EDGE[i]), fill);
    label(lcd, TL_UNITS[i], x + TL_W - 7, TL_Y + 84, TEXT, textdatum_t::baseline_right, &fonts::FreeSansBold9pt7b);
    if (i < 2) {  // Degree sign: the fonts have no glyph for it.
      lcd.setFont(&fonts::FreeSansBold9pt7b);
      lcd.drawCircle(x + TL_W - 7 - lcd.textWidth("C") - 4, TL_Y + 73, 2, TEXT);
    }
  }
  // Centre column between the dials: car, SMART GAUGE, stripes.
  constexpr int CX = 240;
  drawCar(CX, 80, lerpColor(SM_BG888, 0x2ea8ff, 0.35f), 2.5f);
  drawCar(CX, 80, hex(0x4cc3ff), 0.8f);
  label(lcd, "SMART", CX, 129, SM_SILVER, textdatum_t::baseline_center, &fonts::FreeSansBoldOblique12pt7b);
  label(lcd, "GAUGE", CX, 153, SM_GOLD, textdatum_t::baseline_center, &fonts::FreeSansBoldOblique12pt7b);
  for (int k = 0; k < 5; ++k) {
    const int x = 202 + k * 16;
    const uint16_t c = lerpColor(0x0b3a8a, 0x2ea8ff, k / 4.0f);
    lcd.fillTriangle(x + 5, 160, x + 13, 160, x, 168, c);
    lcd.fillTriangle(x + 13, 160, x + 8, 168, x, 168, c);
  }
}

void smartText(const DisplayData& d, bool running) {
  char value[16];
  const bool live = d.link == DisplayData::LIVE, havePacket = d.link != DisplayData::WAITING;
  // Status pill and board name in the centre column.
  const char* status = !havePacket ? "WAITING" : !live ? "NO LINK" : !d.keyOn ? "KEY OFF" : "LIVE";
  const uint16_t statusColor = !live ? RED : d.keyOn ? GREEN : AMBER;
  if (changed(S_STATUS, status, statusColor)) {
    lcd.fillRect(188, 186, 104, 26, SM_BG);
    lcd.fillRoundRect(192, 188, 96, 22, 11, statusColor);
    label(lcd, status, 240, 199, SM_BG, textdatum_t::middle_center);
  }
  char board[8] = "";
  if (havePacket) for (uint8_t i = 0; i < 7 && d.board[i]; ++i) board[i] = toupper(d.board[i]);
  boxText(S_BOARD, 192, 173, 96, 10, SM_BG, board, MUTED, 240, 178, textdatum_t::middle_center, &fonts::Font0);

  // REF1 / REF2 (key on) and BAT (live with the key off).
  for (uint8_t i = 0; i < 3; ++i) {
    uint16_t color = hex(BX_VALUE[i]);
    if (i < 2) {
      if (havePacket && !d.references) { strcpy(value, "n/a"); color = MUTED; }
      else if (running && isnan(d.reference[i])) { strcpy(value, "ERR"); color = RED; }
      else if (running) snprintf(value, sizeof(value), "%.2f", d.reference[i]);
      else { strcpy(value, "--"); color = MUTED; }
    } else if (d.batteryAvailable && isnan(d.batteryV)) { strcpy(value, "ERR"); color = RED; }
    else if (d.batteryAvailable) {
      snprintf(value, sizeof(value), "%.1f", d.batteryV);
      if (d.batteryV < 11.8f || d.batteryV > 15.0f) color = ORANGE;
    } else { strcpy(value, havePacket ? "n/a" : "--"); color = MUTED; }
    const int x = BX_X[i];
    boxText(SM_BOX0 + i, x + 36, BX_Y + 20, BX_W - 36 - 27, 27, neonFill(BX_EDGE[i]), value, color,
            x + BX_W - 28, BX_Y + 45, textdatum_t::baseline_right, &fonts::FreeSansBold18pt7b);
  }

  // Sensor tiles.
  for (uint8_t i = 0; i < 6; ++i) {
    uint16_t color = TEXT;
    if (!running) { strcpy(value, "--"); color = MUTED; }
    else switch (i) {
      case 0: color = sensorText(d.ect, 0, value, sizeof(value)); break;
      case 1: color = sensorText(d.iat, 0, value, sizeof(value)); break;
      case 2: color = sensorText(d.map, 0, value, sizeof(value)); break;
      case 3:
        if (d.o2.afrAvailable) { snprintf(value, sizeof(value), "%.2f", d.o2.volts); color = mixtureColor(d.o2); }
        else { strcpy(value, "ERR"); color = RED; }
        break;
      case 4:
        if (d.o2.afrAvailable) snprintf(value, sizeof(value), "%.1f", d.o2.afr); else { strcpy(value, "ERR"); color = RED; }
        break;
      case 5:
        if (d.tps.available) { snprintf(value, sizeof(value), "%.0f", d.tps.value); color = d.tps.inSpec ? TEXT : AMBER; }
        else { strcpy(value, "ERR"); color = RED; }
        break;
    }
    const int vx = tlValueX(i), right = tlX(i) + TL_W - 5;
    boxText(SM_TILE0 + i, vx, TL_Y + 38, right - vx, 22, neonFill(TL_EDGE[i]), value, color,
            right - 1, TL_Y + 57, textdatum_t::baseline_right, &fonts::FreeSansBold12pt7b);
  }
  const bool tpsOk = running && d.tps.available;
  drawThrottle(tlX(5) + 5, TL_Y + 34, neonFill(TL_EDGE[5]), tpsOk, tpsOk ? throttleDeg(d.tps) : 0);
}

// ---- Shared frame ----------------------------------------------------------
uint16_t viewBg() { return view == RACE ? RACE_BG : view == SMART ? SM_BG : BG; }

// REF 1 / REF 2 boxes in the RACE header.
constexpr int REF_X[2] = {4, 150}, REF_W = 140;

// Header without tabs: the screen name (RACE shows REF 1 / REF 2 instead),
// then the board and the status pill drawn by drawStatus().
void drawHeader() {
  if (view == SMART) return;  // Its REF / BAT boxes are part of drawSmartFrame().
  if (view != RACE) {
    label(lcd, VIEW_NAMES[view], 10, TAB_Y + TAB_H / 2, MUTED, textdatum_t::middle_left, &fonts::Font2);
    return;
  }
  for (uint8_t i = 0; i < 2; ++i) {
    lcd.fillRoundRect(REF_X[i], TAB_Y, REF_W, TAB_H, 6, TILE_BG);
    lcd.drawRoundRect(REF_X[i], TAB_Y, REF_W, TAB_H, 6, TILE_EDGE);
    label(lcd, i ? "REF 2" : "REF 1", REF_X[i] + 8, TAB_Y + TAB_H / 2, RACE_LABEL, textdatum_t::middle_left, &fonts::Font2);
  }
}

void drawFrame() {
  lcd.startWrite();
  lcd.fillScreen(viewBg());
  drawHeader();
  if (view != SMART) lcd.drawFastHLine(0, HEADER_H, W, EDGE);
  lcd.waitDMA();
  for (auto& band : gaugeBand) {
    band.deleteSprite();
    if (view == CLASSIC) band.createSprite(CG_W, CG_BAND);
    else if (view == RACE) band.createSprite(RG_W, RG_BAND);
    else band.createSprite(DG_W, DG_BAND);
  }
  if (view == CLASSIC) drawClassicFrame();
  else if (view == RACE) drawRaceFrame();
  else drawSmartFrame();
  lcd.endWrite();
  memset(shown, 0, sizeof(shown));
  memset(shownColor, 0, sizeof(shownColor));
  shownGaugeKey = shownSpeedKey = -1;
}

// No calibration screen: double-tap only needs to know the panel is pressed,
// not where. A calibration saved earlier by ESP32_LGFX_Touch_Test (NVS
// "lgfxtouch/cal") is still applied if present.
void loadTouchCalibration() {
  Preferences store;
  uint16_t cal[8];
  store.begin("lgfxtouch", true);
  if (store.getBytes("cal", cal, sizeof(cal)) == sizeof(cal)) lcd.setTouchCalibrate(cal);
  store.end();
}

// Boot animation: a tachometer ring sweeps up (cyan -> amber -> red) around
// "ECU", the ECU TESTBENCH title fades in, and a loading bar fills with
// start-up steps underneath.
constexpr uint32_t INTRO_MS = 2600;

// Start-up: a plain dark screen, the ring sweeping round, "ECU" and the
// title fading in and a thin loading bar.
void playIntro() {
  constexpr int RX = W / 2, RY = 118, R = 62;
  constexpr int BAR_X = 170, BAR_Y = 252, BAR_W = 140, BAR_H = 4;
  lcd.startWrite();
  lcd.fillScreen(BG);
  lcd.fillArc(RX, RY, R, R - 6, 0, 360, ARC);        // Ring track.
  lcd.fillRoundRect(BAR_X, BAR_Y, BAR_W, BAR_H, 2, ARC);
  lcd.endWrite();

  float shownDeg = 270;
  int shownFill = 0;
  uint8_t shownFade = 0;
  const uint32_t start = millis();
  for (uint32_t t = 0; t <= INTRO_MS; t = millis() - start) {
    const float p = float(t) / INTRO_MS;
    lcd.startWrite();
    // Ring sweeps once round from the top over the first 60 %.
    const float deg = 270 + 360 * min(1.0f, p / 0.6f);
    for (; shownDeg < deg; shownDeg += 4) lcd.fillArc(RX, RY, R, R - 6, shownDeg, min(shownDeg + 4, deg), ACCENT);
    // Badge and title fade in (redrawn only when the level steps).
    const uint8_t fade = uint8_t(constrain((p - 0.2f) / 0.45f, 0.0f, 1.0f) * 16);
    if (fade != shownFade) {
      shownFade = fade;
      const float f = fade / 16.0f;
      lcd.fillRect(RX - 40, RY - 14, 80, 28, BG);
      label(lcd, "ECU", RX, RY, lerpColor(0x0d141e, 0x66d4f1, f), textdatum_t::middle_center, &fonts::FreeSansBold12pt7b);
      lcd.fillRect(60, 196, W - 120, 36, BG);
      label(lcd, "ECU TESTBENCH", W / 2, 214, lerpColor(0x0d141e, 0xf2f7fc, f), textdatum_t::middle_center,
            &fonts::FreeSansBold18pt7b);
    }
    // Loading bar.
    const int fill = lroundf(min(1.0f, p) * BAR_W);
    if (fill > shownFill) { shownFill = fill; lcd.fillRoundRect(BAR_X, BAR_Y, fill, BAR_H, 2, ACCENT); }
    lcd.endWrite();
    delay(16);
  }
  delay(250);
}

void begin() {
  lcd.init();
  lcd.setRotation(1);  // Landscape 480 x 320.
  lcd.setBrightness(255);
  loadTouchCalibration();
  playIntro();
  throttleSprite.setColorDepth(16);
  throttleSprite.createSprite(TP_SIZE, TP_SIZE);
  for (auto& band : gaugeBand) band.setColorDepth(16);
  buildRacePath();
  drawFrame();
}

void drawStatus(const DisplayData& d) {
  const bool live = d.link == DisplayData::LIVE;
  const char* status = d.link == DisplayData::WAITING ? "WAITING" : !live ? "NO LINK"
                     : !d.keyOn ? "KEY OFF" : "LIVE";
  const uint16_t color = !live ? RED : d.keyOn ? GREEN : AMBER;
  if (changed(S_STATUS, status, color)) {
    lcd.fillRoundRect(384, TAB_Y, 92, TAB_H, 8, color);
    label(lcd, status, 430, TAB_Y + TAB_H / 2, viewBg(), textdatum_t::middle_center);
  }
  char board[8] = "";
  if (d.link != DisplayData::WAITING) for (uint8_t i = 0; i < 7 && d.board[i]; ++i) board[i] = toupper(d.board[i]);
  field(S_BOARD, 336, TAB_Y + TAB_H / 2, textdatum_t::middle_center, &fonts::Font2, MUTED, viewBg(), board, 88);
  if (view != RACE) return;
  const bool running = live && d.keyOn;
  for (uint8_t i = 0; i < 2; ++i) {
    char value[12];
    uint16_t color = TEXT;
    if (d.link != DisplayData::WAITING && !d.references) { strcpy(value, "n/a"); color = MUTED; }
    else if (running && isnan(d.reference[i])) { strcpy(value, "ERR"); color = RED; }
    else if (running) snprintf(value, sizeof(value), "%.2f V", d.reference[i]);
    else { strcpy(value, "-- V"); color = MUTED; }
    field(S_REF1 + i, REF_X[i] + REF_W - 8, TAB_Y + TAB_H / 2, textdatum_t::middle_right, &fonts::Font4,
          color, TILE_BG, value, 86);
  }
}

// Double-tap anywhere for the next screen; a single tap does nothing.
void handleTouch(uint32_t now) {
  if (now - lastTouchPollMs < TOUCH_POLL_MS) return;
  lastTouchPollMs = now;
  int32_t x, y;
  const bool pressed = lcd.getTouch(&x, &y);
  if (pressed && !wasTouched) {
    Serial.printf("# TOUCH x=%ld y=%ld\n", long(x), long(y));
    if (lastTapMs && now - lastTapMs <= DOUBLE_TAP_MS) {
      lastTapMs = 0;
      view = View((view + 1) % VIEW_COUNT);
      drawFrame();
    } else lastTapMs = now;
  }
  // Short release debounce so a resistive panel's dropouts don't count as new taps.
  if (pressed) lastPressMs = now;
  wasTouched = pressed || (wasTouched && now - lastPressMs < 80);
}

// Call every loop.
void update(const DisplayData& d) {
  const uint32_t now = millis();
  handleTouch(now);

  const bool live = d.link == DisplayData::LIVE, running = live && d.keyOn;
  // Key-on self-test: every lamp lit and a full needle / bar sweep.
  if (running && !wasRunning) { selfTestActive = true; selfTestStartMs = now; }
  wasRunning = running;
  const uint32_t testMs = now - selfTestStartMs;
  if (selfTestActive && testMs >= SELF_TEST_MS) selfTestActive = false;
  const bool selfTest = selfTestActive;
  const float sweep = selfTest ? SPEED_MAX * (testMs < SELF_TEST_MS / 2 ? testMs : SELF_TEST_MS - testMs) / (SELF_TEST_MS / 2) : 0;

  // Engine RPM eases at 20,000 rpm/s.
  const float dt = min(0.1f, (now - lastAnimMs) / 1000.0f);
  lastAnimMs = now;
  const float targetRpm = running ? d.rpm : 0;
  animatedRpm += constrain(targetRpm - animatedRpm, -20000 * dt, 20000 * dt);

  const float speed = selfTest ? sweep : running ? d.speedKmh : 0;
  const float rpm = selfTest ? sweep / SPEED_MAX * RPM_MAX : animatedRpm;

  if (view == SMART && now - lastGaugeMs >= GAUGE_REFRESH_MS) {
    lastGaugeMs = now;
    // Each dial is redrawn only when its own value changes.
    const bool locked = live && !d.keyOn, show = running || selfTest;
    const int32_t flags = (selfTest << 30) ^ (running << 29) ^ (locked << 28);
    const int32_t rpmKey = (int32_t(rpm / 50) << 4) ^ flags, speedKey = (int32_t(lroundf(speed * 2)) << 4) ^ flags;
    char text[8];
    lcd.startWrite();
    if (rpmKey != shownGaugeKey) {
      shownGaugeKey = rpmKey;
      if (show) snprintf(text, sizeof(text), "%d", int(lroundf(rpm / 10)) * 10); else strcpy(text, locked ? "0" : "---");
      renderDial(0, rpm, show || locked, text);
    }
    if (speedKey != shownSpeedKey) {
      shownSpeedKey = speedKey;
      if (show) snprintf(text, sizeof(text), "%d", int(lroundf(speed))); else strcpy(text, locked ? "0" : "-");
      renderDial(1, speed, show || locked, text);
    }
    lcd.endWrite();
  } else if (view != SMART && now - lastGaugeMs >= GAUGE_REFRESH_MS) {
    lastGaugeMs = now;
    const bool locked = live && !d.keyOn;
    const int32_t key = (int32_t(lroundf(speed * 2)) << 16) ^ (int32_t(rpm / 50) << 4) 
                      ^ (selfTest << 30) ^ (running << 29) ^ (locked << 28);
    if (key != shownGaugeKey) {
      shownGaugeKey = key;
      char speedText[8];
      const bool show = running || selfTest;
      lcd.startWrite();
      if (view == CLASSIC) {
        if (show) snprintf(speedText, sizeof(speedText), "%d", int(lroundf(rpm / 10)) * 10);
        else strcpy(speedText, locked ? "0" : "---");
        renderClassicGauge(rpm, show, speedText);
      } else {
        if (show) snprintf(speedText, sizeof(speedText), "%d", int(lroundf(speed)));
        else strcpy(speedText, locked ? "0" : "-");
        renderRaceGauge(rpm, show || locked, speedText);
      }
      lcd.endWrite();
    }
  }

  if (now - lastTextMs < TEXT_REFRESH_MS) return;
  lastTextMs = now;
  lcd.startWrite();
  if (view == SMART) smartText(d, running);
  else {
    drawStatus(d);
    uint16_t lamps[LAMP_COUNT];
    lampColors(d, selfTest, speed, now, lamps);
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
      if (view == RACE) drawRaceLamp(i, lamps[i]); else drawClassicLamp(i, lamps[i]);
    }
    if (view == RACE) raceText(d, running); else classicText(d, running, speed, rpm);
  }
  lcd.endWrite();
}

} // namespace localDisplay
