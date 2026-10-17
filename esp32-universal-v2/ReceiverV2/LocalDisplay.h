#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Preferences.h>

// Local 4.0" ST7796S TFT + XPT2046 touch panel (landscape 480 x 320) showing
// the instrument panel of the browser dashboard in three screens:
//   CLASSIC - tachometer (RPM), lamps, sensor column, readout grid and DTC line
//   RACE    - REF 1 / REF 2, LED indicator strip, curved RPM bar, speed / RPM, tiles
//   SENSORS - per-channel volts, value and circuit status, link and battery info
// There are no tabs: double-tap anywhere on the screen for the next screen.
// Display and touch share the VSPI bus with separate chip selects; none of
// these pins overlap the Uno/Nano/Mega link on GPIO16/17.
constexpr int8_t PIN_TFT_SCK = 18, PIN_TFT_MISO = 19, PIN_TFT_MOSI = 23;
constexpr int8_t PIN_TFT_CS = 27, PIN_TFT_DC = 25, PIN_TFT_RST = 26;
constexpr int8_t PIN_TOUCH_CS = 33, PIN_TOUCH_IRQ = 32;
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
  bool keyOn, brake, checkEngine, coolantWarning;
  uint8_t milBlink;        // Blink code of the first DTC, 0 = none.
  float speedKmh, rpm, frequencyHz;
  const char* dtcCode;     // First active DTC, or nullptr.
  const char* dtcSensor;
  SensorReading ect, iat, map, tps;
  OxygenReading o2;
  bool batteryAvailable;   // Mega frame with the A7 battery input.
  float batteryV;
  float currentA;          // Mega A8, 0-10 A; valid when batteryAvailable.
  // SENSORS tab.
  bool references;         // Board sends Reference 1/2 (Nano U4: 0-5 V, Mega U5: 0-30 V).
  const char* board;       // "Uno", "Nano" or "Mega".
  const char* boardFrame;  // "U3", "U4" or "U5".
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
      cfg.pin_int = PIN_TOUCH_IRQ;
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
enum View : uint8_t { CLASSIC, RACE, SENSORS } view = CLASSIC;
const char* const VIEW_NAMES[VIEW_COUNT] = {"CLASSIC", "RACE", "SENSORS"};
constexpr int TAB_Y = 4, TAB_H = 28;           // Header row: status pill, board, REF boxes.
constexpr uint32_t DOUBLE_TAP_MS = 400;        // Second tap within this time switches screen.

bool wasTouched = false, wasRunning = false, selfTestActive = false;
uint32_t lastTapMs = 0, lastPressMs = 0, lastTouchPollMs = 0;
uint32_t lastGaugeMs = 0, lastTextMs = 0, lastAnimMs = 0, selfTestStartMs = 0;
float animatedRpm = 0;
int32_t shownGaugeKey = -1;

// Last text / colour drawn in each slot; a slot is redrawn only when it changes.
// Slots are reused between tabs: drawFrame() forgets them all.
constexpr uint8_t SLOT_COUNT = 64;
char shown[SLOT_COUNT][40];
uint16_t shownColor[SLOT_COUNT];
enum Slot : uint8_t {
  S_STATUS, S_DTC, S_BOARD, S_TP_ICON, S_TP_ANGLE, S_REF1, S_REF2,
  S_LAMP0 = 8, S_VALUE0 = 16
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
// Order matches the browser: smart key, brake, winker, check engine, coolant,
// ABS, TCS. Winker and TCS have no input on this bench.
constexpr uint8_t LAMP_COUNT = 7;
const char* const LAMP_NAMES[LAMP_COUNT] = {"KEY", "BRAKE", "WINKER", "CHECK", "TEMP", "ABS", "TCS"};
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
  char tag[8];
  snprintf(tag, sizeof(tag), "%u", color);
  if (!changed(S_LAMP0 + i, tag, color)) return;
  const int y = 40 + i * 40, cx = 17, cy = y + 20;
  lcd.fillRect(0, y, 87, 40, BG);
  if (color) {
    lcd.fillCircle(cx, cy, 13, lerpColor(0x0d141e, to888(color), 0.35f));  // Soft glow.
    lcd.fillCircle(cx, cy, 10, color);
  } else if (LAMP_WIRED[i]) {
    lcd.fillCircle(cx, cy, 10, LAMP_OFF);
    lcd.drawCircle(cx, cy, 10, LAMP_RING);
  } else {
    for (int a = 0; a < 360; a += 30) lcd.fillArc(cx, cy, 10, 9, a, a + 12, LAMP_RING);  // Dashed: not wired.
  }
  label(lcd, LAMP_NAMES[i], 34, cy, color ? color : LAMP_WIRED[i] ? MUTED : LAMP_RING, textdatum_t::middle_left);
}

// RACE indicator strip: one LED per lamp with its name beside it.
void drawRaceLamp(uint8_t i, uint16_t color) {
  char tag[8];
  snprintf(tag, sizeof(tag), "%u", color);
  if (!changed(S_LAMP0 + i, tag, color)) return;
  const int x = 3 + i * 68, y = 41, w = 66, h = 24, cx = x + 10, cy = y + h / 2;
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
  label(lcd, LAMP_NAMES[i], x + 20, cy, color ? color : LAMP_WIRED[i] ? RACE_DIM : RACE_NA, textdatum_t::middle_left);
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
      case 2:  // Mega A8, live with the key off like the battery.
        if (d.batteryAvailable) snprintf(value, sizeof(value), "%.2f A", d.currentA);
        else { strcpy(value, havePacket ? "n/a" : "-- A"); color = MUTED; }
        break;
      case 3: case 4:
        if (havePacket && !d.references) { strcpy(value, "n/a"); color = MUTED; }
        else if (running) snprintf(value, sizeof(value), "%.2f V", d.reference[i - 3]);
        else { strcpy(value, "-- V"); color = MUTED; }
        break;
      case 5:  // Live with the key off, like the RACE battery tile.
        if (d.batteryAvailable) {
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

// Gradient along x: blue at the foot (low RPM) fading to red at the redline.
uint16_t raceGradient(float vbX) {
  const float t = constrain((vbX - 24) / (588 - 24), 0.0f, 1.0f);
  return lerpColor(0x2f6bff, 0xff3b3b, t);
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
      if (d.batteryAvailable) {
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

// ---- Sensors display -------------------------------------------------------
// One row per channel: pin voltage (bar + value), engineering value and the
// circuit status the DTC logic sees, then a grid of link / board / battery info.
const char* const SENSOR_NAMES[5] = {"IAT", "ECT", "O2", "MAP", "TPS"};
const char* const SENSOR_PINS[5] = {"A0", "A1", "A2", "A3", "A4"};
constexpr int SN_Y0 = 60, SN_ROW = 34, VB_X = 84, VB_W = 120, VB_H = 12;
constexpr int INFO_Y0 = 238, INFO_ROW = 27, INFO_COL = 120;
const char* const INFO_LABELS[12] = {
  "BOARD", "LINK", "PACKET AGE", "BAD FRAMES",
  "REF 1  A5", "REF 2  A6", "D7 FREQUENCY", "ENGINE RPM",
  "BATTERY  A7", "CURRENT  A8", "TPS CAL (raw)", "WI-FI AP"};
enum : uint8_t { SS_ROW0 = S_VALUE0, SS_INFO0 = S_VALUE0 + 20 };  // 4 slots per row, then 12 info cells.

void drawSensorsFrame() {
  label(lcd, "CH", 10, 44, MUTED, textdatum_t::top_left, &fonts::Font0);
  label(lcd, "PIN VOLTAGE  0-5 V", VB_X, 44, MUTED, textdatum_t::top_left, &fonts::Font0);
  label(lcd, "VALUE", 370, 44, MUTED, textdatum_t::top_right, &fonts::Font0);
  label(lcd, "STATUS", 474, 44, MUTED, textdatum_t::top_right, &fonts::Font0);
  for (uint8_t i = 0; i < 5; ++i) {
    const int y = SN_Y0 + i * SN_ROW, cy = y + SN_ROW / 2;
    label(lcd, SENSOR_NAMES[i], 10, cy, TEXT, textdatum_t::middle_left, &fonts::Font2);
    label(lcd, SENSOR_PINS[i], 50, cy, MUTED, textdatum_t::middle_left, &fonts::Font0);
    lcd.drawFastHLine(6, y + SN_ROW - 1, W - 12, EDGE);
  }
  for (uint8_t i = 0; i < 12; ++i) {
    const int x = (i % 4) * INFO_COL, y = INFO_Y0 + (i / 4) * INFO_ROW;
    lcd.fillRect(x + 2, y + 1, INFO_COL - 4, INFO_ROW - 2, PANEL);
    label(lcd, INFO_LABELS[i], x + 6, y + 3, MUTED, textdatum_t::top_left, &fonts::Font0);
  }
}

void drawVoltBar(uint8_t slot, uint8_t channel, int y, float volts, bool on, bool ok) {
  const int fill = on ? lroundf(constrain(volts / 5.0f, 0.0f, 1.0f) * (VB_W - 2)) : 0;
  const uint16_t color = ok ? GREEN : RED;
  char tag[8];
  snprintf(tag, sizeof(tag), "%d", fill);
  if (!changed(slot, tag, color)) return;
  lcd.fillRect(VB_X, y, VB_W, VB_H, BAR_TRACK);
  if (fill) lcd.fillRect(VB_X + 1, y + 1, fill, VB_H - 2, color);
  // This channel's fault limits (no marker for a sensor without a low fault).
  for (float v : {FAULT_LIMITS[channel].low, FAULT_LIMITS[channel].high})
    if (v > 0) lcd.drawFastVLine(VB_X + 1 + lroundf(v / 5.0f * (VB_W - 2)), y - 2, VB_H + 4, AMBER);
  lcd.drawRect(VB_X, y, VB_W, VB_H, BAR_EDGE);
}

void sensorsText(const DisplayData& d, bool running) {
  char text[24];
  const bool inSpec[5] = {d.iat.inSpec, d.ect.inSpec, d.o2.inSpec, d.map.inSpec, d.tps.inSpec};
  for (uint8_t i = 0; i < 5; ++i) {
    const int y = SN_Y0 + i * SN_ROW, cy = y + SN_ROW / 2;
    const uint8_t slot = SS_ROW0 + i * 4, bit = 1 << i;
    const float v = d.volts[i];
    const bool circuit = !sensorFault(i, v);
    drawVoltBar(slot, i, cy - VB_H / 2, v, running, circuit);
    if (running) snprintf(text, sizeof(text), "%.3f V", v); else strcpy(text, "--");
    field(slot + 1, 286, cy, textdatum_t::middle_right, &fonts::Font2, running ? TEXT : MUTED, BG, text, 72);

    uint16_t color = MUTED;
    char number[12];
    if (!running) strcpy(text, "--");
    else if (i == 2) {
      if (d.o2.afrAvailable) { snprintf(text, sizeof(text), "%.1f:1 %s", d.o2.afr, d.o2.mixture); color = mixtureColor(d.o2); }
      else { strcpy(text, "ERR"); color = RED; }
    } else {
      const SensorReading& r = i == 0 ? d.iat : i == 1 ? d.ect : i == 3 ? d.map : d.tps;
      color = sensorText(r, 1, number, sizeof(number));
      snprintf(text, sizeof(text), r.available ? "%s %s" : "%s", number, i == 3 ? "kPa" : i == 4 ? "%" : "C");
    }
    field(slot + 2, 370, cy, textdatum_t::middle_right, &fonts::Font2, color, BG, text, 80);

    if (!running) { strcpy(text, "--"); color = MUTED; }
    else if (d.injectedMask & bit) { snprintf(text, sizeof(text), "%s INJ", SENSOR_DTCS[i].general); color = ORANGE; }
    else if (d.autoMask & bit) { strcpy(text, d.highMask & bit ? SENSOR_DTCS[i].high : SENSOR_DTCS[i].low); color = RED; }
    else if (!circuit) { strcpy(text, sensorFaultHigh(i, v) ? "HIGH ..." : "LOW ..."); color = AMBER; }  // Pending DTC.
    else if (!inSpec[i]) { strcpy(text, "OUT OF SPEC"); color = AMBER; }
    else { strcpy(text, "OK"); color = GREEN; }
    field(slot + 3, 474, cy, textdatum_t::middle_right, &fonts::Font2, color, BG, text, 96);
  }

  const bool havePacket = d.link != DisplayData::WAITING;
  for (uint8_t i = 0; i < 12; ++i) {
    uint16_t color = TEXT;
    switch (i) {
      case 0:
        if (havePacket) snprintf(text, sizeof(text), "%s  (%s)", d.board, d.boardFrame); else strcpy(text, "--");
        break;
      case 1:
        strcpy(text, d.link == DisplayData::LIVE ? "LIVE" : d.link == DisplayData::STALE ? "STALE" : "WAITING");
        color = d.link == DisplayData::LIVE ? GREEN : RED;
        break;
      case 2:
        if (havePacket) snprintf(text, sizeof(text), "%lu ms", (unsigned long)d.packetAgeMs); else strcpy(text, "--");
        break;
      case 3:
        snprintf(text, sizeof(text), "%lu", (unsigned long)d.badFrames);
        if (d.badFrames) color = AMBER;
        break;
      case 4: case 5:
        if (havePacket && !d.references) { snprintf(text, sizeof(text), "n/a on %s", d.board); color = MUTED; }
        else if (running) snprintf(text, sizeof(text), "%.2f V", d.reference[i - 4]);
        else { strcpy(text, "--"); color = MUTED; }
        break;
      case 6:
        if (running) snprintf(text, sizeof(text), "%.1f Hz", d.frequencyHz); else { strcpy(text, "--"); color = MUTED; }
        break;
      case 7:
        if (running) snprintf(text, sizeof(text), "%.0f rpm", d.rpm); else { strcpy(text, "--"); color = MUTED; }
        break;
      case 8:
        if (!d.batteryAvailable) {
          if (havePacket) snprintf(text, sizeof(text), "n/a on %s", d.board); else strcpy(text, "--");
          color = MUTED; break;
        }
        snprintf(text, sizeof(text), "%.2f V %s", d.batteryV,
                 d.batteryV > 15.0f ? "OVER" : d.batteryV >= 13.2f ? "CHG" : d.batteryV < 11.8f ? "LOW" : "");
        color = d.batteryV < 11.8f || d.batteryV > 15.0f ? ORANGE : d.batteryV >= 13.2f ? GREEN : TEXT;
        break;
      case 9:
        if (d.batteryAvailable) snprintf(text, sizeof(text), "%.2f A", d.currentA);
        else { strcpy(text, havePacket ? "n/a" : "--"); color = MUTED; }
        break;
      case 10: snprintf(text, sizeof(text), "%.0f - %.0f", d.tpsClosedRaw, d.tpsWotRaw); break;
      case 11: strcpy(text, "192.168.4.1"); color = ACCENT; break;
    }
    const int x = (i % 4) * INFO_COL, y = INFO_Y0 + (i / 4) * INFO_ROW;
    field(SS_INFO0 + i, x + 6, y + 12, textdatum_t::top_left, &fonts::Font2, color, PANEL, text, INFO_COL - 10);
  }
}

// ---- Shared frame ----------------------------------------------------------
uint16_t viewBg() { return view == RACE ? RACE_BG : BG; }

// REF 1 / REF 2 boxes in the RACE header.
constexpr int REF_X[2] = {4, 150}, REF_W = 140;

// Header without tabs: the screen name (RACE shows REF 1 / REF 2 instead),
// then the board and the status pill drawn by drawStatus().
void drawHeader() {
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
  lcd.drawFastHLine(0, HEADER_H, W, EDGE);
  lcd.waitDMA();
  for (auto& band : gaugeBand) {
    band.deleteSprite();
    if (view == CLASSIC) band.createSprite(CG_W, CG_BAND);
    else if (view == RACE) band.createSprite(RG_W, RG_BAND);
  }
  if (view == CLASSIC) drawClassicFrame();
  else if (view == RACE) drawRaceFrame();
  else drawSensorsFrame();
  lcd.endWrite();
  memset(shown, 0, sizeof(shown));
  memset(shownColor, 0, sizeof(shownColor));
  shownGaugeKey = -1;
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

void playIntro() {
  constexpr int RX = W / 2, RY = 112, R = 70;
  constexpr int BAR_X = 120, BAR_Y = 266, BAR_W = 240, BAR_H = 8;
  const char* const STEPS[4] = {"Starting display", "Loading sensor formulas", "Opening Wi-Fi hotspot", "Ready"};
  lcd.startWrite();
  lcd.fillScreen(BG);
  // Faint background grid and corner brackets.
  for (int x = 0; x < W; x += 24) lcd.drawFastVLine(x, 0, H, rgb(0x10, 0x18, 0x24));
  for (int y = 0; y < H; y += 24) lcd.drawFastHLine(0, y, W, rgb(0x10, 0x18, 0x24));
  for (int k = 0; k < 4; ++k) {
    const int cx = k & 1 ? W - 12 : 12, cy = k & 2 ? H - 12 : 12, dx = k & 1 ? -24 : 24, dy = k & 2 ? -24 : 24;
    lcd.drawFastHLine(min(cx, cx + dx), cy, 24, EDGE);
    lcd.drawFastVLine(cx, min(cy, cy + dy), 24, EDGE);
  }
  // Ring track, ticks and centre.
  lcd.fillArc(RX, RY, R + 4, R + 3, 135, 405, EDGE);
  lcd.fillArc(RX, RY, R, R - 10, 135, 405, ARC);
  for (int i = 0; i <= 15; ++i) {
    const float a = (135 + i * 18) * DEG_TO_RAD;
    const int r1 = i % 3 ? R - 16 : R - 22;
    lcd.drawLine(RX + cosf(a) * (R - 13), RY + sinf(a) * (R - 13), RX + cosf(a) * r1, RY + sinf(a) * r1, i >= 12 ? RED : TICK);
  }
  lcd.fillCircle(RX, RY, R - 28, PANEL);
  lcd.drawCircle(RX, RY, R - 28, EDGE);
  lcd.drawRoundRect(BAR_X - 2, BAR_Y - 2, BAR_W + 4, BAR_H + 4, 5, EDGE);
  lcd.endWrite();

  float shownDeg = 135;
  int shownStep = -1;
  const uint32_t start = millis();
  for (uint32_t t = 0; t <= INTRO_MS; t = millis() - start) {
    const float p = float(t) / INTRO_MS;
    lcd.startWrite();
    // Ring sweeps over the first 70 %, coloured by where it is on the scale.
    const float deg = 135 + 270 * min(1.0f, p / 0.7f);
    for (; shownDeg < deg; shownDeg += 3) {
      const float at = (shownDeg - 135) / 270;
      const uint16_t color = at >= 0.8f ? RED : at >= 0.6f ? AMBER : ACCENT;
      lcd.fillArc(RX, RY, R, R - 10, shownDeg, min(shownDeg + 3, deg), color);
    }
    // Centre badge and title fade in.
    const float fade = constrain((p - 0.15f) / 0.4f, 0.0f, 1.0f);
    label(lcd, "ECU", RX, RY, lerpColor(0x131d29, 0x66d4f1, fade), textdatum_t::middle_center, &fonts::FreeSansBold12pt7b);
    label(lcd, "ECU TESTBENCH", W / 2, 210, lerpColor(0x0d141e, 0xf2f7fc, fade), textdatum_t::middle_center,
          &fonts::FreeSansBold18pt7b);
    label(lcd, "Arduino Uno / Nano / Mega  +  ESP32", W / 2, 238, lerpColor(0x0d141e, 0xacbdd1, fade),
          textdatum_t::middle_center, &fonts::Font2);
    // Loading bar and the current start-up step.
    const int fill = lroundf(min(1.0f, p) * BAR_W);
    if (fill > 0) lcd.fillRoundRect(BAR_X, BAR_Y, fill, BAR_H, 3, ACCENT);
    const int step = min(3, int(p * 4));
    if (step != shownStep) {
      shownStep = step;
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(step == 3 ? GREEN : MUTED, BG);
      lcd.setTextDatum(textdatum_t::middle_center);
      lcd.setTextPadding(BAR_W);
      lcd.drawString(STEPS[step], W / 2, BAR_Y + 24);
      lcd.setTextPadding(0);
    }
    lcd.endWrite();
    delay(16);
  }
  delay(350);
}

void begin() {
  pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);
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

  if (view != SENSORS && now - lastGaugeMs >= GAUGE_REFRESH_MS) {
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
  drawStatus(d);
  if (view == SENSORS) sensorsText(d, running);
  else {
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
