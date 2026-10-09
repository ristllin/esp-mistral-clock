#ifndef CLOCK_UI_HOME_H
#define CLOCK_UI_HOME_H

#include "lvgl.h"
#include "theme_mgr.h"

/* Home screen: time, date | location, current condition and temperature,
 * a 3-day forecast with pixel icons and a settings button. Shows the "time
 * not synced" state (red --:-- + hint) until SNTP syncs; the clock module
 * drives the time and the weather module feeds conditions. */
void ui_home_build(void);
lv_obj_t *ui_home_screen(void);
void ui_home_apply_theme(const theme_palette_t *p);

/* Called by the clock module every second */
void ui_home_set_time(const char *hhmm, const char *date, bool synced);

/* Called by the weather module when its state changes (fetch, cache load,
 * offline transition, location change); the home screen pulls the data. */
void ui_home_weather_changed(void);

#endif
