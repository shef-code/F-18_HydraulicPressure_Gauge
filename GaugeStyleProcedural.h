// ─────────────────────────────────────────────────────────────────────────────
// Gauge style A: PROCEDURAL
//
// The dial face and both needles are computed at boot / on theme change (analytic
// per-pixel shading with anti-aliasing), so no big image arrays live in flash.
// Day and NVG themes are just two palettes. There is no bezel: the dial runs to the
// edge of the display, which is meant to sit behind a real bezel.
//
// Layout (tick sizes, numeral/legend font sizes, needle size) follows the original
// artwork: 63 deg per 1000 psi, 0 psi at 80 deg, needle 19 px wide, 103 px long.
//
// Style interface used by the sketch:
//   styleInit(tft)                         one-time allocation
//   styleBuildTheme(needle1, needle2, g)   (re)build face + needle sprites for a theme
//   styleDrawFace(dst)                     copy the finished dial face into dst
//
// tools/preview_gauges.js is a line-for-line port of this file for desktop preview.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <TFT_eSPI.h>
#include <math.h>
#include "GaugeGlyphs.h"

namespace procstyle
{
  // ── Dial geometry (angles: degrees clockwise from 12 o'clock) ──────────────
  static const float CX = 120.0f, CY = 120.0f;
  static const float A0 = 80.0f;            // 0 psi
  static const float DEG_PER_K = 63.0f;     // per 1000 psi
  static const float MINOR_DEG = 12.6f;     // tick every 200 psi
  static const int   LAST_TICK = 25;        // 5000 psi
  static const float BAND0 = 240.0f, BAND1 = 287.0f;
  static const float DEG2RAD = 0.017453292519943f, RAD2DEG = 57.29577951308232f;

  // ── Palettes ───────────────────────────────────────────────────────────────
  struct Pal
  {
    float tint[3];
    float hubLo, hubHi;
    float mark[3], dialC[3], dialE[3];
    float glass;                            // reflection strength 0..1
    // needle
    float ptrA[3], ptrB[3], boxA[3], boxB[3], txt[3], edge[3];
  };

  static const Pal PAL_DAY = {
    {1.00f, 1.00f, 1.02f}, 60, 200,
    {238, 234, 222}, {34, 34, 36}, {8, 8, 9}, 1.0f,
    {255, 255, 252}, {168, 168, 164}, {52, 52, 54}, {18, 18, 19}, {240, 240, 232}, {120, 120, 118}
  };
  static const Pal PAL_NVG = {
    {0.55f, 0.62f, 0.55f}, 16, 90,
    {90, 255, 100}, {10, 16, 10}, {3, 6, 3}, 0.35f,
    {40, 255, 70}, {10, 150, 30}, {0, 28, 4}, {0, 12, 2}, {120, 255, 140}, {40, 220, 60}
  };

  static TFT_eSprite* faceCache = nullptr;

  // ── Small helpers ──────────────────────────────────────────────────────────
  static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
  static inline float cov(float d) { return clamp01(d + 0.5f); }            // signed inside-distance -> coverage
  static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

  static inline void mix(float* c, const float* to, float t)
  {
    c[0] = lerpf(c[0], to[0], t); c[1] = lerpf(c[1], to[1], t); c[2] = lerpf(c[2], to[2], t);
  }

  static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

  static inline uint16_t pack565(const float* c, int x, int y)
  {
    float d = (float)BAYER[(y & 3) * 4 + (x & 3)] - 7.5f;
    int r = (int)(c[0] + d * 0.5f), g = (int)(c[1] + d * 0.25f), b = (int)(c[2] + d * 0.5f);
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return v == TFT_TRANSPARENT ? (uint16_t)(v + 1) : v;
  }

  // Brushed-metal shading: bright where the surface faces the light direction
  static float metal(float th, float lightDeg, float lo, float hi)
  {
    float d = cosf((th - lightDeg) * DEG2RAD);
    float v = powf(0.5f + 0.5f * d, 1.6f);
    float spec = d > 0 ? powf(d, 24.0f) * 0.35f : 0.0f;
    return lo + (hi - lo) * v + 255.0f * spec;
  }

  // ── Glyph text (alpha atlas) ───────────────────────────────────────────────
  static const GGlyph* findGlyph(const GFont& f, char ch)
  {
    for (uint8_t i = 0; i < f.count; i++) if (f.glyphs[i].c == ch) return &f.glyphs[i];
    return nullptr;
  }

  static void blendPx(TFT_eSprite& s, int x, int y, const float* col, float a)
  {
    if (a <= 0.0f || x < 0 || y < 0 || x >= s.width() || y >= s.height()) return;
    uint16_t p = s.readPixel(x, y);
    float c[3] = {(float)(((p >> 11) & 31) * 255 / 31), (float)(((p >> 5) & 63) * 255 / 63), (float)((p & 31) * 255 / 31)};
    mix(c, col, a);
    s.drawPixel(x, y, pack565(c, x, y));
  }

  // Draws str centred on (cx, cy); cy is the vertical centre of the cap height.
  static void drawText(TFT_eSprite& s, const GFont& f, const char* str, float cx, float cy, const float* col)
  {
    int total = 0;
    for (const char* p = str; *p; p++) { const GGlyph* g = findGlyph(f, *p); if (g) total += g->adv; }
    int x = (int)floorf(cx - total * 0.5f + 0.5f);
    int base = (int)floorf(cy + f.capH * 0.5f + 0.5f);
    for (const char* p = str; *p; p++)
    {
      const GGlyph* g = findGlyph(f, *p);
      if (!g) continue;
      const uint8_t* d = f.data + g->off;
      for (int j = 0; j < g->h; j++)
        for (int i = 0; i < g->w; i++)
          blendPx(s, x + g->xo + i, base + g->yo + j, col, d[j * g->w + i] / 255.0f);
      x += g->adv;
    }
  }

  // ── Dial face ──────────────────────────────────────────────────────────────
  static void shadeFacePixel(int px, int py, const Pal& P, float* out)
  {
    float dx = px + 0.5f - CX, dy = py + 0.5f - CY;
    float r = sqrtf(dx * dx + dy * dy);
    if (r > 120.5f) { out[0] = out[1] = out[2] = 0; return; }

    float th = 0;
    if (r >= 84.0f || r <= 16.5f) { th = atan2f(dx, -dy) * RAD2DEG; if (th < 0) th += 360.0f; }

    // matte dial with a soft hot-spot up-left and a slight vignette toward the edge
    float c[3];
    float hx = px + 0.5f - (CX - 22.0f), hy = py + 0.5f - (CY - 26.0f);
    float t = clamp01(sqrtf(hx * hx + hy * hy) / 140.0f);
    c[0] = lerpf(P.dialC[0], P.dialE[0], t); c[1] = lerpf(P.dialC[1], P.dialE[1], t); c[2] = lerpf(P.dialC[2], P.dialE[2], t);
    if (r > 106.0f)
    {
      float f = 0.45f * clamp01((r - 106.0f) / 14.0f);
      c[0] *= 1 - f; c[1] *= 1 - f; c[2] *= 1 - f;
    }

    // normal-operating band
    if (r >= 90.0f && th >= BAND0 - 2 && th <= BAND1 + 2)
    {
      float a = fminf(th - BAND0, BAND1 - th) * DEG2RAD * r;
      mix(c, P.mark, cov(a) * cov(fminf(r - 91.0f, 108.5f - r)));
    }
    // tick marks: every 200 psi, major every 1000
    if (r >= 84.0f)
    {
      float trel = th - A0; if (trel < 0) trel += 360.0f;
      int idx = (int)floorf(trel / MINOR_DEG + 0.5f);
      if (idx <= LAST_TICK)
      {
        bool major = (idx % 5) == 0;
        float dist = fabsf(trel - idx * MINOR_DEG) * DEG2RAD * r;
        float w = major ? 3.5f : 2.0f, r0 = major ? 86.0f : 98.0f;
        mix(c, P.mark, cov(w * 0.5f - dist) * cov(fminf(r - r0, 109.0f - r)));
      }
    }

    // centre hub: thin machined ring around a black well
    if (r <= 16.5f)
    {
      float well[3] = {4, 4, 4};
      mix(c, well, cov(13.6f - r));
      float k = metal(th, 315.0f, P.hubLo, P.hubHi);
      float ring[3] = {k * P.tint[0], k * P.tint[1], k * P.tint[2]};
      mix(c, ring, cov(fminf(r - 13.6f, 15.6f - r)));
    }

    // glass reflection
    float clip = cov(120.0f - r);
    if (clip > 0)
    {
      float white[3] = {255, 255, 255};
      float x1 = dx, y1 = dy;                                       // crescent: circle 1 minus circle 2
      float r1 = sqrtf(x1 * x1 + y1 * y1);
      float x2 = x1 - 18.0f, y2 = y1 - 22.0f;
      float r2 = sqrtf(x2 * x2 + y2 * y2);
      float cres = cov(112.0f - r1) * (1.0f - cov(112.0f - r2));
      float gx = x1 + 100.0f, gy = y1 + 100.0f;                     // gradient (-100,-100) -> (0,0)
      float tg = clamp01((gx * 100.0f + gy * 100.0f) / (100.0f * 100.0f * 2.0f));
      float a1 = cres * (44.0f * P.glass / 255.0f) * (1.0f - tg);

      // streak: triangle (0,0) (44,-120) (88,-82), gradient (66,-100) -> (11,-22)
      float s[3][2] = {{0, 0}, {44, -120}, {88, -82}};
      float dmin = 1e9f;
      for (int e = 0; e < 3; e++)
      {
        float ax = s[e][0], ay = s[e][1], bx = s[(e + 1) % 3][0], by = s[(e + 1) % 3][1];
        float ex = bx - ax, ey = by - ay, len = sqrtf(ex * ex + ey * ey);
        float dd = (ex * (y1 - ay) - ey * (x1 - ax)) / len;       // positive on the right of the edge
        if (dd < dmin) dmin = dd;
      }
      float ts = clamp01(((x1 - 66.0f) * (-55.0f) + (y1 + 100.0f) * 78.0f) / (55.0f * 55.0f + 78.0f * 78.0f));
      float a2 = cov(dmin) * (12.0f * P.glass / 255.0f) * (1.0f - ts);
      mix(c, white, clip * (a1 + a2 - a1 * a2));
    }
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
  }

  static void buildFace(TFT_eSprite& s, const Pal& P)
  {
    float c[3];
    for (int y = 0; y < 240; y++)
      for (int x = 0; x < 240; x++)
      {
        shadeFacePixel(x, y, P, c);
        s.drawPixel(x, y, pack565(c, x, y));
      }
    // numerals 0..5 and legends
    static const char* NUM[6] = {"0", "1", "2", "3", "4", "5"};
    for (int k = 0; k <= 5; k++)
    {
      float th = (A0 + k * DEG_PER_K) * DEG2RAD;
      float nx = CX + 74.0f * sinf(th), ny = CY - 74.0f * cosf(th);
      if (k == 1) { nx = 163.0f; ny = 191.0f; }   // 1 and 2 sit lower, clear of "PSI X 1000" and the ticks
      if (k == 2) { nx = 96.0f;  ny = 190.0f; }
      drawText(s, fontNumerals, NUM[k], nx, ny, P.mark);
    }
    drawText(s, fontLegend, "HYD PRESS", 121.0f, 81.5f, P.mark);
    drawText(s, fontSmall, "PSI X 1000", 128.0f, 164.5f, P.mark);
  }

  // ── Needle (33x120 sprite, pivot (16,103)) ─────────────────────────────────
  // 19 px wide body, tapering to the tip 103 px from the pivot; the lower part is a
  // dark tag carrying the needle number. The body stops 16 px short of the pivot so
  // the hub ring stays visible.
  static void buildNeedle(TFT_eSprite& s, int num, const Pal& P)
  {
    for (int py = 0; py < 120; py++)
      for (int px = 0; px < 33; px++)
      {
        float x = px - 16.0f, y = py - 103.0f;         // pixel centre relative to pivot
        float ax = fabsf(x);
        float col[3] = {10, 10, 11};
        float alpha = 0;

        if (y > -104.5f && y < -14.5f)
        {
          float hw = fminf(9.5f, 0.5f + 0.4f * (y + 103.5f));
          alpha = cov(hw - ax) * cov(y + 103.5f) * cov(-15.5f - y);
          if (alpha > 0)
          {
            float tp = clamp01(ax / hw), tb = ax / 9.5f;
            float pc[3] = {lerpf(P.ptrA[0], P.ptrB[0], tp), lerpf(P.ptrA[1], P.ptrB[1], tp), lerpf(P.ptrA[2], P.ptrB[2], tp)};
            float bc[3] = {lerpf(P.boxA[0], P.boxB[0], tb), lerpf(P.boxA[1], P.boxB[1], tb), lerpf(P.boxA[2], P.boxB[2], tb)};
            float pw = cov(-49.0f - y);                  // 1 on the pointer, 0 on the tag
            mix(bc, P.edge, (1.0f - pw) * clamp01(0.85f - fabsf(ax - 9.5f) * 1.2f));
            for (int i = 0; i < 3; i++) col[i] = lerpf(10.0f, lerpf(bc[i], pc[i], pw), alpha);
          }
        }
        s.drawPixel(px, py, alpha < 0.35f ? (uint16_t)TFT_TRANSPARENT : pack565(col, px, py));
      }
    char tag[2] = {(char)('0' + num), 0};
    drawText(s, fontNumerals, tag, 16.5f, 103.5f - 32.0f, P.txt);
  }

  // ── Style interface ────────────────────────────────────────────────────────
  static void styleInit(TFT_eSPI* tft)
  {
    if (faceCache) return;
    faceCache = new TFT_eSprite(tft);
    faceCache->setColorDepth(16);
    if (!faceCache->createSprite(240, 240)) Serial.println(F("faceCache alloc failed"));
  }

  static void styleBuildTheme(TFT_eSprite& n1, TFT_eSprite& n2, bool green)
  {
    const Pal& P = green ? PAL_NVG : PAL_DAY;
    buildFace(*faceCache, P);
    buildNeedle(n1, 1, P);
    buildNeedle(n2, 2, P);
  }

  static inline void styleDrawFace(TFT_eSprite& dst)
  {
    faceCache->pushToSprite(&dst, 0, 0);
  }
}

using procstyle::styleInit;
using procstyle::styleBuildTheme;
using procstyle::styleDrawFace;
