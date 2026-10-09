# Asset provenance and licences

## Fonts (SIL OFL 1.1, full text in `fonts/OFL.txt`)

| Asset | Upstream | Author | Licence |
|---|---|---|---|
| `fonts/PressStart2P-Regular.ttf` | Google Fonts, "Press Start 2P" | Cody "CodeMan38" Boisclair | SIL OFL 1.1 (Reserved Font Name: "Press Start 2P") |
| `fonts/Silkscreen-Regular.ttf` | Google Fonts, "Silkscreen" | Jason Kottke | SIL OFL 1.1 (Reserved Font Name: "Silkscreen") |

The TTFs are redistributed unmodified. The LVGL bitmap fonts in `src/fonts/`
are conversions of these TTFs made with the LVGL font converter
(`lv_font_conv`); the exact command line is in the header comment of each
generated file. Per OFL 1.1 the converted files remain under the OFL and keep
the reserved font names; no glyph designs were altered.

## Artwork (all original)

All pixel art in `src/assets/` is original, drawn for this project as
character grids in the `tools/gen_*.py` generators (run them to regenerate;
the generated headers say "do not edit by hand"):

- `mistral_logo.h` (`tools/gen_pixel_art.py`): a low-resolution pixel homage
  to the Mistral AI "M" mark (7x5 cells, five yellow-to-red bands).
- `weather_icons.h` (`tools/gen_pixel_art.py`): 16x16 weather icons (sun,
  moon, cloud, partly cloudy, rain, drizzle, snow, storm, fog) on the same
  warm palette, legible on every theme.
- `scene_art.h` (`tools/gen_sprite_art.py`): skyline, hills, window and
  lightning layers for the home and screensaver scenes.
- `chaton.h` (`tools/gen_chaton.py`): the screensaver and window-sill cat,
  redrawn pixel by pixel after the Mistral pixel cat, plus original props
  (umbrella, scarf, bow tie, sunglasses, yarn, croissant, pixel M).
- `clock_digits.h` (`tools/gen_clock_digits.py`): the large clock digits.

`mistral/MISTRAL_M_GRID.md` documents how the "M" grid was traced from the
public 2025 logo; no Mistral AI logo file is included in this repository.

Mistral AI's names, logo and cat are marks of Mistral AI. The artwork here is
a non-commercial homage; this project is not affiliated with or endorsed by
Mistral AI.

## Media and hardware

The recordings in `docs/media/` and `docs/demo.gif` show this firmware's own
rendering. The case in `hardware/` and its photos were made for this
project. All of them are covered by the top-level `LICENSE.md`.

## Third-party code and data

- `components/esp_lcd_jd9853` and `components/esp_lcd_touch_axs5106` are
  Espressif drivers (Apache-2.0) and `components/esp_bsp` is board support
  code, all from Waveshare's demo for the ESP32-C6-Touch-LCD-1.47; see
  `components/README.md`.
- ESP-IDF, LVGL, esp_lvgl_port and esp_lcd_touch are fetched at build time at
  the versions in `dependencies.lock` and keep their own licences.
- Weather data from Open-Meteo (CC BY 4.0) and METAR observations from
  aviationweather.gov are fetched at runtime; no data is redistributed.
