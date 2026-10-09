#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline bool lvgl_port_lock(uint32_t t) { (void)t; return true; }
static inline void lvgl_port_unlock(void) {}
