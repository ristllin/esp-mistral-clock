#ifndef CLOCK_WEATHER_MGR_H
#define CLOCK_WEATHER_MGR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Weather manager: Open-Meteo (no API key) current conditions + 3-day
 * forecast + sunrise/sunset, cached in NVS so offline shows the last known
 * weather with its age. Location is settable and persisted. */

#define WEATHER_FORECAST_DAYS 3

typedef struct {
    int64_t fetched_epoch;              /* unix time of the fetch */
    float temp_c;
    int wmo_code;
    int humidity_pct;
    int cloud_cover_pct;                /* -1 when unknown */
    int daily_code[WEATHER_FORECAST_DAYS];
    float daily_max[WEATHER_FORECAST_DAYS];
    float daily_min[WEATHER_FORECAST_DAYS];
    int32_t daily_days[WEATHER_FORECAST_DAYS];  /* local date, days since
                                                 * 1970-01-01, from the
                                                 * Open-Meteo daily.time
                                                 * array; 0 = unknown */
    int sunrise_min;                    /* today, minutes after local midnight */
    int sunset_min;                     /* -1 when unknown */
    bool valid;
} weather_data_t;

esp_err_t weather_mgr_init(void);
void weather_mgr_register_commands(void);

void weather_mgr_kick(void);            /* wake the weather task */
void weather_mgr_time_synced(void);     /* SNTP set the clock (time_mgr) */
/* Force a fetch as soon as online. Returns false when throttled (min 10 s
 * between forced fetches, so scripts cannot hammer Open-Meteo). */
bool weather_mgr_request_refresh(void);
const weather_data_t *weather_mgr_data(void);   /* NULL when never fetched */
bool weather_mgr_is_online(void);
int weather_mgr_age_min(void);          /* minutes since the cached fetch */

void weather_mgr_location(float *lat, float *lon, char *name, size_t name_sz);
/* ESP_ERR_INVALID_ARG unless lat is in -90..90, lon in -180..180 (finite)
 * and name is non-empty. Always refetches, never throttled; ends auto mode. */
esp_err_t weather_mgr_set_location(float lat, float lon, const char *name);
/* Automatic location from the public IP (at connect and daily) until a
 * location is set by hand again. Persisted. */
esp_err_t weather_mgr_set_auto(void);
bool weather_mgr_location_auto(void);

int weather_mgr_sunrise_min(void);      /* -1 when unknown */
int weather_mgr_sunset_min(void);

/* Wrap-safe sun clock shared by the auto theme and the scene. Returns
 * minutes since today's sunrise (negative before it, until the midpoint of
 * the night) and stores the day length in *day_len. Times are minutes after
 * local midnight; -1 (unknown) falls back to 07:00-19:00. Sunrise can be
 * later than sunset when the location is far from the device's time zone
 * (Tokyo seen in London time): the day then wraps past midnight. */
static inline int weather_sun_clock(int sunrise, int sunset, int now,
                                    int *day_len)
{
    if (sunrise < 0 || sunset < 0 || sunrise >= 1440 || sunset >= 1440 ||
        sunrise == sunset) {
        sunrise = 7 * 60;
        sunset = 19 * 60;
    }
    int len = (sunset - sunrise + 1440) % 1440;
    int rel = (now - sunrise + 1440) % 1440;
    if (rel >= len + (1440 - len) / 2) { rel -= 1440; }
    *day_len = len;
    return rel;
}

/* Weekday label ("Mon") for forecast day i, derived from the Open-Meteo
 * daily.time date (NOT from the current date), so an offline cache from a
 * previous day or a debug clock override still labels correctly. Falls
 * back to now + i days when the cache holds no date. */
bool weather_mgr_daily_label(int i, char *out, size_t sz);

const char *weather_mgr_cond(int wmo_code);     /* "Light rain" */
const char *weather_mgr_icon(int wmo_code);     /* "sun" "partly" "cloud" ... */

/* Debug weather override (`weather force <cond>|off`):
 * replaces the displayed condition + icon and drives the scene engine's
 * effects; temperature/forecast stay real. */
int weather_mgr_eff_wmo(void);                  /* override applied, or the
                                                 * real code (-1 if no data) */
bool weather_mgr_forced(void);
esp_err_t weather_mgr_force(const char *cond);  /* NULL or "off" clears */

/* The condition reduced to what the scene engine animates. */
typedef enum {
    SCENE_WX_CLEAR = 0, SCENE_WX_CLOUD, SCENE_WX_DRIZZLE, SCENE_WX_RAIN,
    SCENE_WX_SNOW, SCENE_WX_STORM, SCENE_WX_FOG
} scene_wx_t;

scene_wx_t weather_mgr_scene_wx(void);          /* override-aware */

/* Effective cloud cover 0..100 for the scene engine (overcast and
 * precipitation hide the sun/moon/stars and add cloud cover). Real
 * Open-Meteo cloud_cover, or a nominal value for a `weather force` override.
 * -1 when no data at all. */
int weather_mgr_cloud_cover(void);

#endif
