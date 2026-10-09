/* Screensaver.
 *
 * The saver screen is a plain LVGL screen filled with the theme background
 * colour, which is exactly the scene engine's substitution key, so the
 * whole panel shows the scene (sky + weather + the pixel cat) with no LVGL
 * chrome on top. It carries no widgets and no text: full-screen art only.
 *
 * State machine: an esp_timer polls every 250 ms (try-locking LVGL, see
 * display.h). Touch (real or injected) resets the idle timer and wakes the
 * saver; console input resets the idle timer only. After `timeout` seconds without activity the saver loads;
 * the scene task keeps animating it at 10 fps through the same flush path
 * as the home screen. Enabled flag + timeout persist in NVS ("saver"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_console.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "display.h"
#include "saver.h"
#include "scene.h"
#include "scene_chaton.h"
#include "ui_common.h"
#include "ui_home.h"

static const char *TAG = "saver";

#define DEFAULT_TIMEOUT_S 120

static lv_obj_t *s_scr;
static esp_timer_handle_t s_timer;
static bool s_enabled = true;
static uint32_t s_timeout_s = DEFAULT_TIMEOUT_S;
static volatile int64_t s_last_us;
static volatile bool s_wake;

/* ---------------- NVS ---------------- */

static void load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("saver", NVS_READONLY, &h) == ESP_OK) {
        uint8_t en = 1;
        uint32_t tmo = DEFAULT_TIMEOUT_S;
        nvs_get_u8(h, "en", &en);
        nvs_get_u32(h, "tmo", &tmo);
        s_enabled = en != 0;
        s_timeout_s = tmo;
        nvs_close(h);
    }
}

static void save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("saver", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "en", s_enabled ? 1 : 0);
        nvs_set_u32(h, "tmo", s_timeout_s);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---------------- transitions (LVGL lock held) ---------------- */

static void enter_locked(void)
{
    if (!s_scr) { return; }
    lv_screen_load(s_scr);
    scene_set_screen(SCENE_SCR_SAVER);
    scene_chaton_reset();               /* deterministic first scenario */
    s_last_us = esp_timer_get_time();
}

static void exit_locked(void)
{
    lv_screen_load(ui_home_screen());
    scene_set_screen(SCENE_SCR_HOME);
    s_last_us = esp_timer_get_time();
}

/* ---------------- activity hooks ---------------- */

void saver_notify_touch(void)
{
    s_last_us = esp_timer_get_time();
    s_wake = true;
}

void saver_notify_console(void)
{
    s_last_us = esp_timer_get_time();
}

static void idle_cb(void *arg)
{
    (void)arg;
    /* esp_timer callback: never block on the LVGL lock (see display.h) */
    if (!lvgl_port_lock(UI_TIMER_LOCK_MS)) { return; }
    int64_t now = esp_timer_get_time();
    if (s_wake) {
        s_wake = false;
        if (scene_screen() == SCENE_SCR_SAVER) {
            exit_locked();
        }
        s_last_us = now;
    } else if (s_enabled && s_timeout_s > 0
               && scene_screen() != SCENE_SCR_SAVER
               && now - s_last_us > (int64_t)s_timeout_s * 1000000LL) {
        enter_locked();
    }
    lvgl_port_unlock();
}

/* ---------------- public API ---------------- */

bool saver_enabled(void) { return s_enabled; }
uint32_t saver_timeout_s(void) { return s_timeout_s; }

static int64_t saver_idle_s(void)
{
    return (esp_timer_get_time() - s_last_us) / 1000000LL;
}

void saver_set_enabled(bool en)
{
    s_enabled = en;
    if (en && s_timeout_s == 0) { s_timeout_s = DEFAULT_TIMEOUT_S; }
    save_nvs();
    if (!en) {
        lvgl_port_lock(0);
        if (scene_screen() == SCENE_SCR_SAVER) { exit_locked(); }
        lvgl_port_unlock();
    }
}

void saver_set_timeout(uint32_t s)
{
    s_timeout_s = s;
    save_nvs();
}

static void saver_enter_now(void)
{
    lvgl_port_lock(0);
    enter_locked();
    lvgl_port_unlock();
    printf("screensaver on (tap the screen to return)\n");
}

void saver_apply_theme(const theme_palette_t *p)
{
    if (!s_scr) { return; }
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(p->bg), 0);
    lv_obj_invalidate(s_scr);   /* re-render with the new substitution key */
    lvgl_port_unlock();
}

/* ---------------- init ---------------- */

esp_err_t saver_init(void)
{
    load_nvs();
    lvgl_port_lock(0);
    s_scr = ui_screen_base(NULL);
    lvgl_port_unlock();

    s_last_us = esp_timer_get_time();
    const esp_timer_create_args_t targs = {
        .callback = idle_cb,
        .name = "saver_idle",
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_timer_create(&targs, &s_timer);
    esp_timer_start_periodic(s_timer, 250000);
    ESP_LOGI(TAG, "screensaver up: %s, idle timeout %lu s",
             s_enabled ? "on" : "off", (unsigned long)s_timeout_s);
    return ESP_OK;
}

/* ---------------- console command ---------------- */

static void saver_status(void)
{
    printf("saver: %s, timeout %lus, idle %llds, scenario %s (%s), screen %s\n",
           s_enabled ? "on" : "off", (unsigned long)s_timeout_s,
           (long long)saver_idle_s(), scene_chaton_scenario_name(),
           scene_chaton_scenario_pin() < 0 ? "auto" : "pinned",
           scene_screen() == SCENE_SCR_SAVER ? "saver"
           : scene_screen() == SCENE_SCR_HOME ? "home" : "other");
    printf("usage: saver [on|off|now|timeout <s>|scenario [auto|sleep|loaf|"
           "chase|eat|mchase|curl]|neutral on|off]\n");
}

static int saver_cmd(int argc, char **argv)
{
    if (argc < 2) {
        saver_status();
        return 0;
    }
    if (!strcmp(argv[1], "on")) {
        saver_set_enabled(true);
        printf("saver: on\n");
    } else if (!strcmp(argv[1], "off")) {
        saver_set_enabled(false);
        printf("saver: off\n");
    } else if (!strcmp(argv[1], "now")) {
        saver_enter_now();
    } else if (!strcmp(argv[1], "timeout") && argc >= 3) {
        long s = atol(argv[2]);
        if (s < 5) {
            printf("usage: saver timeout <s>  (minimum 5; use 'saver off' "
                   "to disable)\n");
            return 1;
        }
        saver_set_timeout((uint32_t)s);
        printf("saver: timeout %lds\n", s);
    } else if (!strcmp(argv[1], "scenario")) {
        if (argc >= 3) {
            if (scene_chaton_set_scenario(argv[2])) {
                printf("usage: saver scenario [auto|sleep|loaf|chase|eat|mchase|curl]\n");
                return 1;
            }
            printf("saver: scenario %s\n", scene_chaton_scenario_name());
        } else {
            printf("saver: scenario %s (%s)\n", scene_chaton_scenario_name(),
                   scene_chaton_scenario_pin() < 0 ? "auto" : "pinned");
        }
    } else if (!strcmp(argv[1], "neutral") && argc >= 3) {
        bool on = !strcmp(argv[2], "on");
        if (!on && strcmp(argv[2], "off")) {
            printf("usage: saver neutral [on|off]\n");
            return 1;
        }
        scene_chaton_set_neutral_dbg(on);
        printf("saver: neutral indoor scene %s (debug override; the "
               "3 h staleness rule still applies without it)\n",
               on ? "forced on" : "off");
    } else {
        saver_status();
        return 1;
    }
    return 0;
}

void saver_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command = "saver",
         .help = "screensaver: status, on|off, now, timeout <s>, "
                 "scenario <name>, neutral on|off",
         .hint = NULL, .func = &saver_cmd},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}
