/* Live background scene engine.
 *
 * The scene lives in a half-resolution 160x86 4-bpp palette-indexed buffer
 * (6.9 KB static: the ESP32-C6 has no PSRAM and only ~50 KB free heap, so
 * the scene cannot own a full-resolution RGB565 buffer). The display
 * flush path upscales it 2x2: any LVGL framebuffer pixel equal to the
 * theme background colour is replaced with the scene pixel, so LVGL keeps
 * a pure framebuffer (anti-alias blending and `fb dump` stay correct) and
 * the chunky 2x2 scene pixels are a deliberate part of the pixel-art look.
 *
 * Paint order each 100 ms tick (scene task, under the LVGL lock):
 *   sky bands -> sun/moon/stars/clouds -> static layer art -> weather fx
 * so the window frame correctly occludes the sky and rain lands on the
 * glass in front of everything.
 *
 * Everything is deterministic art authored in tools/gen_sprite_art.py and
 * tools/gen_chaton.py (sprites) and below (skyline, hills, window). */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_console.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "scene.h"
#include "scene_art.h"
#include "chaton.h"
#include "scene_chaton.h"
#include "display.h"
#include "theme_mgr.h"
#include "time_mgr.h"
#include "weather_mgr.h"
#include "ui_home.h"

#define SCENE_TICK_MS 100
#define HORIZON_Y 58            /* half-res horizon for city/nature */

static const char *TAG = "scene";

/* ---------------- state ---------------- */

static uint8_t s_buf[SCENE_W * SCENE_H / 2];   /* 4bpp, high nibble = even x */
static uint16_t s_pal[P_COUNT];
static uint16_t s_bg_key;               /* theme bg = substitution key */
static volatile bool s_active;          /* composite on (home/saver) */
static volatile bool s_ready;
static scene_screen_t s_screen = SCENE_SCR_OTHER;
static scene_layer_t s_layer = SCENE_LAYER_AUTO;
static scene_layer_t s_layer_eff = SCENE_LAYER_CITY;
static scene_phase_t s_phase = SCENE_PHASE_DAY;
static uint32_t s_tick;

/* cached palette inputs (rebuilt when any changes) */
static theme_mode_t s_cur_theme = THEME_DARK;
static scene_phase_t s_cur_phase = SCENE_PHASE_DAY;
static int s_cur_storm;                 /* storm darkens the sky */
static int s_cur_flash;                 /* lightning brightens it */
static int s_cur_wx;                    /* weather shades the cloud deck */

/* ---------------- pixel ops ---------------- */

static inline int spx(int x, int y)
{
    return (s_buf[((size_t)y * SCENE_W + x) >> 1] >> ((x & 1) ? 0 : 4)) & 0xF;
}

static inline void sset(int x, int y, int v)
{
    /* Bounds check: several paint paths legitimately aim one pixel outside
     * (sun rays at x-9/x+9, rain streaks 3 px tall at the bottom edge, snow
     * at x+1); clipping here keeps every caller simple and s_buf safe. */
    if (x < 0 || x >= SCENE_W || y < 0 || y >= SCENE_H) { return; }
    uint8_t *b = &s_buf[((size_t)y * SCENE_W + x) >> 1];
    if (x & 1) { *b = (uint8_t)((*b & 0xF0) | v); }
    else       { *b = (uint8_t)((*b & 0x0F) | (v << 4)); }
}

static void sfill(int x0, int y0, int x1, int y1, int v)
{
    if (x0 < 0) { x0 = 0; }
    if (y0 < 0) { y0 = 0; }
    if (x1 >= SCENE_W) { x1 = SCENE_W - 1; }
    if (y1 >= SCENE_H) { y1 = SCENE_H - 1; }
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) { sset(x, y, v); }
    }
}

/* Stamp a palette-indexed sprite map (0 = transparent). */
static void stamp(const uint8_t *map, int w, int h, int x, int y)
{
    for (int j = 0; j < h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= SCENE_H) { continue; }
        for (int i = 0; i < w; i++) {
            int v = map[j * w + i];
            if (!v) { continue; }
            int dx = x + i;
            if (dx < 0 || dx >= SCENE_W) { continue; }
            sset(dx, dy, v);
        }
    }
}

/* Wrappers so scene_chaton.c can paint without owning the buffer. */
void scene_sset(int x, int y, int v) { sset(x, y, v); }
void scene_sfill(int x0, int y0, int x1, int y1, int v)
{
    sfill(x0, y0, x1, y1, v);
}
void scene_stamp(const uint8_t *map, int w, int h, int x, int y)
{
    stamp(map, w, h, x, y);
}
static int s_bolt_left;           /* remaining lightning flash ticks */
bool scene_bolt_active(void) { return s_bolt_left > 0; }

/* ---------------- palette: theme x phase grades ---------------- */

static uint16_t rgb565(int r, int g, int b)
{
    /* RRRRRGGGGGGBBBBB: exactly what lv_color_hex() produces, so palette
     * and substitution-key values match the LVGL framebuffer bit-for-bit */
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t lerp565(uint16_t a, uint16_t b, int num, int den)
{
    int ar = (a >> 8) & 0xF8, ag = (a >> 3) & 0xFC, ab = (a << 3) & 0xF8;
    int br = (b >> 8) & 0xF8, bg = (b >> 3) & 0xFC, bb = (b << 3) & 0xF8;
    return rgb565(ar + (br - ar) * num / den, ag + (bg - ag) * num / den,
                  ab + (bb - ab) * num / den);
}

typedef struct {
    uint32_t sky[4][4];     /* [phase][band], RGB888 top->horizon */
    uint32_t bld, bld2, glow, cloud, rain, snow, fog;
    uint32_t moon, white, dark, collar, acc;
} scene_grade_t;

/* The grades are the "colour grading" that keeps every theme designed:
 * skies are warm (cream/peach day, plum-orange dusk, deep brown night),
 * deliberately no default blue anywhere. RGB888; build_palette() converts. */
static const scene_grade_t s_grade[3] = {
    /* THEME_DARK: moody warm */
    { { { 0x241A2E, 0x4A2A3A, 0x8A3A2E, 0xE8702A },   /* dawn */
        { 0x33261E, 0x5C3A28, 0x8A5A36, 0xC07A42 },   /* day */
        { 0x2A1A2E, 0x5A2E3A, 0xA03A2E, 0xFF8205 },   /* sunset */
        { 0x0E0C14, 0x1A1420, 0x2E2233, 0x4A3040 } }, /* night */
      0x0A0806, 0xFFCD00, 0xFF8205, 0x5C5148, 0x6A7482, 0xC9C2B4,
      0x3A322C, 0xE8DCC0, 0xF5EDE0, 0x1A120B, 0xE75D2D, 0xFF8205 },
    /* THEME_LIGHT: cream and warm greys */
    { { { 0xE8D8E0, 0xF5C8A8, 0xFFB060, 0xFF9A3D },
        { 0xF6EFE3, 0xF9E8C8, 0xFBD9A8, 0xF5C080 },
        { 0xD8C8E0, 0xF5A868, 0xFF8A4A, 0xFA500F },
        { 0x3A3442, 0x4A4050, 0x5C4A54, 0x6E5A50 } },
      0x6B4A3A, 0xFFD84D, 0xD96A00, 0xC9B896, 0x7A8A9A, 0xFFFFFF,
      0xB8A88E, 0xF5EDE0, 0xFFFFFF, 0x2B2118, 0xD94A22, 0xD96A00 },
    /* THEME_MID: dusk brown */
    { { { 0x3A2A3A, 0x6B3A3A, 0xA85A3A, 0xE8823A },
        { 0x6B4A3A, 0x8A6248, 0xB07A50, 0xD89A60 },
        { 0x33222E, 0x6B3A3E, 0xA84A32, 0xFF8205 },
        { 0x14100E, 0x241A20, 0x3A2A30, 0x54404A } },
      0x1A120E, 0xFFD84D, 0xFFA02E, 0x8A7A66, 0x6A7482, 0xE8E0D0,
      0x4A3E34, 0xF2E7D3, 0xF2E7D3, 0x241A12, 0xE75D2D, 0xFFA02E },
};

static uint16_t c565(uint32_t c)
{
    return rgb565((int)(c >> 16), (int)((c >> 8) & 0xFF), (int)(c & 0xFF));
}

static void build_palette(void)
{
    const scene_grade_t *g = &s_grade[s_cur_theme];
    for (int b = 0; b < 4; b++) {
        uint16_t c = c565(g->sky[s_cur_phase][b]);
        if (s_cur_storm) { c = lerp565(c, rgb565(0x14, 0x11, 0x0E), 1, 3); }
        if (s_cur_flash) { c = lerp565(c, rgb565(0xFF, 0xF4, 0xD8), 1, 2); }
        s_pal[P_SKY0 + b] = c;
    }
    s_pal[P_BLD]   = c565(g->bld);
    s_pal[P_BLD2]  = c565(g->bld2);
    s_pal[P_GLOW]  = c565(g->glow);
    s_pal[P_CLOUD] = c565(g->cloud);
    /* Heavier weather = a darker cloud deck. */
    {
        int k = 0;   /* lerp toward near-black by k/10 */
        switch (s_cur_wx) {
        case SCENE_WX_CLOUD:   k = 2; break;
        case SCENE_WX_FOG:     k = 2; break;
        case SCENE_WX_DRIZZLE: k = 3; break;
        case SCENE_WX_RAIN:    k = 4; break;
        case SCENE_WX_SNOW:    k = 1; break;
        case SCENE_WX_STORM:   k = 5; break;
        default: break;
        }
        if (k) {
            s_pal[P_CLOUD] = lerp565(s_pal[P_CLOUD],
                                     rgb565(0x10, 0x0C, 0x0A), k, 10);
        }
    }
    s_pal[P_RAIN]  = c565(g->rain);
    s_pal[P_SNOW]  = c565(g->snow);
    s_pal[P_FOG]   = c565(g->fog);
    s_pal[P_MOON]  = c565(g->moon);
    s_pal[P_WHITE] = c565(g->white);
    s_pal[P_DARK]  = c565(g->dark);
    s_pal[P_COLLAR] = c565(g->collar);
    s_pal[P_ACC]   = c565(g->acc);
}

/* ---------------- sky ---------------- */

/* Band edges per layer (city/nature share the horizon; the window view is
 * taller, so its bands stretch over the full height). Band 3 runs to the
 * bottom and is overpainted by the layer art below the horizon. */
static const int s_band_city[4] = { 14, 30, 44, SCENE_H };
static const int s_band_win[4]  = { 22, 44, 66, SCENE_H };

static void paint_sky(scene_layer_t layer)
{
    const int *edges = (layer == SCENE_LAYER_WINDOW) ? s_band_win
                                                     : s_band_city;
    int prev = 0;
    for (int b = 0; b < 4; b++) {
        sfill(0, prev, SCENE_W - 1, edges[b] - 1, P_SKY0 + b);
        prev = edges[b];
    }
}

/* ---------------- city: a London-ish skyline ---------------- */

typedef struct { uint8_t x, w, h, kind; } bld_t;
/* kind: 0 block, 1 clock tower (Big Ben-ish), 2 shard (point),
 *       3 dome (St Paul's-ish), 4 gherkin (bullet) */
static const bld_t s_city[] = {
    { 0, 14, 16, 0 }, { 16, 10, 26, 1 }, { 28, 12, 14, 0 },
    { 42, 9, 22, 2 }, { 53, 14, 12, 0 }, { 69, 11, 27, 4 },
    { 82, 13, 15, 0 }, { 97, 10, 24, 3 }, { 109, 12, 13, 0 },
    { 123, 9, 19, 1 }, { 134, 12, 15, 0 }, { 148, 12, 22, 0 },
};

static uint32_t whash(int a, int b, int c)
{
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u
               ^ (uint32_t)c * 83492791u;
    return h;
}

static void paint_city(void)
{
    /* ground mass */
    sfill(0, HORIZON_Y, SCENE_W - 1, SCENE_H - 1, P_BLD);

    int lit_pct = (s_phase == SCENE_PHASE_NIGHT) ? 40
                : (s_phase == SCENE_PHASE_DAY) ? 6 : 22;

    for (size_t i = 0; i < sizeof(s_city) / sizeof(s_city[0]); i++) {
        const bld_t *b = &s_city[i];
        int top = HORIZON_Y - b->h;
        int cx = b->x + b->w / 2;
        switch (b->kind) {
        case 1:   /* clock tower with a lit face and a spire */
            sfill(b->x, top + 3, b->x + b->w - 1, HORIZON_Y - 1, P_BLD);
            sfill(cx - 2, top + 4, cx + 1, top + 7, P_GLOW);
            sset(cx - 1, top + 1, P_BLD); sset(cx, top + 1, P_BLD);
            sset(cx, top + 2, P_BLD);
            sset(cx - 1, top, P_BLD); sset(cx, top, P_BLD);
            break;
        case 2:   /* shard: pyramid */
            for (int r = 0; r < b->h; r++) {
                int half = (r * (b->w / 2)) / b->h;
                sfill(cx - half, top + r, cx + half, top + r, P_BLD);
            }
            sset(cx, top - 1, P_BLD);
            break;
        case 3:   /* dome: stepped half-ellipse */
            for (int r = 0; r < 8; r++) {
                int half = (r < 4) ? (r + 1) : (8 - r + 3);
                if (half > b->w / 2) { half = b->w / 2; }
                sfill(cx - half, top + r, cx + half, top + r, P_BLD);
            }
            sfill(b->x, top + 8, b->x + b->w - 1, HORIZON_Y - 1, P_BLD);
            sset(cx, top - 1, P_BLD);
            break;
        case 4:   /* gherkin: stacked narrowing segments */
            for (int r = 0; r < b->h; r++) {
                int seg = r / 4;
                int wseg = b->w - seg * 2;
                if (wseg < 3) { wseg = 3; }
                sfill(cx - wseg / 2, top + r, cx + wseg / 2, top + r, P_BLD);
            }
            break;
        default:  /* block */
            sfill(b->x, top, b->x + b->w - 1, HORIZON_Y - 1, P_BLD);
            break;
        }
        /* lit windows: a deterministic grid that changes every ~0.8 s */
        for (int wy = top + 2; wy < HORIZON_Y - 2; wy += 4) {
            for (int wx = b->x + 2; wx < b->x + b->w - 3; wx += 4) {
                if ((int)((whash(wx, wy, (int)(s_tick / 8)) >> 8) % 100)
                    < lit_pct) {
                    sfill(wx, wy, wx + 1, wy + 1, P_BLD2);
                }
            }
        }
    }
}

/* ---------------- nature: hills, trees, a river ---------------- */

/* Hill-top offsets above the horizon, computed on the fly (~0.3 ms per
 * tick) rather than tabulated: lookup tables would cost 1.2 KB of static
 * RAM that the weather fetch's TLS handshake needs as heap headroom. */
static int hill_far(int x)  { return 8 + (int)(6.0 * sin(x / 18.0)); }
static int hill_near(int x) { return 3 + (int)(4.0 * sin(x / 11.0 + 1.7)); }

static const uint8_t s_tree_x[6] = { 12, 34, 61, 88, 118, 141 };

static void paint_nature(void)
{
    for (int x = 0; x < SCENE_W; x++) {
        sfill(x, HORIZON_Y - hill_far(x), x, SCENE_H - 1, P_BLD2);
    }
    for (int x = 0; x < SCENE_W; x++) {
        sfill(x, HORIZON_Y - hill_near(x), x, SCENE_H - 1, P_BLD);
    }
    /* trees on the near hill */
    for (size_t i = 0; i < sizeof(s_tree_x); i++) {
        int x = s_tree_x[i];
        int y = HORIZON_Y - hill_near(x) - ART_TREE_H;
        stamp(art_tree, ART_TREE_W, ART_TREE_H, x - 3, y);
    }
    /* river along the bottom, reflecting the sky, with drifting sparkle */
    sfill(0, 74, SCENE_W - 1, SCENE_H - 1, P_SKY2);
    for (int x = 0; x < SCENE_W; x++) {
        if (((x / 3 + (int)(s_tick / 2)) % 9) < 2) {
            sset(x, 77, P_GLOW);
            if (((x / 5 + (int)(s_tick / 3)) % 11) < 2) {
                sset(x, 82, P_GLOW);
            }
        }
    }
}

/* ---------------- window: frame, sill, plant, mini cat ---------------- */

static void paint_window(void)
{
    /* glass is the sky painted full-height; the frame occludes it */
    sfill(0, 0, 5, SCENE_H - 1, P_DARK);          /* left jamb */
    sfill(154, 0, SCENE_W - 1, SCENE_H - 1, P_DARK);  /* right jamb */
    sfill(0, 0, SCENE_W - 1, 5, P_DARK);         /* lintel */
    sfill(78, 6, 81, 73, P_DARK);                /* mullion, vertical */
    sfill(6, 40, 153, 43, P_DARK);               /* mullion, horizontal */
    sfill(0, 74, SCENE_W - 1, SCENE_H - 1, P_DARK);   /* sill */
    sfill(0, 74, SCENE_W - 1, 74, P_BLD2);       /* sill top highlight */
    /* potted plant and mini cat on the sill, right: clear of the home
     * screen's weather plate (which covers the left ~60% of the sill) */
    stamp(art_plant, ART_PLANT_W, ART_PLANT_H, 122, 74 - ART_PLANT_H);
    /* the little Mistral cat sits on the sill and blinks now and then */
    const uint8_t *mc = ((s_tick % 40) < 2) ? art_minicat_1 : art_minicat_0;
    stamp(mc, ART_MINICAT_0_W, ART_MINICAT_0_H, 134, 75 - ART_MINICAT_0_H);
}

static void paint_static(scene_layer_t layer)
{
    switch (layer) {
    case SCENE_LAYER_CITY:   paint_city();   break;
    case SCENE_LAYER_NATURE: paint_nature(); break;
    case SCENE_LAYER_WINDOW: paint_window(); break;
    default:                 paint_city();   break;
    }
}

/* ---------------- dynamics: celestial bodies and clouds ---------------- */

static void sun_pos(int *x, int *y)
{
    struct tm tm;
    time_mgr_now(&tm);
    int len;
    int rel = weather_sun_clock(weather_mgr_sunrise_min(),
                                weather_mgr_sunset_min(),
                                tm.tm_hour * 60 + tm.tm_min, &len);
    int p = rel * 100 / len;
    if (p < 0) { p = 0; }
    if (p > 100) { p = 100; }
    *x = 8 + p * 144 / 100;
    /* Low arc: the sun sits on the horizon at sunrise/sunset (the skyline
     * occludes it there) and its top stays below the home clock digits. */
    *y = 62 - (int)(sin(p * M_PI / 100.0) * 32.0);
}

static void paint_sun(int cover)
{
    if (s_phase == SCENE_PHASE_NIGHT) { return; }
    if (cover >= 70) { return; }   /* overcast: the sun is hidden */
    int x, y;
    sun_pos(&x, &y);
    if (cover >= 40) {
        /* sun through haze: pale disc, no rays */
        for (int j = -3; j <= 3; j++) {
            for (int i = -3; i <= 3; i++) {
                if (i * i + j * j <= 10) { sset(x + i, y + j, P_MOON); }
            }
        }
        return;
    }
    /* disc */
    for (int j = -5; j <= 5; j++) {
        for (int i = -5; i <= 5; i++) {
            if (i * i + j * j <= 26) { sset(x + i, y + j, P_GLOW); }
        }
    }
    /* 8 slowly rotating rays */
    int a0 = (int)((s_tick * 3) % 360);
    for (int r = 0; r < 8; r++) {
        double a = (a0 + r * 45) * M_PI / 180.0;
        sset(x + (int)(cos(a) * 8), y + (int)(sin(a) * 8), P_GLOW);
        sset(x + (int)(cos(a) * 9), y + (int)(sin(a) * 9), P_GLOW);
    }
}

static void paint_moon_stars(int cover)
{
    if (s_phase == SCENE_PHASE_DAY) { return; }
    int dim = (s_phase == SCENE_PHASE_NIGHT) ? 0 : 1;
    /* the moon is hidden by a heavy cloud deck */
    if (cover < 80) {
        int mx = 122, my = 14 + (int)(2.0 * sin(s_tick / 20.0));
        stamp(art_moon, ART_MOON_W, ART_MOON_H, mx, my);
    }
    /* stars fade out with cloud cover */
    int budget = (dim ? 6 : 26) * (100 - cover) / 100;
    for (int i = 0; i < 24; i++) {
        int sx = (i * 37) % 150 + 4;
        int sy = (i * 53) % 34 + 4;
        int on = (int)(((s_tick + i * 17) % 40));
        if (on < budget) { sset(sx, sy, P_WHITE); }
    }
}

typedef struct { int16_t xq; int16_t y; uint8_t spd; uint8_t kind; } cloud_t;
static cloud_t s_cloud[6];

static const uint8_t *cloud_map(int kind, int frame)
{
    switch (kind) {
    case 0:  return frame ? art_cloud_0b : art_cloud_0a;
    case 1:  return frame ? art_cloud_1b : art_cloud_1a;
    default: return frame ? art_cloud_2b : art_cloud_2a;
    }
}
static void cloud_size(int kind, int *w, int *h)
{
    switch (kind) {
    case 0:  *w = ART_CLOUD_0A_W; *h = ART_CLOUD_0A_H; break;
    case 1:  *w = ART_CLOUD_1A_W; *h = ART_CLOUD_1A_H; break;
    default: *w = ART_CLOUD_2A_W; *h = ART_CLOUD_2A_H; break;
    }
}

/* Cloud count follows the real cloud cover: clear sky drifts one or two
 * clouds, overcast is a full deck. */
static int cloud_count(int cover)
{
    int n = 1 + cover * 5 / 100;
    if (n > 6) { n = 6; }
    return n;
}

static void clouds_init(void)
{
    for (int i = 0; i < 6; i++) {
        s_cloud[i].xq = (int16_t)((i * 53) % (SCENE_W * 4));
        s_cloud[i].y  = 8 + (i * 7) % 26;
        s_cloud[i].spd = 3 + (uint8_t)(i % 4);   /* quarter px per tick */
        s_cloud[i].kind = (uint8_t)(i % 3);
    }
}

static void paint_clouds(int cover)
{
    int n = cloud_count(cover);
    int frame = (int)((s_tick / 15) & 1);
    for (int i = 0; i < n; i++) {
        cloud_t *c = &s_cloud[i];
        c->xq += c->spd;
        int w, h;
        cloud_size(c->kind, &w, &h);
        if (c->xq >= SCENE_W * 4) { c->xq = -w * 4; }
        int x = c->xq / 4;
        if (c->y + h > HORIZON_Y - 2) { c->y = HORIZON_Y - 2 - h; }
        stamp(cloud_map(c->kind, frame), w, h, x, c->y);
    }
}

/* ---------------- dynamics: rain, snow, fog, lightning ---------------- */

typedef struct { int16_t x; int16_t yq; uint8_t spd; } drop_t;
static drop_t s_rain[56];
static drop_t s_snow[56];

typedef struct { int16_t xq; int16_t y; } fog_t;
static fog_t s_fog[3];

static uint32_t s_bolt_next;      /* tick of the next lightning bolt */
static int s_bolt_x;

static void precip_init(void)
{
    for (int i = 0; i < 56; i++) {
        s_rain[i].x = (int16_t)((i * 29) % SCENE_W);
        s_rain[i].yq = (int16_t)((i * 47) % (SCENE_H * 4));
        s_rain[i].spd = (uint8_t)(6 + (i % 5) * 2);      /* qtr px / tick */
        s_snow[i].x = (int16_t)((i * 41) % SCENE_W);
        s_snow[i].yq = (int16_t)((i * 67) % (SCENE_H * 4));
        s_snow[i].spd = (uint8_t)(1 + (i % 3));         /* qtr px / tick */
    }
    s_fog[0].xq = 0;   s_fog[0].y = 30;
    s_fog[1].xq = 40;  s_fog[1].y = 42;
    s_fog[2].xq = 90;  s_fog[2].y = 52;
    s_bolt_next = 40;
}

/* Rain/snow stop at the window glass (they are on the glass, not inside). */
static int precip_limit(scene_layer_t layer)
{
    return (layer == SCENE_LAYER_WINDOW) ? 73 : SCENE_H - 1;
}

static void paint_rain(scene_wx_t wx, scene_layer_t layer)
{
    int n, spd0;
    if (wx == SCENE_WX_STORM)      { n = 56; spd0 = 10; }
    else if (wx == SCENE_WX_RAIN)  { n = 40; spd0 = 8; }
    else if (wx == SCENE_WX_DRIZZLE) { n = 18; spd0 = 5; }
    else { return; }
    int limit = precip_limit(layer);
    for (int i = 0; i < n; i++) {
        drop_t *d = &s_rain[i];
        d->yq += d->spd;
        if (d->yq >= limit * 4) {
            d->yq = 0;
            d->x = (int16_t)((whash(i, (int)s_tick, 7) >> 8) % SCENE_W);
            d->spd = (uint8_t)(spd0 + (i % 5));
        }
        int y = d->yq / 4;
        sset(d->x, y, P_RAIN);
        sset(d->x, y + 1, P_RAIN);
        sset(d->x, y + 2, P_RAIN);
        if (wx == SCENE_WX_STORM) { sset(d->x + 1, y + 1, P_RAIN); }
    }
}

static void paint_snow(scene_layer_t layer)
{
    int limit = precip_limit(layer);
    for (int i = 0; i < 56; i++) {
        drop_t *d = &s_snow[i];
        d->yq += d->spd;
        if (d->yq >= limit * 4) {
            d->yq = 0;
            d->x = (int16_t)((whash(i, (int)s_tick, 13) >> 8) % SCENE_W);
        }
        int y = d->yq / 4;
        int wob = (int)(2.0 * sin((s_tick + i * 9) / 6.0));
        int x = d->x + wob;
        if (x < 0) { x = 0; }
        if (x >= SCENE_W) { x = SCENE_W - 1; }
        sset(x, y, P_SNOW);
        if ((i & 3) == 0) { sset(x + 1, y, P_SNOW); }
    }
}

static void paint_fog(scene_layer_t layer)
{
    int base = (layer == SCENE_LAYER_WINDOW) ? 8 : 0;
    for (int b = 0; b < 3; b++) {
        fog_t *f = &s_fog[b];
        f->xq += (b & 1) ? 1 : 2;
        if (f->xq >= SCENE_W * 4) { f->xq = 0; }
        int x0 = f->xq / 4;
        int y = f->y + base;
        if (y > HORIZON_Y + 6) { y = HORIZON_Y + 6; }
        /* dashed band, wraps so the drift is visible */
        for (int i = -12; i < SCENE_W + 12; i += 12) {
            int xa = (x0 + i) % SCENE_W;
            if (xa < 0) { xa += SCENE_W; }
            sfill(xa, y, xa + 4, y + 2, P_FOG);
        }
    }
}

static void paint_lightning(void)
{
    if (s_bolt_left > 0) {
        s_bolt_left--;
        stamp(art_bolt, ART_BOLT_W, ART_BOLT_H, s_bolt_x, 4);
        return;
    }
    if (s_tick >= s_bolt_next) {
        s_bolt_left = 3;
        s_bolt_x = 20 + (int)((whash((int)s_tick, 3, 5) >> 8) % 120);
        s_bolt_next = s_tick + 30 + (whash((int)s_tick, 9, 2) >> 8) % 50;
    }
}

/* ---------------- phase, paint-all, composite ---------------- */

static scene_phase_t compute_phase(void)
{
    struct tm tm;
    time_mgr_now(&tm);
    int len;
    int rel = weather_sun_clock(weather_mgr_sunrise_min(),
                                weather_mgr_sunset_min(),
                                tm.tm_hour * 60 + tm.tm_min, &len);
    if (rel < -45 || rel >= len + 45) { return SCENE_PHASE_NIGHT; }
    if (rel < 45)        { return SCENE_PHASE_DAWN; }
    if (rel < len - 45)  { return SCENE_PHASE_DAY; }
    return SCENE_PHASE_SUNSET;
}

static scene_layer_t resolve_layer(scene_layer_t want)
{
    if (want != SCENE_LAYER_AUTO) { return want; }
    return (s_phase == SCENE_PHASE_DAY) ? SCENE_LAYER_NATURE
                                        : SCENE_LAYER_CITY;
}

static void paint_all(theme_mode_t theme, scene_phase_t phase,
                      scene_wx_t wx)
{
    s_phase = phase;
    scene_layer_t layer = resolve_layer(s_layer);
    s_layer_eff = layer;

    int storm = (wx == SCENE_WX_STORM);
    int flash = (wx == SCENE_WX_STORM && s_bolt_left > 0);
    if (theme != s_cur_theme || phase != s_cur_phase || storm != s_cur_storm
        || flash != s_cur_flash || (int)wx != s_cur_wx) {
        s_cur_theme = theme;
        s_cur_phase = phase;
        s_cur_storm = storm;
        s_cur_flash = flash;
        s_cur_wx = (int)wx;
        build_palette();
    }

    /* Real cloud cover drives sun/moon/star visibility and the cloud
     * count. Forced weather uses a nominal cover; unknown defaults mid. */
    int cover = weather_mgr_cloud_cover();
    if (cover < 0) { cover = 50; }

    /* Screensaver truthfulness rule: with no fresh real weather data (or
     * no synced clock) the environment must not invent a sky, so paint a
     * neutral indoor scene instead. The `weather force` debug override
     * bypasses the rule. */
    bool neutral = (s_screen == SCENE_SCR_SAVER) && scene_chaton_neutral();
    if (!neutral) {
        paint_sky(layer);
        paint_sun(cover);
        paint_moon_stars(cover);
        paint_clouds(cover);
    }
    if (s_screen == SCENE_SCR_SAVER) {
        scene_chaton_paint(phase, wx, neutral);
    } else {
        paint_static(layer);
    }
    if (!neutral) {
        paint_rain(wx, layer);
        if (wx == SCENE_WX_SNOW) { paint_snow(layer); }
        if (wx == SCENE_WX_FOG)  { paint_fog(layer); }
        if (wx == SCENE_WX_STORM) { paint_lightning(); }
    }
}

void scene_composite_row(int y, const uint8_t *src, uint8_t *dst,
                         int x1, int x2)
{
    const uint16_t *s = (const uint16_t *)src;
    uint16_t *d = (uint16_t *)dst;
    int n = x2 - x1 + 1;
    if (!s_active || !s_ready || y < 0 || y >= LCD_VRES || n <= 0) {
        memcpy(d, s, (size_t)n * 2);
        return;
    }
    uint16_t key = s_bg_key;
    int sy = y >> 1;
    for (int i = 0; i < n; i++) {
        uint16_t px = s[i];
        if (px == key) {
            int sx = (x1 + i) >> 1;
            int idx = spx(sx, sy);
            px = s_pal[idx];
        }
        d[i] = px;
    }
}

/* ---------------- task, public API, commands ---------------- */

static void scene_task(void *arg)
{
    (void)arg;
    while (!s_ready) { vTaskDelay(pdMS_TO_TICKS(50)); }
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SCENE_TICK_MS));
        s_tick++;
        theme_mode_t theme = theme_mgr_resolved();
        scene_phase_t phase = compute_phase();
        scene_wx_t wx = weather_mgr_scene_wx();
        lvgl_port_lock(0);
        paint_all(theme, phase, wx);
        /* Push the new scene WITHOUT an LVGL re-render: the widgets are
         * static between their own invalidations, so re-sending the current
         * framebuffer through the composite path is enough. Invalidating
         * the screen instead would make LVGL redraw every widget 10x per
         * second, which (with the scene paint on top) starves the IDLE task
         * and trips the task watchdog. */
        if (s_screen == SCENE_SCR_HOME || s_screen == SCENE_SCR_SAVER) {
            display_flush_fb();
        }
        /* LOCK DISCIPLINE: tasks block indefinitely on lvgl_port_lock()
         * (timeout 0) and never touch the panel without it. Flushing
         * unlocked while another holder (e.g. `fb dump`) drives SPI DMA
         * wedges the panel driver. Holders only ever wait on hardware,
         * never on another task, so indefinite blocking cannot deadlock.
         * esp_timer callbacks use a short timeout instead (display.h). */
        lvgl_port_unlock();
    }
}

void scene_set_screen(scene_screen_t s)
{
    s_screen = s;
    s_active = (s == SCENE_SCR_HOME || s == SCENE_SCR_SAVER);
    if (s_active) {
        lvgl_port_lock(0);
        lv_obj_t *scr = ui_home_screen();
        if (scr) { lv_obj_invalidate(scr); }
        lvgl_port_unlock();
    }
}

scene_screen_t scene_screen(void) { return s_screen; }

/* The theme palettes store RGB888; the framebuffer holds RGB565. The
 * substitution key must be the RGB565 conversion (exactly what
 * lv_color_hex() produces), not a truncation. */
static uint16_t bg_key(uint32_t rgb888)
{
    return rgb565((int)(rgb888 >> 16), (int)((rgb888 >> 8) & 0xFF),
                  (int)(rgb888 & 0xFF));
}

void scene_theme_changed(void)
{
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    lvgl_port_lock(0);
    s_bg_key = bg_key(p->bg);
    build_palette();
    lv_obj_t *scr = ui_home_screen();
    if (scr) { lv_obj_invalidate(scr); }
    lvgl_port_unlock();
}

static const char *layer_name(scene_layer_t layer)
{
    switch (layer) {
    case SCENE_LAYER_CITY:   return "city";
    case SCENE_LAYER_NATURE: return "nature";
    case SCENE_LAYER_WINDOW: return "window";
    default:                 return "auto";
    }
}

static const char *phase_name(void)
{
    switch (s_phase) {
    case SCENE_PHASE_DAWN:   return "dawn";
    case SCENE_PHASE_DAY:    return "day";
    case SCENE_PHASE_SUNSET: return "sunset";
    default:                 return "night";
    }
}

static const char *wx_name(scene_wx_t wx)
{
    switch (wx) {
    case SCENE_WX_CLEAR:   return "clear";
    case SCENE_WX_CLOUD:   return "cloud";
    case SCENE_WX_DRIZZLE: return "drizzle";
    case SCENE_WX_RAIN:    return "rain";
    case SCENE_WX_SNOW:    return "snow";
    case SCENE_WX_STORM:   return "storm";
    case SCENE_WX_FOG:     return "fog";
    default:               return "?";
    }
}

static int scene_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("scene:    %s (configured %s)\n", layer_name(s_layer_eff),
               layer_name(s_layer));
        printf("phase:    %s   wx: %s   screen: %s\n",
               phase_name(), wx_name(weather_mgr_scene_wx()),
               s_screen == SCENE_SCR_HOME ? "home"
               : s_screen == SCENE_SCR_SAVER ? "saver" : "other");
        printf("usage: scene [city|nature|window|auto]\n");
        return 0;
    }
    scene_layer_t want;
    if (!strcmp(argv[1], "city"))         { want = SCENE_LAYER_CITY; }
    else if (!strcmp(argv[1], "nature"))  { want = SCENE_LAYER_NATURE; }
    else if (!strcmp(argv[1], "window"))  { want = SCENE_LAYER_WINDOW; }
    else if (!strcmp(argv[1], "auto"))    { want = SCENE_LAYER_AUTO; }
    else {
        printf("usage: scene [city|nature|window|auto]\n");
        return 1;
    }
    s_layer = want;
    nvs_handle_t h;
    if (nvs_open("scene", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "layer", (uint8_t)want);
        nvs_commit(h);
        nvs_close(h);
    }
    lvgl_port_lock(0);
    lv_obj_t *scr = ui_home_screen();
    if (scr) { lv_obj_invalidate(scr); }
    lvgl_port_unlock();
    /* the configured layer: "auto" resolves on the next paint tick */
    printf("scene: %s\n", argv[1]);
    return 0;
}

void scene_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "scene",
        .help = "live background scene: [city|nature|window|auto]",
        .hint = NULL,
        .func = &scene_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

esp_err_t scene_init(void)
{
    nvs_handle_t h;
    if (nvs_open("scene", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = SCENE_LAYER_AUTO;
        if (nvs_get_u8(h, "layer", &v) == ESP_OK && v < SCENE_LAYER_COUNT) {
            s_layer = (scene_layer_t)v;
        }
        nvs_close(h);
    }
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    s_bg_key = bg_key(p->bg);
    clouds_init();
    precip_init();
    build_palette();
    s_ready = true;
    xTaskCreate(scene_task, "scene", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "scene engine up: layer %s, %dx%d 4bpp, %d ms tick",
             layer_name(s_layer), SCENE_W, SCENE_H, SCENE_TICK_MS);
    return ESP_OK;
}
