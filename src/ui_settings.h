#ifndef CLOCK_UI_SETTINGS_H
#define CLOCK_UI_SETTINGS_H

#include "theme_mgr.h"

/* Settings screen: network, IP, signal, saved networks, location, theme,
 * screensaver timeout, firmware version + back button. Opened from the home screen's touch
 * button or the `settings` console command. */
void ui_settings_show(void);
void ui_settings_register_commands(void);
void ui_settings_apply_theme(const theme_palette_t *p);

#endif
