/* Touch: AXS5106L over I2C -> LVGL pointer indev.
 *
 * Coordinate mapping: the controller's raw axes
 * follow the panel's native portrait scan. The vendor demo (rotation 0,
 * portrait 172x320) uses mirror_x=1, mirror_y=0, swap_xy=0, i.e.
 * raw_x is reversed along the 172-wide GRAM X window and raw_y is aligned
 * with the 320-tall GRAM Y. Our glass is landscape with the display path
 * mapping glass_y = GRAM_X window and glass_x = 319 - GRAM_Y (flip_h).
 * So: glass_y = 171 - raw_x, glass_x = 319 - raw_y. The esp_lcd_touch core
 * applies mirror_x (x = x_max - x), mirror_y (y = y_max - y) and then
 * swap_xy, which yields exactly that with:
 *   x_max = 171, y_max = 319, mirror_x = 1, mirror_y = 1, swap_xy = 1.
 * `touch raw` shows live glass coordinates to check the mapping by hand. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_axs5106.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "bsp_i2c.h"
#include "bsp_touch.h"
#include "display.h"
#include "touch.h"
#include "saver.h"

static const char *TAG = "touch";

static i2c_master_bus_handle_t g_bus;
static i2c_master_dev_handle_t g_dev;
static esp_lcd_touch_handle_t g_touch;
static lv_indev_t *g_indev;

/* Injected touch point (glass coordinates), set by `touch inject` */
static volatile int g_inj_x = -1, g_inj_y = -1, g_inj_down = 0;

/* First touch point from the controller (glass coordinates); false when
 * nothing touches the glass, the I2C read failed or touch is not up.
 * Runs in the LVGL task (indev read); console callers must hold the LVGL
 * lock so they never interleave with it on the shared driver handle. */
static bool read_point(uint16_t *x, uint16_t *y)
{
    esp_lcd_touch_point_data_t pt;
    uint8_t n = 0;
    if (!g_touch || esp_lcd_touch_read_data(g_touch) != ESP_OK ||
        esp_lcd_touch_get_data(g_touch, &pt, &n, 1) != ESP_OK || n == 0) {
        return false;
    }
    *x = pt.x;
    *y = pt.y;
    return true;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (g_inj_down) {
        data->point.x = (lv_coord_t)g_inj_x;
        data->point.y = (lv_coord_t)g_inj_y;
        data->state = LV_INDEV_STATE_PRESSED;
        data->continue_reading = false;
        saver_notify_touch();   /* injected taps wake/reset the saver too */
        return;
    }
    uint16_t x = 0, y = 0;
    if (read_point(&x, &y)) {
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
        data->state = LV_INDEV_STATE_PRESSED;
        saver_notify_touch();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->continue_reading = false;
}

esp_err_t touch_init(void)
{
    g_bus = bsp_i2c_init();

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &dev_cfg, &g_dev),
                        TAG, "i2c add AXS5106");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 171,
        .y_max = 319,
        .rst_gpio_num = EXAMPLE_PIN_TP_RST,
        .int_gpio_num = EXAMPLE_PIN_TP_INT,
        .flags = { .swap_xy = 1, .mirror_x = 1, .mirror_y = 1 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_axs5106(g_dev, &tp_cfg, &g_touch),
                        TAG, "axs5106 init");

    lvgl_port_lock(0);
    g_indev = lv_indev_create();
    lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_indev, touch_read_cb);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "AXS5106L ready at 0x%02X, indev in glass coords",
             ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS);
    return ESP_OK;
}

/* --- console commands --- */

static int touch_i2c_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!g_bus || !g_dev) {
        printf("touch not initialised\n");
        return 1;
    }
    esp_err_t err = i2c_master_probe(g_bus, ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS, 1000);
    if (err != ESP_OK) {
        printf("AXS5106L NOT responding at 0x%02X: %s\n",
               ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS, esp_err_to_name(err));
        return 1;
    }
    /* Raw register dump, same transaction shape the driver uses (write
     * reg 0x01, STOP, then read 14 bytes; points count = byte 1 & 0x0F).
     * The AXS5106L does NOT accept a combined repeated-start write+read
     * (i2c_master_transmit_receive times out with ESP_ERR_INVALID_STATE),
     * so the write and the read must be two separate transactions, exactly
     * like esp_lcd_touch_axs5106's touch_axs5106_i2c_read. The LVGL lock
     * keeps the indev read from slipping in between them. NB: the points
     * register is read-to-clear / sticky: it can still show the last touch
     * for one read after release, which is why the authoritative state
     * below comes from the driver path used by `touch raw`. */
    uint8_t reg = 0x01;
    uint8_t buf[14] = {0};
    lvgl_port_lock(0);
    err = i2c_master_transmit(g_dev, &reg, 1, 1000);
    if (err == ESP_OK) {
        err = i2c_master_receive(g_dev, buf, sizeof(buf), 1000);
    }
    lvgl_port_unlock();
    if (err != ESP_OK) {
        printf("probe OK but register read failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("AXS5106L responding at 0x%02X, raw regs 0x01..0x0E:",
           ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS);
    for (size_t i = 0; i < sizeof(buf); i++) { printf(" %02X", buf[i]); }
    printf("  (raw points=%u, read-to-clear)\n", buf[1] & 0x0F);

    /* Driver view, identical to `touch raw`, so the two never disagree */
    uint16_t x = 0, y = 0;
    lvgl_port_lock(0);
    bool touched = read_point(&x, &y);
    lvgl_port_unlock();
    printf("driver: %s", touched ? "touched" : "idle");
    if (touched) { printf(" at glass (%u,%u)", x, y); }
    printf("\n");
    return 0;
}

static int touch_inject_cmd(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: touch inject <x> <y> [hold_ms=150]  (glass coords, full tap)\n");
        return 1;
    }
    int x = atoi(argv[1]), y = atoi(argv[2]);
    /* Default hold 150 ms: long enough to span several LVGL ticks even when
     * the lvgl task is briefly busy (scene repaint / display flush), so a
     * simulated tap is not swallowed whole. Real fingers hold longer still. */
    int hold_ms = (argc > 3) ? atoi(argv[3]) : 150;
    if (x < 0 || x >= LCD_HRES || y < 0 || y >= LCD_VRES) {
        printf("coords out of range 0..%d x 0..%d\n", LCD_HRES - 1, LCD_VRES - 1);
        return 1;
    }
    if (hold_ms < 0 || hold_ms > 5000) {   /* the REPL blocks while held */
        printf("hold_ms must be 0..5000\n");
        return 1;
    }
    g_inj_x = x; g_inj_y = y; g_inj_down = 1;
    printf("touch inject (%d,%d) hold %d ms\n", x, y, hold_ms);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
    g_inj_down = 0;
    printf("touch released\n");
    return 0;
}

static int touch_up_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_inj_down = 0;
    printf("touch released\n");
    return 0;
}

static int touch_raw_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint16_t x = 0, y = 0;
    lvgl_port_lock(0);
    bool touched = read_point(&x, &y);
    lvgl_port_unlock();
    printf("injection %s; controller: %s", g_inj_down ? "ACTIVE" : "off",
           touched ? "touched" : "idle");
    if (touched) { printf(" at glass (%u,%u)", x, y); }
    printf("\n");
    return 0;
}

/* One esp_console command per command group; the first argument selects the
 * action, so `touch inject 10 20` is a single one-line command. */
static int touch_cmd(int argc, char **argv)
{
    if (argc < 2) {
        return touch_raw_cmd(argc, argv);
    }
    if (strcmp(argv[1], "i2c") == 0) {
        return touch_i2c_cmd(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "inject") == 0) {
        return touch_inject_cmd(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "up") == 0) {
        return touch_up_cmd(argc - 1, argv + 1);
    }
    if (strcmp(argv[1], "raw") == 0) {
        return touch_raw_cmd(argc - 1, argv + 1);
    }
    printf("usage: touch [i2c | inject <x> <y> [hold_ms] | up | raw]\n");
    return 1;
}

void touch_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "touch",
        .help = "touch i2c | touch inject <x> <y> [hold_ms] | touch up | touch raw",
        .hint = NULL,
        .func = &touch_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
