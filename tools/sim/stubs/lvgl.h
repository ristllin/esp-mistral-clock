#pragma once
#include <stdint.h>
typedef struct lv_obj_t lv_obj_t;
typedef struct lv_display_t lv_display_t;
typedef struct { uint8_t blue, green, red; } lv_color_t;
static inline void lv_obj_invalidate(lv_obj_t *o) { (void)o; }
