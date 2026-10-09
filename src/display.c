/* Display bring-up and the LVGL->panel path for the Mistral clock.
 *
 * The JD9853 is driven over 4-wire SPI. The glass is mounted 90 degrees
 * from the controller's native scan direction (native window CASET 34..205
 * x RASET 0..319 = 172 wide x 320 tall; the glass is 320 wide x 172 tall),
 * so LVGL renders landscape 320x172 into one full-screen DMA buffer
 * (DIRECT mode) and the flush callback transposes every LVGL row into a
 * GRAM column strip. The mapping is explicit and tunable at runtime
 * (`orient` console command) instead of relying on MADCTL rotation bits. */
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "bsp_spi.h"
#include "bsp_display.h"
#include "esp_lcd_jd9853.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "display.h"
#include "scene.h"

#define LCD_CMD_NOP 0x00

static const char *TAG = "disp";

esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_disp;
uint8_t *g_fb;

int g_x_base = 34;
int g_flip_v = 0;
int g_flip_h = 1;    /* the panel maps GRAM Y to glass X reversed */
int g_flush_count = 0;
uint8_t *g_last_pxmap = 0;      /* last px_map passed to disp_flush_cb */
int g_last_area[4] = {0, 0, 0, 0};

static int gram_x_for_y(int y)
{
    return g_x_base + (g_flip_v ? (LCD_VRES - 1 - y) : y);
}

void panel_dma_barrier(void)
{
    /* esp_lcd_panel_io_tx_param drains all in-flight DMA color transactions
     * before sending the command, so a NOP leaves the panel fully updated. */
    esp_lcd_panel_io_tx_param(g_io, LCD_CMD_NOP, NULL, 0);
}

/* Send LVGL row y (pixels x1..x2, `row` points at x1) as one GRAM column
 * strip. With MADCTL=0 the panel streams the X counter fastest; a 1-wide
 * CASET range makes each pixel land on the next GRAM row (Y).
 * The panel wants each RGB565 pixel MSB-first; LVGL's RGB565 buffer is
 * little-endian, so bytes are swapped into a scratch row (never in place,
 * so the framebuffer always holds true pixel values for `fb dump`). The DMA
 * barrier after each row keeps async DMA off the scratch buffer. */
static void panel_send_row(int y, int x1, int x2, uint8_t *row)
{
    int gx = gram_x_for_y(y);
    int n = x2 - x1 + 1;
    /* Composite the live scene under the LVGL chrome first (theme-bg
     * pixels become scene pixels); the LVGL framebuffer itself stays pure
     * so anti-alias blending and `fb dump` see real LVGL output. */
    static uint16_t crow[LCD_HRES];
    scene_composite_row(y, row, (uint8_t *)crow, x1, x2);
    const uint16_t *src = crow;
    static uint8_t sw[LCD_HRES * 2];
    if (!g_flip_h) {
        for (int i = 0; i < n; i++) {
            sw[i * 2] = (uint8_t)(src[i] >> 8);
            sw[i * 2 + 1] = (uint8_t)(src[i] & 0xFF);
        }
        esp_lcd_panel_draw_bitmap(g_panel, gx, x1, gx + 1, x2 + 1, sw);
        panel_dma_barrier();
        return;
    }
    for (int i = 0; i < n; i++) {
        sw[i * 2] = (uint8_t)(src[n - 1 - i] >> 8);
        sw[i * 2 + 1] = (uint8_t)(src[n - 1 - i] & 0xFF);
    }
    esp_lcd_panel_draw_bitmap(g_panel, gx, GRAM_Y_MAX - 1 - x2, gx + 1, GRAM_Y_MAX - x1, sw);
    panel_dma_barrier();
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* DIRECT mode: px_map is the full-screen buffer base, stride LCD_HRES*2 */
    g_flush_count++;
    g_last_pxmap = px_map;
    g_last_area[0] = area->x1; g_last_area[1] = area->y1;
    g_last_area[2] = area->x2; g_last_area[3] = area->y2;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint8_t *row = px_map + ((size_t)y * LCD_HRES + area->x1) * 2;
        panel_send_row((int)y, (int)area->x1, (int)area->x2, row);
    }
    panel_dma_barrier();
    lv_display_flush_ready(disp);
}

/* Step-by-step hardware init with logged error checks (the BSP wrappers
 * ESP_ERROR_CHECK-abort, and panic text goes to UART0, not the USB console). */
esp_err_t display_hw_init(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "step: spi bus");
    bsp_spi_init();

    ESP_LOGI(TAG, "step: panel io");
    esp_lcd_panel_io_spi_config_t io_config = JD9853_PANEL_IO_SPI_CONFIG(EXAMPLE_PIN_LCD_CS, EXAMPLE_PIN_LCD_DC, NULL, NULL);
    io_config.pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_SPI_HOST, &io_config, &g_io);
    if (err != ESP_OK) { ESP_LOGE(TAG, "panel io: %s", esp_err_to_name(err)); return err; }

    ESP_LOGI(TAG, "step: jd9853 panel");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_jd9853(g_io, &panel_config, &g_panel);
    if (err != ESP_OK) { ESP_LOGE(TAG, "jd9853 panel: %s", esp_err_to_name(err)); return err; }

    ESP_LOGI(TAG, "step: panel reset");
    err = esp_lcd_panel_reset(g_panel);
    if (err != ESP_OK) { ESP_LOGE(TAG, "panel reset: %s", esp_err_to_name(err)); return err; }

    ESP_LOGI(TAG, "step: panel init");
    err = esp_lcd_panel_init(g_panel);
    if (err != ESP_OK) { ESP_LOGE(TAG, "panel init: %s", esp_err_to_name(err)); return err; }

    ESP_LOGI(TAG, "step: invert on");
    err = esp_lcd_panel_invert_color(g_panel, true);
    if (err != ESP_OK) { ESP_LOGE(TAG, "invert: %s", esp_err_to_name(err)); return err; }

    ESP_LOGI(TAG, "step: mirror off");
    err = esp_lcd_panel_mirror(g_panel, false, false);
    if (err != ESP_OK) { ESP_LOGE(TAG, "mirror: %s", esp_err_to_name(err)); return err; }

    ESP_LOGI(TAG, "step: disp on");
    err = esp_lcd_panel_disp_on_off(g_panel, true);
    if (err != ESP_OK) { ESP_LOGE(TAG, "disp on: %s", esp_err_to_name(err)); return err; }

    ESP_LOGI(TAG, "step: backlight init");
    bsp_display_brightness_init();

    ESP_LOGI(TAG, "step: backlight 100%%");
    bsp_display_set_brightness(100);

    return ESP_OK;
}

void display_lvgl_init(void)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    g_disp = lv_display_create(LCD_HRES, LCD_VRES);
    /* RGB565 (not _SWAPPED): LVGL 9.3's Kconfig has no
     * LV_DRAW_SW_SUPPORT_RGB565_SWAPPED option, so the SW renderer silently
     * draws NOTHING into RGB565_SWAPPED buffers in ESP-IDF builds. The flush
     * callback swaps bytes to panel order instead (esp_lvgl_port's
     * swap_bytes approach). */
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    g_fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!g_fb) {
        ESP_LOGE(TAG, "failed to allocate %d-byte framebuffer", FB_BYTES);
        return;
    }
    ESP_LOGI(TAG, "framebuffer %p (%d bytes), free heap %u",
             (void *)g_fb, FB_BYTES, (unsigned)esp_get_free_heap_size());
    lv_display_set_buffers(g_disp, g_fb, NULL, FB_BYTES, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(g_disp, disp_flush_cb);
}

/* Fill a GRAM rectangle (end-exclusive) with a solid RGB565 color,
 * bypassing LVGL entirely. Pixels go out MSB-first (panel byte order). */
void gram_fill(int x0, int y0, int x1, int y1, uint16_t rgb565)
{
    static uint8_t block[GRAM_X_MAX * 8 * 2];
    int w = x1 - x0;
    for (int i = 0; i < w; i++) {
        block[i * 2] = rgb565 >> 8;
        block[i * 2 + 1] = rgb565 & 0xFF;
    }
    for (int y = y0; y < y1; y += 8) {
        int rows = (y1 - y < 8) ? (y1 - y) : 8;
        for (int r = 1; r < rows; r++) {
            memcpy(block + r * w * 2, block, (size_t)w * 2);
        }
        esp_lcd_panel_draw_bitmap(g_panel, x0, y, x1, y + rows, block);
    }
    panel_dma_barrier();
}

void display_repaint(void)
{
    lvgl_port_lock(0);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    lvgl_port_unlock();
}

/* Full-screen flush of the CURRENT framebuffer contents (no widget
 * re-render) through the same per-row composite path disp_flush_cb uses, so
 * the live scene shows through the theme-background key pixels. This is what
 * lets the scene task animate at 10 Hz for ~6 ms of CPU per tick instead of
 * paying a full LVGL screen render (~10x more) every tick. Call with the
 * lvgl_port lock held: it shares the static row scratch with disp_flush_cb
 * and must not interleave with an LVGL flush. */
void display_flush_fb(void)
{
    if (!g_fb || !g_panel) { return; }
    for (int y = 0; y < LCD_VRES; y++) {
        uint8_t *row = g_fb + (size_t)y * LCD_HRES * 2;
        panel_send_row(y, 0, LCD_HRES - 1, row);
    }
    panel_dma_barrier();
}
