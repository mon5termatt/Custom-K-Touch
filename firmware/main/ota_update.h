#pragma once
/* Prusa-Touch — GitHub Releases auto-updater (self-OTA from the project's repo). */
#include <stdbool.h>

typedef struct {
    char current[24];   /* this firmware's version            */
    char latest[40];    /* latest release tag on GitHub        */
    char url[300];      /* download URL of the .bin asset      */
    bool available;     /* latest != current and a .bin exists */
} ota_check_t;

/* Query the configured GitHub repo's latest release. Returns true on success
 * (fills *out). Needs WiFi up. */
bool ota_update_check(ota_check_t *out);

/* Download bin_url and OTA-flash it, then reboot. Runs to completion (blocking);
 * call from a dedicated task, not the LVGL thread. */
void ota_update_apply(const char *bin_url);
