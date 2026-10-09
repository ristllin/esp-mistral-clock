/* Wi-Fi manager: NVS-backed multi-network store with priority, standalone
 * auto-connect with backoff, console commands.
 *
 * Store: one NVS blob, fixed-size records (max 8). Passwords live only in
 * NVS and in RAM while connecting; they are never printed, logged or echoed
 * (the console REPL masks the password token of `wifi add` lines).
 *
 * Selection: among saved networks found in a scan, the strongest RSSI wins;
 * ties within 3 dB go to the lower priority number. Retries use exponential
 * backoff (1 s doubling to 60 s), reset after a successful connection.
 * The device needs no USB host: the wifi task runs from boot. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"

#include "wifi_mgr.h"

static const char *TAG = "wifi";

#define WIFI_MAX_NETS   8
#define WIFI_SCAN_MAX   20
#define NVS_NAMESPACE   "wifi"
#define NVS_KEY_STORE   "store"
#define STORE_MAGIC     0x4D574831   /* "MWH1" */

typedef struct {
    char ssid[33];
    char pass[65];
    uint8_t prio;        /* 1 = highest priority */
    uint8_t in_use;
    uint8_t reserved[2];
} wifi_net_t;

typedef struct {
    uint32_t magic;
    uint8_t count;
    uint8_t reserved[3];
    wifi_net_t nets[WIFI_MAX_NETS];
} wifi_store_t;

static wifi_store_t g_store;
static SemaphoreHandle_t g_store_lock;  /* g_store; held briefly, never
                                         * across a scan or a connect */
static SemaphoreHandle_t g_scan_lock;   /* one radio scan at a time */
static TaskHandle_t g_wifi_task;

static volatile bool g_sta_started = false;
static volatile bool g_connected = false;
static volatile bool g_got_ip = false;
static volatile bool g_disconnected = false;
static char g_ssid[33] = "";
static char g_ip[16] = "";
static int g_rssi = 0;

/* Zero a buffer that held a password. Plain memset() on a buffer that is
 * dead afterwards may be optimised away; volatile stores may not. */
static void secure_wipe(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--) { *v++ = 0; }
}

/* --- NVS store --- */

static void store_defaults(void)
{
    memset(&g_store, 0, sizeof(g_store));
    g_store.magic = STORE_MAGIC;
}

static void store_load(void)
{
    store_defaults();
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) { return; }
    size_t len = sizeof(g_store);
    if (nvs_get_blob(h, NVS_KEY_STORE, &g_store, &len) != ESP_OK ||
        len != sizeof(g_store) || g_store.magic != STORE_MAGIC) {
        store_defaults();
    }
    nvs_close(h);
    /* drop stale in_use flags, default a missing prio, terminate strings */
    g_store.count = 0;
    for (int i = 0; i < WIFI_MAX_NETS; i++) {
        g_store.nets[i].ssid[sizeof(g_store.nets[i].ssid) - 1] = '\0';
        g_store.nets[i].pass[sizeof(g_store.nets[i].pass) - 1] = '\0';
        if (g_store.nets[i].in_use && g_store.nets[i].ssid[0]) {
            g_store.nets[i].prio = g_store.nets[i].prio ? g_store.nets[i].prio : (uint8_t)(g_store.count + 1);
            g_store.count++;
        } else {
            g_store.nets[i].in_use = 0;
        }
    }
}

static esp_err_t store_save(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h), TAG, "nvs open");
    esp_err_t err = nvs_set_blob(h, NVS_KEY_STORE, &g_store, sizeof(g_store));
    if (err == ESP_OK) { err = nvs_commit(h); }
    nvs_close(h);
    return err;
}

static wifi_net_t *store_find(const char *ssid)
{
    for (int i = 0; i < WIFI_MAX_NETS; i++) {
        if (g_store.nets[i].in_use && strncmp(g_store.nets[i].ssid, ssid, 32) == 0) {
            return &g_store.nets[i];
        }
    }
    return NULL;
}

static wifi_net_t *store_find_free(void)
{
    for (int i = 0; i < WIFI_MAX_NETS; i++) {
        if (!g_store.nets[i].in_use) { return &g_store.nets[i]; }
    }
    return NULL;
}

/* Physically sort the slot array by priority (stable insertion sort, used
 * slots compacted to the front) and reassign priorities as a strict 1..N
 * order. Call after add/reorder so two networks can never share a priority
 * and `wifi list` / saved-names always show priority order.
 * Caller holds g_store_lock. */
static void store_normalize_prio(void)
{
    for (int i = 1; i < WIFI_MAX_NETS; i++) {
        if (!g_store.nets[i].in_use) { continue; }
        wifi_net_t key = g_store.nets[i];
        int j = i - 1;
        while (j >= 0 && (!g_store.nets[j].in_use ||
                          g_store.nets[j].prio > key.prio)) {
            g_store.nets[j + 1] = g_store.nets[j];
            j--;
        }
        g_store.nets[j + 1] = key;
    }
    int p = 1;
    for (int i = 0; i < WIFI_MAX_NETS; i++) {
        if (g_store.nets[i].in_use) { g_store.nets[i].prio = (uint8_t)p++; }
    }
}

/* --- wifi events + task --- */

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        g_sta_started = true;
        if (g_wifi_task) { xTaskNotifyGive(g_wifi_task); }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
        g_connected = false;
        g_got_ip = false;
        g_disconnected = true;
        ESP_LOGW(TAG, "disconnected (reason %d)", ev->reason);
        if (g_wifi_task) { xTaskNotifyGive(g_wifi_task); }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(g_ip, sizeof(g_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        g_connected = true;
        g_got_ip = true;
        g_disconnected = false;
        esp_wifi_sta_get_rssi(&g_rssi);
        ESP_LOGI(TAG, "connected: %s  ip %s  rssi %d", g_ssid, g_ip, g_rssi);
        if (g_wifi_task) { xTaskNotifyGive(g_wifi_task); }
    }
}

/* Blocking all-channel scan (about 2 s). Never call it with g_store_lock
 * held: UI code reads the store under that lock. Returns the number of APs,
 * or -1 when the driver refused to scan (e.g. while connecting). */
static int scan_aps(wifi_ap_record_t *aps, uint16_t max)
{
    /* explicit all-channel active scan: with several APs sharing an SSID on
     * different channels, only an all-channel scan sees them all, so the
     * strongest one can be picked (and pinned by BSSID at connect time) */
    wifi_scan_config_t cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .channel = 0,               /* 0 = all channels */
        .show_hidden = false,
    };
    xSemaphoreTake(g_scan_lock, portMAX_DELAY);
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    uint16_t num = max;
    if (err == ESP_OK) { err = esp_wifi_scan_get_ap_records(&num, aps); }
    xSemaphoreGive(g_scan_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return -1;
    }
    return (int)num;
}

/* Pick the best saved network among the scan results: strongest RSSI wins,
 * ties within 3 dB go to the lower priority number. The winning AP record
 * (BSSID + channel) is copied to *out_ap so the caller can pin it. */
static const wifi_net_t *select_best(const wifi_ap_record_t *aps, int n,
                                     wifi_ap_record_t *out_ap, int *out_rssi)
{
    const wifi_net_t *best = NULL;
    int best_rssi = -999, best_prio = 999;
    int best_idx = -1;
    for (int i = 0; i < n; i++) {
        const wifi_net_t *net = store_find((const char *)aps[i].ssid);
        if (!net) { continue; }
        int rssi = aps[i].rssi;
        bool better = rssi > best_rssi + 3 ||
                      (rssi >= best_rssi - 3 && rssi <= best_rssi + 3 && net->prio < best_prio);
        if (!best || better) {
            best = net; best_rssi = rssi; best_prio = net->prio; best_idx = i;
        }
    }
    if (out_ap && best_idx >= 0) { *out_ap = aps[best_idx]; }
    if (out_rssi) { *out_rssi = best ? best_rssi : 0; }
    return best;
}

static void connect_to(const wifi_net_t *net, const wifi_ap_record_t *ap)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, net->ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, net->pass, sizeof(cfg.sta.password));
    if (ap) {
        /* pin the exact AP we measured: with several APs sharing an SSID
         * the supplicant must not wander to a weaker one */
        memcpy(cfg.sta.bssid, ap->bssid, sizeof(cfg.sta.bssid));
        cfg.sta.bssid_set = true;
        cfg.sta.channel = ap->primary;
        /* our own scan already picked the AP: probe only its channel */
        cfg.sta.scan_method = WIFI_FAST_SCAN;
    } else {
        cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    }
    strlcpy(g_ssid, net->ssid, sizeof(g_ssid));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    /* the driver keeps its own copy: wipe the password from the stack */
    secure_wipe(&cfg, sizeof(cfg));
    if (err == ESP_OK) { err = esp_wifi_connect(); }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect to %s failed: %s", net->ssid, esp_err_to_name(err));
    }
}

static void wifi_task(void *arg)
{
    (void)arg;
    while (!g_sta_started) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
    }
    int backoff_ms = 1000;
    static wifi_ap_record_t aps[WIFI_SCAN_MAX];
    /* copies: they must stay valid after unlock */
    static wifi_net_t chosen;
    static wifi_ap_record_t chosen_ap;
    while (1) {
        int rssi = 0;
        int n = g_connected ? 0 : scan_aps(aps, WIFI_SCAN_MAX);
        const wifi_net_t *best = NULL;
        if (n > 0) {
            xSemaphoreTake(g_store_lock, portMAX_DELAY);
            best = select_best(aps, n, &chosen_ap, &rssi);
            if (best) { chosen = *best; best = &chosen; }
            xSemaphoreGive(g_store_lock);
        }

        if (!best) {
            ESP_LOGI(TAG, "no saved network in range, retry in %d ms", backoff_ms);
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff_ms))) { continue; }
            backoff_ms = backoff_ms * 2 > 60000 ? 60000 : backoff_ms * 2;
            continue;
        }

        ESP_LOGI(TAG, "connecting to %s (rssi %d dBm, ch %d)", best->ssid, rssi,
                 chosen_ap.primary);
        g_disconnected = false;
        connect_to(best, &chosen_ap);
        secure_wipe(chosen.pass, sizeof(chosen.pass));

        /* wait for connected or disconnect, up to 15 s (the event handler
         * notifies this task on both) */
        TickType_t t0 = xTaskGetTickCount();
        while (!g_connected && !g_disconnected &&
               xTaskGetTickCount() - t0 < pdMS_TO_TICKS(15000)) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        }
        if (g_connected) {
            backoff_ms = 1000;
            while (g_connected) {    /* stay connected until the link drops */
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        backoff_ms = backoff_ms * 2 > 60000 ? 60000 : backoff_ms * 2;
    }
}

esp_err_t wifi_mgr_init(void)
{
    g_store_lock = xSemaphoreCreateMutex();
    g_scan_lock = xSemaphoreCreateMutex();
    xSemaphoreTake(g_store_lock, portMAX_DELAY);
    store_load();
    xSemaphoreGive(g_store_lock);
    ESP_LOGI(TAG, "%d saved network(s)", g_store.count);

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* Default WIFI_STORAGE_FLASH: the driver also keeps the active network's
     * config in NVS, plaintext like our own store. Erase the flash before
     * giving the device away. */
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            &event_handler, NULL, NULL),
                        TAG, "wifi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            &event_handler, NULL, NULL),
                        TAG, "ip event handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    xTaskCreate(wifi_task, "wifi", 6144, NULL, 3, &g_wifi_task);
    return ESP_OK;
}

/* --- status accessors --- */

bool wifi_mgr_is_connected(void) { return g_connected && g_got_ip; }
int wifi_mgr_rssi(void) { return g_connected ? g_rssi : 0; }
const char *wifi_mgr_ssid(void) { return g_ssid; }
const char *wifi_mgr_ip(void) { return g_got_ip ? g_ip : ""; }

/* Comma-separated list of the saved SSIDs, truncated to fit. Called from an
 * esp_timer callback (settings refresh), so it never waits long for the
 * store: false when a console command holds it (NVS commit in progress). */
bool wifi_mgr_saved_names(char *buf, size_t sz)
{
    if (sz == 0) { return false; }
    if (xSemaphoreTake(g_store_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    buf[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < WIFI_MAX_NETS; i++) {
        if (!g_store.nets[i].in_use) { continue; }
        const char *ssid = g_store.nets[i].ssid;
        size_t need = strlen(ssid) + (used ? 2 : 0);
        if (used + need + 5 >= sz) {   /* keep room for ", ..." */
            snprintf(buf + used, sz - used, ", ...");
            break;
        }
        used += snprintf(buf + used, sz - used, "%s%s", used ? ", " : "", ssid);
    }
    xSemaphoreGive(g_store_lock);
    return true;
}

/* --- console commands ---
 * Passwords are never printed: `wifi add` takes the password as an argument,
 * the REPL prints a fixed "****" instead of echoing it and no command ever
 * prints it back. */

static const char *auth_str(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    default: return "?";
    }
}

/* SSIDs come over the air (up to 32 arbitrary bytes): print control and
 * non-ASCII bytes as '?' so a beacon cannot inject terminal escapes. */
static void print_ssid(const uint8_t *ssid)
{
    for (int i = 0; i < 32 && ssid[i]; i++) {
        putchar((ssid[i] >= 0x20 && ssid[i] < 0x7F) ? ssid[i] : '?');
    }
}

static int wifi_scan_cmd(void)
{
    static wifi_ap_record_t aps[WIFI_SCAN_MAX];
    printf("scanning...\n");
    int n = scan_aps(aps, WIFI_SCAN_MAX);
    if (n < 0) {
        printf("scan failed (busy connecting? try again)\n");
        return 1;
    }
    printf("%d network(s):\n", n);
    /* sort by RSSI descending */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (aps[j].rssi > aps[i].rssi) {
                wifi_ap_record_t t = aps[i]; aps[i] = aps[j]; aps[j] = t;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        xSemaphoreTake(g_store_lock, portMAX_DELAY);
        bool saved = store_find((const char *)aps[i].ssid) != NULL;
        xSemaphoreGive(g_store_lock);
        printf("  %3d dBm  ch %2d  %-8s  ", aps[i].rssi, aps[i].primary,
               auth_str(aps[i].authmode));
        print_ssid(aps[i].ssid);
        printf("%s\n", saved ? "  [saved]" : "");
    }
    return 0;
}

static int wifi_add_cmd(int argc, char **argv)
{
    /* argv (after "add"): <ssid> <pass> [prio] */
    if (argc < 3) {
        printf("usage: wifi add <ssid> <pass> [prio]\n");
        return 1;
    }
    const char *ssid = argv[1];
    const char *pass = argv[2];
    if (strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(pass) > 64) {
        printf("ssid 1..32 chars, pass 0..64 chars\n");
        return 1;
    }
    xSemaphoreTake(g_store_lock, portMAX_DELAY);
    wifi_net_t *net = store_find(ssid);
    bool is_new = false;
    if (!net) {
        net = store_find_free();
        is_new = true;
    }
    if (!net) {
        xSemaphoreGive(g_store_lock);
        printf("store full (%d networks max)\n", WIFI_MAX_NETS);
        return 1;
    }
    uint8_t old_prio = net->prio;   /* re-adding keeps the priority */
    memset(net, 0, sizeof(*net));
    strlcpy(net->ssid, ssid, sizeof(net->ssid));
    strlcpy(net->pass, pass, sizeof(net->pass));
    if (argc > 3) {
        /* insert at that position: networks at or after it move down */
        int prio = atoi(argv[3]);
        prio = (prio < 1) ? 1 : (prio > WIFI_MAX_NETS) ? WIFI_MAX_NETS : prio;
        for (int i = 0; i < WIFI_MAX_NETS; i++) {
            wifi_net_t *o = &g_store.nets[i];
            if (o != net && o->in_use && o->prio >= prio) { o->prio++; }
        }
        net->prio = (uint8_t)prio;
    } else if (is_new) {
        net->prio = (uint8_t)(g_store.count + 1);
    } else {
        net->prio = old_prio ? old_prio : 1;
    }
    net->in_use = 1;
    if (is_new) { g_store.count++; }
    store_normalize_prio();     /* re-sorts the slots: net may move */
    esp_err_t err = store_save();
    int prio = store_find(ssid)->prio;
    int count = g_store.count;
    xSemaphoreGive(g_store_lock);
    if (err != ESP_OK) {
        printf("nvs save failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("saved %s (prio %d, %d network(s) total)\n", ssid, prio, count);
    return 0;
}

static int wifi_remove_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: wifi remove <ssid>\n");
        return 1;
    }
    xSemaphoreTake(g_store_lock, portMAX_DELAY);
    wifi_net_t *net = store_find(argv[1]);
    if (!net) {
        xSemaphoreGive(g_store_lock);
        printf("not found: %s\n", argv[1]);
        return 1;
    }
    bool was_connected = g_connected && strncmp(g_ssid, net->ssid, 32) == 0;
    memset(net, 0, sizeof(*net));
    g_store.count--;
    store_normalize_prio();     /* keep priorities a strict 1..N */
    esp_err_t err = store_save();
    int count = g_store.count;
    xSemaphoreGive(g_store_lock);
    printf("removed %s (%d network(s) left)\n", argv[1], count);
    if (was_connected) {
        printf("disconnecting; will pick another saved network\n");
        esp_wifi_disconnect();
    }
    return err == ESP_OK ? 0 : 1;
}

static int wifi_list_cmd(void)
{
    xSemaphoreTake(g_store_lock, portMAX_DELAY);
    printf("%d saved network(s):\n", g_store.count);
    for (int i = 0; i < WIFI_MAX_NETS; i++) {
        if (!g_store.nets[i].in_use) { continue; }
        bool conn = g_connected && strncmp(g_ssid, g_store.nets[i].ssid, 32) == 0;
        printf("  prio %d  %s%s\n", g_store.nets[i].prio, g_store.nets[i].ssid,
               conn ? "  [connected]" : "");
    }
    xSemaphoreGive(g_store_lock);
    return 0;
}

static int wifi_reorder_cmd(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: wifi reorder <ssid> <prio 1..8>\n");
        return 1;
    }
    int prio = atoi(argv[2]);
    if (prio < 1 || prio > WIFI_MAX_NETS) {
        printf("prio must be 1..%d\n", WIFI_MAX_NETS);
        return 1;
    }
    xSemaphoreTake(g_store_lock, portMAX_DELAY);
    wifi_net_t *net = store_find(argv[1]);
    if (!net) {
        xSemaphoreGive(g_store_lock);
        printf("not found: %s\n", argv[1]);
        return 1;
    }
    /* Move the network to position `prio` in the current priority order
     * and close the gap: reordering produces a strict 1..N order, never
     * two networks at one priority. */
    wifi_net_t *order[WIFI_MAX_NETS];
    int cnt = 0;
    for (int i = 0; i < WIFI_MAX_NETS; i++) {
        if (g_store.nets[i].in_use) { order[cnt++] = &g_store.nets[i]; }
    }
    for (int i = 1; i < cnt; i++) {           /* stable sort by prio */
        wifi_net_t *key = order[i];
        int j = i - 1;
        while (j >= 0 && order[j]->prio > key->prio) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    int ti = 0;
    while (ti < cnt && order[ti] != net) { ti++; }
    memmove(&order[ti], &order[ti + 1], (size_t)(cnt - ti - 1) * sizeof(order[0]));
    cnt--;
    int pos = prio - 1;
    if (pos > cnt) { pos = cnt; }
    memmove(&order[pos + 1], &order[pos], (size_t)(cnt - pos) * sizeof(order[0]));
    order[pos] = net;
    cnt++;
    for (int i = 0; i < cnt; i++) { order[i]->prio = (uint8_t)(i + 1); }
    printf("new order:");
    for (int i = 0; i < cnt; i++) {
        printf("  %d:%s", order[i]->prio, order[i]->ssid);
    }
    printf("\n");
    store_normalize_prio();     /* physical slot order = priority order */
    esp_err_t err = store_save();
    xSemaphoreGive(g_store_lock);
    return err == ESP_OK ? 0 : 1;
}

static int wifi_status_cmd(void)
{
    if (g_connected && g_got_ip) {
        printf("connected to %s\n  ip %s  rssi %d dBm\n", g_ssid, g_ip, g_rssi);
    } else if (g_connected) {
        printf("associating with %s (no IP yet)\n", g_ssid);
    } else {
        printf("not connected\n");
    }
    xSemaphoreTake(g_store_lock, portMAX_DELAY);
    printf("saved networks: %d\n", g_store.count);
    xSemaphoreGive(g_store_lock);
    return 0;
}

static int wifi_connect_cmd(void)
{
    printf("reconnecting to the best saved network...\n");
    if (g_connected) { esp_wifi_disconnect(); }
    if (g_wifi_task) { xTaskNotifyGive(g_wifi_task); }
    return 0;
}

static int wifi_cmd(int argc, char **argv)
{
    if (argc < 2) {
        return wifi_status_cmd();
    }
    if (strcmp(argv[1], "scan") == 0)    { return wifi_scan_cmd(); }
    if (strcmp(argv[1], "add") == 0)     { return wifi_add_cmd(argc - 1, argv + 1); }
    if (strcmp(argv[1], "remove") == 0)  { return wifi_remove_cmd(argc - 1, argv + 1); }
    if (strcmp(argv[1], "list") == 0)    { return wifi_list_cmd(); }
    if (strcmp(argv[1], "reorder") == 0) { return wifi_reorder_cmd(argc - 1, argv + 1); }
    if (strcmp(argv[1], "status") == 0)  { return wifi_status_cmd(); }
    if (strcmp(argv[1], "connect") == 0) { return wifi_connect_cmd(); }
    printf("usage: wifi [scan | add <ssid> <pass> [prio] | remove <ssid> | list | reorder <ssid> <prio> | status | connect]\n");
    return 1;
}

void wifi_mgr_register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help = "wifi scan | add <ssid> <pass> [prio] | remove <ssid> | list | reorder <ssid> <prio> | status | connect",
        .hint = NULL,
        .func = &wifi_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
