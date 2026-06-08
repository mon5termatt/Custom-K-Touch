#pragma once
/* Prusa-Touch — shared state, polling, and the command worker. */
#include "pandaprusa.h"

typedef enum {
    PP_CMD_PAUSE,
    PP_CMD_RESUME,
    PP_CMD_STOP,
    PP_CMD_PRINT,         /* uses path  */
    PP_CMD_LIST,          /* list active storage root */
    PP_CMD_SET_PRINTER,   /* uses index */
    PP_CMD_WIFI_SCAN,     /* scan -> ui_apply_wifi_list */
    PP_CMD_WIFI_CONNECT,  /* uses path=ssid, arg2=password */
    PP_CMD_THUMB,         /* uses path=thumbnail ref -> ui_apply_thumb */
    PP_CMD_THUMB_DASH,    /* uses path=ref, index=idx -> ui_apply_thumb_dash */
    PP_CMD_GCODE,         /* uses path=gcode string      */
    PP_CMD_PREHEAT,       /* uses index=material_idx     */
    PP_CMD_DASH_REFRESH,
} pp_cmd_kind_t;

typedef struct {
    pp_cmd_kind_t kind;
    char path[160];
    char arg2[80];
    int  index;
} pp_cmd_t;

/* Start WiFi-independent state machinery: creates the network task that polls
 * status and drains the command queue. Call after wifi + display are up. */
void app_state_start(void);

/* Thread-safe snapshot of the active printer's latest status. */
void app_state_get(pp_status_t *out);

/* Thread-safe snapshot of the whole fleet cache. Copies up to `max` entries into
 * arr[] and writes the count to *count. */
void app_state_get_fleet(pp_status_t *arr, int max, int *count);

/* Enqueue a control/file command (non-blocking; safe from the LVGL thread). */
void app_state_post_cmd(pp_cmd_kind_t kind, const char *path);

/* Switch the active printer (by store index) and refresh. */
void app_state_select_printer(int index);
void app_state_refresh_dashboard(void);

/* Call after the printer store changes (add/edit/remove) so the fleet cache is
 * reset and re-polled (avoids stale/misindexed dashboard cards). */
void app_state_printers_changed(void);

/* WiFi onboarding (run off the UI thread). */
void app_state_wifi_scan(void);                                 /* -> ui_apply_wifi_list */
void app_state_wifi_connect(const char *ssid, const char *pass);

/* Fetch a gcode thumbnail (PNG) by its ref -> ui_apply_thumb. */
void app_state_fetch_thumb(const char *ref);
void app_state_fetch_thumb_dash(const char *ref, int idx);
