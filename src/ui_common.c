/* Shared LVGL building blocks, see ui_common.h. */
#include "lvgl.h"

#include "fonts/fonts.h"
#include "mistral_logo.h"
#include "ui_common.h"

const lv_image_dsc_t ui_logo_dsc = {
    .header = { .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_RGB565A8,
                .w = mistral_logo_img_W, .h = mistral_logo_img_H,
                .stride = mistral_logo_img_W * 2 },
    .data_size = mistral_logo_img_A8_SIZE,
    .data = (const uint8_t *)mistral_logo_img_a8,
};

static const theme_palette_t *palette(void)
{
    const theme_palette_t *p;
    theme_mgr_colors(&p);
    return p;
}

lv_obj_t *ui_screen_base(lv_obj_t *scr)
{
    if (!scr) { scr = lv_obj_create(NULL); }
    lv_obj_set_style_bg_color(scr, lv_color_hex(palette()->bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, uint32_t color,
                   int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font_px8, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y);
    return l;
}

lv_obj_t *ui_chip_button(lv_obj_t *parent, int x, int y, int w, int h,
                         const char *text, lv_event_cb_t cb,
                         lv_obj_t **label)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 2, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font_px8, 0);
    lv_obj_center(l);
    ui_chip_recolor(btn, l, palette());
    if (label) { *label = l; }
    return btn;
}

void ui_chip_recolor(lv_obj_t *btn, lv_obj_t *label, const theme_palette_t *p)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(p->panel), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(p->accent), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(p->accent), 0);
}

void ui_header_create(ui_header_t *h, lv_obj_t *scr, const char *title,
                      lv_event_cb_t back_cb)
{
    lv_obj_t *logo = lv_image_create(scr);
    lv_image_set_src(logo, &ui_logo_dsc);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 8, 4);
    h->title = ui_label(scr, title, palette()->accent, 36, 9);
    /* BACK chip, top-right: 52x18 at (260,4), centre (286,13) */
    h->back_btn = ui_chip_button(scr, 260, 4, 52, 18, "BACK", back_cb,
                                 &h->back_label);
}

void ui_header_recolor(const ui_header_t *h, const theme_palette_t *p)
{
    lv_obj_set_style_text_color(h->title, lv_color_hex(p->accent), 0);
    ui_chip_recolor(h->back_btn, h->back_label, p);
}
