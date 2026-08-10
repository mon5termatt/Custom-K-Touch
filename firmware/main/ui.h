#pragma once
/* Prusa-Touch — LVGL UI (Prusa-themed). */
#include "pandaprusa.h"
#include "layout.h"               /* pp_layout_t — for the off-screen layout preview */
#ifndef PP_HOST_SIM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"      /* SemaphoreHandle_t — preview render handshake (device only) */
#endif

typedef struct {
    int       count;
    pp_file_t items[PP_MAX_FILES];
} pp_file_list_t;

#define PP_WIFI_MAX_SCAN 24
typedef struct {
    int  count;
    char ssids[PP_WIFI_MAX_SCAN][33];
} pp_wifi_list_t;

/* Snapshot of every configured printer's status, for the fleet dashboard. */
typedef struct {
    int         count;
    bool        conn_expired;   /* cloud printers configured but Connect sign-in lapsed */
    pp_status_t items[PP_MAX_PRINTERS];
} pp_dash_t;

/* Build the UI. Must run on the LVGL thread (call under PT_LVGL_SCOPE_LOCK). */
void ui_init(void);

/* Update the boot screen progress (0..100) and status text. Thread-safe. */
void ui_boot_update(int progress, const char *status);

/* Scheduled appliers — run on the LVGL thread, take ownership of arg, free it. */
void ui_apply_status(void *status_copy);     /* arg: pp_status_t*     */
void ui_apply_files(void *file_list_copy);    /* arg: pp_file_list_t*  */
void ui_apply_wifi_list(void *wifi_list_copy);  /* arg: pp_wifi_list_t* */
void ui_apply_dashboard(void *dash_copy);     /* arg: pp_dash_t*       */
void ui_apply_printers(void *unused);          /* rebuild the Settings printer list (after a store write) */
void ui_apply_lock_cfg(void *unused);          /* (re)arm the idle screen-lock timer after a config change */
void ui_lock_now(void);                        /* engage the screen lock immediately (manual lock) */
void ui_show_lock_prompt(void);                /* pop the PIN-unlock prompt (badge tap / preview)  */
void ui_apply_farm(void *farm_copy);          /* arg: pp_farm_t* (Prusa Farm view) */
void ui_apply_thumb(void *image_copy);        /* arg: pp_image_t* (takes ownership) */

typedef struct {
    pp_image_t *image;
    int         index;
} pp_thumb_dash_t;
void ui_apply_thumb_dash(void *arg);          /* arg: pp_thumb_dash_t* */

/* Automation/testing nav API. ui_request_screen() is thread-safe (marshals to the
 * LVGL thread); names: dash, status, control, files, printers, wifi, about. */
void ui_request_screen(const char *name);
const char *ui_current_screen(void);          /* active screen name (test verify) */

/* Apply the logo preference (show/hide wordmark bylines). Scheduled on the LVGL
 * thread by app_state after the NVS write. arg unused. */
void ui_apply_logo(void *unused);

/* Apply the screen-orientation preference (landscape / 180°). Scheduled on the LVGL
 * thread by app_state after the NVS write, and once at boot. arg unused. */
void ui_apply_orient(void *unused);

/* Apply a fetched webcam snapshot (JPEG) to the Webcam screen. arg: pp_image_t* (owned). */
void ui_apply_snapshot(void *arg);

/* Apply AFC lane status (Moonraker BoxTurtle etc.). arg: pp_afc_t* (owned). */
void ui_apply_afc(void *arg);

/* Tools submenu appliers (owned args freed here). */
void ui_apply_gcode_log(void *arg);   /* pp_gcode_log_t* */
void ui_apply_macros(void *arg);      /* pp_macro_list_t* */
void ui_apply_endstops(void *arg);    /* pp_endstop_list_t* */

/* Build Tools hub + child screens (called from ui_init). */
void ui_tools_init(void);
void ui_tools_open(void);
void ui_tools_open_afc(void);
bool ui_tools_is_hub_active(void);
bool ui_tools_is_webcam_active(void);
void ui_tools_refresh_menu(void);
void ui_tools_estop(void);   /* confirm + ESTOP (Moonraker) */
void ui_tools_show_fault_if_needed(const char *state);
void ui_status_set_afc_chip(const pp_afc_t *a);

/* For ui_tools.c — mirrors ui_locked_block. */
bool ui_locked_block_public(void);

/* Firmware update check result (arg: pp_upd_check_t*, owned -> freed here). Shows a dialog:
 * update available / up to date / check failed. */
void ui_apply_update_check(void *arg);
/* OTA apply failed (arg: char* message, owned -> freed here). Shown only on failure;
 * a successful apply reboots. */
void ui_apply_update_fail(void *arg);

#ifndef PP_HOST_SIM
/* Off-screen layout preview (web "Generate preview"). The httpd handler fills spec/w/h + a binary
 * sem, schedules ui_layout_preview_render on the LVGL task, and blocks on sem; the applier renders
 * the spec to a packed RGB565 buffer (w*h*2, PSRAM) in `rgb` and gives `sem`. The caller owns rgb. */
typedef struct {
    pp_layout_t       spec;   /* in                                                   */
    int               w, h;   /* out: panel native size, filled by the applier        */
    uint8_t          *rgb;    /* out: packed RGB565, heap_caps PSRAM                   */
    bool              ok;     /* out                                                  */
    SemaphoreHandle_t sem;    /* signalled once when done (success or failure)        */
    int               refs;   /* shared ownership: handler + applier each hold one ref */
    portMUX_TYPE      mux;     /* guards refs so exactly one side frees (no leak/UAF)  */
} pp_preview_job_t;
void ui_layout_preview_render(void *arg);   /* runs on the LVGL task via pt_display_schedule_ui */
void pp_preview_job_release(pp_preview_job_t *j);   /* drop one ref; last one frees rgb+sem+job */
#endif

