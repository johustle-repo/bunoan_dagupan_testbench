#pragma once
#include <Arduino.h>
#include "AaFonts.h"

// Anti-aliased drawing into an RGB565 buffer that covers part of the screen
// and is then pushed to the TFT in one SPI burst. Every shape works out how
// much of each pixel it covers and blends with what is already in the buffer,
// so edges, arcs, needles and text are smooth on any background.
// All coordinates are screen coordinates; drawing is clipped to the buffer.
namespace aa {

struct Buf {
  uint16_t* px;
  int w, h;    // Buffer size.
  int ox, oy;  // Screen position of the buffer's top-left pixel.
};

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// fg over bg with coverage a (0-255).
inline uint16_t mix(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a == 0) return bg;
  if (a == 255) return fg;
  const int r = ((fg >> 11) * a + (bg >> 11) * (255 - a)) / 255;
  const int g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a)) / 255;
  const int b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
  return r << 11 | g << 5 | b;
}

inline void plot(Buf& b, int x, int y, uint16_t c, uint8_t a) {
  x -= b.ox;
  y -= b.oy;
  if (x < 0 || y < 0 || x >= b.w || y >= b.h || !a) return;
  uint16_t& p = b.px[y * b.w + x];
  p = mix(p, c, a);
}

// Coverage of a pixel whose centre is d pixels outside an edge (negative = inside).
inline uint8_t cover(float d) { return d <= -0.5f ? 255 : d >= 0.5f ? 0 : uint8_t((0.5f - d) * 255); }

// Clips a screen rectangle to the buffer; false if nothing is left.
inline bool clip(const Buf& b, int& x0, int& y0, int& x1, int& y1) {
  x0 = max(x0, b.ox); y0 = max(y0, b.oy);
  x1 = min(x1, b.ox + b.w); y1 = min(y1, b.oy + b.h);
  return x0 < x1 && y0 < y1;
}

void fill(Buf& b, uint16_t c) {
  for (int i = 0; i < b.w * b.h; ++i) b.px[i] = c;
}

void fillRect(Buf& b, int x, int y, int w, int h, uint16_t c) {
  int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  if (!clip(b, x0, y0, x1, y1)) return;
  for (int py = y0; py < y1; ++py)
    for (int px = x0; px < x1; ++px) b.px[(py - b.oy) * b.w + px - b.ox] = c;
}

// Vertical gradient from c0 (top) to c1 (bottom).
void gradient(Buf& b, int x, int y, int w, int h, uint16_t c0, uint16_t c1) {
  int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  if (!clip(b, x0, y0, x1, y1)) return;
  for (int py = y0; py < y1; ++py) {
    const uint16_t c = mix(c0, c1, h > 1 ? (py - y) * 255 / (h - 1) : 0);
    for (int px = x0; px < x1; ++px) b.px[(py - b.oy) * b.w + px - b.ox] = c;
  }
}

// Rounded rectangle with an optional border (borderW 0 = none). fill may be
// skipped with fillIt = false for an outline only.
void roundRect(Buf& b, float x, float y, float w, float h, float r, uint16_t fillC, uint16_t borderC = 0,
               float borderW = 0, bool fillIt = true) {
  int x0 = floorf(x), y0 = floorf(y), x1 = ceilf(x + w), y1 = ceilf(y + h);
  if (!clip(b, x0, y0, x1, y1)) return;
  const float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - r, hy = h / 2 - r;
  for (int py = y0; py < y1; ++py)
    for (int px = x0; px < x1; ++px) {
      const float qx = fabsf(px + 0.5f - cx) - hx, qy = fabsf(py + 0.5f - cy) - hy;
      const float ox = max(qx, 0.0f), oy = max(qy, 0.0f);
      const float d = (ox > 0 || oy > 0 ? sqrtf(ox * ox + oy * oy) : 0) + min(max(qx, qy), 0.0f) - r;
      if (d >= 0.5f) continue;
      uint16_t& p = b.px[(py - b.oy) * b.w + px - b.ox];
      if (borderW > 0) {
        p = mix(p, borderC, cover(d));
        if (fillIt) p = mix(p, fillC, cover(d + borderW));
      } else if (fillIt) {
        p = mix(p, fillC, cover(d));
      }
    }
}

void circle(Buf& b, float cx, float cy, float r, uint16_t c, uint8_t opacity = 255) {
  int x0 = floorf(cx - r - 1), y0 = floorf(cy - r - 1), x1 = ceilf(cx + r + 1), y1 = ceilf(cy + r + 1);
  if (!clip(b, x0, y0, x1, y1)) return;
  for (int py = y0; py < y1; ++py)
    for (int px = x0; px < x1; ++px) {
      const float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
      const uint8_t a = cover(sqrtf(dx * dx + dy * dy) - r);
      if (a) plot(b, px, py, c, a * opacity / 255);
    }
}

// Angle of a pixel around (cx, cy) in [pi/2, 5pi/2): left = pi, top = 3pi/2,
// right = 2pi (screen y points down), so a dial from the left over the top
// to the right is one continuous range.
inline float dialAngle(float dx, float dy) {
  float a = atan2f(dy, dx);
  if (a < HALF_PI) a += TWO_PI;
  return a;
}

// Ring segment between radii r0 and r1 from angle a0 to a1 (see dialAngle),
// coloured by color(angle). Both radii and both ends are anti-aliased.
template <class ColorFn>
void arc(Buf& b, float cx, float cy, float r0, float r1, float a0, float a1, ColorFn color, uint8_t opacity = 255) {
  if (a1 <= a0) return;
  int x0 = floorf(cx - r1 - 1), y0 = floorf(cy - r1 - 1), x1 = ceilf(cx + r1 + 1), y1 = ceilf(cy + r1 + 1);
  if (!clip(b, x0, y0, x1, y1)) return;
  const float in2 = (r0 - 1) * (r0 - 1), out2 = (r1 + 1) * (r1 + 1);
  for (int py = y0; py < y1; ++py)
    for (int px = x0; px < x1; ++px) {
      const float dx = px + 0.5f - cx, dy = py + 0.5f - cy, d2 = dx * dx + dy * dy;
      if (d2 < in2 || d2 > out2) continue;
      const float d = sqrtf(d2);
      uint8_t a = min(cover(d - r1), cover(r0 - d));
      if (!a) continue;
      const float ang = dialAngle(dx, dy);
      const float edge = max((a0 - ang) * d, (ang - a1) * d);  // Pixels outside the ends.
      a = min(a, cover(edge));
      if (a) plot(b, px, py, color(min(max(ang, a0), a1)), a * opacity / 255);
    }
}

// Tapered stroke from (x0, y0), width w0, to (x1, y1), width w1, round ends.
void stroke(Buf& b, float x0, float y0, float x1, float y1, float w0, float w1, uint16_t c, uint8_t opacity = 255) {
  const float pad = max(w0, w1) / 2 + 1;
  int bx0 = floorf(min(x0, x1) - pad), by0 = floorf(min(y0, y1) - pad);
  int bx1 = ceilf(max(x0, x1) + pad), by1 = ceilf(max(y0, y1) + pad);
  if (!clip(b, bx0, by0, bx1, by1)) return;
  const float vx = x1 - x0, vy = y1 - y0, len2 = max(vx * vx + vy * vy, 1e-6f);
  for (int py = by0; py < by1; ++py)
    for (int px = bx0; px < bx1; ++px) {
      const float qx = px + 0.5f - x0, qy = py + 0.5f - y0;
      const float t = constrain((qx * vx + qy * vy) / len2, 0.0f, 1.0f);
      const float ex = qx - t * vx, ey = qy - t * vy;
      const uint8_t a = cover(sqrtf(ex * ex + ey * ey) - (w0 + (w1 - w0) * t) / 2);
      if (a) plot(b, px, py, c, a * opacity / 255);
    }
}

// ---- Text ----

inline AaGlyph glyphOf(const AaFont& f, char ch) {
  AaGlyph g{};
  if (uint8_t(ch) < f.first || uint8_t(ch) > f.last) return g;
  memcpy_P(&g, &f.glyphs[uint8_t(ch) - f.first], sizeof(g));
  return g;
}

// Width of s in pixels; tracking adds space between letters.
int textWidth(const AaFont& f, const char* s, int tracking = 0) {
  int w = 0;
  for (const char* p = s; *p; ++p) w += glyphOf(f, *p).advance + (p[1] ? tracking : 0);
  return w;
}

enum Align : uint8_t { LEFT, CENTER, RIGHT };

// Draws s with its baseline at y, aligned on x. Returns the x after the text.
int text(Buf& b, const AaFont& f, const char* s, int x, int y, uint16_t c, Align align = LEFT, int tracking = 0) {
  if (align != LEFT) x -= align == CENTER ? textWidth(f, s, tracking) / 2 : textWidth(f, s, tracking);
  for (const char* p = s; *p; ++p) {
    const AaGlyph g = glyphOf(f, *p);
    const uint8_t* bits = f.bitmap + g.offset;
    const int rowBytes = (g.width + 1) / 2;
    for (int gy = 0; gy < g.height; ++gy)
      for (int gx = 0; gx < g.width; ++gx) {
        const uint8_t v = pgm_read_byte(bits + gy * rowBytes + gx / 2);
        const uint8_t a = gx & 1 ? v & 0x0F : v >> 4;
        if (a) plot(b, x + g.xOffset + gx, y + g.yOffset + gy, c, a * 17);
      }
    x += g.advance + (p[1] ? tracking : 0);
  }
  return x;
}

// Text centred on (x, y) both ways, using the font's cap height.
void textMid(Buf& b, const AaFont& f, const char* s, int x, int y, uint16_t c, int tracking = 0) {
  text(b, f, s, x, y + f.capHeight / 2, c, CENTER, tracking);
}

}  // namespace aa
