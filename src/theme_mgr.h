#ifndef CLOCK_THEME_MGR_H
#define CLOCK_THEME_MGR_H

#include <stdint.h>
#include "esp_err.h"

/* Theme manager: dark / light / mid / auto. Each theme is a designed
 * Mistral-warm palette (not just an inversion). The mode is persisted in
 * NVS and applied to every screen. Auto resolves to light by day, dark by
 * night and mid within 45 min of sunrise/sunset, from the weather data's sun
 * times (fallback: fixed 07:00-19:00 day window). */

typedef struct {
    uint32_t bg;      /* screen background */
    uint32_t panel;   /* buttons / cards */
    uint32_t text;    /* primary text */
    uint32_t dim;     /* secondary text */
    uint32_t accent;  /* Mistral orange (brand) */
    uint32_t red;     /* alert / unsynced */
    uint32_t yellow;  /* Mistral yellow (highlights) */
} theme_palette_t;

typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT = 1,
    THEME_MID = 2,
    THEME_AUTO = 3,
} theme_mode_t;

esp_err_t theme_mgr_init(void);
void theme_mgr_register_commands(void);

esp_err_t theme_mgr_set(theme_mode_t mode);   /* persist + apply */
theme_mode_t theme_mgr_mode(void);
theme_mode_t theme_mgr_resolved(void);        /* what auto currently picks */
const char *theme_mgr_mode_name(void);        /* "dark" "light" "mid" "auto" */
void theme_mgr_colors(const theme_palette_t **out);
void theme_mgr_recheck(void);                 /* re-evaluate auto now */

#endif
