/* Host simulator for the scene renderer (src/scene.c + src/scene_chaton.c).
 *
 * Compiles the real firmware sources against tiny stubs (tools/sim/stubs) and
 * the controllable fakes below, then writes raw RGB565 frames exactly as the
 * device would composite them. Used for fast art iteration and for the README
 * GIF (tools/sim/render.py drives it). Not part of the firmware build.
 *
 *   sim <out.rgb565> <frames> <screen saver|home> <theme dark|light|mid>
 *       <phase dawn|day|sunset|night> <wx clear|cloud|fog|drizzle|rain|snow|storm>
 *       <layer city|nature|window|auto> <scenario auto|sleep|loaf|chase|eat|mchase|curl>
 *       [hour] [temp_c] [neutral 0|1] [chrome.rgb565]
 *
 * With a chrome file (an `fb dump raw` of the home screen, RGB565 LE), every
 * chrome pixel equal to the theme background key is replaced by the scene,
 * which is what display.c does on the device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../src/scene.c"
#include "../../src/scene_chaton.c"

/* ---------------- controllable fakes ---------------- */
static int f_hour = 12, f_wday = 3;          /* Wednesday noon by default */
static float f_temp = 12.0f;
static scene_wx_t f_wx = SCENE_WX_CLEAR;
static theme_mode_t f_theme = THEME_DARK;
static int f_cover = 30;
static weather_data_t f_data;

bool time_mgr_is_synced(void) { return true; }
void time_mgr_now(struct tm *out)
{
    memset(out, 0, sizeof(*out));
    out->tm_year = 126; out->tm_mon = 9; out->tm_mday = 7 + f_wday;
    out->tm_wday = f_wday; out->tm_hour = f_hour; out->tm_min = 0;
}
int weather_mgr_sunrise_min(void) { return 7 * 60 + 12; }
int weather_mgr_sunset_min(void) { return 18 * 60 + 22; }
int weather_mgr_cloud_cover(void) { return f_cover; }
scene_wx_t weather_mgr_scene_wx(void) { return f_wx; }
const weather_data_t *weather_mgr_data(void) { return &f_data; }
int weather_mgr_age_min(void) { return 1; }
bool weather_mgr_forced(void) { return false; }
theme_mode_t theme_mgr_resolved(void) { return f_theme; }
static theme_palette_t f_pal;
void theme_mgr_colors(const theme_palette_t **out) { *out = &f_pal; }
lv_obj_t *ui_home_screen(void) { return NULL; }
void display_flush_fb(void) {}

static int parse(const char *s, const char *const *names, int n)
{
    for (int i = 0; i < n; i++) { if (!strcmp(s, names[i])) { return i; } }
    fprintf(stderr, "unknown value '%s'\n", s);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc < 9) {
        fprintf(stderr, "usage: see header of tools/sim/sim.c\n");
        return 2;
    }
    static const char *const scr_n[] = { "home", "saver" };
    static const char *const thm_n[] = { "dark", "light", "mid" };
    static const char *const ph_n[] = { "dawn", "day", "sunset", "night" };
    static const char *const wx_n[] = { "clear", "cloud", "fog",
                                        "drizzle", "rain", "snow", "storm" };
    static const scene_wx_t wx_v[] = { SCENE_WX_CLEAR, SCENE_WX_CLOUD,
        SCENE_WX_FOG, SCENE_WX_DRIZZLE, SCENE_WX_RAIN, SCENE_WX_SNOW,
        SCENE_WX_STORM };
    static const char *const ly_n[] = { "city", "nature", "window", "auto" };
    static const theme_mode_t thm_v[] = { THEME_DARK, THEME_LIGHT, THEME_MID };

    const char *out = argv[1];
    int frames = atoi(argv[2]);
    int screen = parse(argv[3], scr_n, 2);
    f_theme = thm_v[parse(argv[4], thm_n, 3)];
    scene_phase_t phase = (scene_phase_t)parse(argv[5], ph_n, 4);
    f_wx = wx_v[parse(argv[6], wx_n, 7)];
    s_layer = (scene_layer_t)parse(argv[7], ly_n, 4);
    if (scene_chaton_set_scenario(argv[8])) { fprintf(stderr, "bad scenario\n"); return 2; }
    if (argc > 9)  { f_hour = atoi(argv[9]); }
    if (argc > 10) { f_temp = (float)atof(argv[10]); }
    if (argc > 11 && atoi(argv[11])) { scene_chaton_set_neutral_dbg(true); }
    const char *chrome_path = argc > 12 ? argv[12] : NULL;

    f_data.valid = true;
    f_data.temp_c = f_temp;
    f_cover = (f_wx == SCENE_WX_CLEAR) ? 5 : 95;

    static uint16_t chrome[LCD_HRES * LCD_VRES];
    uint16_t key = 0;
    if (chrome_path) {
        FILE *cf = fopen(chrome_path, "rb");
        if (!cf || fread(chrome, 2, LCD_HRES * LCD_VRES, cf) != LCD_HRES * LCD_VRES) {
            fprintf(stderr, "cannot read chrome %s\n", chrome_path);
            return 1;
        }
        fclose(cf);
        key = (uint16_t)atoi(getenv("SIM_KEY") ? getenv("SIM_KEY") : "0");
    }

    s_screen = screen ? SCENE_SCR_SAVER : SCENE_SCR_HOME;
    s_cur_wx = -1;            /* force build_palette() on the first paint */
    clouds_init();            /* seed the particles as scene_init() does */
    precip_init();
    FILE *f = fopen(out, "wb");
    if (!f) { perror(out); return 1; }
    static uint16_t frame[LCD_HRES * LCD_VRES];
    for (int n = 0; n < frames; n++) {
        s_tick++;
        paint_all(f_theme, phase, f_wx);
        for (int y = 0; y < LCD_VRES; y++) {
            for (int x = 0; x < LCD_HRES; x++) {
                uint16_t px = s_pal[spx(x >> 1, y >> 1)];
                if (chrome_path) {
                    uint16_t c = chrome[y * LCD_HRES + x];
                    px = (c == key) ? px : c;
                }
                frame[y * LCD_HRES + x] = px;
            }
        }
        fwrite(frame, 2, LCD_HRES * LCD_VRES, f);
    }
    fclose(f);
    return 0;
}
