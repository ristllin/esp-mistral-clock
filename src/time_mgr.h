#ifndef CLOCK_TIME_MGR_H
#define CLOCK_TIME_MGR_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

/* Time manager: SNTP sync once Wi-Fi has an IP, Europe/London local time
 * with DST (POSIX TZ string), a 1 s UI tick that feeds the home clock, and
 * a debug clock override used to force the auto theme across a full day. */

esp_err_t time_mgr_init(void);
void time_mgr_register_commands(void);

bool time_mgr_is_synced(void);          /* SNTP synced, or override active */
void time_mgr_now(struct tm *out);      /* local time, override-aware */
int64_t time_mgr_epoch(void);           /* unix seconds, override-aware */

/* Debug override: the clock runs from <epoch> onwards at real speed.
 * Used to validate the auto theme across a full day without waiting. */
void time_mgr_set_override(int64_t epoch);
void time_mgr_clear_override(void);

#endif
