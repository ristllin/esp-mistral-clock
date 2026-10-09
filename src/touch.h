#ifndef CLOCK_TOUCH_H
#define CLOCK_TOUCH_H

#include "esp_err.h"

/* AXS5106L touch on I2C (BSP pins: SDA 18 / SCL 19, TP_INT 21, TP_RST 20),
 * registered as an LVGL pointer indev in glass (320x172 landscape)
 * coordinates. Also provides the `touch inject` console commands used to
 * walk the UI over serial without physical touch. */
esp_err_t touch_init(void);
void touch_register_commands(void);

#endif
