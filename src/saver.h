#ifndef CLOCK_SAVER_H
#define CLOCK_SAVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "theme_mgr.h"

/* Screensaver state machine: optional (on/off + idle timeout, default
 * 120 s, persisted in NVS), full-screen art only (no text), entered after
 * the idle timeout, exited by any touch. Console input resets the idle
 * timer but never wakes the saver (so `fb dump` works while it runs). */
esp_err_t saver_init(void);
void saver_register_commands(void);

void saver_notify_touch(void);    /* any touch (real or injected) */
void saver_notify_console(void);  /* console input: resets idle only */
void saver_apply_theme(const theme_palette_t *p);

bool saver_enabled(void);
uint32_t saver_timeout_s(void);
void saver_set_enabled(bool en);
void saver_set_timeout(uint32_t s);

#endif
