#pragma once
/* Prusa-Touch — LVGL UI (Prusa-themed). */
#include "pandaprusa.h"

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

