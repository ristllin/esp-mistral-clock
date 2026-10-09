/* Time manager: SNTP (started when Wi-Fi gets an IP) + Europe/London with
 * DST via a POSIX TZ string. A 1 s esp_timer feeds the home clock and runs
 * the SNTP start/stop state machine; the debug override replaces the RTC
 * source so the auto theme can be forced across a full day. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_console.h"

#include "esp_lvgl_port.h"

#include "display.h"
#include "time_mgr.h"
#include "wifi_mgr.h"
#include "weather_mgr.h"
#include "theme_mgr.h"
#include "ui_home.h"

static const char *TAG = "time";

/* Europe/London in POSIX TZ form: GMT in winter, BST (GMT+1) from the last
 * Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC. */
#define TZ_LONDON "GMT0BST,M3.5.0/1,M10.5.0"

static bool s_synced;        /* SNTP has set the system clock at least once */
static bool s_started;       /* SNTP client running */
static bool s_override;
static int64_t s_override_epoch;
static int64_t s_override_base_us;  /* esp_timer_get_time() when override set */
static esp_timer_handle_t s_tick;

/* The auto theme follows a clock override change immediately. Task context
 * only (console): theme state is otherwise touched under the LVGL lock. */
static void recheck_theme(void)
{
    lvgl_port_lock(0);
    theme_mgr_recheck();
    lvgl_port_unlock();
}

int64_t time_mgr_epoch(void)
{
    if (s_override) {
        return s_override_epoch +
               (esp_timer_get_time() - s_override_base_us) / 1000000LL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec;
}

void time_mgr_now(struct tm *out)
{
    time_t now = (time_t)time_mgr_epoch();
    localtime_r(&now, out);
}

bool time_mgr_is_synced(void)
{
    return s_synced || s_override;
}

void time_mgr_set_override(int64_t epoch)
{
    /* publish the values before the flag: readers on other tasks check
     * s_override first and must never pair it with a stale epoch */
    s_override = false;
    s_override_epoch = epoch;
    s_override_base_us = esp_timer_get_time();
    __atomic_store_n(&s_override, true, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "clock override: epoch %lld", (long long)epoch);
    recheck_theme();
}

void time_mgr_clear_override(void)
{
    s_override = false;
    ESP_LOGI(TAG, "clock override off, back to %s",
             s_synced ? "SNTP time" : "unsynced clock");
    recheck_theme();
}

static void time_sync_notification_cb(struct timeval *tv)
{
    s_synced = true;
    struct tm tm;
    time_mgr_now(&tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
    ESP_LOGI(TAG, "time synced: %s", buf);
    weather_mgr_time_synced();   /* restamps a fetch made before the sync */
}

static void sntp_state_machine(void)
{
    bool connected = wifi_mgr_is_connected();
    if (connected && !s_started) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        esp_sntp_init();
        s_started = true;
        ESP_LOGI(TAG, "sntp started");
    } else if (!connected && s_started) {
        esp_sntp_stop();
        s_started = false;
        /* s_synced stays: the RTC keeps ticking from the last synced time,
         * which is the "last known time" the UI shows while offline */
        ESP_LOGI(TAG, "sntp stopped (wifi down)");
    }
}

static void ui_tick_cb(void *arg)
{
    (void)arg;
    sntp_state_machine();

    struct tm tm;
    time_mgr_now(&tm);
    char hhmm[8], date[24];
    strftime(hhmm, sizeof(hhmm), "%H:%M", &tm);
    strftime(date, sizeof(date), "%a %d %b", &tm);
    /* esp_timer callback: never block on the LVGL lock (see display.h) */
    if (lvgl_port_lock(UI_TIMER_LOCK_MS)) {
        ui_home_set_time(hhmm, date, time_mgr_is_synced());
        lvgl_port_unlock();
    }
}

static int clock_cmd(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "override")) {
        if (argc >= 3 && strcmp(argv[2], "off")) {
            char *end;
            long long epoch = strtoll(argv[2], &end, 10);
            if (end == argv[2] || *end != '\0' || epoch < 0) {
                printf("usage: clock override <unix_epoch|off>\n");
                return 1;
            }
            time_mgr_set_override(epoch);
            printf("clock override set\n");
        } else {
            time_mgr_clear_override();
            printf("clock override off\n");
        }
        return 0;
    }
    struct tm tm;
    time_mgr_now(&tm);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
    printf("time:   %s (epoch %lld)\n", buf, (long long)time_mgr_epoch());
    printf("synced: %s%s\n", time_mgr_is_synced() ? "yes" : "no",
           s_synced ? "" : " (time not synced)");
    printf("tz:     %s (DST rule in the TZ string)\n", TZ_LONDON);
    printf("sntp:   %s\n", s_started ? "running" : "stopped");
    if (s_override) {
        printf("override: active (debug clock)\n");
    }
    return 0;
}

void time_mgr_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "clock",
        .help = "clock status | clock override <unix_epoch|off> (debug clock)",
        .hint = NULL,
        .func = &clock_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

esp_err_t time_mgr_init(void)
{
    setenv("TZ", TZ_LONDON, 1);
    tzset();

    const esp_timer_create_args_t args = {
        .callback = ui_tick_cb,
        .arg = NULL,
        .name = "clock_tick",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_tick, 1000000));
    return ESP_OK;
}
