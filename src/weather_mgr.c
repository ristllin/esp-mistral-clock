/* Weather manager: Open-Meteo HTTPS fetch in its own task (TLS needs a big
 * stack), cJSON parsing, NVS last-known cache, location store, and the
 * console commands. The home screen is fed via ui_home_weather_changed.
 *
 * Current conditions come from two sources. Open-Meteo is a forecast model:
 * it supplies temperature, humidity, cloud cover, sun times and the 3-day
 * forecast, but its "current" weather code often misses local showers. So
 * the nearest airport weather report (METAR, aviationweather.gov, no API
 * key) within ~30 km is fetched every 15 min; while it is under 90 min old,
 * precipitation, fog or thunder it reports replaces the model's condition.
 * It only ever adds weather: a station report is up to an hour old and some
 * km away, so "nothing falling there" never removes rain the model shows.
 *
 * The location is either set by hand (city picker, `loc`) or automatic:
 * looked up from the clock's public IP (ipinfo.io, fallback ipapi.co, no
 * keys) at connect and daily. IP geolocation finds the provider's nearest
 * hub, usually the right town; a VPN on the router would move it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_lvgl_port.h"

#include "weather_mgr.h"
#include "wifi_mgr.h"
#include "time_mgr.h"
#include "ui_home.h"

static const char *TAG = "weather";

#define NVS_NS        "weather"
#define NVS_KEY_CACHE "cache"
#define CACHE_MAGIC   0x57784831   /* "WxH1" */
#define REFRESH_MS    (30 * 60 * 1000)
#define RETRY_MIN_MS  (30 * 1000)  /* after a failed fetch: 30 s doubling */
#define HTTP_BUF_SZ   8192
#define JSON_MAX_DEPTH 8           /* Open-Meteo nests 2 deep */
#define OBS_REFRESH_MS (15 * 60 * 1000)
#define OBS_MAX_AGE_S  (90 * 60)   /* METARs are issued every 30-60 min */
#define OBS_BOX_LAT    0.25f       /* search box around the location, deg */
#define OBS_BOX_LON    0.35f       /* (~28 km at UK latitudes) */
#define GEO_REFRESH_MS (24 * 60 * 60 * 1000)   /* automatic location */
#define GEO_RETRY_MS   (10 * 60 * 1000)
#define GEO_MOVE_KM    5           /* smaller moves keep the current cache */
#define EPOCH_2020    1577836800LL /* earlier stamps predate the SNTP sync */

typedef struct {
    float lat;
    float lon;
    char name[24];
} loc_t;

typedef struct {
    uint32_t magic;
    loc_t loc;                  /* the location this cache belongs to */
    weather_data_t data;
} cache_blob_t;

/* London (default) */
static loc_t s_loc = { 51.5074f, -0.1278f, "London" };
/* s_loc is written by the console / LVGL tasks and read by the weather
 * task: copies and the location-checked cache store go through this */
static portMUX_TYPE s_loc_mux = portMUX_INITIALIZER_UNLOCKED;
static cache_blob_t s_cache;
static bool s_online;
static volatile bool s_force;   /* a refresh was requested explicitly */
static TaskHandle_t s_task;

/* Latest airport observation near the location (METAR). */
typedef struct {
    char icao[5];
    char wx[24];            /* present weather, e.g. "-RA", "" = none */
    int wmo;                /* WMO code for wx, -1 = nothing significant */
    int64_t obs_epoch;      /* 0 = no observation */
    int km;                 /* distance from the location */
} obs_t;
static obs_t s_obs;
static volatile bool s_obs_force;   /* refetch the observation now */
static bool s_auto;                 /* location follows the IP lookup */
static volatile bool s_geo_force;   /* look the location up now */

static void publish(void);      /* fwd: used by apply_location */
static bool obs_fresh(obs_t *out);
static int32_t days_from_civil(int y, int m, int d);
static void civil_from_days(int32_t z, int *y, int *m, int *d);

/* WMO weather_code -> everything the firmware derives from it: condition
 * text, forecast icon (sun moon cloud partly rain drizzle snow), the scene
 * effect, and a nominal cloud cover for `weather force` overrides. */
typedef struct {
    int code;
    const char *cond;
    const char *icon;
    scene_wx_t wx;
    uint8_t cover;      /* % */
} wmo_map_t;
static const wmo_map_t s_wmo[] = {
    { 0, "Clear sky", "sun", SCENE_WX_CLEAR, 8 },
    { 1, "Mainly clear", "sun", SCENE_WX_CLOUD, 45 },
    { 2, "Partly cloudy", "partly", SCENE_WX_CLOUD, 45 },
    { 3, "Overcast", "cloud", SCENE_WX_CLOUD, 88 },
    { 45, "Fog", "cloud", SCENE_WX_FOG, 100 },
    { 48, "Rime fog", "cloud", SCENE_WX_FOG, 100 },
    { 51, "Light drizzle", "drizzle", SCENE_WX_DRIZZLE, 82 },
    { 53, "Drizzle", "drizzle", SCENE_WX_DRIZZLE, 82 },
    { 55, "Dense drizzle", "drizzle", SCENE_WX_DRIZZLE, 82 },
    { 56, "Freezing drizzle", "drizzle", SCENE_WX_DRIZZLE, 82 },
    { 57, "Freezing drizzle", "drizzle", SCENE_WX_DRIZZLE, 82 },
    { 61, "Light rain", "rain", SCENE_WX_RAIN, 92 },
    { 63, "Rain", "rain", SCENE_WX_RAIN, 92 },
    { 65, "Heavy rain", "rain", SCENE_WX_RAIN, 92 },
    { 66, "Freezing rain", "rain", SCENE_WX_RAIN, 92 },
    { 67, "Freezing rain", "rain", SCENE_WX_RAIN, 92 },
    { 71, "Light snow", "snow", SCENE_WX_SNOW, 85 },
    { 73, "Snow", "snow", SCENE_WX_SNOW, 85 },
    { 75, "Heavy snow", "snow", SCENE_WX_SNOW, 85 },
    { 77, "Snow grains", "snow", SCENE_WX_SNOW, 85 },
    { 80, "Light showers", "rain", SCENE_WX_RAIN, 92 },
    { 81, "Showers", "rain", SCENE_WX_RAIN, 92 },
    { 82, "Heavy showers", "rain", SCENE_WX_RAIN, 92 },
    { 85, "Snow showers", "snow", SCENE_WX_SNOW, 85 },
    { 86, "Snow showers", "snow", SCENE_WX_SNOW, 85 },
    { 95, "Thunderstorm", "rain", SCENE_WX_STORM, 97 },
    { 96, "Thunderstorm", "rain", SCENE_WX_STORM, 97 },
    { 99, "Thunderstorm", "rain", SCENE_WX_STORM, 97 },
};

static const wmo_map_t *wmo_find(int code)
{
    for (size_t i = 0; i < sizeof(s_wmo) / sizeof(s_wmo[0]); i++) {
        if (s_wmo[i].code == code) { return &s_wmo[i]; }
    }
    return NULL;
}

const char *weather_mgr_cond(int code)
{
    const wmo_map_t *m = wmo_find(code);
    return m ? m->cond : "Unknown";
}

const char *weather_mgr_icon(int code)
{
    const wmo_map_t *m = wmo_find(code);
    return m ? m->icon : "cloud";
}

/* ---- debug weather override (`weather force <cond>|off`) ---- */

static int s_force_wmo = -1;   /* -1 = off */

int weather_mgr_eff_wmo(void)
{
    if (s_force_wmo >= 0) { return s_force_wmo; }
    const weather_data_t *d = weather_mgr_data();
    if (!d) { return -1; }
    obs_t o;
    if (obs_fresh(&o) && o.wmo >= 0) {
        return o.wmo;   /* observed rain/snow/thunder/fog the model missed */
    }
    return d->wmo_code;
}

bool weather_mgr_forced(void) { return s_force_wmo >= 0; }

/* `weather force` names -> a representative WMO code */
static const struct { const char *name; int wmo; } s_force_names[] = {
    { "clear", 0 }, { "partly", 1 }, { "cloud", 3 }, { "fog", 45 },
    { "drizzle", 51 }, { "rain", 61 }, { "snow", 71 }, { "storm", 95 },
};
#define FORCE_USAGE \
    "usage: weather force <clear|partly|cloud|drizzle|rain|snow|storm|fog|off>\n"

esp_err_t weather_mgr_force(const char *cond)
{
    if (!cond || !strcmp(cond, "off")) {
        s_force_wmo = -1;
        ui_home_weather_changed();
        return ESP_OK;
    }
    for (size_t i = 0; i < sizeof(s_force_names) / sizeof(s_force_names[0]); i++) {
        if (!strcmp(cond, s_force_names[i].name)) {
            s_force_wmo = s_force_names[i].wmo;
            ui_home_weather_changed();
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

scene_wx_t weather_mgr_scene_wx(void)
{
    const wmo_map_t *m = wmo_find(weather_mgr_eff_wmo());
    return m ? m->wx : SCENE_WX_CLOUD;
}

const weather_data_t *weather_mgr_data(void)
{
    return s_cache.data.valid ? &s_cache.data : NULL;
}

int weather_mgr_cloud_cover(void)
{
    if (s_force_wmo >= 0) { return wmo_find(s_force_wmo)->cover; }
    const weather_data_t *d = weather_mgr_data();
    if (!d || d->cloud_cover_pct < 0) { return -1; }
    int cover = d->cloud_cover_pct > 100 ? 100 : d->cloud_cover_pct;
    /* observed precipitation implies the cloud the model may have missed */
    const wmo_map_t *m = wmo_find(weather_mgr_eff_wmo());
    if (m && m->code >= 45 && m->cover > cover) { cover = m->cover; }
    return cover;
}

bool weather_mgr_is_online(void) { return s_online; }

int weather_mgr_age_min(void)
{
    if (!s_cache.data.valid) { return -1; }
    int64_t age = time_mgr_epoch() - s_cache.data.fetched_epoch;
    return (int)(age / 60);
}

int weather_mgr_sunrise_min(void)
{
    return s_cache.data.valid ? s_cache.data.sunrise_min : -1;
}

int weather_mgr_sunset_min(void)
{
    return s_cache.data.valid ? s_cache.data.sunset_min : -1;
}

bool weather_mgr_daily_label(int i, char *out, size_t sz)
{
    if (i < 0 || i >= WEATHER_FORECAST_DAYS || !out || sz == 0) {
        return false;
    }
    if (s_cache.data.valid && s_cache.data.daily_days[i] > 0) {
        int y, m, d;
        civil_from_days(s_cache.data.daily_days[i], &y, &m, &d);
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        tm.tm_year = y - 1900;
        tm.tm_mon = m - 1;
        tm.tm_mday = d;
        tm.tm_hour = 12;    /* noon: never inside a DST transition */
        mktime(&tm);        /* fills tm_wday, which strftime %a reads */
        return strftime(out, sz, "%a", &tm) > 0;
    }
    /* fallback when the cache holds no forecast dates */
    time_t t = (time_t)time_mgr_epoch() + (time_t)i * 86400;
    struct tm tm;
    localtime_r(&t, &tm);
    return strftime(out, sz, "%a", &tm) > 0;
}

static loc_t loc_snapshot(void)
{
    taskENTER_CRITICAL(&s_loc_mux);
    loc_t l = s_loc;
    taskEXIT_CRITICAL(&s_loc_mux);
    return l;
}

void weather_mgr_location(float *lat, float *lon, char *name, size_t name_sz)
{
    loc_t l = loc_snapshot();
    *lat = l.lat;
    *lon = l.lon;
    snprintf(name, name_sz, "%s", l.name);
}

static void save_cache(const loc_t *loc)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        s_cache.magic = CACHE_MAGIC;
        s_cache.loc = *loc;    /* the cache stores the location it belongs to */
        nvs_set_blob(h, NVS_KEY_CACHE, &s_cache, sizeof(s_cache));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_cache(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_cache);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, NVS_KEY_CACHE, &s_cache, &len) == ESP_OK &&
            s_cache.magic == CACHE_MAGIC && len == sizeof(s_cache)) {
            /* only show the cache when it belongs to the current location */
            bool same = s_cache.loc.lat == s_loc.lat &&
                        s_cache.loc.lon == s_loc.lon &&
                        strcmp(s_cache.loc.name, s_loc.name) == 0;
            if (same) {
                ESP_LOGI(TAG, "loaded cached weather (age %d min)",
                         weather_mgr_age_min());
            } else {
                ESP_LOGI(TAG, "cached weather is for another location, dropped");
                s_cache.data.valid = false;
            }
        } else {
            s_cache.data.valid = false;
        }
        nvs_close(h);
    } else {
        s_cache.data.valid = false;
    }
}

static void save_loc(const loc_t *loc)
{
    nvs_handle_t h;
    if (nvs_open("loc", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "loc", loc, sizeof(*loc));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void save_auto(void)
{
    nvs_handle_t h;
    if (nvs_open("loc", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "auto", s_auto ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_loc(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_loc);
    s_auto = true;      /* nothing stored yet: follow the internet connection */
    if (nvs_open("loc", NVS_READONLY, &h) == ESP_OK) {
        bool have = nvs_get_blob(h, "loc", &s_loc, &len) == ESP_OK &&
                    len == sizeof(s_loc) && isfinite(s_loc.lat) &&
                    isfinite(s_loc.lon);
        if (!have) {
            s_loc.lat = 51.5074f;
            s_loc.lon = -0.1278f;
            snprintf(s_loc.name, sizeof(s_loc.name), "London");
        }
        s_loc.name[sizeof(s_loc.name) - 1] = '\0';
        /* a location stored before the auto flag existed was set by hand */
        uint8_t a;
        s_auto = (nvs_get_u8(h, "auto", &a) == ESP_OK) ? a != 0 : !have;
        nvs_close(h);
    }
}

static bool loc_valid(float lat, float lon, const char *name)
{
    return isfinite(lat) && isfinite(lon) && lat >= -90.0f && lat <= 90.0f &&
           lon >= -180.0f && lon <= 180.0f && name && name[0];
}

static void apply_location(float lat, float lon, const char *name)
{
    loc_t nl = { .lat = lat, .lon = lon };
    snprintf(nl.name, sizeof(nl.name), "%s", name);
    /* the old cache belongs to the old location: drop it and refetch. A
     * location change always refetches (it bypasses the scripted-refresh
     * throttle); a fetch already in flight for the old location is
     * discarded by fetch_and_store. */
    taskENTER_CRITICAL(&s_loc_mux);
    s_loc = nl;
    s_cache.data.valid = false;
    s_obs.obs_epoch = 0;        /* the station belonged to the old place */
    taskEXIT_CRITICAL(&s_loc_mux);
    s_obs_force = true;
    save_loc(&nl);
    publish();
    s_force = true;
    weather_mgr_kick();
}

esp_err_t weather_mgr_set_location(float lat, float lon, const char *name)
{
    if (!loc_valid(lat, lon, name)) { return ESP_ERR_INVALID_ARG; }
    if (s_auto) {               /* a location set by hand ends auto mode */
        s_auto = false;
        save_auto();
    }
    apply_location(lat, lon, name);
    return ESP_OK;
}

esp_err_t weather_mgr_set_auto(void)
{
    s_auto = true;
    save_auto();
    s_geo_force = true;
    weather_mgr_kick();
    publish();
    return ESP_OK;
}

bool weather_mgr_location_auto(void) { return s_auto; }

/* ---- HTTP fetch + JSON parse ---- */

/* Strings from the network end up on the console, in logs and on screen:
 * replace control bytes so a response cannot inject terminal escapes. */
static void sanitize_text(char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || *s == 0x7F) { *s = '?'; }
    }
}

/* One response buffer for both sources (only the weather task fetches). */
static char s_http_body[HTTP_BUF_SZ];

static esp_err_t http_fetch(const char *url, char *buf, size_t buf_sz)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 12000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { return ESP_ERR_NO_MEM; }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    size_t total = 0;
    int r;
    while (total < buf_sz - 1 &&
           (r = esp_http_client_read(client, buf + total, buf_sz - 1 - total)) > 0) {
        total += (size_t)r;
    }
    buf[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGW(TAG, "HTTP status %d", status);
        return ESP_FAIL;
    }
    return total > 0 ? ESP_OK : ESP_FAIL;
}

/* Parse an Open-Meteo daily "2026-10-08T07:12" local time into minutes
 * after local midnight. */
static int iso_hhmm_to_min(const char *s)
{
    const char *t = strchr(s, 'T');
    if (!t) { return -1; }
    int hh, mm;
    if (sscanf(t + 1, "%d:%d", &hh, &mm) != 2 || hh < 0 || hh > 23 ||
        mm < 0 || mm > 59) {
        return -1;
    }
    return hh * 60 + mm;
}

/* Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). */
static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 +
                   (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int32_t)(era * 146097 + (int)doe - 719468);
}

static void civil_from_days(int32_t z, int *y, int *m, int *d)
{
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy = (int32_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mm = mp + (mp < 10 ? 3 : (unsigned)-9);
    *y = yy + (mm <= 2);
    *m = (int)mm;
    *d = (int)dd;
}

/* cJSON recurses once per nesting level (about 64 B of stack each, limit
 * 1000): reject deeply nested bodies before parsing so a hostile or broken
 * response cannot overflow the weather task's stack. */
static bool json_depth_ok(const char *s)
{
    int depth = 0;
    bool in_str = false;
    for (; *s; s++) {
        if (in_str) {
            if (*s == '\\' && s[1]) { s++; }
            else if (*s == '"') { in_str = false; }
        } else if (*s == '"') {
            in_str = true;
        } else if (*s == '{' || *s == '[') {
            if (++depth > JSON_MAX_DEPTH) { return false; }
        } else if (*s == '}' || *s == ']') {
            depth--;
        }
    }
    return true;
}

static esp_err_t parse_forecast(const char *json, weather_data_t *out)
{
    if (!json_depth_ok(json)) { return ESP_FAIL; }
    cJSON *root = cJSON_Parse(json);
    if (!root) { return ESP_FAIL; }

    cJSON *current = cJSON_GetObjectItem(root, "current");
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (!current || !daily) { cJSON_Delete(root); return ESP_FAIL; }

    cJSON *j;
    if ((j = cJSON_GetObjectItem(current, "temperature_2m")) &&
        cJSON_IsNumber(j)) {
        out->temp_c = (float)j->valuedouble;
    }
    if ((j = cJSON_GetObjectItem(current, "weather_code")) &&
        cJSON_IsNumber(j)) {
        out->wmo_code = j->valueint;
    }
    if ((j = cJSON_GetObjectItem(current, "relative_humidity_2m")) &&
        cJSON_IsNumber(j)) {
        out->humidity_pct = j->valueint;
    }
    if ((j = cJSON_GetObjectItem(current, "cloud_cover")) &&
        cJSON_IsNumber(j)) {
        out->cloud_cover_pct = j->valueint;
    }

    cJSON *d_code = cJSON_GetObjectItem(daily, "weather_code");
    cJSON *d_max = cJSON_GetObjectItem(daily, "temperature_2m_max");
    cJSON *d_min = cJSON_GetObjectItem(daily, "temperature_2m_min");
    cJSON *d_sunrise = cJSON_GetObjectItem(daily, "sunrise");
    cJSON *d_sunset = cJSON_GetObjectItem(daily, "sunset");
    cJSON *d_time = cJSON_GetObjectItem(daily, "time");
    if (!d_code || !d_max || !d_min || !d_sunrise || !d_sunset) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
        cJSON *c = cJSON_GetArrayItem(d_code, i);
        cJSON *mx = cJSON_GetArrayItem(d_max, i);
        cJSON *mn = cJSON_GetArrayItem(d_min, i);
        if (!c || !mx || !mn) { cJSON_Delete(root); return ESP_FAIL; }
        out->daily_code[i] = c->valueint;
        out->daily_max[i] = (float)mx->valuedouble;
        out->daily_min[i] = (float)mn->valuedouble;
        /* daily.time[i] = "2026-10-08" (local date): the forecast labels
         * must come from the API's own dates, not from the current date */
        out->daily_days[i] = 0;
        cJSON *dt = d_time ? cJSON_GetArrayItem(d_time, i) : NULL;
        if (cJSON_IsString(dt)) {
            int y, m, d;
            if (sscanf(dt->valuestring, "%4d-%2d-%2d", &y, &m, &d) == 3 &&
                y >= 1970 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
                out->daily_days[i] = days_from_civil(y, m, d);
            }
        }
    }
    cJSON *sr = cJSON_GetArrayItem(d_sunrise, 0);
    cJSON *ss = cJSON_GetArrayItem(d_sunset, 0);
    if (cJSON_IsString(sr)) { out->sunrise_min = iso_hhmm_to_min(sr->valuestring); }
    if (cJSON_IsString(ss)) { out->sunset_min = iso_hhmm_to_min(ss->valuestring); }

    cJSON_Delete(root);
    out->fetched_epoch = time_mgr_epoch();
    out->valid = true;
    return ESP_OK;
}

/* ---- UI publish ---- */

static void publish(void)
{
    /* pull model: the home screen reads weather_mgr_data() itself */
    ui_home_weather_changed();
}

/* ESP_OK when stored, ESP_ERR_INVALID_STATE when the location changed while
 * the request was in flight (result dropped; the new location's fetch is
 * already queued), ESP_FAIL otherwise. */
static esp_err_t fetch_and_store(void)
{
    const loc_t req = loc_snapshot();   /* the location this fetch is for */
    char url[320];
    int n = snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,cloud_cover"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
             "&timezone=Europe%%2FLondon&forecast_days=%d",
             (double)req.lat, (double)req.lon, WEATHER_FORECAST_DAYS);
    if (n < 0 || n >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "forecast URL does not fit (%d bytes)", n);
        return ESP_FAIL;
    }

    char *body = s_http_body;
    if (http_fetch(url, body, sizeof(s_http_body)) != ESP_OK) {
        return ESP_FAIL;
    }

    weather_data_t fresh;
    memset(&fresh, 0, sizeof(fresh));
    fresh.sunrise_min = -1;
    fresh.sunset_min = -1;
    fresh.cloud_cover_pct = -1;
    if (parse_forecast(body, &fresh) != ESP_OK) {
        ESP_LOGW(TAG, "forecast parse failed");
        return ESP_FAIL;
    }

    /* The readers (home screen, screensaver) run in the LVGL task, so the
     * commit holds the LVGL lock: they never see a half-copied struct. */
    lvgl_port_lock(0);
    taskENTER_CRITICAL(&s_loc_mux);
    bool same = memcmp(&req, &s_loc, sizeof(req)) == 0;
    if (same) { s_cache.data = fresh; }
    taskEXIT_CRITICAL(&s_loc_mux);
    lvgl_port_unlock();
    if (!same) {
        ESP_LOGI(TAG, "location changed during fetch, result dropped");
        return ESP_ERR_INVALID_STATE;
    }
    save_cache(&req);
    s_online = true;
    ESP_LOGI(TAG, "fetched: %.1fC %s (sunrise %d, sunset %d)",
             fresh.temp_c, weather_mgr_cond(fresh.wmo_code),
             fresh.sunrise_min, fresh.sunset_min);
    publish();
    return ESP_OK;
}

/* ---- observed conditions (METAR) ---- */

/* METAR present weather ("-RA", "+SHRA", "TSRA", "-RA -DZ", "FG", ...) to
 * a WMO code from s_wmo, or -1 when nothing falling / no fog / no thunder.
 * Vicinity (VC) and recent (RE) groups are ignored; the most significant
 * group wins (thunder > snow > rain > drizzle > fog). */
static int metar_wx_to_wmo(const char *wx)
{
    int best = -1, best_rank = 0;
    char buf[24];
    snprintf(buf, sizeof(buf), "%s", wx ? wx : "");
    for (char *save = NULL, *t = strtok_r(buf, " ", &save); t;
         t = strtok_r(NULL, " ", &save)) {
        int lvl = 1;                            /* 0 light, 1 moderate, 2 heavy */
        if (*t == '-') { lvl = 0; t++; }
        else if (*t == '+') { lvl = 2; t++; }
        if (!strncmp(t, "VC", 2) || !strncmp(t, "RE", 2)) { continue; }
        bool sh = strstr(t, "SH") != NULL, fz = strstr(t, "FZ") != NULL;
        int code = -1, rank = 0;
        if (strstr(t, "TS")) {
            code = 95; rank = 5;
        } else if (strstr(t, "SN") || strstr(t, "SG") || strstr(t, "PL") ||
                   strstr(t, "GS") || strstr(t, "GR")) {
            static const int snow[3] = { 71, 73, 75 };
            code = sh ? (lvl == 2 ? 86 : 85) : snow[lvl]; rank = 4;
        } else if (strstr(t, "RA")) {
            static const int rain[3] = { 61, 63, 65 }, shower[3] = { 80, 81, 82 };
            code = fz ? (lvl == 2 ? 67 : 66) : sh ? shower[lvl] : rain[lvl];
            rank = 3;
        } else if (strstr(t, "DZ")) {
            static const int dz[3] = { 51, 53, 55 };
            code = fz ? (lvl == 2 ? 57 : 56) : dz[lvl]; rank = 2;
        } else if (strstr(t, "FG")) {
            code = fz ? 48 : 45; rank = 1;
        }
        if (rank > best_rank) { best = code; best_rank = rank; }
    }
    return best;
}

static bool obs_fresh(obs_t *out)
{
    taskENTER_CRITICAL(&s_loc_mux);
    obs_t o = s_obs;
    taskEXIT_CRITICAL(&s_loc_mux);
    if (o.obs_epoch == 0 || !time_mgr_is_synced()) { return false; }
    int64_t age = time_mgr_epoch() - o.obs_epoch;
    if (age < -600 || age > OBS_MAX_AGE_S) { return false; }
    if (out) { *out = o; }
    return true;
}

/* Fetch the METARs in a box around the location and keep the nearest. */
static void fetch_obs(void)
{
    const loc_t req = loc_snapshot();
    char url[160];
    snprintf(url, sizeof(url),
             "https://aviationweather.gov/api/data/metar?bbox=%.2f,%.2f,%.2f,%.2f"
             "&format=json",
             (double)(req.lat - OBS_BOX_LAT), (double)(req.lon - OBS_BOX_LON),
             (double)(req.lat + OBS_BOX_LAT), (double)(req.lon + OBS_BOX_LON));
    if (http_fetch(url, s_http_body, sizeof(s_http_body)) != ESP_OK ||
        !json_depth_ok(s_http_body)) {
        /* no station in range (HTTP 204) or a failed fetch: a previous
         * observation simply ages out after OBS_MAX_AGE_S */
        ESP_LOGI(TAG, "no airport observation within ~30 km");
        return;
    }
    cJSON *root = cJSON_Parse(s_http_body);
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return; }
    obs_t best = { .wmo = -1 };
    float best_d2 = 1e9f;
    const float kx = 111.32f * cosf(req.lat * (float)M_PI / 180.0f);
    cJSON *m;
    cJSON_ArrayForEach(m, root) {
        cJSON *id = cJSON_GetObjectItem(m, "icaoId");
        cJSON *la = cJSON_GetObjectItem(m, "lat");
        cJSON *lo = cJSON_GetObjectItem(m, "lon");
        cJSON *ot = cJSON_GetObjectItem(m, "obsTime");
        cJSON *wx = cJSON_GetObjectItem(m, "wxString");
        /* obsTime is unix seconds; the range check keeps the int64 cast
         * defined for any number the server might send */
        if (!cJSON_IsString(id) || !cJSON_IsNumber(la) || !cJSON_IsNumber(lo) ||
            !cJSON_IsNumber(ot) || !(ot->valuedouble > 0.0) ||
            ot->valuedouble > 4102444800.0) {
            continue;
        }
        float dx = ((float)lo->valuedouble - req.lon) * kx;
        float dy = ((float)la->valuedouble - req.lat) * 110.57f;
        float d2 = dx * dx + dy * dy;
        if (d2 >= best_d2) { continue; }
        best_d2 = d2;
        snprintf(best.icao, sizeof(best.icao), "%s", id->valuestring);
        snprintf(best.wx, sizeof(best.wx), "%s",
                 cJSON_IsString(wx) ? wx->valuestring : "");
        best.obs_epoch = (int64_t)ot->valuedouble;
        best.km = (int)(sqrtf(d2) + 0.5f);
    }
    cJSON_Delete(root);
    if (best.obs_epoch == 0) { return; }
    sanitize_text(best.icao);
    sanitize_text(best.wx);
    best.wmo = metar_wx_to_wmo(best.wx);
    lvgl_port_lock(0);                 /* see fetch_and_store() */
    taskENTER_CRITICAL(&s_loc_mux);
    bool same = memcmp(&req, &s_loc, sizeof(req)) == 0;
    if (same) { s_obs = best; }
    taskEXIT_CRITICAL(&s_loc_mux);
    lvgl_port_unlock();
    if (same) {
        ESP_LOGI(TAG, "observed: %s %s (%d km) -> %s", best.icao,
                 best.wx[0] ? best.wx : "no weather",
                 best.km, best.wmo >= 0 ? weather_mgr_cond(best.wmo) : "-");
    }
}

/* ---- automatic location (IP geolocation) ---- */

/* ipinfo.io: {"city":"London","loc":"51.5085,-0.1257",...} */
static bool geo_parse_ipinfo(const cJSON *root, float *lat, float *lon,
                             char *city, size_t n)
{
    const cJSON *l = cJSON_GetObjectItem(root, "loc");
    const cJSON *c = cJSON_GetObjectItem(root, "city");
    if (!cJSON_IsString(l) || sscanf(l->valuestring, "%f,%f", lat, lon) != 2) {
        return false;
    }
    snprintf(city, n, "%s", cJSON_IsString(c) ? c->valuestring : "");
    return true;
}

/* ipapi.co: {"city":"London","latitude":51.5,"longitude":-0.12,...} */
static bool geo_parse_ipapi(const cJSON *root, float *lat, float *lon,
                            char *city, size_t n)
{
    const cJSON *la = cJSON_GetObjectItem(root, "latitude");
    const cJSON *lo = cJSON_GetObjectItem(root, "longitude");
    const cJSON *c = cJSON_GetObjectItem(root, "city");
    if (!cJSON_IsNumber(la) || !cJSON_IsNumber(lo)) { return false; }
    *lat = (float)la->valuedouble;
    *lon = (float)lo->valuedouble;
    snprintf(city, n, "%s", cJSON_IsString(c) ? c->valuestring : "");
    return true;
}

static bool geo_lookup(const char *url,
                       bool (*parse)(const cJSON *, float *, float *, char *, size_t),
                       float *lat, float *lon, char *city, size_t n)
{
    if (http_fetch(url, s_http_body, sizeof(s_http_body)) != ESP_OK ||
        !json_depth_ok(s_http_body)) {
        return false;
    }
    cJSON *root = cJSON_Parse(s_http_body);
    bool ok = cJSON_IsObject(root) && parse(root, lat, lon, city, n);
    cJSON_Delete(root);
    if (ok) { sanitize_text(city); }
    if (ok && !city[0]) { snprintf(city, n, "%.1f, %.1f", (double)*lat, (double)*lon); }
    return ok && loc_valid(*lat, *lon, city);
}

/* Look the location up from the public IP; true when it succeeded. */
static bool resolve_auto_location(void)
{
    float lat, lon;
    char city[24];
    if (!geo_lookup("https://ipinfo.io/json", geo_parse_ipinfo,
                    &lat, &lon, city, sizeof(city)) &&
        !geo_lookup("https://ipapi.co/json/", geo_parse_ipapi,
                    &lat, &lon, city, sizeof(city))) {
        ESP_LOGW(TAG, "automatic location lookup failed");
        return false;
    }
    loc_t cur = loc_snapshot();
    float dx = (lon - cur.lon) * 111.32f * cosf(cur.lat * (float)M_PI / 180.0f);
    float dy = (lat - cur.lat) * 110.57f;
    if (dx * dx + dy * dy < GEO_MOVE_KM * GEO_MOVE_KM && !strcmp(city, cur.name)) {
        ESP_LOGI(TAG, "automatic location unchanged: %s", city);
        return true;
    }
    ESP_LOGI(TAG, "automatic location: %s (%.2f, %.2f)", city, (double)lat,
             (double)lon);
    apply_location(lat, lon, city);
    return true;
}

/* ---- Task + console ---- */

static void weather_task(void *arg)
{
    (void)arg;
    TickType_t last_fetch = 0;    /* last successful fetch */
    TickType_t last_try = 0;      /* last failed attempt */
    TickType_t retry_wait = 0;    /* after a failure: 30 s doubling to 30 min */
    TickType_t last_obs = 0;      /* last observation attempt */
    TickType_t last_geo = 0;      /* last automatic location lookup */
    TickType_t geo_wait = 0;      /* 24 h after success, 10 min after failure */
    bool geo_tried = false;
    bool have_fetched = false;
    bool retry_pending = false;   /* the last attempt failed */
    bool obs_tried = false;
    bool was_connected = false;

    publish();   /* show the NVS cache immediately, even before Wi-Fi */

    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));

        TickType_t now = xTaskGetTickCount();
        bool connected = wifi_mgr_is_connected();
        bool stale = (now - last_fetch) > pdMS_TO_TICKS(REFRESH_MS);
        /* automatic location first, so the forecast is for the right place */
        if (connected && s_auto &&
            (s_geo_force || !geo_tried || (now - last_geo) >= geo_wait)) {
            s_geo_force = false;
            geo_tried = true;
            last_geo = now;
            geo_wait = pdMS_TO_TICKS(resolve_auto_location() ? GEO_REFRESH_MS
                                                             : GEO_RETRY_MS);
        }
        /* forced and just-connected fetches go immediately; scheduled ones
         * and retries after a failure wait out the backoff so an outage, a
         * 429 or a bad location cannot turn into a request every 5 s */
        bool want = !have_fetched || stale || retry_pending;
        bool due = s_force || !was_connected ||
                   (want && (!retry_pending || (now - last_try) >= retry_wait));

        if (connected && due) {
            s_force = false;
            esp_err_t err = fetch_and_store();
            if (err == ESP_OK) {
                last_fetch = xTaskGetTickCount();
                have_fetched = true;
                retry_pending = false;
                retry_wait = 0;
            } else if (err != ESP_ERR_INVALID_STATE) {
                last_try = xTaskGetTickCount();
                retry_pending = true;
                retry_wait = retry_wait ? retry_wait * 2
                                        : pdMS_TO_TICKS(RETRY_MIN_MS);
                if (retry_wait > pdMS_TO_TICKS(REFRESH_MS)) {
                    retry_wait = pdMS_TO_TICKS(REFRESH_MS);
                }
                /* offline as a normal state: keep showing the cache */
                s_online = false;
                publish();
            }
            /* ESP_ERR_INVALID_STATE: dropped for a newer location, whose
             * forced fetch runs on the next pass */
        } else if (!connected && was_connected) {
            s_online = false;
            publish();
        }
        /* airport observation: needs real time to judge its age */
        now = xTaskGetTickCount();
        if (connected && time_mgr_is_synced() &&
            (s_obs_force || !obs_tried ||
             (now - last_obs) >= pdMS_TO_TICKS(OBS_REFRESH_MS))) {
            s_obs_force = false;
            obs_tried = true;
            last_obs = now;
            fetch_obs();
            publish();
        }
        was_connected = connected;
    }
}

void weather_mgr_kick(void)
{
    if (s_task) { xTaskNotifyGive(s_task); }
}

void weather_mgr_time_synced(void)
{
    /* a fetch that finished before the first SNTP sync was stamped with a
     * 1970 clock, which would read as decades old: refetch to restamp it */
    if (s_cache.data.valid && s_cache.data.fetched_epoch < EPOCH_2020) {
        s_force = true;
    }
    weather_mgr_kick();
}

/* Minimum interval between explicitly forced fetches, so scripts calling
 * `weather` in a loop cannot hammer Open-Meteo. The periodic refresh in the
 * weather task is not affected. */
#define FORCE_MIN_INTERVAL_MS 10000
static TickType_t s_last_force_req;

bool weather_mgr_request_refresh(void)
{
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_force_req < pdMS_TO_TICKS(FORCE_MIN_INTERVAL_MS)) {
        return false;   /* throttled */
    }
    s_last_force_req = now;
    s_force = true;
    s_obs_force = true;
    weather_mgr_kick();
    return true;
}

static int weather_cmd(int argc, char **argv)
{
    /* `weather force <cond>|off`: debug override for the scene engine and
     * the displayed condition; temperature stays real. */
    if (argc >= 2 && !strcmp(argv[1], "force")) {
        if (argc < 3 || weather_mgr_force(argv[2]) != ESP_OK) {
            printf(FORCE_USAGE);
            return 1;
        }
        printf("weather force: %s\n",
               weather_mgr_forced() ? argv[2] : "off (real data)");
        return 0;
    }
    if (!weather_mgr_request_refresh()) {
        printf("fetch:    throttled (min 10 s between forced fetches)\n");
    }

    char name[24];
    float lat, lon;
    weather_mgr_location(&lat, &lon, name, sizeof(name));
    printf("location: %s (%.4f, %.4f)%s\n", name, (double)lat, (double)lon,
           s_auto ? " [auto]" : "");
    printf("online:   %s\n", wifi_mgr_is_connected() ? "yes" : "no");
    if (weather_mgr_forced()) {
        printf("force:    %s (debug override, scene + condition)\n",
               weather_mgr_cond(weather_mgr_eff_wmo()));
    }

    const weather_data_t *d = weather_mgr_data();
    if (!d) {
        printf("weather:  no data yet\n");
        return 0;
    }
    int age = weather_mgr_age_min();
    printf("weather:  %.1fC  %s  (humidity %d%%)\n", d->temp_c,
           weather_mgr_cond(weather_mgr_eff_wmo()), d->humidity_pct);
    {
        int cc = weather_mgr_cloud_cover();
        if (cc < 0) { printf("cloud:    unknown\n"); }
        else { printf("cloud:    %d%%%s\n", cc,
                       weather_mgr_forced() ? " (forced nominal)" : ""); }
    }
    printf("age:      %d min%s\n", age, s_online ? "" : " (showing last known)");
    obs_t o;
    if (obs_fresh(&o)) {
        time_t t = (time_t)o.obs_epoch;
        struct tm tm;
        localtime_r(&t, &tm);
        printf("observed: %s %02d:%02d, %d km: %s%s%s\n", o.icao, tm.tm_hour,
               tm.tm_min, o.km, o.wx[0] ? o.wx : "no precipitation",
               o.wmo >= 0 ? " -> " : "", o.wmo >= 0 ? weather_mgr_cond(o.wmo) : "");
    } else {
        printf("observed: none (no airport report within ~30 km in 90 min)\n");
    }
    printf("forecast:");
    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
        char day[8];
        if (!weather_mgr_daily_label(i, day, sizeof(day))) {
            snprintf(day, sizeof(day), "?");
        }
        printf("  %s %.0f/%.0fC %s", day, d->daily_max[i], d->daily_min[i],
               weather_mgr_cond(d->daily_code[i]));
    }
    printf("\n");
    if (d->sunrise_min >= 0 && d->sunset_min >= 0) {
        printf("sun:      %02d:%02d - %02d:%02d\n", d->sunrise_min / 60,
               d->sunrise_min % 60, d->sunset_min / 60, d->sunset_min % 60);
    }
    return 0;
}

/* Strict float parse: the whole token must be a finite number in range. */
static bool parse_coord(const char *s, float lo, float hi, float *out)
{
    char *end;
    float v = strtof(s, &end);
    if (end == s || *end != '\0' || !isfinite(v) || v < lo || v > hi) {
        return false;
    }
    *out = v;
    return true;
}

static int loc_cmd(int argc, char **argv)
{
    static const char *usage =
        "usage: loc <lat -90..90> <lon -180..180> <name>  |  loc auto  |  loc default\n";
    if (argc < 2) {
        char name[24];
        float lat, lon;
        weather_mgr_location(&lat, &lon, name, sizeof(name));
        printf("location: %s (%.4f, %.4f)\n", name, (double)lat, (double)lon);
        printf("mode:     %s\n", s_auto ? "auto (from the internet connection)"
                                          : "manual");
        printf("%s", usage);
        return 0;
    }
    if (!strcmp(argv[1], "auto")) {
        weather_mgr_set_auto();
        printf("location: auto (looking up the internet connection)\n");
        return 0;
    }
    if (!strcmp(argv[1], "default")) {
        weather_mgr_set_location(51.5074f, -0.1278f, "London");
        printf("location reset to London\n");
        return 0;
    }
    float lat, lon;
    if (argc < 4 || !parse_coord(argv[1], -90.0f, 90.0f, &lat) ||
        !parse_coord(argv[2], -180.0f, 180.0f, &lon)) {
        printf("%s", usage);
        return 1;
    }
    /* the name is the remaining arguments joined by spaces, truncated to
     * the stored size (23 chars) */
    char name[24] = "";
    size_t used = 0;
    for (int i = 3; i < argc && used < sizeof(name) - 1; i++) {
        int w = snprintf(name + used, sizeof(name) - used, "%s%s",
                         (i > 3) ? " " : "", argv[i]);
        if (w < 0) { break; }
        used += (size_t)w;
    }
    if (weather_mgr_set_location(lat, lon, name) != ESP_OK) {
        printf("%s", usage);
        return 1;
    }
    printf("location set: %s\n", name);
    return 0;
}

void weather_mgr_register_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        {.command = "weather",
         .help = "weather status + force refresh; "
                 "'weather force <cond>|off' overrides the scene (debug)",
         .hint = NULL, .func = &weather_cmd},
        {.command = "loc",
         .help = "show/set location: loc <lat> <lon> <name> | loc auto | loc default",
         .hint = NULL, .func = &loc_cmd},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

esp_err_t weather_mgr_init(void)
{
    load_loc();
    load_cache();
    xTaskCreate(weather_task, "weather", 10240, NULL, 4, &s_task);
    return ESP_OK;
}
