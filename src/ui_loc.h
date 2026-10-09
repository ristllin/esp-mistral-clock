#ifndef CLOCK_UI_LOC_H
#define CLOCK_UI_LOC_H

#include "lvgl.h"
#include "theme_mgr.h"

/* Location picker screen: preset city list, opened from the LOC row of the
 * settings screen. Picking a city persists it (NVS), drops the weather cache
 * (it belongs to the old location) and forces a refetch. */
void ui_loc_show(void);
void ui_loc_apply_theme(const theme_palette_t *p);

#endif
