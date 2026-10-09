#ifndef CLOCK_UI_COMMON_H
#define CLOCK_UI_COMMON_H

#include "lvgl.h"
#include "theme_mgr.h"

/* Shared LVGL building blocks for the screens (home, settings, location
 * picker, screensaver). Every function expects the caller to hold the LVGL
 * lock. New objects take the active theme's colours; each screen's
 * *_apply_theme() recolours them when the theme changes. */

/* Mistral pixel-M logo, 21x15 RGB565A8 */
extern const lv_image_dsc_t ui_logo_dsc;

/* Full-screen base: theme background, opaque, no padding, not scrollable.
 * scr == NULL creates a new screen, otherwise the given one is styled. */
lv_obj_t *ui_screen_base(lv_obj_t *scr);

/* px8 label at a top-left position */
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, uint32_t color,
                   int x, int y);

/* "Chip" button: panel fill, 1 px accent border, radius 2, with a centred
 * px8 label in the accent colour (returned in *label). */
lv_obj_t *ui_chip_button(lv_obj_t *parent, int x, int y, int w, int h,
                         const char *text, lv_event_cb_t cb,
                         lv_obj_t **label);
void ui_chip_recolor(lv_obj_t *btn, lv_obj_t *label, const theme_palette_t *p);

/* Sub-screen header: logo + title top-left, BACK chip top-right. */
typedef struct {
    lv_obj_t *title;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;
} ui_header_t;

void ui_header_create(ui_header_t *h, lv_obj_t *scr, const char *title,
                      lv_event_cb_t back_cb);
void ui_header_recolor(const ui_header_t *h, const theme_palette_t *p);

#endif
