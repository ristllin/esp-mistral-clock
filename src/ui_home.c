/* Home screen, "scene as hero" composition: the live weather/time scene
 * fills the whole panel and the chrome floats on it. Left masthead column: Mistral M + MISTRAL wordmark lockup, a big
 * sprite-digit clock (5x7 glyphs x4, each tile a solid plate in the scheme
 * outline colour carrying the glyph, so the digits stay solid and legible
 * over ANY scene), and the date line. Top-right: SETTINGS chip. Bottom-left:
 * ONE designed weather plate (temperature + humidity + condition, then a
 * 3-day forecast with pixel icons). No card grid, no per-label boxes.
 * Fonts: PS2P for the wordmark + chip + temperature, Silkscreen for the
 * small text (narrower, lighter: a real hierarchy). */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "fonts/fonts.h"
#include "weather_icons.h"
#include "clock_digits.h"
#include "theme_mgr.h"
#include "weather_mgr.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "ui_common.h"

#define COL_STROKE_D 0x1A120B  /* label outline, dark/mid themes */
#define COL_STROKE_L 0xFFFFFF  /* label outline, light theme */

static lv_obj_t *s_home_scr;
static lv_obj_t *s_wordmark;
static lv_obj_t *s_date_label;
static lv_obj_t *s_dig[4];            /* sprite digits HH MM */
static lv_obj_t *s_colon;
static lv_obj_t *s_btn;
static lv_obj_t *s_btn_label;
static lv_obj_t *s_plate;
static lv_obj_t *s_temp_label;
static lv_obj_t *s_cond_label;
static lv_obj_t *s_hum_label;
static lv_obj_t *s_divider;
static lv_obj_t *s_fc_day[3];
static lv_obj_t *s_fc_icon[3];
static lv_obj_t *s_fc_temp[3];
static lv_obj_t *s_fc_div[2];   /* hairlines between forecast columns */
static bool s_synced;
static bool s_blink;
static char s_date[24];
static char s_dig_ch[4];             /* last shown digit per slot */
static int s_dig_scheme = -1;

/* 16x16 RGB565A8 pixel icons (colour plane + alpha plane, 0x0000 pixels
 * are transparent and blend over anything beneath). */
#define ICON_DSC(nm, arr) \
    static const lv_image_dsc_t s_dsc_##nm = { \
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, \
                    .cf = LV_COLOR_FORMAT_RGB565A8, \
                    .w = 16, .h = 16, .stride = 32 }, \
        .data_size = 16 * 16 * 3, \
        .data = (const uint8_t *)(arr ## _a8), \
    }

ICON_DSC(cloud, weather_icons_cloud);
ICON_DSC(rain, weather_icons_rain);
ICON_DSC(drizzle, weather_icons_drizzle);
ICON_DSC(snow, weather_icons_snow);
ICON_DSC(storm, weather_icons_storm);
ICON_DSC(fog, weather_icons_fog);
ICON_DSC(sun, weather_icons_sun);
ICON_DSC(moon, weather_icons_moon);
ICON_DSC(partly, weather_icons_partly);

typedef struct { const char *name; const lv_image_dsc_t *dsc; } icon_ent_t;
static const icon_ent_t s_icons[] = {
    { "sun", &s_dsc_sun },       { "moon", &s_dsc_moon },
    { "cloud", &s_dsc_cloud },   { "partly", &s_dsc_partly },
    { "rain", &s_dsc_rain },     { "drizzle", &s_dsc_drizzle },
    { "snow", &s_dsc_snow },     { "storm", &s_dsc_storm },
    { "fog", &s_dsc_fog },
};

static const lv_image_dsc_t *icon_dsc(const char *name)
{
    for (size_t i = 0; i < sizeof(s_icons) / sizeof(s_icons[0]); i++) {
        if (!strcmp(s_icons[i].name, name)) { return s_icons[i].dsc; }
    }
    return &s_dsc_cloud;
}

static void settings_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_settings_show();
}

lv_obj_t *ui_home_screen(void)
{
    return s_home_scr;
}

/* Sprite-digit descriptors are built once from the generated table. */
static lv_image_dsc_t s_dig_dsc[CLOCK_DIG_SCHEMES][CLOCK_DIG_GLYPHS];
static bool s_dig_dsc_ready;

static void dig_dsc_init(void)
{
    if (s_dig_dsc_ready) { return; }
    for (int s = 0; s < CLOCK_DIG_SCHEMES; s++) {
        for (int g = 0; g < CLOCK_DIG_GLYPHS; g++) {
            const clock_dig_t *d = &clock_digits[s][g];
            lv_image_dsc_t *dd = &s_dig_dsc[s][g];
            dd->header.magic = LV_IMAGE_HEADER_MAGIC;
            dd->header.cf = LV_COLOR_FORMAT_RGB565A8;
            dd->header.w = d->w;
            dd->header.h = d->h;
            dd->header.stride = d->w * 2;
            dd->data_size = (uint32_t)d->w * d->h * 3;
            dd->data = d->a8;
        }
    }
    s_dig_dsc_ready = true;
}

/* clock_digits.h scheme order: 0 dark, 1 light, 2 mid, 3 red(dark bg),
 * 4 red(light bg). Unsynced shows red digits: an obvious state. */
static int dig_scheme(bool synced)
{
    theme_mode_t r = theme_mgr_resolved();
    if (!synced) { return (r == THEME_LIGHT) ? 4 : 3; }
    switch (r) {
    case THEME_LIGHT: return 1;
    case THEME_MID:   return 2;
    default:          return 0;
    }
}

/* Date line is "<date> | <location>"; rebuilt when either side changes. */
static void update_date_line_locked(void)
{
    char name[24];
    float lat, lon;
    weather_mgr_location(&lat, &lon, name, sizeof(name));
    char buf[56];
    snprintf(buf, sizeof(buf), "%s | %s", s_date, name);
    lv_label_set_text(s_date_label, buf);
}

void ui_home_set_time(const char *hhmm, const char *date, bool synced)
{
    if (!s_home_scr) { return; }
    lvgl_port_lock(0);
    s_synced = synced;
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    snprintf(s_date, sizeof(s_date), "%s", date);
    int scheme = dig_scheme(synced);
    if (scheme != s_dig_scheme) {
        s_dig_scheme = scheme;
        /* Re-key EVERY digit: slots 1-3 keep their old characters otherwise
         * and stay in the previous scheme (mixed-scheme clock after a
         * theme change while the time is static). */
        for (int i = 0; i < 4; i++) { s_dig_ch[i] = '\0'; }
        lv_image_set_src(s_colon, &s_dig_dsc[scheme][10]);
    }
    const char *show = synced ? hhmm : "--:--";
    static const int map[4] = { 0, 1, 3, 4 };   /* skip ':' at index 2 */
    for (int i = 0; i < 4; i++) {
        char c = show[map[i]];
        if (s_dig_ch[i] != c) {
            s_dig_ch[i] = c;
            int g = (c >= '0' && c <= '9') ? c - '0' : 11;   /* dash */
            lv_image_set_src(s_dig[i], &s_dig_dsc[scheme][g]);
        }
    }
    /* blink the colon once per call (~1 Hz tick) */
    s_blink = !s_blink;
    if (synced && !s_blink) {
        lv_obj_add_flag(s_colon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_colon, LV_OBJ_FLAG_HIDDEN);
    }
    if (synced) {
        lv_obj_set_style_text_color(s_date_label, lv_color_hex(p->dim), 0);
        update_date_line_locked();
    } else {
        lv_label_set_text(s_date_label, "time not synced");
        lv_obj_set_style_text_color(s_date_label, lv_color_hex(p->red), 0);
    }
    lvgl_port_unlock();
}

/* Pull model: the weather module calls this whenever its state changes
 * (fetch done, cache loaded at boot, offline transition, location change)
 * and the home screen reads the data itself. */
void ui_home_weather_changed(void)
{
    if (!s_home_scr) { return; }
    const weather_data_t *d = weather_mgr_data();
    lvgl_port_lock(0);
    update_date_line_locked();
    if (!d) {
        lv_label_set_text(s_temp_label, "--");
        lv_label_set_text(s_cond_label,
                          weather_mgr_is_online() ? "no data"
                                                  : "weather offline");
        lv_label_set_text(s_hum_label, "");
        for (int i = 0; i < 3; i++) {
            lv_label_set_text(s_fc_day[i], "");
            lv_label_set_text(s_fc_temp[i], "");
            lv_image_set_src(s_fc_icon[i], &s_dsc_cloud);
        }
        lvgl_port_unlock();
        return;
    }

    char temp[16], cond[48], hum[12];
    snprintf(temp, sizeof(temp), "%.1f\xC2\xB0" "C", (double)d->temp_c);

    int age = weather_mgr_age_min();
    bool online = weather_mgr_is_online();
    int eff = weather_mgr_eff_wmo();
    if (online) {
        snprintf(cond, sizeof(cond), "%s", weather_mgr_cond(eff));
    } else if (age >= 0) {
        if (age < 60) {
            snprintf(cond, sizeof(cond), "%s, %d min old",
                     weather_mgr_cond(eff), age);
        } else {
            snprintf(cond, sizeof(cond), "%s, %dh old",
                     weather_mgr_cond(eff), age / 60);
        }
    } else {
        snprintf(cond, sizeof(cond), "%s", weather_mgr_cond(eff));
    }
    snprintf(hum, sizeof(hum), "%d%% RH", d->humidity_pct);

    lv_label_set_text(s_temp_label, temp);
    lv_label_set_text(s_cond_label, cond);
    lv_label_set_text(s_hum_label, hum);

    /* forecast columns: day, pixel icon, "max/min". Day labels come from the
     * Open-Meteo daily.time dates, so they stay correct with an offline
     * cache from a previous day or a debug clock override. */
    for (int i = 0; i < 3; i++) {
        char day[8], tcol[16];
        if (!weather_mgr_daily_label(i, day, sizeof(day))) {
            snprintf(day, sizeof(day), "?");
        }
        for (char *c = day; *c; c++) {   /* pixel caps read better tiny */
            if (*c >= 'a' && *c <= 'z') { *c -= 32; }
        }
        snprintf(tcol, sizeof(tcol), "%.0f/%.0f",
                 (double)d->daily_max[i], (double)d->daily_min[i]);
        lv_label_set_text(s_fc_day[i], day);
        lv_label_set_text(s_fc_temp[i], tcol);
        lv_image_set_src(s_fc_icon[i],
                         icon_dsc(weather_mgr_icon(d->daily_code[i])));
    }
    lvgl_port_unlock();
}

/* Labels floating over the live scene get a per-glyph STROKE in a fixed
 * contrasting colour (NOT the theme bg: bg-coloured stroke pixels equal
 * the scene substitution key and would vanish into the scene). 1px is
 * enough next to the fill colour; plate labels pass stroke_w = 0. */
static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color,
                            lv_align_t align, int x, int y,
                            int stroke_w, uint32_t stroke_col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    if (stroke_w > 0) {
        lv_obj_set_style_text_outline_stroke_width(l, stroke_w, 0);
        lv_obj_set_style_text_outline_stroke_color(l,
                                                   lv_color_hex(stroke_col),
                                                   0);
        lv_obj_set_style_text_outline_stroke_opa(l, LV_OPA_COVER, 0);
    }
    lv_obj_align(l, align, x, y);
    return l;
}

static uint32_t stroke_for(void)
{
    return (theme_mgr_resolved() == THEME_LIGHT) ? COL_STROKE_L : COL_STROKE_D;
}

void ui_home_apply_theme(const theme_palette_t *p)
{
    if (!s_home_scr) { return; }
    uint32_t stroke = stroke_for();
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(s_home_scr, lv_color_hex(p->bg), 0);
    lv_obj_set_style_text_color(s_wordmark, lv_color_hex(p->accent), 0);
    lv_obj_set_style_text_outline_stroke_color(s_wordmark,
                                               lv_color_hex(stroke), 0);
    lv_obj_set_style_text_color(s_date_label,
                                lv_color_hex(s_synced ? p->dim : p->red), 0);
    lv_obj_set_style_text_outline_stroke_color(s_date_label,
                                               lv_color_hex(stroke), 0);
    /* the clock digits switch to the theme's sprite scheme on the next
     * 1 s tick: ui_home_set_time() re-keys them when the scheme changes */
    lv_obj_set_style_bg_color(s_plate, lv_color_hex(p->panel), 0);
    lv_obj_set_style_border_color(s_plate, lv_color_hex(p->dim), 0);
    lv_obj_set_style_bg_color(s_divider, lv_color_hex(p->dim), 0);
    lv_obj_set_style_text_color(s_temp_label, lv_color_hex(p->text), 0);
    lv_obj_set_style_text_color(s_cond_label, lv_color_hex(p->dim), 0);
    lv_obj_set_style_text_color(s_hum_label, lv_color_hex(p->dim), 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_text_color(s_fc_day[i], lv_color_hex(p->dim), 0);
        lv_obj_set_style_text_color(s_fc_temp[i], lv_color_hex(p->text), 0);
    }
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(s_fc_div[i], lv_color_hex(p->dim), 0);
    }
    ui_chip_recolor(s_btn, s_btn_label, p);
    lvgl_port_unlock();
}

void ui_home_build(void)
{
    dig_dsc_init();
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    lv_obj_t *scr = ui_screen_base(lv_screen_active());
    s_home_scr = scr;

    /* --- masthead lockup: gradient M + MISTRAL wordmark, top-left --- */
    lv_obj_t *logo = lv_image_create(scr);
    lv_image_set_src(logo, &ui_logo_dsc);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 8, 2);
    s_wordmark = make_label(scr, "MISTRAL", &font_px8, p->accent,
                            LV_ALIGN_TOP_LEFT, 34, 7, 1, COL_STROKE_D);
    lv_obj_set_style_text_letter_space(s_wordmark, 2, 0);

    /* --- hero clock: sprite digits, 24x32 tiles at y=20 --- */
    static const int dx[4] = { 8, 34, 74, 100 };
    for (int i = 0; i < 4; i++) {
        s_dig[i] = lv_image_create(scr);
        lv_image_set_src(s_dig[i], &s_dig_dsc[0][11]);   /* dash until 1st tick */
        lv_obj_align(s_dig[i], LV_ALIGN_TOP_LEFT, dx[i], 20);
        s_dig_ch[i] = '\0';
    }
    s_colon = lv_image_create(scr);
    lv_image_set_src(s_colon, &s_dig_dsc[0][10]);
    lv_obj_align(s_colon, LV_ALIGN_TOP_LEFT, 60, 20);
    s_dig_scheme = -1;   /* first ui_home_set_time keys everything */

    /* date line under the clock (Silkscreen: lighter, narrower) */
    s_date_label = make_label(scr, "time not synced", &font_silk8, p->dim,
                              LV_ALIGN_TOP_LEFT, 8, 56, 1, COL_STROKE_D);

    /* --- SETTINGS chip, top-right (clear of the moon at (244,28)) --- */
    s_btn = ui_chip_button(scr, 228, 4, 84, 20, "SETTINGS", settings_btn_cb,
                           &s_btn_label);

    /* --- weather plate, bottom-left: ONE designed plate, not per-label
     * boxes. Current weather left, 3-day forecast right. --- */
    s_plate = lv_obj_create(scr);
    lv_obj_set_size(s_plate, 232, 44);
    lv_obj_align(s_plate, LV_ALIGN_TOP_LEFT, 8, 124);
    lv_obj_set_style_bg_color(s_plate, lv_color_hex(p->panel), 0);
    lv_obj_set_style_bg_opa(s_plate, 224, 0); /* ~88%: scene shows through faintly */
    lv_obj_set_style_radius(s_plate, 4, 0);
    lv_obj_set_style_border_width(s_plate, 1, 0);
    lv_obj_set_style_border_color(s_plate, lv_color_hex(p->dim), 0);
    lv_obj_set_style_shadow_width(s_plate, 6, 0);
    lv_obj_set_style_shadow_ofs_y(s_plate, 2, 0);
    lv_obj_set_style_shadow_color(s_plate, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_plate, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(s_plate, 0, 0);
    lv_obj_remove_flag(s_plate, LV_OBJ_FLAG_SCROLLABLE);

    /* current: big temperature + humidity on row 1, condition on row 2 */
    s_temp_label = make_label(s_plate, "--", &font_px16, p->text,
                              LV_ALIGN_TOP_LEFT, 6, 6, 0, 0);
    s_hum_label = make_label(s_plate, "", &font_silk8, p->dim,
                             LV_ALIGN_TOP_LEFT, 104, 14, 0, 0);
    s_cond_label = make_label(s_plate, "weather offline", &font_silk8,
                              p->dim, LV_ALIGN_TOP_LEFT, 6, 26, 0, 0);

    /* divider between current and forecast zones */
    s_divider = lv_obj_create(s_plate);
    lv_obj_set_size(s_divider, 1, 32);
    lv_obj_align(s_divider, LV_ALIGN_TOP_LEFT, 140, 6);
    lv_obj_set_style_bg_color(s_divider, lv_color_hex(p->dim), 0);
    lv_obj_set_style_bg_opa(s_divider, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_divider, 0, 0);
    lv_obj_remove_flag(s_divider, LV_OBJ_FLAG_SCROLLABLE);

    /* forecast: three 24px entries (day in caps, pixel icon, max/min)
     * (plate-local x; entries end at local 226, plate is 232 wide) */
    static const int fx[3] = { 146, 174, 202 };
    /* hairline separators between the forecast columns, so adjacent temp
     * strings do not run together */
    static const int fdiv[2] = { 174, 202 };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *dv = s_fc_div[i] = lv_obj_create(s_plate);
        lv_obj_set_size(dv, 1, 32);
        lv_obj_align(dv, LV_ALIGN_TOP_LEFT, fdiv[i], 6);
        lv_obj_set_style_bg_color(dv, lv_color_hex(p->dim), 0);
        lv_obj_set_style_bg_opa(dv, LV_OPA_50, 0);
        lv_obj_set_style_border_width(dv, 0, 0);
        lv_obj_remove_flag(dv, LV_OBJ_FLAG_SCROLLABLE);
    }
    for (int i = 0; i < 3; i++) {
        s_fc_day[i] = make_label(s_plate, "", &font_silk8, p->dim,
                                 LV_ALIGN_TOP_LEFT, fx[i] + 3, 3, 0, 0);
        s_fc_icon[i] = lv_image_create(s_plate);
        lv_image_set_src(s_fc_icon[i], &s_dsc_cloud);
        lv_obj_align(s_fc_icon[i], LV_ALIGN_TOP_LEFT, fx[i] + 4, 13);
        s_fc_temp[i] = make_label(s_plate, "", &font_silk8, p->text,
                                  LV_ALIGN_TOP_LEFT, fx[i], 31, 0, 0);
    }

    ui_home_apply_theme(p);   /* stroke colours and the theme's digit scheme */
}
