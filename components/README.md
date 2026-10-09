# Vendored board support components

These ESP-IDF components come from the demo firmware that Waveshare publishes
for the **ESP32-C6-Touch-LCD-1.47** board
([wiki](https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47)).

| Component | Contents | Copyright |
|-----------|----------|-----------|
| `esp_bsp/` | Board pin map plus SPI bus, I2C bus, LCD backlight (LEDC) and touch init helpers | Waveshare (no licence notice in the demo files) |
| `esp_lcd_jd9853/` | `esp_lcd` panel driver for the JD9853 LCD controller | Espressif Systems (Shanghai) CO LTD |
| `esp_lcd_touch_axs5106/` | `esp_lcd_touch` driver for the AXS5106L touch controller | Espressif Systems (Shanghai) CO LTD |

## Local modifications

- Code is unchanged from the demo.
- Removed the demo helpers this firmware does not use from `esp_bsp/`
  (`bsp_battery`, `bsp_qmi8658`, `bsp_sdcard`, `bsp_wifi`) and dropped the
  matching `REQUIRES` entries from `esp_bsp/CMakeLists.txt`.
- Added the Espressif SPDX header from the driver's public header to
  `esp_lcd_touch_axs5106.c`, and replaced one full-width punctuation mark in
  a comment with ASCII.

## License

The JD9853 and AXS5106L drivers are Apache License 2.0, as stated in their
Espressif headers (<https://www.apache.org/licenses/LICENSE-2.0>). The
`esp_bsp/` helpers are Waveshare's demo code, which is published for use with
their boards without a separate licence notice; they are redistributed here
unchanged apart from the removals listed above.
The rest of this repository is covered by the top-level `LICENSE.md`.
