/* Settings screen, recoloured by the theme. Rows: NET / IP / SIG / NETS /
 * LOC (opens the city picker) / THEME (cycles dark, light, mid, auto) /
 * SAVER (cycles off and the idle timeouts) / FW. */
#include <stdio.h>

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_console.h"

#include "display.h"
#include "version.h"
#include "wifi_mgr.h"
#include "weather_mgr.h"
#include "theme_mgr.h"
#include "ui_home.h"
#include "ui_loc.h"
#include "ui_settings.h"
#include "scene.h"
#include "saver.h"
#include "ui_common.h"

#define NROWS 8         /* NET IP SIG NETS LOC THEME SAVER FW */

static lv_obj_t *s_scr;
static ui_header_t s_hdr;
static lv_obj_t *s_line;
static lv_obj_t *s_row_keys[NROWS];
static lv_obj_t *s_val_net;
static lv_obj_t *s_val_ip;
static lv_obj_t *s_val_sig;
static lv_obj_t *s_val_nets;
static lv_obj_t *s_loc_btn;
static lv_obj_t *s_val_loc;
static lv_obj_t *s_theme_btn;
static lv_obj_t *s_theme_btn_label;
static lv_obj_t *s_saver_btn;
static lv_obj_t *s_saver_btn_label;
static lv_obj_t *s_val_fw;
static esp_timer_handle_t s_refresh_timer;
static int s_row_idx;

/* Row key label in the left column (recoloured by apply_theme). */
static void row_key(const char *key, int y)
{
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    lv_obj_t *k = ui_label(s_scr, key, p->dim, 8, y);
    if (s_row_idx < NROWS) { s_row_keys[s_row_idx++] = k; }
}

/* Text row: key + value label at x=72; returns the value label. */
static lv_obj_t *row(const char *key, const char *value, int y)
{
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    row_key(key, y);
    return ui_label(s_scr, value, p->text, 72, y);
}

static void theme_btn_cb(lv_event_t *e)
{
    (void)e;
    theme_mode_t next = (theme_mode_t)(((int)theme_mgr_mode() + 1) %
                                       (THEME_AUTO + 1));
    theme_mgr_set(next);
    lv_label_set_text(s_theme_btn_label, theme_mgr_mode_name());
}

static void loc_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_loc_show();
}

/* Screensaver row: cycles OFF -> 30s -> 60s -> 2min -> 5min -> 10min. */
static const uint32_t s_saver_steps[6] = { 0, 30, 60, 120, 300, 600 };
static const char *const s_saver_labels[6] = { "OFF", "30S", "60S",
                                               "2MIN", "5MIN", "10MIN" };

#define NSTEPS ((int)(sizeof(s_saver_steps) / sizeof(s_saver_steps[0])))

/* Index of the current saver setting in s_saver_steps (OFF if unknown). */
static int saver_step(void)
{
    uint32_t cur = saver_enabled() ? saver_timeout_s() : 0;
    for (int i = 0; i < NSTEPS; i++) {
        if (s_saver_steps[i] == cur) { return i; }
    }
    return 0;
}

static void saver_label_update(void)
{
    if (!s_saver_btn_label) { return; }
    lv_label_set_text(s_saver_btn_label, s_saver_labels[saver_step()]);
}

static void saver_btn_cb(lv_event_t *e)
{
    (void)e;
    int next = (saver_step() + 1) % NSTEPS;
    /* OFF only disables: the timeout is kept so `saver on` restores it */
    saver_set_enabled(s_saver_steps[next] > 0);
    if (s_saver_steps[next] > 0) { saver_set_timeout(s_saver_steps[next]); }
    saver_label_update();
}

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    scene_set_screen(SCENE_SCR_HOME);
    lv_screen_load(ui_home_screen());
}

static void build(void)
{
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    s_row_idx = 0;
    s_scr = ui_screen_base(NULL);
    ui_header_create(&s_hdr, s_scr, "SETTINGS", back_btn_cb);

    /* divider under the header */
    s_line = lv_obj_create(s_scr);
    lv_obj_set_size(s_line, 304, 1);
    lv_obj_align(s_line, LV_ALIGN_TOP_LEFT, 8, 26);
    lv_obj_set_style_bg_color(s_line, lv_color_hex(p->panel), 0);
    lv_obj_set_style_border_width(s_line, 0, 0);
    lv_obj_remove_flag(s_line, LV_OBJ_FLAG_SCROLLABLE);

    s_val_net  = row("NET",   "--", 34);
    s_val_ip   = row("IP",    "--", 51);
    s_val_sig  = row("SIG",   "--", 68);
    s_val_nets = row("NETS",  "--", 85);

    /* LOC row: button showing the city (opens the picker), 200x16 at
     * (72,99), centre (172,107); the city name is left-aligned text */
    row_key("LOC", 102);
    s_loc_btn = ui_chip_button(s_scr, 72, 99, 200, 16, "", loc_btn_cb,
                               &s_val_loc);
    lv_obj_set_style_text_color(s_val_loc, lv_color_hex(p->text), 0);
    lv_obj_set_width(s_val_loc, 184);   /* long names end in "..." */
    lv_label_set_long_mode(s_val_loc, LV_LABEL_LONG_DOT);
    lv_obj_align(s_val_loc, LV_ALIGN_LEFT_MID, 8, 0);

    /* THEME row: cycling button (56x16 at (72,116), centre (100,124)) */
    row_key("THEME", 119);
    s_theme_btn = ui_chip_button(s_scr, 72, 116, 56, 16,
                                 theme_mgr_mode_name(), theme_btn_cb,
                                 &s_theme_btn_label);

    /* SAVER row: cycling button (88x16 at (72,133), centre (116,141))
     * showing OFF / 30S / 60S / 2MIN / 5MIN / 10MIN */
    row_key("SAVER", 136);
    s_saver_btn = ui_chip_button(s_scr, 72, 133, 88, 16, "", saver_btn_cb,
                                 &s_saver_btn_label);
    saver_label_update();

    s_val_fw = row("FW", "v" FIRMWARE_VERSION, 153);

    ui_settings_apply_theme(p);   /* adopt the persisted theme */
}

void ui_settings_apply_theme(const theme_palette_t *p)
{
    if (!s_scr) { return; }
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(p->bg), 0);
    ui_header_recolor(&s_hdr, p);
    lv_obj_set_style_bg_color(s_line, lv_color_hex(p->panel), 0);
    for (int i = 0; i < s_row_idx; i++) {
        lv_obj_set_style_text_color(s_row_keys[i], lv_color_hex(p->dim), 0);
    }
    lv_obj_t *vals[] = { s_val_net, s_val_ip, s_val_sig, s_val_nets,
                         s_val_fw };
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        lv_obj_set_style_text_color(vals[i], lv_color_hex(p->text), 0);
    }
    ui_chip_recolor(s_loc_btn, s_val_loc, p);
    lv_obj_set_style_text_color(s_val_loc, lv_color_hex(p->text), 0);
    ui_chip_recolor(s_theme_btn, s_theme_btn_label, p);
    ui_chip_recolor(s_saver_btn, s_saver_btn_label, p);
    lvgl_port_unlock();
}

static void refresh_locked(void)
{
    if (!s_scr) { return; }

    const char *ssid = wifi_mgr_ssid();
    const char *ip = wifi_mgr_ip();
    char buf[48];

    lv_label_set_text(s_val_net, ssid[0] ? ssid : "offline");
    lv_label_set_text(s_val_ip, ip[0] ? ip : "--");

    int rssi = wifi_mgr_rssi();
    if (rssi) {
        snprintf(buf, sizeof(buf), "%d dBm", rssi);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(s_val_sig, buf);

    char names[96];
    if (wifi_mgr_saved_names(names, sizeof(names))) {   /* else keep it */
        lv_label_set_text(s_val_nets, names[0] ? names : "--");
    }

    char name[24];
    float lat, lon;
    weather_mgr_location(&lat, &lon, name, sizeof(name));
    char loc[36];
    snprintf(loc, sizeof(loc), "%s%s", name,
             weather_mgr_location_auto() ? " (auto)" : "");
    lv_label_set_text(s_val_loc, loc);

    lv_label_set_text(s_val_fw, "v" FIRMWARE_VERSION);
    lv_label_set_text(s_theme_btn_label, theme_mgr_mode_name());
    saver_label_update();
}

static void refresh_timer_cb(void *arg)
{
    (void)arg;
    /* esp_timer callback: never block on the LVGL lock (see display.h) */
    if (!s_scr || !lvgl_port_lock(UI_TIMER_LOCK_MS)) { return; }
    if (lv_screen_active() == s_scr) { refresh_locked(); }
    lvgl_port_unlock();
}

void ui_settings_show(void)
{
    lvgl_port_lock(0);
    if (!s_scr) {
        build();
        const esp_timer_create_args_t targs = {
            .callback = refresh_timer_cb,
            .name = "settings_refresh",
            .dispatch_method = ESP_TIMER_TASK,
        };
        esp_timer_create(&targs, &s_refresh_timer);
        esp_timer_start_periodic(s_refresh_timer, 5000000);
    }
    refresh_locked();
    lv_screen_load(s_scr);
    scene_set_screen(SCENE_SCR_OTHER);
    lvgl_port_unlock();
}

static int settings_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    ui_settings_show();
    printf("settings screen open (touch BACK or send 'home' to return)\n");
    return 0;
}

static int home_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    lvgl_port_lock(0);
    lv_screen_load(ui_home_screen());
    scene_set_screen(SCENE_SCR_HOME);
    lvgl_port_unlock();
    printf("home screen\n");
    return 0;
}

void ui_settings_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command = "settings",
         .help = "open the settings screen",
         .hint = NULL, .func = &settings_cmd},
        {.command = "home",
         .help = "return to the home screen",
         .hint = NULL, .func = &home_cmd},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}
