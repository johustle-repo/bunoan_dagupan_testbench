#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Preferences.h>

// ABS tester screen on the 4.0" ST7796S TFT + XPT2046 touch panel (landscape
// 480 x 320), same panel and wiring as esp32-universal-v2:
//   title | 0-200 km/h gauge + speed bar | BATTERY / CURRENT / REF1 tiles |
//   output and brake state, START/STOP and RESET.
// The speed is the testbench D7 knob; only what an ABS unit depends on is
// shown, no engine sensors.
constexpr int8_t PIN_TFT_SCK = 18, PIN_TFT_MISO = 19, PIN_TFT_MOSI = 23;
constexpr int8_t PIN_TFT_CS = 27, PIN_TFT_DC = 25, PIN_TFT_RST = 26;
constexpr int8_t PIN_TOUCH_CS = 33, PIN_TOUCH_IRQ = 32;
constexpr uint32_t TFT_SPI_HZ = 27000000;
constexpr uint32_t GAUGE_REFRESH_MS = 50, TEXT_REFRESH_MS = 100, TOUCH_POLL_MS = 20;

// Wheel-speed output: a 50 % square wave whose frequency follows the speed,
// like a wheel-speed sensor on a tone ring. -1 = screen only (SIM).
// Free pins: GPIO4, 13, 14, 21, 22.
constexpr int8_t ABS_OUTPUT_PIN = -1;
constexpr uint8_t ABS_LEDC_CHANNEL = 4;
constexpr float ABS_TONE_TEETH = 48, ABS_WHEEL_CIRCUMFERENCE_M = 1.9f;
constexpr float ABS_HZ_PER_KMH = ABS_TONE_TEETH / ABS_WHEEL_CIRCUMFERENCE_M / 3.6f;  // About 7 Hz per km/h.
constexpr float ABS_SPEED_MAX = 200;

// What the screen shows, filled by the sketch each loop.
struct AbsData {
  enum Link : uint8_t { WAITING, LIVE, STALE } link;
  bool keyOn, brake;
  float speedKmh;
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
constexpr int AS_Y = 234, AS_H = 22, AS_X0 = 18, AS_LEN = 280;             // Speed bar sprite (x 6-322).
constexpr int AT_X = 330, AT_W = 146, AT_Y0 = 48, AT_H = 72, AT_STEP = 76; // Right tiles.
constexpr int AB_Y = 276, AB_H = 42;                                       // Bottom row.
constexpr int START_X = 150, START_W = 172, RESET_X = 330, RESET_W = 142;
const char* const TILES[3] = {"BATTERY", "CURRENT", "REF1"};

// Last text / colour drawn in each slot; a slot is redrawn only when it changes.
enum Slot : uint8_t { S_TILE0, S_STATE = 3, S_START, S_TITLE, S_BRAKE, S_BRAKE_TEXT, SLOT_COUNT };
char shown[SLOT_COUNT][24];
uint16_t shownColor[SLOT_COUNT];

float absKmh = -1;  // Shown / output speed; -1 forces the first draw.
bool outputOn = false;
bool wasTouched = false;
uint32_t lastTapMs = 0, lastPressMs = 0, lastTouchPollMs = 0, lastGaugeMs = 0, lastTextMs = 0;
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

inline float gaugeAngle(float kmh) { return PI + constrain(kmh, 0.0f, ABS_SPEED_MAX) / ABS_SPEED_MAX * PI; }

inline void gaugePolar(float kmh, float r, int oy, float& x, float& y) {
  const float a = gaugeAngle(kmh);
  x = AG_CX + cosf(a) * r;
  y = AG_CY - oy + sinf(a) * r;
}

// Above 200 km/h the needle stays at full scale and the digits keep counting.
void renderGauge(float kmh) {
  if (!gaugeBand[0].getBuffer() || !gaugeBand[1].getBuffer()) return;
  char digits[6];
  snprintf(digits, sizeof(digits), "%d", int(lroundf(kmh)));
  for (int oy = 0, band = 0; oy < AG_H; oy += AG_BAND, ++band) {
    LGFX_Sprite& c = gaugeBand[band & 1];
    c.fillScreen(BG);
    const int cy = AG_CY - oy;
    // Cyan arc to 170 km/h, amber above, ticks every 10 km/h, labels every 50.
    c.fillArc(AG_CX, cy, AG_R, AG_R - 6, 180, 333, ACCENT);
    c.fillArc(AG_CX, cy, AG_R, AG_R - 6, 333, 360, AMBER);
    for (int i = 0; i <= 20; ++i) {
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
    // Speed and unit centred as one group below the hub.
    c.setFont(&fonts::FreeSansBold24pt7b);
    const int dw = c.textWidth(digits);
    c.setFont(&fonts::FreeSansBold9pt7b);
    const int uw = c.textWidth("km/h");
    const int gx = AG_CX - (dw + 6 + uw) / 2;
    label(c, digits, gx, cy + 44, TEXT, textdatum_t::baseline_left, &fonts::FreeSansBold24pt7b);
    label(c, "km/h", gx + dw + 6, cy + 42, TEXT, textdatum_t::baseline_left, &fonts::FreeSansBold9pt7b);
    // Tapered white needle with a cyan hub.
    float tx, ty;
    gaugePolar(kmh, AG_R - 20, oy, tx, ty);
    const float a = gaugeAngle(kmh), px = -sinf(a) * 5, py = cosf(a) * 5;
    c.fillTriangle(AG_CX + px, cy + py, AG_CX - px, cy - py, tx, ty, TEXT);
    c.fillCircle(AG_CX, cy, 10, TEXT);
    c.fillCircle(AG_CX, cy, 7, ACCENT);
    lcd.waitDMA();  // The other buffer's transfer must end before this one starts.
    c.pushSprite(&lcd, AG_X, AG_Y + oy);
  }
}

// Read-only speed bar with a marker at the current speed.
void renderBar(float kmh) {
  LGFX_Sprite& c = gaugeBand[0];
  if (!c.getBuffer()) return;
  lcd.waitDMA();  // The last gauge band may still be sending from this buffer.
  c.fillScreen(BG);
  const int cy = AS_H / 2, tx = AS_X0 + lroundf(constrain(kmh, 0.0f, ABS_SPEED_MAX) / ABS_SPEED_MAX * AS_LEN);
  c.fillRoundRect(AS_X0, cy - 4, AS_LEN, 8, 4, TRACK);
  if (tx > AS_X0) c.fillRoundRect(AS_X0, cy - 4, tx - AS_X0, 8, 4, ACCENT);
  c.fillCircle(tx, cy, 7, TEXT);
  c.fillCircle(tx, cy, 5, ACCENT);
  c.pushSprite(&lcd, AG_X, AS_Y);
}

void applyOutput() {
  if (ABS_OUTPUT_PIN < 0) return;
  static bool attached = false;
  if (!attached) {
    ledcSetup(ABS_LEDC_CHANNEL, 1000, 10);
    ledcAttachPin(ABS_OUTPUT_PIN, ABS_LEDC_CHANNEL);
    attached = true;
  }
  const float hz = outputOn ? absKmh * ABS_HZ_PER_KMH : 0;
  ledcWriteTone(ABS_LEDC_CHANNEL, hz >= 1 ? uint32_t(lroundf(hz)) : 0);
}

void drawFrame() {
  lcd.startWrite();
  lcd.fillScreen(BG);
  lcd.drawRoundRect(4, 2, W - 8, 42, 4, EDGE);
  label(lcd, "ABS TESTER", 14, 39, ACCENT, textdatum_t::baseline_left, &fonts::FreeSansBold24pt7b);
  lcd.drawRoundRect(AG_X - 2, AG_Y - 2, AG_W + 4, AG_H + 4, 4, EDGE);
  lcd.drawRoundRect(AG_X - 2, AS_Y - 2, AG_W + 4, 40, 4, EDGE);
  label(lcd, "0", AG_X + AS_X0, AS_Y + 36, TEXT, textdatum_t::bottom_center, &fonts::Font2);
  label(lcd, "200", AG_X + AS_X0 + AS_LEN, AS_Y + 36, TEXT, textdatum_t::bottom_center, &fonts::Font2);
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

void drawText(const AbsData& d, bool running) {
  char text[16];
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
  const bool braking = havePacket && d.brake;
  if (changed(S_BRAKE, braking ? "1" : "0", braking ? RED : LAMP_OFF)) {
    lcd.fillCircle(20, AB_Y + 30, 5, braking ? RED : LAMP_OFF);
    lcd.drawCircle(20, AB_Y + 30, 5, braking ? RED : LAMP_RING);
  }
  field(S_BRAKE_TEXT, 32, AB_Y + 30, textdatum_t::middle_left, &fonts::Font2, braking ? RED : MUTED,
        braking ? "BRAKE ON" : "BRAKE OFF", 100);
  // START turns into an amber STOP while the output runs.
  if (changed(S_START, outputOn ? "STOP" : "START", outputOn ? AMBER : ACCENT)) {
    lcd.fillRoundRect(START_X, AB_Y + 4, START_W, AB_H - 8, 6, outputOn ? AMBER : ACCENT);
    label(lcd, outputOn ? "STOP" : "START", START_X + START_W / 2, AB_Y + AB_H / 2, BG,
          textdatum_t::middle_center, &fonts::FreeSansBold12pt7b);
  }
  // Title corner: link problems first, then SIM (no output pin) or the output frequency.
  uint16_t color = AMBER;
  if (d.link == AbsData::WAITING) { strcpy(text, "WAITING"); color = RED; }
  else if (d.link == AbsData::STALE) { strcpy(text, "NO LINK"); color = RED; }
  else if (!d.keyOn) strcpy(text, "KEY OFF");
  else if (ABS_OUTPUT_PIN < 0) strcpy(text, "SIM");
  else if (outputOn) snprintf(text, sizeof(text), "%.0f Hz", absKmh * ABS_HZ_PER_KMH);
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
  drawFrame();
}

// Call every loop.
void update(const AbsData& d) {
  const uint32_t now = millis();
  handleTouch(now);
  const bool running = d.link == AbsData::LIVE && d.keyOn;
  // The output follows the speed, so it is updated whenever the speed changes.
  const float kmh = running ? roundf(d.speedKmh) : 0;
  if (kmh != absKmh) { absKmh = kmh; applyOutput(); }
  if (now - lastGaugeMs >= GAUGE_REFRESH_MS) {
    lastGaugeMs = now;
    if (int32_t(absKmh) != shownGaugeKey) {
      shownGaugeKey = int32_t(absKmh);
      lcd.startWrite();
      renderGauge(absKmh);
      renderBar(absKmh);
      lcd.endWrite();
    }
  }
  if (now - lastTextMs < TEXT_REFRESH_MS) return;
  lastTextMs = now;
  lcd.startWrite();
  drawText(d, running);
  lcd.endWrite();
}

}  // namespace absDisplay
