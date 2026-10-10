# F-18 Hydraulic Pressure Gauge

Hydraulic Pressure gauge in the Right AUX panel of the F-18, driven by DCS-BIOS on a Waveshare ESP32-S3-LCD-1.28 (240x240 round display).

This code is primarily based on source Tanarg posted in the DCS Forums.
It has been modified to work with the Waveshare ESP32-S3-LCD-1.28.
The original code was modified to prevent the continuous create/destroy of the sprites. DCS callbacks no longer trigger a re-write of the sprite directly.

<img width="281" height="280" alt="ESP32-S3-Touch-LCD-1 28-usenotes-03" src="https://github.com/user-attachments/assets/f2d18af4-637b-487d-b24f-f8862d516707" />

## Features

- Left and right hydraulic needles from DCS-BIOS (`HYD_IND_LEFT` / `HYD_IND_RIGHT`).
- DAY / NITE / NVG look from the cockpit light mode switch (NITE and NVG use the green dial).
- Backlight follows the **INST PNL** dimmer knob in every mode (never fully dark; NVG is capped).
- Dial and needles are drawn procedurally at boot and on a light-mode change, so the flash cost is small. Needles have a drop shadow and the dial has a subtle glass reflection. There is no bezel; the display is meant to sit in a real one.
- The dial uses the MS33558 instrument lettering. Glyphs are pre-rendered into [GaugeGlyphs.h](GaugeGlyphs.h).

Previews (rendered on a PC, not the device): [docs/preview/](docs/preview/).

## Files

| File | Purpose |
|------|---------|
| `F-18_HydraulicPressure_Gauge.ino` | Sketch: DCS-BIOS callbacks, brightness, render loop, boot test |
| `GaugeStyleProcedural.h` | Draws the dial face and needles |
| `GaugeGlyphs.h` | Generated glyph atlas (do not edit by hand) |
| `tools/render_glyph_atlas.ps1` | Regenerates `GaugeGlyphs.h` from MS33558 (needs the font installed; Windows PowerShell) |
| `tools/preview_gauges.js` | `node tools/preview_gauges.js` writes PNG previews to `docs/preview/` |

The face costs about 115 KB of RAM (a cached 240x240 sprite); no PSRAM is needed.

## Arduino IDE settings

Tested with the ESP32 Arduino core 3.3.x and TFT_eSPI 2.5.x:

- Board: ESP32S3 Dev Module
- Flash size 16 MB, partition scheme `app3M_fat9M_16MB`, PSRAM disabled
- USB CDC On Boot: **Disabled**, so `Serial` is the UART and DCS-BIOS works through the board's USB-serial chip
- DCS-BIOS opens the serial port at 250000 baud

CLI equivalent: `esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=disabled`

<img width="552" height="980" alt="image" src="https://github.com/user-attachments/assets/3482b5d7-2889-4fbc-9fee-45bccfe5c5c5" />

## TFT_eSPI library modifications

Additional changes are required for the display to render.
Included: TFT_eSPI support files.

Replace the `User_Setup_Select.h` file in the TFT_eSPI folder.<br>
Add `Custom_Waveshare_ESP32S31_28LCD.h` to the `User_Setups` folder.

After testing one of the touch screen editions of this display it would seem the pins are mapped a bit differently.
Give these a try if you need to:

```
#define LCD_DC_PIN      (8)
#define LCD_CS_PIN      (9)
#define LCD_CLK_PIN     (10)
#define LCD_MOSI_PIN    (11)
#define LCD_MISO_PIN    (12)
#define LCD_RST_PIN     (14)
#define LCD_BL_PIN      (2)
```
