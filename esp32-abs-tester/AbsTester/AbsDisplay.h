#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Preferences.h>

// ABS tester screen on the 4.0" ST7796S TFT + XPT2046 touch panel (landscape
// 480 x 320), same panel and wiring as esp32-universal-v2:
//   title | 0-250 km/h gauge + front / rear speed bars | BATTERY / CURRENT /
//   REF1 tiles | output and brake state, START/STOP and RESET.
// Two wheel-speed sensors, each Hz / 6 km/h: rear on D7 (white needle) and
// front on Nano / Mega D8 (green needle). Only what an ABS unit depends on is
// shown, no engine sensors. Every power-up plays an ABS lamp
// splash, and the switch-on self-test (needle sweep + lamp check) runs when
// live data first arrives, since the Mega has no smart-key button.
constexpr int8_t PIN_TFT_SCK = 18, PIN_TFT_MISO = 19, PIN_TFT_MOSI = 23;
constexpr int8_t PIN_TFT_CS = 27, PIN_TFT_DC = 25, PIN_TFT_RST = 26;
constexpr int8_t PIN_TOUCH_CS = 33, PIN_TOUCH_IRQ = 32;
constexpr uint32_t TFT_SPI_HZ = 27000000;
constexpr uint32_t GAUGE_REFRESH_MS = 50, TEXT_REFRESH_MS = 100, TOUCH_POLL_MS = 20;
constexpr uint32_t INTRO_MS = 2600, SELF_TEST_MS = 1600;  // Power-on splash, switch-on sweep.

// Speed formula, the same as the Mega and esp32-universal-v2, for each wheel:
//   speed = Hz / 6 km/h, max 250; the brake pulls it to 0 at 100 km/h/s.
constexpr float ABS_HZ_PER_KMH = 6, ABS_SPEED_MAX = 250, ABS_AMBER_KMH = 210;
enum Wheel : uint8_t { REAR, FRONT };  // D7, D8.

// Wheel-speed outputs: a 50 % square wave per wheel at speed x 6 Hz, so each
// gives back its input frequency (0 while braking). -1 = screen only (SIM).
// Free pins: GPIO4, 13, 14, 21, 22. The LEDC channels are on separate timers
// so the two frequencies are independent.
constexpr int8_t ABS_OUTPUT_PIN[2] = {-1, -1};  // Rear, front.
constexpr uint8_t ABS_LEDC_CHANNEL[2] = {4, 6};
constexpr bool ABS_OUTPUT_SIM = ABS_OUTPUT_PIN[REAR] < 0 && ABS_OUTPUT_PIN[FRONT] < 0;

// What the screen shows, filled by the sketch each loop.
struct AbsData {
  enum Link : uint8_t { WAITING, LIVE, STALE } link;
  bool keyOn, brake;
  bool front;                            // Frame carries the D8 front sensor (Mega).
  float speedKmh[2], frequencyHz[2];     // Per Wheel, speed from its frequency.
  bool batteryAvailable;  // Mega frame (battery A7, current A8).
  float batteryV, currentA;
  bool references;        // Board sends Reference 1 (Nano / Mega).
  float ref1V;
};

namespace absDisplay {

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

constexpr int W = 480, H = 320;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t BG = rgb(0x06, 0x09, 0x0e), EDGE = rgb(0x43, 0x54, 0x6b), TRACK = rgb(0x1b, 0x24, 0x30);
constexpr uint16_t TEXT = rgb(0xf2, 0xf7, 0xfc), MUTED = rgb(0xac, 0xbd, 0xd1), ACCENT = rgb(0x66, 0xd4, 0xf1);
constexpr uint16_t AMBER = rgb(0xff, 0xbd, 0x64), ORANGE = rgb(0xff, 0x9b, 0x36), RED = rgb(0xff, 0x4d, 0x4d);
constexpr uint16_t GREEN = rgb(0x42, 0xd9, 0x7c), LAMP_OFF = rgb(0x23, 0x30, 0x44), LAMP_RING = rgb(0x63, 0x73, 0x8a);

Panel lcd;
// The gauge is drawn off-screen in bands and pushed with DMA; the bands
// alternate between two buffers and each reuse waits for the DMA first.
LGFX_Sprite gaugeBand[2] = {LGFX_Sprite(&lcd), LGFX_Sprite(&lcd)};

// Layout: title | gauge + speed bar on the left, tiles on the right | bottom row.
constexpr int AG_X = 6, AG_Y = 50, AG_W = 316, AG_H = 176, AG_BAND = 22;   // Gauge bands.
constexpr int AG_CX = 158, AG_CY = 128, AG_R = 122;                        // In band coordinates.
constexpr int AS_Y = 234, AS_H = 22, AS_X0 = 18, AS_LEN = 280;             // Front / rear bar sprite (x 6-322).
constexpr uint16_t WHEEL_COLOR[2] = {TEXT, GREEN};                          // Rear white, front green.
constexpr int AT_X = 330, AT_W = 146, AT_Y0 = 48, AT_H = 72, AT_STEP = 76; // Right tiles.
constexpr int AB_Y = 276, AB_H = 42;                                       // Bottom row.
constexpr int START_X = 150, START_W = 172, RESET_X = 330, RESET_W = 142;
const char* const TILES[3] = {"BATTERY", "CURRENT", "REF1"};

// Last text / colour drawn in each slot; a slot is redrawn only when it changes.
enum Slot : uint8_t { S_TILE0, S_STATE = 3, S_START, S_TITLE, S_BRAKE, S_BRAKE_TEXT, S_HZ, SLOT_COUNT };
char shown[SLOT_COUNT][24];
uint16_t shownColor[SLOT_COUNT];

float absKmh[2] = {-1, -1};  // Shown / output speed per Wheel; -1 forces the first draw.
bool outputOn = false;
bool wasTouched = false, wasRunning = false, selfTestActive = false;
uint32_t lastTapMs = 0, lastPressMs = 0, lastTouchPollMs = 0, lastGaugeMs = 0, lastTextMs = 0, selfTestStartMs = 0;
int32_t shownGaugeKey = -1;

bool changed(uint8_t slot, const char* text, uint16_t color) {
  if (!strcmp(shown[slot], text) && shownColor[slot] == color) return false;
  strlcpy(shown[slot], text, sizeof(shown[slot]));
  shownColor[slot] = color;
  return true;
}

// Transparent text, for the frame and sprites.
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
           uint16_t color, const char* text, int padPx) {
  if (!changed(slot, text, color)) return;
  lcd.setFont(font);
  lcd.setTextSize(1);
  lcd.setTextColor(color, BG);
  lcd.setTextDatum(datum);
  lcd.setTextPadding(padPx);
  lcd.drawString(text, x, y);
  lcd.setTextPadding(0);
}

uint16_t lerpColor(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int shift) { return uint8_t(((a >> shift) & 0xFF) + t * (int((b >> shift) & 0xFF) - int((a >> shift) & 0xFF))); };
  return rgb(ch(16), ch(8), ch(0));
}

inline float gaugeAngle(float kmh) { return PI + constrain(kmh, 0.0f, ABS_SPEED_MAX) / ABS_SPEED_MAX * PI; }

inline void gaugePolar(float kmh, float r, int oy, float& x, float& y) {
  const float a = gaugeAngle(kmh);
  x = AG_CX + cosf(a) * r;
  y = AG_CY - oy + sinf(a) * r;
}

// Tapered needle from the hub to the speed.
void drawNeedle(LGFX_Sprite& c, float kmh, int oy, uint16_t color) {
  float tx, ty;
  gaugePolar(kmh, AG_R - 20, oy, tx, ty);
  const float a = gaugeAngle(kmh), px = -sinf(a) * 5, py = cosf(a) * 5;
  const int cy = AG_CY - oy;
  c.fillTriangle(AG_CX + px, cy + py, AG_CX - px, cy - py, tx, ty, color);
}

// Front (green, left) and rear (white, right) needles and readouts; without
// the D8 sensor the front needle is hidden and its readout shows "--".
void renderGauge(const float* kmh, bool front) {
  if (!gaugeBand[0].getBuffer() || !gaugeBand[1].getBuffer()) return;
  char digits[2][6];
  for (uint8_t w = 0; w < 2; ++w) snprintf(digits[w], sizeof(digits[w]), "%d", int(lroundf(kmh[w])));
  if (!front) strcpy(digits[FRONT], "--");
  for (int oy = 0, band = 0; oy < AG_H; oy += AG_BAND, ++band) {
    LGFX_Sprite& c = gaugeBand[band & 1];
    c.fillScreen(BG);
    const int cy = AG_CY - oy;
    // Cyan arc to 210 km/h, amber above, ticks every 10 km/h, labels every 50.
    constexpr float AMBER_DEG = 180 + ABS_AMBER_KMH / ABS_SPEED_MAX * 180;
    c.fillArc(AG_CX, cy, AG_R, AG_R - 6, 180, AMBER_DEG, ACCENT);
    c.fillArc(AG_CX, cy, AG_R, AG_R - 6, AMBER_DEG, 360, AMBER);
    for (int i = 0; i <= int(ABS_SPEED_MAX) / 10; ++i) {
      const bool major = i % 5 == 0;
      float x1, y1, x2, y2;
      gaugePolar(i * 10, AG_R - 10, oy, x1, y1);
      gaugePolar(i * 10, major ? AG_R - 26 : AG_R - 19, oy, x2, y2);
      c.drawWideLine(x1, y1, x2, y2, major ? 2.0f : 1.2f, TEXT);
      if (major) {
        float tx, ty;
        gaugePolar(i * 10, AG_R - 44, oy, tx, ty);
        char text[4];
        snprintf(text, sizeof(text), "%d", i * 10);
        label(c, text, tx, ty, TEXT, textdatum_t::middle_center, &fonts::FreeSansBold9pt7b);
      }
    }
    // FRONT readout left of the hub, REAR right, km/h between them.
    for (uint8_t w = 0; w < 2; ++w) {
      const int x = w == FRONT ? AG_CX - 68 : AG_CX + 68;
      const uint16_t color = w == FRONT && !front ? MUTED : WHEEL_COLOR[w];
      label(c, w == FRONT ? "FRONT" : "REAR", x, cy + 16, color, textdatum_t::middle_center, &fonts::Font2);
      label(c, digits[w], x, cy + 47, color, textdatum_t::baseline_center, &fonts::FreeSansBold18pt7b);
    }
    label(c, "km/h", AG_CX, cy + 44, MUTED, textdatum_t::baseline_center, &fonts::Font2);
    // Front needle under the rear one, then the cyan hub.
    if (front) drawNeedle(c, kmh[FRONT], oy, WHEEL_COLOR[FRONT]);
    drawNeedle(c, kmh[REAR], oy, WHEEL_COLOR[REAR]);
    c.fillCircle(AG_CX, cy, 10, TEXT);
    c.fillCircle(AG_CX, cy, 7, ACCENT);
    lcd.waitDMA();  // The other buffer's transfer must end before this one starts.
    c.pushSprite(&lcd, AG_X, AG_Y + oy);
  }
}

// Read-only speed bars, F (front) on top and R (rear) below, each with a
// marker at its speed. The front bar stays empty without the D8 sensor.
void renderBar(const float* kmh, bool front) {
  LGFX_Sprite& c = gaugeBand[0];
  if (!c.getBuffer()) return;
  lcd.waitDMA();  // The last gauge band may still be sending from this buffer.
  c.fillScreen(BG);
  for (uint8_t w = 0; w < 2; ++w) {
    const int cy = w == FRONT ? 5 : 16;
    const bool shown = w == REAR || front;
    label(c, w == FRONT ? "F" : "R", 3, cy, shown ? WHEEL_COLOR[w] : MUTED, textdatum_t::middle_left, &fonts::Font0);
    c.fillRoundRect(AS_X0, cy - 2, AS_LEN, 5, 2, TRACK);
    if (!shown) continue;
    const int tx = AS_X0 + lroundf(constrain(kmh[w], 0.0f, ABS_SPEED_MAX) / ABS_SPEED_MAX * AS_LEN);
    if (tx > AS_X0) c.fillRoundRect(AS_X0, cy - 2, tx - AS_X0, 5, 2, ACCENT);
    c.fillCircle(tx, cy, 5, WHEEL_COLOR[w]);
    c.fillCircle(tx, cy, 3, ACCENT);
  }
  c.pushSprite(&lcd, AG_X, AS_Y);
}

void applyOutput() {
  static bool attached[2] = {};
  for (uint8_t w = 0; w < 2; ++w) {
    if (ABS_OUTPUT_PIN[w] < 0) continue;
    if (!attached[w]) {
      ledcSetup(ABS_LEDC_CHANNEL[w], 1000, 10);
      ledcAttachPin(ABS_OUTPUT_PIN[w], ABS_LEDC_CHANNEL[w]);
      attached[w] = true;
    }
    const float hz = outputOn ? absKmh[w] * ABS_HZ_PER_KMH : 0;
    ledcWriteTone(ABS_LEDC_CHANNEL[w], hz >= 1 ? uint32_t(lroundf(hz)) : 0);
  }
}

void drawFrame() {
  lcd.startWrite();
  lcd.fillScreen(BG);
  lcd.drawRoundRect(4, 2, W - 8, 42, 4, EDGE);
  label(lcd, "ABS TESTER", 14, 39, ACCENT, textdatum_t::baseline_left, &fonts::FreeSansBold24pt7b);
  lcd.drawRoundRect(AG_X - 2, AG_Y - 2, AG_W + 4, AG_H + 4, 4, EDGE);
  lcd.drawRoundRect(AG_X - 2, AS_Y - 2, AG_W + 4, 40, 4, EDGE);
  label(lcd, "0", AG_X + AS_X0, AS_Y + 36, TEXT, textdatum_t::bottom_center, &fonts::Font2);
  label(lcd, "250", AG_X + AS_X0 + AS_LEN, AS_Y + 36, TEXT, textdatum_t::bottom_center, &fonts::Font2);
  for (uint8_t i = 0; i < 3; ++i) {
    const int y = AT_Y0 + i * AT_STEP;
    lcd.drawRoundRect(AT_X, y, AT_W, AT_H, 4, EDGE);
    label(lcd, TILES[i], AT_X + 8, y + 6, TEXT, textdatum_t::top_left, &fonts::FreeSansBold9pt7b);
  }
  lcd.drawRoundRect(4, AB_Y, W - 8, AB_H, 4, EDGE);
  lcd.drawRoundRect(RESET_X, AB_Y + 4, RESET_W, AB_H - 8, 6, TEXT);
  lcd.drawRoundRect(RESET_X + 1, AB_Y + 5, RESET_W - 2, AB_H - 10, 5, TEXT);
  label(lcd, "RESET", RESET_X + RESET_W / 2, AB_Y + AB_H / 2, TEXT, textdatum_t::middle_center, &fonts::FreeSansBold12pt7b);
  lcd.endWrite();
  memset(shown, 0, sizeof(shown));
  memset(shownColor, 0, sizeof(shownColor));
  shownGaugeKey = -1;
}

// Power-on splash: an ABS warning lamp (a ring sweeping round "ABS", side
// brackets lighting up), the ABS TESTER title fading in and a loading bar with
// start-up steps. The needle sweep is the switch-on self-test in update().
void powerOnAnimation() {
  constexpr int RX = W / 2, RY = 100, R = 50;
  constexpr int BAR_X = 120, BAR_Y = 260, BAR_W = 240, BAR_H = 8;
  const char* const STEPS[4] = {"Starting display", "Speed = D7 Hz / 6, max 250 km/h", "Opening testbench link", "Ready"};
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
  // Lamp ring track with a clear centre for the badge.
  lcd.fillArc(RX, RY, R, R - 8, 0, 360, TRACK);
  lcd.fillCircle(RX, RY, R - 12, BG);
  lcd.drawRoundRect(BAR_X - 2, BAR_Y - 2, BAR_W + 4, BAR_H + 4, 5, EDGE);
  lcd.endWrite();

  float shownDeg = 270;
  int shownStep = -1;
  const uint32_t start = millis();
  for (uint32_t t = 0; t <= INTRO_MS; t = millis() - start) {
    const float p = float(t) / INTRO_MS;
    lcd.startWrite();
    // Ring sweeps clockwise from the top over the first 60 %, amber at the end.
    const float deg = 270 + 360 * min(1.0f, p / 0.6f);
    for (; shownDeg < deg; shownDeg += 4) {
      const uint16_t color = shownDeg - 270 >= 290 ? AMBER : ACCENT;
      lcd.fillArc(RX, RY, R, R - 8, shownDeg, min(shownDeg + 4, deg), color);
    }
    // Lamp brackets, the ABS badge and the title fade in.
    const float fade = constrain((p - 0.2f) / 0.4f, 0.0f, 1.0f);
    const uint16_t bracket = lerpColor(0x06090e, 0xffbd64, fade);
    lcd.fillArc(RX, RY, R + 18, R + 13, 140, 220, bracket);
    lcd.fillArc(RX, RY, R + 18, R + 13, 320, 400, bracket);
    label(lcd, "ABS", RX, RY, lerpColor(0x131d29, 0xffbd64, fade), textdatum_t::middle_center, &fonts::FreeSansBold18pt7b);
    label(lcd, "ABS TESTER", W / 2, 196, lerpColor(0x0d141e, 0x66d4f1, fade), textdatum_t::middle_center,
          &fonts::FreeSansBold24pt7b);
    label(lcd, "Wheel speed  |  Battery  |  Brake", W / 2, 230, lerpColor(0x0d141e, 0xacbdd1, fade),
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
      lcd.setTextPadding(BAR_W + 40);
      lcd.drawString(STEPS[step], W / 2, BAR_Y + 26);
      lcd.setTextPadding(0);
    }
    lcd.endWrite();
    delay(16);
  }
  delay(350);
  drawFrame();
}

void drawText(const AbsData& d, bool running, bool selfTest) {
  char text[24];
  const bool havePacket = d.link != AbsData::WAITING;
  for (uint8_t i = 0; i < 3; ++i) {
    uint16_t color = TEXT;
    if (i == 0 && d.batteryAvailable) {  // Battery and current are live with the key off.
      snprintf(text, sizeof(text), "%.1f V", d.batteryV);
      color = d.batteryV < 11.8f || d.batteryV > 15.0f ? ORANGE : d.batteryV >= 13.2f ? GREEN : TEXT;
    } else if (i == 1 && d.batteryAvailable) {
      snprintf(text, sizeof(text), "%.2f A", d.currentA);
    } else if (i < 2) {
      strcpy(text, !havePacket ? (i ? "-- A" : "-- V") : "n/a"); color = MUTED;
    } else if (havePacket && !d.references) { strcpy(text, "n/a"); color = MUTED; }
    else if (running) snprintf(text, sizeof(text), "%.1f V", d.ref1V);
    else { strcpy(text, "-- V"); color = MUTED; }
    const int y = AT_Y0 + i * AT_STEP;
    field(S_TILE0 + i, AT_X + AT_W - 8, y + AT_H - 6, textdatum_t::bottom_right, &fonts::FreeSansBold18pt7b,
          color, text, AT_W - 16);
  }
  // Output state on top, brake switch (D9) below it.
  field(S_STATE, 14, AB_Y + 13, textdatum_t::middle_left, &fonts::FreeSansBold9pt7b,
        outputOn ? GREEN : TEXT, outputOn ? "OUTPUT ON" : "OUTPUT OFF", 128);
  // The switch-on self-test lights the lamp as a lamp check.
  const bool braking = havePacket && d.brake, lit = braking || selfTest;
  if (changed(S_BRAKE, lit ? "1" : "0", lit ? RED : LAMP_OFF)) {
    lcd.fillCircle(20, AB_Y + 30, 5, lit ? RED : LAMP_OFF);
    lcd.drawCircle(20, AB_Y + 30, 5, lit ? RED : LAMP_RING);
  }
  field(S_BRAKE_TEXT, 32, AB_Y + 30, textdatum_t::middle_left, &fonts::Font2, selfTest ? AMBER : braking ? RED : MUTED,
        selfTest ? "LAMP CHECK" : braking ? "BRAKE ON" : "BRAKE OFF", 100);
  // The speed formula's input under the speed bar.
  // Each wheel's input frequency (speed = Hz / 6) under the bars.
  if (!running) strcpy(text, "F -- Hz | R -- Hz");
  else if (d.front) snprintf(text, sizeof(text), "F %.0f Hz | R %.0f Hz", d.frequencyHz[FRONT], d.frequencyHz[REAR]);
  else snprintf(text, sizeof(text), "F n/a | R %.0f Hz", d.frequencyHz[REAR]);
  field(S_HZ, AG_X + AS_X0 + AS_LEN / 2, AS_Y + 36, textdatum_t::bottom_center, &fonts::Font2,
        running ? MUTED : LAMP_RING, text, 200);
  // START turns into an amber STOP while the output runs.
  if (changed(S_START, outputOn ? "STOP" : "START", outputOn ? AMBER : ACCENT)) {
    lcd.fillRoundRect(START_X, AB_Y + 4, START_W, AB_H - 8, 6, outputOn ? AMBER : ACCENT);
    label(lcd, outputOn ? "STOP" : "START", START_X + START_W / 2, AB_Y + AB_H / 2, BG,
          textdatum_t::middle_center, &fonts::FreeSansBold12pt7b);
  }
  // Title corner: link problems first, then SIM (no output pin) or the output frequency.
  uint16_t color = AMBER;
  if (selfTest) { strcpy(text, "SELF TEST"); color = ACCENT; }
  else if (d.link == AbsData::WAITING) { strcpy(text, "WAITING"); color = RED; }
  else if (d.link == AbsData::STALE) { strcpy(text, "NO LINK"); color = RED; }
  else if (!d.keyOn) strcpy(text, "KEY OFF");
  else if (ABS_OUTPUT_SIM) strcpy(text, "SIM");
  else if (outputOn) strcpy(text, "OUT ON");  // The frequencies are under the bars.
  else strcpy(text, "OFF");
  field(S_TITLE, W - 14, 24, textdatum_t::middle_right, &fonts::FreeSansBold12pt7b, color, text, 130);
}

// START/STOP toggles the output; RESET turns it off.
void handleTouch(uint32_t now) {
  if (now - lastTouchPollMs < TOUCH_POLL_MS) return;
  lastTouchPollMs = now;
  int32_t x, y;
  const bool pressed = lcd.getTouch(&x, &y);
  if (pressed && !wasTouched && now - lastTapMs >= 250 && y >= AB_Y) {
    lastTapMs = now;
    if (x >= START_X && x < START_X + START_W) { outputOn = !outputOn; applyOutput(); }
    else if (x >= RESET_X && x < RESET_X + RESET_W) { outputOn = false; applyOutput(); }
  }
  // Short release debounce so a resistive panel's dropouts don't count as new taps.
  if (pressed) lastPressMs = now;
  wasTouched = pressed || (wasTouched && now - lastPressMs < 80);
}

// Calibration is shared with ESP32_LGFX_Touch_Test / ESP32_Touch_Games (same
// NVS key), so a panel calibrated there is reused. Hold the screen while the
// ESP32 resets to calibrate again.
void loadOrCalibrate() {
  Preferences store;
  uint16_t cal[8];
  store.begin("lgfxtouch", false);
  const bool have = store.getBytes("cal", cal, sizeof(cal)) == sizeof(cal);
  const bool force = digitalRead(PIN_TOUCH_IRQ) == LOW;
  if (have && !force) {
    lcd.setTouchCalibrate(cal);
  } else {
    lcd.fillScreen(BG);
    label(lcd, "Touch calibration", W / 2, H / 2 - 24, TEXT, textdatum_t::middle_center, &fonts::FreeSansBold12pt7b);
    if (force) {
      label(lcd, "Release the screen...", W / 2, H / 2 + 12, MUTED, textdatum_t::middle_center);
      const uint32_t start = millis();
      while (digitalRead(PIN_TOUCH_IRQ) == LOW && millis() - start < 5000) delay(10);
      delay(300);
      lcd.fillRect(0, H / 2 + 2, W, 20, BG);
    }
    label(lcd, "Tap the tip of the arrow in each corner", W / 2, H / 2 + 12, MUTED, textdatum_t::middle_center);
    lcd.calibrateTouch(cal, ACCENT, BG, 30);
    store.putBytes("cal", cal, sizeof(cal));
  }
  store.end();
}

void begin() {
  pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);
  lcd.init();
  lcd.setRotation(1);  // Landscape 480 x 320.
  lcd.setBrightness(255);
  loadOrCalibrate();
  for (auto& band : gaugeBand) {
    band.setColorDepth(16);
    band.createSprite(AG_W, AG_BAND);
  }
  powerOnAnimation();
}

// Call every loop.
void update(const AbsData& d) {
  const uint32_t now = millis();
  handleTouch(now);
  const bool running = d.link == AbsData::LIVE && d.keyOn;
  // Switch-on self-test: when live data first arrives (or the key turns on)
  // both needles and bars sweep 0 -> 250 -> 0 km/h with the lamp lit.
  if (running && !wasRunning) { selfTestActive = true; selfTestStartMs = now; }
  wasRunning = running;
  const uint32_t testMs = now - selfTestStartMs;
  if (selfTestActive && testMs >= SELF_TEST_MS) selfTestActive = false;
  const bool selfTest = selfTestActive;
  // The outputs follow the real speeds (also during the self-test), so they
  // are updated whenever a speed changes.
  bool speedChanged = false;
  for (uint8_t w = 0; w < 2; ++w) {
    const float kmh = running && (w == REAR || d.front) ? roundf(d.speedKmh[w]) : 0;
    if (kmh != absKmh[w]) { absKmh[w] = kmh; speedChanged = true; }
  }
  if (speedChanged) applyOutput();
  if (now - lastGaugeMs >= GAUGE_REFRESH_MS || selfTest) {
    lastGaugeMs = now;
    const float sweep = ABS_SPEED_MAX * sinf(PI * testMs / SELF_TEST_MS);  // Eases out and back.
    const float shownKmh[2] = {selfTest ? sweep : absKmh[REAR], selfTest ? sweep : absKmh[FRONT]};
    const bool front = selfTest || d.front;
    const int32_t key = int32_t(lroundf(shownKmh[REAR])) | int32_t(lroundf(shownKmh[FRONT])) << 9 |
                        int32_t(selfTest) << 18 | int32_t(front) << 19;
    if (key != shownGaugeKey) {
      shownGaugeKey = key;
      lcd.startWrite();
      renderGauge(shownKmh, front);
      renderBar(shownKmh, front);
      lcd.endWrite();
    }
  }
  if (now - lastTextMs < TEXT_REFRESH_MS) return;
  lastTextMs = now;
  lcd.startWrite();
  drawText(d, running, selfTest);
  lcd.endWrite();
}

}  // namespace absDisplay
