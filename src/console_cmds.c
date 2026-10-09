/* Panel debug helpers (brightness, orientation, raw panel registers, fills,
 * repaint, render diagnostics). They are useful when porting to another
 * panel revision and the test tools use some of them. Every action is a
 * one-line command so it is scriptable over serial (see README). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "bsp_display.h"
#include "console_cmds.h"
#include "display.h"
#include "version.h"

#define LCD_CMD_MADCTL 0x36

static int hello_cmd(int argc, char **argv)
{
    printf("hello from %s v%s\n", FIRMWARE_NAME, FIRMWARE_VERSION);
    return 0;
}

static int info_cmd(int argc, char **argv)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("chip: cores=%d features=%04x\n", chip.cores, chip.features);
    printf("free heap: %u\n", (unsigned)esp_get_free_heap_size());
    /* largest contiguous 8-bit block: catches heap fragmentation (the TLS
     * handshake's in-buffer and RSA modexp allocations need contiguous RAM) */
    printf("largest block: %u\n",
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    printf("mapping: x_base=%d flip_v=%d flip_h=%d\n", g_x_base, g_flip_v, g_flip_h);
    return 0;
}

static int brightness_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: brightness <0-100>\n");
        return 1;
    }
    int pct = atoi(argv[1]);
    if (pct < 0 || pct > 100) {
        printf("brightness out of range\n");
        return 1;
    }
    bsp_display_set_brightness((uint8_t)pct);
    printf("brightness %d%%\n", pct);
    return 0;
}

static int invert_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: invert on|off\n");
        return 1;
    }
    bool on = (strcmp(argv[1], "on") == 0);
    lvgl_port_lock(0);   /* panel IO: never interleave with a scene flush */
    esp_lcd_panel_invert_color(g_panel, on);
    lvgl_port_unlock();
    printf("invert %s\n", on ? "on" : "off");
    return 0;
}

static int mirror_cmd(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: mirror <x 0|1> <y 0|1>\n");
        return 1;
    }
    bool mx = atoi(argv[1]) != 0;
    bool my = atoi(argv[2]) != 0;
    lvgl_port_lock(0);
    esp_lcd_panel_mirror(g_panel, mx, my);
    lvgl_port_unlock();
    printf("mirror x=%d y=%d\n", mx, my);
    return 0;
}

static int gap_cmd(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: gap <x_gap> <y_gap>\n");
        return 1;
    }
    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    lvgl_port_lock(0);
    esp_lcd_panel_set_gap(g_panel, x, y);
    lvgl_port_unlock();
    printf("gap x=%d y=%d\n", x, y);
    return 0;
}

static int madctl_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: madctl <hex>   (e.g. 00, 08=BGR, 40/80 mirror, 20 swap xy)\n");
        return 1;
    }
    uint8_t val = (uint8_t)strtoul(argv[1], NULL, 16);
    lvgl_port_lock(0);
    esp_lcd_panel_io_tx_param(g_io, LCD_CMD_MADCTL, &val, 1);
    lvgl_port_unlock();
    printf("MADCTL <- 0x%02X\n", val);
    return 0;
}

static int orient_cmd(int argc, char **argv)
{
    if (argc >= 4) {
        g_x_base = atoi(argv[1]);
        g_flip_v = atoi(argv[2]) != 0;
        g_flip_h = atoi(argv[3]) != 0;
    }
    printf("orient x_base=%d flip_v=%d flip_h=%d (run `repaint` to apply)\n",
           g_x_base, g_flip_v, g_flip_h);
    return 0;
}

static int rawfill_cmd(int argc, char **argv)
{
    if (argc < 6) {
        printf("usage: rawfill <x0> <y0> <x1> <y1> <rgb565>  (GRAM coords, end-exclusive, bypasses LVGL)\n");
        return 1;
    }
    int x0 = atoi(argv[1]), y0 = atoi(argv[2]);
    int x1 = atoi(argv[3]), y1 = atoi(argv[4]);
    uint16_t color = (uint16_t)strtoul(argv[5], NULL, 16);
    if (x0 < 0 || y0 < 0 || x1 > GRAM_X_MAX || y1 > GRAM_Y_MAX || x1 <= x0 || y1 <= y0) {
        printf("bad range (X 0..%d, Y 0..%d, end-exclusive)\n", GRAM_X_MAX, GRAM_Y_MAX);
        return 1;
    }
    lvgl_port_lock(0);
    gram_fill(x0, y0, x1, y1, color);
    lvgl_port_unlock();
    printf("rawfill (%d,%d)-(%d,%d) 0x%04X\n", x0, y0, x1, y1, color);
    return 0;
}

/* Paint an 8x8 coded grid over the WHOLE GRAM (full-width 8-row strips, the
 * shape that reliably lands). Cell (i,j): X band i (30 px wide), Y band j
 * (40 px tall), color R8~i*33, G8~130, B8~j*33  (RGB565: R5=i*4, G6=32, B5=j*4).
 * Seeing where each coded cell lands on the glass reveals the complete
 * GRAM->glass mapping. */
static int rawgrid_cmd(int argc, char **argv)
{
    static uint8_t strip[GRAM_X_MAX * 8 * 2];
    lvgl_port_lock(0);
    for (int y = 0; y < GRAM_Y_MAX; y += 8) {
        for (int r = 0; r < 8; r++) {
            int yy = y + r;
            int j4 = (yy / 40) * 4;
            for (int x = 0; x < GRAM_X_MAX; x++) {
                uint16_t v = (uint16_t)(((x / 30) * 4 << 11) | (32 << 5) | j4);
                strip[(r * GRAM_X_MAX + x) * 2] = v >> 8;
                strip[(r * GRAM_X_MAX + x) * 2 + 1] = v & 0xFF;
            }
        }
        esp_lcd_panel_draw_bitmap(g_panel, 0, y, GRAM_X_MAX, y + 8, strip);
    }
    panel_dma_barrier();
    lvgl_port_unlock();
    printf("rawgrid done (cell = X/30, Y/40; R~i*33 G~130 B~j*33)\n");
    return 0;
}

static int fill_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: fill <rgb565 hex>  (solid whole panel, bypasses LVGL)\n");
        return 1;
    }
    uint16_t color = (uint16_t)strtoul(argv[1], NULL, 16);
    lvgl_port_lock(0);
    gram_fill(0, 0, GRAM_X_MAX, GRAM_Y_MAX, color);
    lvgl_port_unlock();
    printf("fill 0x%04X done\n", color);
    return 0;
}

static int repaint_cmd(int argc, char **argv)
{
    display_repaint();
    printf("repainted\n");
    return 0;
}

static int lvdbg_cmd(int argc, char **argv)
{
    lvgl_port_lock(0);
    lv_display_t *d = lv_display_get_default();
    printf("disp=%p g_disp=%p hor=%d ver=%d scr=%p g_fb=%p flush_count=%d\n",
           (void *)d, (void *)g_disp,
           d ? (int)lv_display_get_horizontal_resolution(d) : -1,
           d ? (int)lv_display_get_vertical_resolution(d) : -1,
           (void *)lv_screen_active(), (void *)g_fb, g_flush_count);
    memset(g_fb, 0x5A, FB_BYTES);      /* sentinel everywhere */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    lvgl_port_unlock();
    printf("flush_count after repaint: %d\n", g_flush_count);
    printf("last flush: px_map=%p (g_fb=%p, delta=%d) area=(%d,%d)-(%d,%d)\n",
           (void *)g_last_pxmap, (void *)g_fb, (int)(g_last_pxmap - g_fb),
           g_last_area[0], g_last_area[1], g_last_area[2], g_last_area[3]);
    /* scan whole buffer for bytes the render wrote */
    int n = 0, first[8], last = -1;
    for (int i = 0; i < FB_BYTES; i++) {
        if (g_fb[i] != 0x5A) {
            if (n < 8) first[n] = i;
            n++;
            last = i;
        }
    }
    printf("non-sentinel bytes: %d, last offset %d\n", n, last);
    for (int i = 0; i < 8 && i < n; i++) {
        printf("  [%d] =", first[i]);
        for (int k = first[i]; k < first[i] + 4 && k < FB_BYTES; k++) {
            printf(" %02X", g_fb[k]);
        }
        printf("\n");
    }
    printf("head:");
    for (int i = 0; i < 16; i++) printf(" %02X", g_fb[i]);
    printf("\n");
    return 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int lcdreg_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: lcdreg <cmd hex> [hexdata]  (write raw panel register;\n");
        printf("       data = one compact hex string '3F322929...' or space-separated bytes)\n");
        return 1;
    }
    uint8_t cmd = (uint8_t)strtoul(argv[1], NULL, 16);
    uint8_t data[64];
    int n = 0;
    if (argc == 3 && strlen(argv[2]) > 2) {
        /* compact hex string: one arg, no argv-splitter limit */
        const char *s = argv[2];
        size_t len = strlen(s);
        for (size_t i = 0; i + 1 < len && n < 64; i += 2) {
            int hi = hexval(s[i]), lo = hexval(s[i + 1]);
            if (hi < 0 || lo < 0) { printf("bad hex at %zu\n", i); return 1; }
            data[n++] = (uint8_t)((hi << 4) | lo);
        }
    } else {
        for (int i = 2; i < argc && n < 64; i++) {
            data[n++] = (uint8_t)strtoul(argv[i], NULL, 16);
        }
    }
    lvgl_port_lock(0);
    esp_lcd_panel_io_tx_param(g_io, cmd, data, n);
    lvgl_port_unlock();
    printf("lcdreg 0x%02X <- %d bytes\n", cmd, n);
    return 0;
}

void console_register_commands(void)
{
    const esp_console_cmd_t commands[] = {
        {.command = "hello", .help = "say hello", .hint = NULL, .func = &hello_cmd},
        {.command = "info", .help = "chip, heap and mapping info", .hint = NULL, .func = &info_cmd},
        {.command = "brightness", .help = "set backlight 0-100", .hint = NULL, .func = &brightness_cmd},
        {.command = "invert", .help = "invert on|off", .hint = NULL, .func = &invert_cmd},
        {.command = "mirror", .help = "mirror <x 0|1> <y 0|1>", .hint = NULL, .func = &mirror_cmd},
        {.command = "gap", .help = "set panel gap <x> <y>", .hint = NULL, .func = &gap_cmd},
        {.command = "madctl", .help = "write raw MADCTL <hex>", .hint = NULL, .func = &madctl_cmd},
        {.command = "orient", .help = "orient [x_base flip_v flip_h] glass->GRAM mapping", .hint = NULL, .func = &orient_cmd},
        {.command = "rawfill", .help = "rawfill <x0> <y0> <x1> <y1> <rgb565> GRAM fill", .hint = NULL, .func = &rawfill_cmd},
        {.command = "rawgrid", .help = "paint 8x8 coded grid over whole GRAM", .hint = NULL, .func = &rawgrid_cmd},
        {.command = "fill", .help = "fill <rgb565> solid panel", .hint = NULL, .func = &fill_cmd},
        {.command = "repaint", .help = "re-render the LVGL screen to the panel", .hint = NULL, .func = &repaint_cmd},
        {.command = "lvdbg", .help = "LVGL render diagnostics", .hint = NULL, .func = &lvdbg_cmd},
        {.command = "fb", .help = "dump framebuffer (fb dump)", .hint = NULL, .func = &fbdump_cmd},
        {.command = "lcdreg", .help = "write raw panel register: lcdreg <cmd hex> <hexdata>", .hint = NULL, .func = &lcdreg_cmd},
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }
}
