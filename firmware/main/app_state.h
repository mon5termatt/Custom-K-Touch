#pragma once
/* Prusa-Touch — shared state, polling, and the command worker. */
#include "pandaprusa.h"

typedef enum {
    PP_CMD_PAUSE,
    PP_CMD_RESUME,
    PP_CMD_STOP,
    PP_CMD_PRINT,         /* uses path  */
    PP_CMD_LIST,          /* list active storage root */
    PP_CMD_LIST_USB,      /* list local USB drive      */
    PP_CMD_UPLOAD,        /* upload path to printer    */
    PP_CMD_SET_PRINTER,   /* uses index */
    PP_CMD_WIFI_SCAN,     /* scan -> ui_apply_wifi_list */
    PP_CMD_WIFI_CONNECT,  /* uses path=ssid, arg2=password */
    PP_CMD_THUMB,         /* uses path=thumbnail ref -> ui_apply_thumb */
    PP_CMD_THUMB_DASH,    /* uses path=ref, index=idx -> ui_apply_thumb_dash */
    PP_CMD_GCODE,         /* uses path=gcode string      */
    PP_CMD_PREHEAT,       /* uses index=material_idx     */
    PP_CMD_DASH_REFRESH,
    PP_CMD_SET_PREF,      /* uses index = packed pref (NVS write off the LVGL task) */
    PP_CMD_FARM_REFRESH,  /* fetch Prusa Farm stats+orders -> ui_apply_farm */
    PP_CMD_SNAPSHOT,      /* fetch active cloud printer's webcam JPEG -> ui_apply_snapshot */
    PP_CMD_HOME,          /* home axes (Connect HOME / gcode G28), no args */
    PP_CMD_MOVE,          /* relative jog: index=axis(0=X,1=Y,2=Z), i32a=dist*100 mm, i32b=feedrate */
    PP_CMD_DIALOG_ACTION, /* answer the active printer's attention dialog: index=dialog_id, path=button label */
    PP_CMD_STORE_ADD,     /* add a printer (cmd.printer); routes the NVS write off the LVGL task */
    PP_CMD_STORE_UPDATE,  /* update printer at cmd.index with cmd.printer                        */
    PP_CMD_STORE_REMOVE,  /* remove printer at cmd.index                                         */
} pp_cmd_kind_t;

/* Preference writes are routed through the net task because the LVGL task's stack
 * lives in PSRAM and cannot perform flash/NVS writes. */
typedef enum { PP_PREF_SORT, PP_PREF_HIDE_OFFLINE, PP_PREF_LOGO, PP_PREF_AUTOUPDATE, PP_PREF_ORIENT } pp_pref_kind_t;
void app_state_set_pref(pp_pref_kind_t pref, int value);

typedef struct {
    pp_cmd_kind_t kind;
    char path[160];
    char arg2[80];
    int  index;
    int  i32a;   /* generic numeric arg (e.g. jog distance in mm*100) */
    int  i32b;   /* generic numeric arg (e.g. jog feedrate)           */
    pp_printer_t printer;   /* payload for PP_CMD_STORE_ADD / STORE_UPDATE */
} pp_cmd_t;

/* Printer-store mutations from the LVGL task MUST go through here (the store's NVS write
 * can't run on the PSRAM-stacked LVGL task). The net task applies them + republishes. */
void app_state_store_add(const pp_printer_t *p);
void app_state_store_update(int idx, const pp_printer_t *p);
void app_state_store_remove(int idx);

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
/* Post a command carrying numeric args (preheat index, jog axis/distance/feedrate). */
void app_state_post_cmd_n(pp_cmd_kind_t kind, int index, int i32a, int i32b);

/* Answer the active printer's attention dialog (Connect DIALOG_ACTION). */
void app_state_dialog_action(int dialog_id, const char *button);

/* Switch the active printer (by store index) and refresh. */
void app_state_select_printer(int index);
void app_state_refresh_dashboard(void);
void app_state_farm_refresh(void);   /* -> ui_apply_farm */
void app_state_fetch_snapshot(void); /* active cloud printer webcam -> ui_apply_snapshot */

/* Call after the printer store changes (add/edit/remove) so the fleet cache is
 * reset and re-polled (avoids stale/misindexed dashboard cards). */
void app_state_printers_changed(void);

/* WiFi onboarding (run off the UI thread). */
void app_state_wifi_scan(void);                                 /* -> ui_apply_wifi_list */
void app_state_wifi_connect(const char *ssid, const char *pass);

/* Fetch a gcode thumbnail (PNG) by its ref -> ui_apply_thumb. */
void app_state_fetch_thumb(const char *ref);
void app_state_fetch_thumb_dash(const char *ref, int idx);
