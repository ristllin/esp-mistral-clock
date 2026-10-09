/* Location picker: "Auto" plus a flat list of preset cities. All 11 rows
 * fit the 172 px height without scrolling (13 px pitch), so the list is
 * operable with plain taps, no drag-scroll needed. */
#include <stdio.h>
#include <stdint.h>

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "fonts/fonts.h"
#include "weather_mgr.h"
#include "theme_mgr.h"
#include "ui_settings.h"
#include "ui_loc.h"
#include "scene.h"
#include "ui_common.h"

typedef struct { const char *name; float lat, lon; } city_t;

static const city_t s_cities[] = {
    { "London",    51.5074f,    -0.1278f },
    { "Paris",     48.8566f,      2.3522f },
    { "Berlin",    52.5200f,     13.4050f },
    { "Madrid",    40.4168f,     -3.7038f },
    { "Rome",      41.9028f,     12.4964f },
    { "Amsterdam", 52.3676f,      4.9041f },
    { "Dublin",    53.3498f,     -6.2603f },
    { "Edinburgh", 55.9533f,     -3.1883f },
    { "New York",  40.7128f,    -74.0060f },
    { "Tokyo",     35.6762f,    139.6503f },
};
#define NCITIES ((int)(sizeof(s_cities) / sizeof(s_cities[0])))
/* row 0 is "Auto (from internet)", rows 1..NCITIES the cities */
#define NROWS (NCITIES + 1)

static lv_obj_t *s_scr;
static ui_header_t s_hdr;
static lv_obj_t *s_btn[NROWS];
static lv_obj_t *s_lbl[NROWS];

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_settings_show();
}

static void city_btn_cb(lv_event_t *e)
{
    int row = (int)(intptr_t)lv_event_get_user_data(e);
    if (row == 0) {
        weather_mgr_set_auto();
    } else if (row > 0 && row < NROWS) {
        weather_mgr_set_location(s_cities[row - 1].lat, s_cities[row - 1].lon,
                                 s_cities[row - 1].name);
    } else {
        return;
    }
    ui_settings_show();
}

/* Row of the current setting: 0 = auto, 1..NCITIES = a city, -1 = a
 * location set on the console. */
static int current_row(void)
{
    if (weather_mgr_location_auto()) { return 0; }
    float lat, lon;
    char name[24];
    weather_mgr_location(&lat, &lon, name, sizeof(name));
    for (int i = 0; i < NCITIES; i++) {
        if (s_cities[i].lat == lat && s_cities[i].lon == lon) { return i + 1; }
    }
    return -1;
}

/* (Re)write the row labels, marking the current setting with '>'. The
 * colours (current row in the accent colour) are set by apply_theme. */
static void mark_current_locked(void)
{
    int cur = current_row();
    for (int r = 0; r < NROWS; r++) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%s%s", r == cur ? "> " : "  ",
                 r == 0 ? "Auto (from internet)" : s_cities[r - 1].name);
        lv_label_set_text(s_lbl[r], buf);
    }
}

static void build(void)
{
    s_scr = ui_screen_base(NULL);
    ui_header_create(&s_hdr, s_scr, "LOCATION", back_btn_cb);

    /* rows (auto + cities): 304x12 at 13 px pitch from y=24 */
    for (int i = 0; i < NROWS; i++) {
        s_btn[i] = lv_button_create(s_scr);
        lv_obj_set_size(s_btn[i], 304, 12);
        lv_obj_align(s_btn[i], LV_ALIGN_TOP_LEFT, 8, 24 + i * 13);
        lv_obj_set_style_bg_opa(s_btn[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_btn[i], 0, 0);
        lv_obj_set_style_radius(s_btn[i], 2, 0);
        lv_obj_set_style_pad_all(s_btn[i], 0, 0);
        lv_obj_add_event_cb(s_btn[i], city_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        s_lbl[i] = lv_label_create(s_btn[i]);
        lv_obj_set_style_text_font(s_lbl[i], &font_px8, 0);
        lv_obj_align(s_lbl[i], LV_ALIGN_LEFT_MID, 8, 0);
    }
}

void ui_loc_apply_theme(const theme_palette_t *p)
{
    if (!s_scr) { return; }
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(p->bg), 0);
    ui_header_recolor(&s_hdr, p);
    int cur = current_row();
    for (int i = 0; i < NROWS; i++) {
        lv_obj_set_style_bg_color(s_btn[i], lv_color_hex(p->panel), 0);
        lv_obj_set_style_text_color(s_lbl[i],
            lv_color_hex(i == cur ? p->accent : p->text), 0);
    }
    lvgl_port_unlock();
}

void ui_loc_show(void)
{
    lvgl_port_lock(0);
    if (!s_scr) { build(); }
    mark_current_locked();
    /* re-apply the active theme (location may have changed while hidden) */
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    ui_loc_apply_theme(p);
    lv_screen_load(s_scr);
    scene_set_screen(SCENE_SCR_OTHER);
    lvgl_port_unlock();
}
