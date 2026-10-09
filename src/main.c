/* Mistral weather clock: app entry point.
 *
 * Boot order: NVS -> display hardware -> LVGL framebuffer -> touch -> home UI
 * -> theme, scene, screensaver -> Wi-Fi, time, weather -> console (menu +
 * one-line commands; console_init() registers every module's commands). */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"

#include "version.h"
#include "display.h"
#include "touch.h"
#include "ui_home.h"
#include "console.h"
#include "wifi_mgr.h"
#include "time_mgr.h"
#include "weather_mgr.h"
#include "theme_mgr.h"
#include "scene.h"
#include "saver.h"

static const char *TAG = "clock";

static void halt_forever(void)
{
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}

static void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s booting (reset reason %d)", FIRMWARE_NAME,
             FIRMWARE_VERSION, esp_reset_reason());

    nvs_init();

    if (display_hw_init() != ESP_OK) {
        ESP_LOGE(TAG, "hardware init failed, halting");
        halt_forever();
    }
    display_lvgl_init();
    if (!g_fb) {
        ESP_LOGE(TAG, "no framebuffer, halting");
        halt_forever();
    }

    if (touch_init() != ESP_OK) {
        /* Touch is nice to have; the clock still works over the console */
        ESP_LOGE(TAG, "touch init failed, continuing without touch");
    }

    lvgl_port_lock(0);
    ui_home_build();
    lvgl_port_unlock();

    /* Theme (persisted in NVS) is applied to the home screen here and to
     * the lazily-built settings screen when it opens */
    if (theme_mgr_init() != ESP_OK) {
        ESP_LOGE(TAG, "theme init failed");
    }

    /* Live background scene engine: animates the home screen behind the
     * LVGL chrome at 10 fps, ~11 KB RAM total */
    if (scene_init() != ESP_OK) {
        ESP_LOGE(TAG, "scene init failed");
    }
    scene_set_screen(SCENE_SCR_HOME);

    /* Screensaver: idle timeout -> full-screen art with the Mistral pixel
     * cat; touch wakes. On/off and timeout are persisted in NVS. */
    if (saver_init() != ESP_OK) {
        ESP_LOGE(TAG, "saver init failed");
    }

    /* Wi-Fi starts before the console so the clock is standalone from boot;
     * it connects to the strongest saved network with backoff, no USB needed */
    if (wifi_mgr_init() != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed, continuing without wifi");
    }

    /* SNTP starts automatically once Wi-Fi has an IP; feeds the home clock */
    if (time_mgr_init() != ESP_OK) {
        ESP_LOGE(TAG, "time init failed");
    }

    /* Weather refreshes on Wi-Fi up / SNTP sync / every 30 min; the NVS
     * cache is shown while offline */
    if (weather_mgr_init() != ESP_OK) {
        ESP_LOGE(TAG, "weather init failed");
    }

    console_init();

    ESP_LOGI(TAG, "boot complete: %dx%d landscape, console on USB-Serial/JTAG",
             LCD_HRES, LCD_VRES);
}
