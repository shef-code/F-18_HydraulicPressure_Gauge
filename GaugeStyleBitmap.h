// ─────────────────────────────────────────────────────────────────────────────
// Gauge style B: BITMAP
//
// Photo-real dial face and needles rendered offline by tools/render_bitmap_assets.ps1
// and stored as RGB565 arrays in assets_bitmap/. More detail than the procedural
// style, at the cost of ~230 KB of flash for the two dial faces.
//
// Same style interface as GaugeStyleProcedural.h.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <TFT_eSPI.h>

#include "assets_bitmap/faceDay.h"
#include "assets_bitmap/faceGreen.h"
#include "assets_bitmap/needle1Day.h"
#include "assets_bitmap/needle1Green.h"
#include "assets_bitmap/needle2Day.h"
#include "assets_bitmap/needle2Green.h"

namespace bmpstyle
{
  static bool greenTheme = false;

  static void styleInit(TFT_eSPI*) {}

  static void styleBuildTheme(TFT_eSprite& n1, TFT_eSprite& n2, bool green)
  {
    greenTheme = green;
    n1.pushImage(0, 0, 33, 120, green ? bmpNeedle1Green : bmpNeedle1Day);
    n2.pushImage(0, 0, 33, 120, green ? bmpNeedle2Green : bmpNeedle2Day);
  }

  static inline void styleDrawFace(TFT_eSprite& dst)
  {
    dst.pushImage(0, 0, 240, 240, greenTheme ? bmpFaceGreen : bmpFaceDay);
  }
}

using bmpstyle::styleInit;
using bmpstyle::styleBuildTheme;
using bmpstyle::styleDrawFace;
