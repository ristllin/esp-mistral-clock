/* Framebuffer dump over the console: `fb dump` renders the screen and streams
 * the full framebuffer as checksummed base64 rows:
 *   FB START <w> <h>
 *   FB <y> <fletcher16> <base64 of w*2 bytes>     (one line per row)
 *   FB DONE <fletcher16 of the whole buffer>
 * The host tool (tools/fbdump.py) validates every checksum and writes a PNG. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "console_cmds.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "display.h"
#include "scene.h"

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const uint8_t *data, size_t len, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        int n = 1;
        if (i + 1 < len) { v |= (uint32_t)data[i + 1] << 8; n = 2; }
        if (i + 2 < len) { v |= data[i + 2]; n = 3; }
        out[o++] = b64_table[(v >> 18) & 0x3F];
        out[o++] = b64_table[(v >> 12) & 0x3F];
        out[o++] = n >= 2 ? b64_table[(v >> 6) & 0x3F] : '=';
        out[o++] = n >= 3 ? b64_table[v & 0x3F] : '=';
    }
    out[o] = '\0';
    return o;
}

/* Running Fletcher-16: feeding the rows one after another gives the
 * checksum of their concatenation. */
typedef struct { uint16_t s1, s2; } fletcher16_t;

static void fletcher16_feed(fletcher16_t *f, const uint8_t *d, size_t n)
{
    while (n--) {
        f->s1 = (uint16_t)((f->s1 + *d++) % 255);
        f->s2 = (uint16_t)((f->s2 + f->s1) % 255);
    }
}

static unsigned fletcher16_value(const fletcher16_t *f)
{
    return (unsigned)((f->s2 << 8) | f->s1);
}

int fbdump_cmd(int argc, char **argv)
{
    /* `fb dump raw` skips the scene composite: theme-background key pixels
     * stay as they are, so a host tool can re-composite an animated scene
     * under the real widgets (tools/sim, README GIF). */
    bool raw = (argc >= 3 && strcmp(argv[2], "raw") == 0);
    /* mute ESP logs while the framebuffer streams: a log line from another
     * task interleaves with the dump rows and breaks the host-side decode */
    esp_log_level_t old_level = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);
    lvgl_port_lock(0);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);   /* render pending invalidations into g_fb (and panel) */
    printf("FB START %d %d\n", LCD_HRES, LCD_VRES);
    static char line[1024];
    static uint16_t crow[LCD_HRES];
    fletcher16_t whole = {0};   /* FB DONE: checksum of the whole buffer */
    for (int y = 0; y < LCD_VRES; y++) {
        const uint8_t *row = g_fb + (size_t)y * LCD_HRES * 2;
        /* apply the same scene composite the panel flush applies, so the
         * capture shows exactly what is on the glass */
        if (raw) {
            memcpy(crow, row, LCD_HRES * 2);
        } else {
            scene_composite_row(y, row, (uint8_t *)crow, 0, LCD_HRES - 1);
        }
        const uint8_t *cb = (const uint8_t *)crow;
        fletcher16_t r = {0};
        fletcher16_feed(&r, cb, LCD_HRES * 2);
        fletcher16_feed(&whole, cb, LCD_HRES * 2);
        b64_encode(cb, LCD_HRES * 2, line);
        printf("FB %d %u %s\n", y, fletcher16_value(&r), line);
    }
    printf("FB DONE %u\n", fletcher16_value(&whole));
    lvgl_port_unlock();
    esp_log_level_set("*", old_level);
    return 0;
}
