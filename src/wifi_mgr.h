#ifndef CLOCK_WIFI_MGR_H
#define CLOCK_WIFI_MGR_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Wi-Fi manager: multiple networks in NVS with priority, standalone
 * auto-connect to the strongest saved network in range (with backoff),
 * console commands: wifi scan | add | remove | list | reorder | status |
 * connect. Passwords are stored in NVS but never printed or logged. */
esp_err_t wifi_mgr_init(void);
void wifi_mgr_register_commands(void);

/* Status accessors (for the UI / settings screen) */
bool wifi_mgr_is_connected(void);
int wifi_mgr_rssi(void);            /* dBm, 0 when not connected */
const char *wifi_mgr_ssid(void);    /* connected SSID or "" */
const char *wifi_mgr_ip(void);      /* IPv4 string or "" */
/* "ssid, ssid, ..."; false (buf untouched) if the store is busy */
bool wifi_mgr_saved_names(char *buf, size_t sz);

#endif
