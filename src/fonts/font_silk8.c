/*******************************************************************************
 * Size: 8 px
 * Bpp: 4
 * Opts: --size 8 --bpp 4 --format lvgl --font assets/fonts/Silkscreen-Regular.ttf -r 0x20-0x7E,0xB0 --no-kerning --no-compress --lv-font-name font_silk8 -o src/fonts/font_silk8.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef FONT_SILK8
#define FONT_SILK8 1
#endif

#if FONT_SILK8

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0xff, 0xf0, 0xf0,

    /* U+0022 "\"" */
    0xf0, 0xff, 0xf,

    /* U+0023 "#" */
    0xf, 0xf, 0xf, 0xff, 0xff, 0xf, 0xf, 0xf,
    0xff, 0xff, 0xf, 0xf, 0x0,

    /* U+0024 "$" */
    0x0, 0xf0, 0xf, 0xff, 0xf0, 0x0, 0xf, 0xf0,
    0x0, 0xf, 0xff, 0xf0, 0xf, 0x0,

    /* U+0025 "%" */
    0xff, 0xf, 0xf, 0xf0, 0xf0, 0x0, 0xf0, 0x0,
    0xf0, 0xff, 0xf, 0xf, 0xf0,

    /* U+0026 "&" */
    0x0, 0xf0, 0xf, 0xff, 0xf0, 0x0, 0xf, 0xf0,
    0xf0, 0x0, 0xf, 0xff, 0x0, 0xf0,

    /* U+0027 "'" */
    0xff,

    /* U+0028 "(" */
    0xf, 0xf0, 0xf0, 0xf0, 0xf,

    /* U+0029 ")" */
    0xf0, 0xf, 0xf, 0xf, 0xf0,

    /* U+002A "*" */
    0x0, 0xf0, 0xf, 0xf, 0xf, 0xf, 0xff, 0xf,
    0xf, 0xf, 0x0, 0xf0, 0x0,

    /* U+002B "+" */
    0x0, 0xf0, 0x0, 0xf, 0x0, 0xff, 0xff, 0xf0,
    0xf, 0x0, 0x0, 0xf0, 0x0,

    /* U+002C "," */
    0xf, 0xf0,

    /* U+002D "-" */
    0xff, 0xf0,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0x0, 0xf0, 0xf, 0xf, 0xf, 0x0, 0xf0, 0x0,

    /* U+0030 "0" */
    0xf, 0xf0, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0031 "1" */
    0xff, 0x0, 0xf0, 0xf, 0x0, 0xf0, 0xff, 0xf0,

    /* U+0032 "2" */
    0xff, 0xf0, 0x0, 0xf, 0xf, 0xf0, 0xf0, 0x0,
    0xff, 0xff,

    /* U+0033 "3" */
    0xff, 0xf0, 0x0, 0xf, 0xf, 0xf0, 0x0, 0xf,
    0xff, 0xf0,

    /* U+0034 "4" */
    0xf0, 0xf0, 0xf0, 0xf0, 0xff, 0xff, 0x0, 0xf0,
    0x0, 0xf0,

    /* U+0035 "5" */
    0xff, 0xff, 0xf0, 0x0, 0xff, 0xf0, 0x0, 0xf,
    0xff, 0xf0,

    /* U+0036 "6" */
    0xf, 0xf0, 0xf0, 0x0, 0xfe, 0xe0, 0xf2, 0x2f,
    0xf, 0xf0,

    /* U+0037 "7" */
    0xff, 0xff, 0x0, 0xf, 0x0, 0xf0, 0xf, 0x0,
    0xf, 0x0,

    /* U+0038 "8" */
    0xf, 0xf0, 0xf0, 0xf, 0xf, 0xf0, 0xf3, 0x3f,
    0xf, 0xf0,

    /* U+0039 "9" */
    0xf, 0xf0, 0xf0, 0xf, 0xe, 0xef, 0x1, 0x1f,
    0xf, 0xf0,

    /* U+003A ":" */
    0xf0, 0xf0,

    /* U+003B ";" */
    0xf, 0x0, 0xf, 0xf0,

    /* U+003C "<" */
    0x0, 0xf0, 0xf0, 0xf0, 0x0, 0xf0, 0x0, 0xf0,

    /* U+003D "=" */
    0xff, 0xf0, 0x0, 0xff, 0xf0,

    /* U+003E ">" */
    0xf0, 0x0, 0xf0, 0x0, 0xf0, 0xf0, 0xf0, 0x0,

    /* U+003F "?" */
    0xff, 0xf0, 0x0, 0xf, 0xf, 0xf0, 0x0, 0x0,
    0xf, 0x0,

    /* U+0040 "@" */
    0xf, 0xff, 0xf, 0xf, 0xf, 0xf0, 0xff, 0xf,
    0x0, 0x0, 0xf, 0xff, 0x0,

    /* U+0041 "A" */
    0xf, 0xf0, 0xf0, 0xf, 0xff, 0xff, 0xf0, 0xf,
    0xf0, 0xf,

    /* U+0042 "B" */
    0xff, 0xf0, 0xf0, 0xf, 0xff, 0xff, 0xf0, 0xf,
    0xff, 0xf0,

    /* U+0043 "C" */
    0xf, 0xf0, 0xf0, 0xf, 0xf0, 0x0, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0044 "D" */
    0xff, 0xf0, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xff, 0xf0,

    /* U+0045 "E" */
    0xff, 0xff, 0x0, 0xff, 0xff, 0x0, 0xff, 0xf0,

    /* U+0046 "F" */
    0xff, 0xff, 0x0, 0xff, 0xff, 0x0, 0xf0, 0x0,

    /* U+0047 "G" */
    0xf, 0xff, 0xf0, 0x0, 0xf0, 0xff, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0048 "H" */
    0xf0, 0xf, 0xf0, 0xf, 0xff, 0xff, 0xf0, 0xf,
    0xf0, 0xf,

    /* U+0049 "I" */
    0xff, 0xff, 0xf0,

    /* U+004A "J" */
    0x0, 0xf, 0x0, 0xf, 0x0, 0xf, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+004B "K" */
    0xf0, 0xf, 0xf0, 0xf0, 0xff, 0x0, 0xf0, 0xf0,
    0xf0, 0xf,

    /* U+004C "L" */
    0xf0, 0xf, 0x0, 0xf0, 0xf, 0x0, 0xff, 0xf0,

    /* U+004D "M" */
    0xf0, 0x0, 0xff, 0xf0, 0xff, 0xf0, 0xf0, 0xff,
    0x0, 0xf, 0xf0, 0x0, 0xf0,

    /* U+004E "N" */
    0xf0, 0x0, 0xff, 0xf0, 0xf, 0xf0, 0xf0, 0xff,
    0x0, 0xff, 0xf0, 0x0, 0xf0,

    /* U+004F "O" */
    0xf, 0xf0, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0050 "P" */
    0xff, 0xf0, 0xf0, 0xf, 0xff, 0xf0, 0xf0, 0x0,
    0xf0, 0x0,

    /* U+0051 "Q" */
    0xf, 0xf0, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xf, 0xf0, 0x0, 0xf,

    /* U+0052 "R" */
    0xff, 0xf0, 0xf0, 0xf, 0xff, 0xf0, 0xf0, 0xf0,
    0xf0, 0xf,

    /* U+0053 "S" */
    0xf, 0xff, 0xf0, 0x0, 0xf, 0xf0, 0x0, 0xf,
    0xff, 0xf0,

    /* U+0054 "T" */
    0xff, 0xf0, 0xf0, 0xf, 0x0, 0xf0, 0xf, 0x0,

    /* U+0055 "U" */
    0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0056 "V" */
    0xf0, 0x0, 0xff, 0x0, 0xf, 0xf, 0xf, 0x0,
    0xf0, 0xf0, 0x0, 0xf0, 0x0,

    /* U+0057 "W" */
    0xf0, 0x0, 0xff, 0xf, 0xf, 0xf0, 0xf0, 0xff,
    0xf, 0xf, 0xf, 0xf, 0x0,

    /* U+0058 "X" */
    0xf0, 0x0, 0xf0, 0xf0, 0xf0, 0x0, 0xf0, 0x0,
    0xf0, 0xf0, 0xf0, 0x0, 0xf0,

    /* U+0059 "Y" */
    0xf0, 0x0, 0xf0, 0xf0, 0xf0, 0x0, 0xf0, 0x0,
    0xf, 0x0, 0x0, 0xf0, 0x0,

    /* U+005A "Z" */
    0xff, 0xf0, 0xf, 0xf, 0xf, 0x0, 0xff, 0xf0,

    /* U+005B "[" */
    0xff, 0xf0, 0xf0, 0xf0, 0xff,

    /* U+005C "\\" */
    0xf0, 0xf, 0x0, 0xf, 0x0, 0xf, 0x0, 0xf0,

    /* U+005D "]" */
    0xff, 0xf, 0xf, 0xf, 0xff,

    /* U+005E "^" */
    0xf, 0xf, 0xf,

    /* U+005F "_" */
    0xff, 0xff,

    /* U+0060 "`" */
    0xf0, 0xf,

    /* U+0061 "a" */
    0xf, 0xf0, 0xf0, 0xf, 0xff, 0xff, 0xf0, 0xf,
    0xf0, 0xf,

    /* U+0062 "b" */
    0xff, 0xf0, 0xf0, 0xf, 0xff, 0xff, 0xf0, 0xf,
    0xff, 0xf0,

    /* U+0063 "c" */
    0xf, 0xf0, 0xf0, 0xf, 0xf0, 0x0, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0064 "d" */
    0xff, 0xf0, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xff, 0xf0,

    /* U+0065 "e" */
    0xff, 0xff, 0x0, 0xff, 0xff, 0x0, 0xff, 0xf0,

    /* U+0066 "f" */
    0xff, 0xff, 0x0, 0xff, 0xff, 0x0, 0xf0, 0x0,

    /* U+0067 "g" */
    0xf, 0xff, 0xf0, 0x0, 0xf0, 0xff, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0068 "h" */
    0xf0, 0xf, 0xf0, 0xf, 0xff, 0xff, 0xf0, 0xf,
    0xf0, 0xf,

    /* U+0069 "i" */
    0xff, 0xff, 0xf0,

    /* U+006A "j" */
    0x0, 0xf, 0x0, 0xf, 0x0, 0xf, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+006B "k" */
    0xf0, 0xf, 0xf0, 0xf0, 0xff, 0x0, 0xf0, 0xf0,
    0xf0, 0xf,

    /* U+006C "l" */
    0xf0, 0xf, 0x0, 0xf0, 0xf, 0x0, 0xff, 0xf0,

    /* U+006D "m" */
    0xf0, 0x0, 0xff, 0xf0, 0xff, 0xf0, 0xf0, 0xff,
    0x0, 0xf, 0xf0, 0x0, 0xf0,

    /* U+006E "n" */
    0xf0, 0x0, 0xff, 0xf0, 0xf, 0xf0, 0xf0, 0xff,
    0x0, 0xff, 0xf0, 0x0, 0xf0,

    /* U+006F "o" */
    0xf, 0xf0, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0070 "p" */
    0xff, 0xf0, 0xf0, 0xf, 0xff, 0xf0, 0xf0, 0x0,
    0xf0, 0x0,

    /* U+0071 "q" */
    0xf, 0xf0, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xf, 0xf0, 0x0, 0xf,

    /* U+0072 "r" */
    0xff, 0xf0, 0xf0, 0xf, 0xff, 0xf0, 0xf0, 0xf0,
    0xf0, 0xf,

    /* U+0073 "s" */
    0xf, 0xff, 0xf0, 0x0, 0xf, 0xf0, 0x0, 0xf,
    0xff, 0xf0,

    /* U+0074 "t" */
    0xff, 0xf0, 0xf0, 0xf, 0x0, 0xf0, 0xf, 0x0,

    /* U+0075 "u" */
    0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf, 0xf0, 0xf,
    0xf, 0xf0,

    /* U+0076 "v" */
    0xf0, 0x0, 0xff, 0x0, 0xf, 0xf, 0xf, 0x0,
    0xf0, 0xf0, 0x0, 0xf0, 0x0,

    /* U+0077 "w" */
    0xf0, 0x0, 0xff, 0xf, 0xf, 0xf0, 0xf0, 0xff,
    0xf, 0xf, 0xf, 0xf, 0x0,

    /* U+0078 "x" */
    0xf0, 0x0, 0xf0, 0xf0, 0xf0, 0x0, 0xf0, 0x0,
    0xf0, 0xf0, 0xf0, 0x0, 0xf0,

    /* U+0079 "y" */
    0xf0, 0x0, 0xf0, 0xf0, 0xf0, 0x0, 0xf0, 0x0,
    0xf, 0x0, 0x0, 0xf0, 0x0,

    /* U+007A "z" */
    0xff, 0xf0, 0xf, 0xf, 0xf, 0x0, 0xff, 0xf0,

    /* U+007B "{" */
    0xf, 0xf0, 0xf0, 0xf0, 0x0, 0xf0, 0xf, 0xf0,

    /* U+007C "|" */
    0xff, 0xff, 0xff, 0xf0,

    /* U+007D "}" */
    0xff, 0x0, 0xf0, 0x0, 0xf0, 0xf0, 0xff, 0x0,

    /* U+007E "~" */
    0xf, 0xf, 0xf0, 0xf0,

    /* U+00B0 "°" */
    0xe, 0xf, 0x3f, 0xf, 0x0, 0x10
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 64, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 48, .box_w = 1, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 3, .adv_w = 80, .box_w = 3, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 6, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 19, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 33, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 46, .adv_w = 96, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 60, .adv_w = 48, .box_w = 1, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 61, .adv_w = 64, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 66, .adv_w = 64, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 71, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 97, .adv_w = 64, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 99, .adv_w = 80, .box_w = 3, .box_h = 1, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 101, .adv_w = 48, .box_w = 1, .box_h = 1, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 102, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 120, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 128, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 148, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 168, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 188, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 198, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 48, .box_w = 1, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 210, .adv_w = 64, .box_w = 2, .box_h = 4, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 214, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 80, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 227, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 235, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 245, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 258, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 268, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 278, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 288, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 298, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 314, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 324, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 334, .adv_w = 48, .box_w = 1, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 337, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 347, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 357, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 365, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 378, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 391, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 401, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 411, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 423, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 433, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 443, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 451, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 461, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 474, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 487, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 500, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 513, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 521, .adv_w = 64, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 526, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 534, .adv_w = 64, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 539, .adv_w = 80, .box_w = 3, .box_h = 2, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 542, .adv_w = 96, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 544, .adv_w = 64, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 546, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 556, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 566, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 576, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 586, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 594, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 602, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 612, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 622, .adv_w = 48, .box_w = 1, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 625, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 635, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 645, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 653, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 666, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 679, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 689, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 699, .adv_w = 96, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 711, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 721, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 731, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 739, .adv_w = 96, .box_w = 4, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 749, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 762, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 775, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 788, .adv_w = 112, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 801, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 809, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 817, .adv_w = 48, .box_w = 1, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 821, .adv_w = 80, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 829, .adv_w = 96, .box_w = 4, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 833, .adv_w = 80, .box_w = 3, .box_h = 4, .ofs_x = 1, .ofs_y = 1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 176, .range_length = 1, .glyph_id_start = 96,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t font_silk8 = {
#else
lv_font_t font_silk8 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 9,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FONT_SILK8*/

