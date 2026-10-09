#ifndef CLOCK_DISPLAY_H
#define CLOCK_DISPLAY_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

/* LVGL works in glass coordinates: landscape 320x172 */
#define LCD_HRES 320
#define LCD_VRES 172
#define FB_BYTES (LCD_HRES * LCD_VRES * 2)

/* Panel GRAM geometry (JD9853): 240 columns (X, CASET) x 320 rows (Y, RASET).
 * The glass is mounted 90 degrees from the controller's native scan:
 * GRAM X is the physical vertical axis, GRAM Y the physical horizontal one. */
#define GRAM_X_MAX 240
#define GRAM_Y_MAX 320

extern esp_lcd_panel_io_handle_t g_io;
extern esp_lcd_panel_handle_t g_panel;
extern lv_display_t *g_disp;
extern uint8_t *g_fb;

/* LVGL (glass) -> GRAM mapping, tunable at runtime with `orient`:
 *   GRAM X = g_x_base + (flip_v ? (VRES-1-y) : y)   (glass vertical axis)
 *   GRAM Y = flip_h ? (HRES-1-x) : x                (glass horizontal axis) */
extern int g_x_base;
extern int g_flip_v;
extern int g_flip_h;
extern int g_flush_count;   /* incremented in disp_flush_cb (diagnostics) */
extern uint8_t *g_last_pxmap;
extern int g_last_area[4];

/* LVGL lock discipline: lvgl_port_lock(0) waits forever and the mutex is
 * recursive. Tasks may block on it, but esp_timer callbacks must not: they
 * share one task with every other esp_timer (Wi-Fi driver timers included)
 * and `fb dump` holds the lock for seconds. esp_timer callbacks take it
 * with this timeout and skip the tick when the lock is busy. */
#define UI_TIMER_LOCK_MS 50

esp_err_t display_hw_init(void);    /* SPI bus, panel io, JD9853, backlight */
void display_lvgl_init(void);       /* lvgl_port + 320x172 DIRECT framebuffer */
void panel_dma_barrier(void);       /* wait until all queued SPI DMA is done */
void gram_fill(int x0, int y0, int x1, int y1, uint16_t rgb565);
void display_repaint(void);         /* invalidate screen + render now */
/* Re-send the current framebuffer to the panel through the scene-composite
 * flush path WITHOUT an LVGL re-render (the scene task animates the
 * background at 10 Hz; widgets are only redrawn when LVGL invalidates them).
 * Caller must hold lvgl_port_lock. */
void display_flush_fb(void);

#endif
