#define USE_HSPI_PORT

#include <Arduino.h>
#include <TFT_eSPI.h>

#define DCSBIOS_DEFAULT_SERIAL
#define DCSBIOS_DISABLE_SERVO
#include <DcsBios.h>

#define TFT_BL 40

// ── Assets ─────────────────────────────────────────
#include "hydPressBackground.h"
#include "hydPressureBackgroundGreen.h"

#include "hydPressNeedle1.h"
#include "hydPressNeedle2.h"

#include "hydPressNeedle1Green.h"
#include "hydPressNeedle2Green.h"

// ── Display constants ─────────────────────────────
const byte colorDepth = 16;
static const uint16_t W = 240, H = 240;

static volatile uint16_t instPnlDimmer = 65535;

// ── TFT & Sprites ─────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

TFT_eSprite gaugeBack(&tft);
TFT_eSprite needle1(&tft);
TFT_eSprite needle2(&tft);

// ── Backlight helper ──────────────────────────────
void setBrightness(uint8_t value)
{
  analogWrite(TFT_BL, value);
}

// ── DCS driven state ──────────────────────────────
static volatile uint16_t raw1 = 0;
static volatile uint16_t raw2 = 0;

static volatile bool dirty1 = true;
static volatile bool dirty2 = true;

static volatile bool useGreenBg = false;
static volatile bool dirtyBg = true;

static volatile uint8_t cockpitLightMode = 0;

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
void renderGauge(int16_t a1, int16_t a2, bool greenBg);
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

  // Update brightness immediately when mode changes
  if (cockpitLightMode == 0)
  {
    setBrightness(255);  // DAY
  }
  else
  {
    int brt = map(instPnlDimmer, 0, 65535, 0, 255);

    if (cockpitLightMode == 2)   // NVG
      brt = min(brt, 80);

    setBrightness(brt);
  }

  lastDcsMs = millis();
}
DcsBios::IntegerBuffer cockkpitLightModeSwBuffer(0x74c8, 0x0600, 9, onCockkpitLightModeSwChange);

// brightness from cockpit dimmer
void onInstPnlDimmerChange(unsigned int newValue)
{
  instPnlDimmer = newValue;

  if (cockpitLightMode == 0)
  {
    setBrightness(255);  // DAY ignores knob
    return;
  }

  int brt = map(newValue, 0, 65535, 0, 255);

  if (cockpitLightMode == 2)   // NVG
    brt = min(brt, 80);

  setBrightness(brt);
}
DcsBios::IntegerBuffer instPnlDimmerBuffer(0x7546, 0xffff, 0, onInstPnlDimmerChange);

// ── Setup ─────────────────────────────────────────
void setup()
{
  Serial.begin(115200);

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

  // Needle 1
  needle1.setColorDepth(colorDepth);
  needle1.createSprite(33, 120);
  needle1.setSwapBytes(true);
  needle1.setPivot(17, 103);

  // Needle 2
  needle2.setColorDepth(colorDepth);
  needle2.createSprite(33, 120);
  needle2.setSwapBytes(true);
  needle2.setPivot(17, 103);

  renderGauge(map_hyd(raw1), map_hyd(raw2), useGreenBg);

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

    dirty1 = false;
    dirty2 = false;
    dirtyBg = false;

    interrupts();

    renderGauge(map_hyd(r1), map_hyd(r2), green);

    lastFrameMs = now;
  }
}

// ── Rendering ─────────────────────────────────────
void renderGauge(int16_t angle1, int16_t angle2, bool greenBg)
{
  gaugeBack.fillSprite(TFT_BLACK);

  if (greenBg)
  {
    gaugeBack.pushImage(0, 0, W, H, hydPressureBackgroundGreen);

    needle1.pushImage(0, 0, 33, 120, hydPressNeedle1Green);
    needle2.pushImage(0, 0, 33, 120, hydPressNeedle2Green);
  }
  else
  {
    gaugeBack.pushImage(0, 0, W, H, hydPressBackground);

    needle1.pushImage(0, 0, 33, 120, hydPressNeedle1);
    needle2.pushImage(0, 0, 33, 120, hydPressNeedle2);
  }

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

  bool green = true;

  // Needle 1 sweep
  for (int i = 0; i <= 320; i += 5)
  {
    renderGauge(map(i, 0, 320, -280, 40), 80, green);
    delay(5);
  }

  for (int i = 320; i >= 0; i -= 5)
  {
    renderGauge(map(i, 0, 320, -280, 40), 80, green);
    delay(5);
  }

  // Needle 2 sweep
  for (int i = 0; i <= 320; i += 5)
  {
    renderGauge(80, map(i, 0, 320, -280, 40), green);
    delay(5);
  }

  for (int i = 320; i >= 0; i -= 5)
  {
    renderGauge(80, map(i, 0, 320, -280, 40), green);
    delay(5);
  }
}