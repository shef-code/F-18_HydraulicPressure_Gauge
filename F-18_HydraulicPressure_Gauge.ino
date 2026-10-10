#define USE_HSPI_PORT

#include <Arduino.h>
#if ARDUINO_USB_MODE == 0
#include <USB.h>
#endif
#include <TFT_eSPI.h>

#define DCSBIOS_DEFAULT_SERIAL
#define DCSBIOS_DISABLE_SERVO
#include <DcsBios.h>

#define TFT_BL 40

// ── Gauge look ─────────────────────────────────────
// 1 = procedural (face + needles computed at boot, tiny flash footprint)
// 2 = bitmap     (pre-rendered photo-real assets in assets_bitmap/)
#define GAUGE_STYLE 1

#if GAUGE_STYLE == 1
  #include "GaugeStyleProcedural.h"
#elif GAUGE_STYLE == 2
  #include "GaugeStyleBitmap.h"
#else
  #error "GAUGE_STYLE must be 1 (procedural) or 2 (bitmap)"
#endif

// ── Display constants ─────────────────────────────
const byte colorDepth = 16;
static const uint16_t W = 240, H = 240;

static volatile uint16_t instPnlDimmer = 65535;

// ── TFT & Sprites ─────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

TFT_eSprite gaugeBack(&tft);
TFT_eSprite needle1(&tft);
TFT_eSprite needle2(&tft);
TFT_eSprite needleShadow(&tft);   // silhouette of the needle, drawn offset for depth

static const int16_t NEEDLE_W = 33, NEEDLE_H = 120, NEEDLE_PIVOT_X = 16, NEEDLE_PIVOT_Y = 103;
static const int16_t SHADOW_DX = 3, SHADOW_DY = 4;

// ── Backlight helper ──────────────────────────────
void setBrightness(uint8_t value)
{
  analogWrite(TFT_BL, value);
}

// ── DCS driven state ──────────────────────────────
static volatile uint8_t cockpitLightMode = 0;   // 0 DAY, 1 NITE, 2 NVG

// INST PNL knob sets the backlight in every mode (never fully dark); NVG is capped.
void applyBrightness()
{
  int brt = map(instPnlDimmer, 0, 65535, 20, 255);
  if (cockpitLightMode == 2) brt = min(brt, 80);
  setBrightness(brt);
}

static volatile uint16_t raw1 = 0;
static volatile uint16_t raw2 = 0;

static volatile bool dirty1 = true;
static volatile bool dirty2 = true;

static volatile bool useGreenBg = false;
static volatile bool dirtyBg = true;

static volatile uint32_t lastDcsMs = 0;

// ── Render scheduler ──────────────────────────────
static uint32_t lastFrameMs = 0;

static const uint32_t FRAME_INTERVAL_MS = 33;
static const uint32_t WATCHDOG_MS = 500;

// ── Mapping ───────────────────────────────────────
static inline int16_t map_hyd(uint16_t v)
{
  return map(v, 0, 65535, -280, 40);
}

// ── Forward declarations ──────────────────────────
void applyTheme(bool green);
void renderGauge(int16_t a1, int16_t a2);
void bitTest();

// ── DCS callbacks ─────────────────────────────────
void onHydIndLeftChange(unsigned int v)
{
  raw1 = v;
  dirty1 = true;
  lastDcsMs = millis();
}
DcsBios::IntegerBuffer hydIndLeftBuffer(0x751e, 0xffff, 0, onHydIndLeftChange);

void onHydIndRightChange(unsigned int v)
{
  raw2 = v;
  dirty2 = true;
  lastDcsMs = millis();
}
DcsBios::IntegerBuffer hydIndRightBuffer(0x7520, 0xffff, 0, onHydIndRightChange);

void onCockkpitLightModeSwChange(unsigned int newValue)
{
  cockpitLightMode = newValue;

  bool green = (newValue == 1) || (newValue == 2);

  useGreenBg = green;
  dirtyBg = true;

  applyBrightness();

  lastDcsMs = millis();
}
DcsBios::IntegerBuffer cockkpitLightModeSwBuffer(0x74c8, 0x0600, 9, onCockkpitLightModeSwChange);

// brightness from cockpit dimmer
void onInstPnlDimmerChange(unsigned int newValue)
{
  instPnlDimmer = newValue;
  applyBrightness();
  lastDcsMs = millis();
}
DcsBios::IntegerBuffer instPnlDimmerBuffer(0x7546, 0xffff, 0, onInstPnlDimmerChange);

// ── Setup ─────────────────────────────────────────
void setup()
{
#if ARDUINO_USB_MODE == 0
  // Name shown by DCS-TALK (needs Tools > USB Mode: USB-OTG (TinyUSB) and USB CDC On Boot: Enabled).
  USB.productName("HYD_PRESS");
  USB.manufacturerName("F18 Cockpit");
  USB.begin();
#endif
  // DCS-BIOS opens Serial at 250000 baud (~25 KB/s); a frame redraw takes tens of ms, so buffer generously.
  Serial.setRxBufferSize(4096);

  pinMode(TFT_BL, OUTPUT);
  setBrightness(255);

  DcsBios::setup();

  tft.init();
  tft.fillScreen(TFT_BLACK);

  // Background sprite
  gaugeBack.setColorDepth(colorDepth);
  gaugeBack.createSprite(W, H);
  gaugeBack.setSwapBytes(true);
  gaugeBack.setPivot(W / 2, H / 2);

  // Needles + shadow
  TFT_eSprite* needles[3] = {&needle1, &needle2, &needleShadow};
  for (TFT_eSprite* n : needles)
  {
    n->setColorDepth(colorDepth);
    if (!n->createSprite(NEEDLE_W, NEEDLE_H)) Serial.println(F("needle sprite alloc failed"));
    n->setSwapBytes(true);
    n->setPivot(NEEDLE_PIVOT_X, NEEDLE_PIVOT_Y);
  }

  styleInit(&tft);
  dirtyBg = false;
  applyTheme(useGreenBg);

  renderGauge(map_hyd(raw1), map_hyd(raw2));

  bitTest();
}

// ── Main loop ─────────────────────────────────────
void loop()
{
  DcsBios::loop();

  uint32_t now = millis();

  bool anyDirty = dirty1 || dirty2 || dirtyBg;

  bool frameDue = (now - lastFrameMs) >= FRAME_INTERVAL_MS;

  bool dcsActive = (now - lastDcsMs) < 2000;

  bool watchdog = dcsActive && ((now - lastFrameMs) >= WATCHDOG_MS);

  if ((anyDirty && frameDue) || watchdog)
  {
    noInterrupts();

    uint16_t r1 = raw1;
    uint16_t r2 = raw2;
    bool green = useGreenBg;
    bool themeChanged = dirtyBg;

    dirty1 = false;
    dirty2 = false;
    dirtyBg = false;

    interrupts();

    if (themeChanged) applyTheme(green);

    renderGauge(map_hyd(r1), map_hyd(r2));

    lastFrameMs = now;
  }
}

// ── Rendering ─────────────────────────────────────
// Rebuild the needle sprites (and, for the procedural style, the dial face) for a theme.
// Only needed when the cockpit light mode changes, not every frame.
void applyTheme(bool green)
{
  styleBuildTheme(needle1, needle2, green);

  // Needle silhouette for the drop shadow
  for (int y = 0; y < NEEDLE_H; y++)
    for (int x = 0; x < NEEDLE_W; x++)
      needleShadow.drawPixel(x, y, needle1.readPixel(x, y) == TFT_TRANSPARENT ? TFT_TRANSPARENT : TFT_BLACK);
}

void renderGauge(int16_t angle1, int16_t angle2)
{
  styleDrawFace(gaugeBack);

  // Shadows first, offset by moving the destination pivot
  gaugeBack.setPivot(W / 2 + SHADOW_DX, H / 2 + SHADOW_DY);
  needleShadow.pushRotated(&gaugeBack, angle1, TFT_TRANSPARENT);
  needleShadow.pushRotated(&gaugeBack, angle2, TFT_TRANSPARENT);
  gaugeBack.setPivot(W / 2, H / 2);

  needle1.pushRotated(&gaugeBack, angle1, TFT_TRANSPARENT);
  needle2.pushRotated(&gaugeBack, angle2, TFT_TRANSPARENT);

  gaugeBack.pushSprite(0, 0);
}

// ── BIT Test ──────────────────────────────────────
void bitTest()
{
  // Backlight fade test
  for (int b = 0; b <= 255; b += 5)
  {
    setBrightness(b);
    delay(8);
  }

  for (int b = 255; b >= 0; b -= 5)
  {
    setBrightness(b);
    delay(8);
  }

  setBrightness(255);

  // Needle 1 sweep
  for (int i = 0; i <= 320; i += 5)
  {
    renderGauge(map(i, 0, 320, -280, 40), 80);
    delay(5);
  }

  for (int i = 320; i >= 0; i -= 5)
  {
    renderGauge(map(i, 0, 320, -280, 40), 80);
    delay(5);
  }

  // Needle 2 sweep
  for (int i = 0; i <= 320; i += 5)
  {
    renderGauge(80, map(i, 0, 320, -280, 40));
    delay(5);
  }

  for (int i = 320; i >= 0; i -= 5)
  {
    renderGauge(80, map(i, 0, 320, -280, 40));
    delay(5);
  }
}