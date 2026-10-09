#ifndef CLOCK_SCENE_CHATON_H
#define CLOCK_SCENE_CHATON_H

#include <stdbool.h>
#include "scene.h"
#include "weather_mgr.h"

/* Screensaver character layer: paints the Mistral pixel cat and its
 * animated scenarios into the scene buffer while the saver screen is
 * active. Art only: the saver deliberately shows no text. */

/* Paint one tick of the saver scene. Called from scene.c's paint_all under
 * the LVGL lock. `neutral` = truthfulness rule engaged (no fresh real data):
 * a flat indoor scene, never invented weather. */
void scene_chaton_paint(scene_phase_t phase, scene_wx_t wx, bool neutral);

/* True when the saver must show the neutral indoor scene: no synced clock,
 * no valid cached weather, or data older than 3 h. The `weather force`
 * debug override bypasses the rule; the `saver neutral` debug flag forces
 * the indoor scene on. */
bool scene_chaton_neutral(void);
void scene_chaton_set_neutral_dbg(bool on);

const char *scene_chaton_scenario_name(void);   /* effective scenario */
int scene_chaton_scenario_pin(void);           /* -1 = auto rotate */
int scene_chaton_set_scenario(const char *name);  /* 0 ok, 1 usage */
void scene_chaton_reset(void);                 /* restart auto rotation */

#endif
