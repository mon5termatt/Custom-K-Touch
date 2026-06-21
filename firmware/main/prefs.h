#pragma once
/* Prusa-Touch — user preferences (NVS-backed): dashboard sort/filter + logo style. */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PP_SORT_STATUS = 0,   /* group by printer state (printing -> error -> ... -> offline) */
    PP_SORT_NAME,         /* alphabetical by name                                         */
    PP_SORT_MODEL,        /* group by model string                                        */
    PP_SORT_PROGRESS,     /* by completion %: printing (high->low) then finished/idle/off  */
    PP_SORT_COUNT
} pp_sort_t;

typedef enum {
    PP_LOGO_STACKED = 0,  /* [PRUSA|TOUCH] + "by NomadsGalaxy" (default) */
    PP_LOGO_SINGLE,       /* single-line [PRUSA|TOUCH], no byline        */
} pp_logo_t;

typedef enum {
    PP_ORIENT_LANDSCAPE = 0,     /* 800x480 normal (default)      */
    PP_ORIENT_LANDSCAPE_FLIPPED, /* 180° (upside-down mounting)   */
    PP_ORIENT_PORTRAIT,          /* 90°  — 480x800 (single column)*/
    PP_ORIENT_PORTRAIT_FLIPPED,  /* 270° — 480x800 (other way up) */
} pp_orient_t;

/* Load cached prefs from NVS (call once at boot, before the UI is built). */
void prefs_load(void);

pp_sort_t prefs_sort(void);
void      prefs_set_sort(pp_sort_t s);   /* persists */

bool prefs_hide_offline(void);
void prefs_set_hide_offline(bool v);     /* persists */

pp_logo_t prefs_logo(void);
void      prefs_set_logo(pp_logo_t l);   /* persists */

/* Opt-in automatic firmware updates from GitHub Releases (default off). */
bool prefs_auto_update(void);
void prefs_set_auto_update(bool v);      /* persists */

/* Screen orientation (default landscape). */
pp_orient_t prefs_orient(void);
void        prefs_set_orient(pp_orient_t o);   /* persists */

/* --- security opt-ins (all off/empty by default) --- */
/* Auto-lock the touchscreen after N idle minutes (0 = off). While locked, browsing is allowed
 * but actions need the screen PIN. */
uint8_t     prefs_lock_min(void);
void        prefs_set_lock_min(uint8_t m);
const char *prefs_scrpin(void);                /* on-device unlock PIN ("" = no lock)        */
void        prefs_set_scrpin(const char *p);
const char *prefs_web_pass(void);              /* web-interface password ("" = open access)  */
void        prefs_set_web_pass(const char *p);

/* Daily maintenance reboot to clear RAM. hour = local hour 0..23 (0xFF = disabled, the default).
 * tz_off = the device's UTC offset in hours, so "hour" is wall-clock local (no DST handling). */
uint8_t prefs_reboot_hour(void);
void    prefs_set_reboot_hour(uint8_t h);      /* 0..23 enables; anything else disables */
int8_t  prefs_tz_offset(void);
void    prefs_set_tz_offset(int8_t hrs);
