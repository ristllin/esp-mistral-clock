/* Theme manager: three designed Mistral-warm palettes + auto (sunrise/sunset
 * from the weather data, fixed-hours fallback). Mode persists in NVS; the
 * resolved palette is applied to every screen and the scene. A 5 s timer
 * re-evaluates auto so the theme follows the day (and the debug clock
 * override). */
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_console.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"

#include "display.h"
#include "theme_mgr.h"
#include "time_mgr.h"
#include "weather_mgr.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "ui_loc.h"
#include "scene.h"
#include "saver.h"

static const char *TAG = "theme";

/* Mistral identity: warm yellow/orange/red on warm neutrals.
 * dark:  near-black warm ground (the default clock look)
 * light: warm paper, espresso text, deep orange accent (readable)
 * mid:   dusk amber, between the two */
static const theme_palette_t s_pal_dark = {
    .bg = 0x14110E, .panel = 0x241F17, .text = 0xF5EDE0,
    .dim = 0x9A8F7A, .accent = 0xFF8205, .red = 0xFA500F,
    .yellow = 0xFFCD00,
};
static const theme_palette_t s_pal_light = {
    .bg = 0xF6EFE3, .panel = 0xEADFCB, .text = 0x2B2118,
    .dim = 0x8C7D66, .accent = 0xD96A00, .red = 0xC93A0E,
    .yellow = 0xA87B12,
};
static const theme_palette_t s_pal_mid = {
    .bg = 0x3B2D20, .panel = 0x4E3C2B, .text = 0xF2E7D3,
    .dim = 0xB3A284, .accent = 0xFFA02E, .red = 0xFF6B3D,
    .yellow = 0xFFD84D,
};

static theme_mode_t s_mode = THEME_DARK;
static theme_mode_t s_resolved = THEME_DARK;
static esp_timer_handle_t s_timer;

static const theme_palette_t *palette_for(theme_mode_t m)
{
    switch (m) {
    case THEME_LIGHT: return &s_pal_light;
    case THEME_MID:   return &s_pal_mid;
    default:          return &s_pal_dark;
    }
}

static const char *name_for(theme_mode_t m)
{
    switch (m) {
    case THEME_LIGHT: return "light";
    case THEME_MID:   return "mid";
    case THEME_AUTO:  return "auto";
    default:          return "dark";
    }
}

static theme_mode_t resolve_auto(void)
{
    struct tm tm;
    time_mgr_now(&tm);
    int len;
    int rel = weather_sun_clock(weather_mgr_sunrise_min(),
                                weather_mgr_sunset_min(),
                                tm.tm_hour * 60 + tm.tm_min, &len);
    /* dawn and dusk windows resolve to the mid (amber) theme */
    const int W = 45;
    if ((rel >= -W && rel < W) || (rel >= len - W && rel < len + W)) {
        return THEME_MID;
    }
    return (rel >= 0 && rel < len) ? THEME_LIGHT : THEME_DARK;
}

void theme_mgr_colors(const theme_palette_t **out)
{
    *out = palette_for(s_resolved);
}

const char *theme_mgr_mode_name(void) { return name_for(s_mode); }
theme_mode_t theme_mgr_mode(void) { return s_mode; }
theme_mode_t theme_mgr_resolved(void) { return s_resolved; }

static void theme_mgr_apply(void)
{
    const theme_palette_t *p = palette_for(s_resolved);
    ui_home_apply_theme(p);
    ui_settings_apply_theme(p);
    ui_loc_apply_theme(p);
    saver_apply_theme(p);
    scene_theme_changed();   /* regrade the scene palette + repaint */
}

void theme_mgr_recheck(void)
{
    theme_mode_t want = (s_mode == THEME_AUTO) ? resolve_auto() : s_mode;
    if (want != s_resolved) {
        ESP_LOGI(TAG, "auto theme: %s -> %s", name_for(s_resolved),
                 name_for(want));
        s_resolved = want;
        theme_mgr_apply();
    }
}

esp_err_t theme_mgr_set(theme_mode_t mode)
{
    s_mode = mode;
    nvs_handle_t h;
    if (nvs_open("theme", NVS_READWRITE, &h) == ESP_OK) {
        uint8_t m = (uint8_t)mode;
        nvs_set_u8(h, "mode", m);
        nvs_commit(h);
        nvs_close(h);
    }
    s_resolved = (mode == THEME_AUTO) ? resolve_auto() : mode;
    theme_mgr_apply();
    return ESP_OK;
}

static void timer_cb(void *arg)
{
    (void)arg;
    /* esp_timer callback: never block on the LVGL lock (see display.h);
     * the nested locks in theme_mgr_apply() are recursive */
    if (lvgl_port_lock(UI_TIMER_LOCK_MS)) {
        theme_mgr_recheck();
        lvgl_port_unlock();
    }
}

static int theme_cmd(int argc, char **argv)
{
    if (argc >= 2) {
        theme_mode_t m;
        if (!strcmp(argv[1], "dark"))       { m = THEME_DARK; }
        else if (!strcmp(argv[1], "light")) { m = THEME_LIGHT; }
        else if (!strcmp(argv[1], "mid"))   { m = THEME_MID; }
        else if (!strcmp(argv[1], "auto"))  { m = THEME_AUTO; }
        else {
            printf("usage: theme dark | light | mid | auto\n");
            return 1;
        }
        theme_mgr_set(m);
    }
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    printf("theme:   %s%s\n", name_for(s_mode),
           s_mode == THEME_AUTO ? " (resolves to light/mid/dark by sun times; mid at dawn/dusk)"
                                : "");
    printf("active:  %s  bg #%06X  accent #%06X  text #%06X\n",
           name_for(s_resolved), (unsigned)p->bg, (unsigned)p->accent,
           (unsigned)p->text);
    return 0;
}

void theme_mgr_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "theme",
        .help = "theme dark | light | mid | auto  (or: theme for status)",
        .hint = NULL,
        .func = &theme_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

esp_err_t theme_mgr_init(void)
{
    uint8_t m = 0;
    nvs_handle_t h;
    if (nvs_open("theme", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, "mode", &m) == ESP_OK && m <= THEME_AUTO) {
            s_mode = (theme_mode_t)m;
        }
        nvs_close(h);
    }
    s_resolved = (s_mode == THEME_AUTO) ? resolve_auto() : s_mode;

    const esp_timer_create_args_t targs = {
        .callback = timer_cb,
        .name = "theme_tick",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, 5000000));

    theme_mgr_apply();
    ESP_LOGI(TAG, "theme %s (active %s)", name_for(s_mode),
             name_for(s_resolved));
    return ESP_OK;
}
