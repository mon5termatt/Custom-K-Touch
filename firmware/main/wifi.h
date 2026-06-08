#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define WIFI_MAX_SCAN 24

/* Bring up the WiFi stack (STA) and, if credentials are stored in NVS (or seeded
 * from Kconfig), begin connecting. Non-blocking — the UI shows offline until up. */
void wifi_init_start(void);

bool wifi_is_connected(void);
bool wifi_has_creds(void);

/* Provisioning hotspot (SoftAP) state — raised automatically when no network is
 * reachable, so the web UI is available at http://192.168.4.1 to set up WiFi. */
bool wifi_is_ap_active(void);
const char *wifi_ap_ssid(void);   /* "PrusaTouch-XXXX" once the hotspot is up */

/* Blocking scan (~2 s). Fills ssids[] (each >=33 bytes); returns count. Run this
 * off the LVGL thread (the network task does). */
int  wifi_scan(char ssids[][33], int max);

/* Persist credentials to NVS and (re)connect. Safe from the network task. */
void wifi_save_and_connect(const char *ssid, const char *pass);
