#pragma once
/* Prusa-Touch — Moonraker client (Klipper; the API behind Fluidd / Mainsail).
 *
 * Mirrors the shape of the PrusaLink client but every call takes the printer
 * explicitly, so app_state can dispatch per-printer. Moonraker listens on
 * :7125 by default — configure a Klipper printer with that port (or "host:7125"
 * in the host field) and the backend auto-detects it.
 *
 * Implemented against the documented Moonraker HTTP API; NOT yet hardware-tested.
 */
#include <stdint.h>
#include "esp_err.h"
#include "pandaprusa.h"

/* True if host:port answers as a Moonraker instance (GET /printer/info 200). */
bool moonraker_probe(const pp_printer_t *pr);

/* GET /printer/objects/query -> fill pp_status_t (temps, state, job, progress). */
esp_err_t moonraker_get_status_of(const pp_printer_t *pr, pp_status_t *out);

/* GET /printer/info -> friendly model (hostname) + firmware (software_version). */
esp_err_t moonraker_get_info(const pp_printer_t *pr, char *model, size_t ml,
                             char *fw, size_t fl);

/* GET /server/files/list -> printable gcode files (newest-first handled by caller). */
esp_err_t moonraker_list(const pp_printer_t *pr, pp_file_t *arr, int max, int *count);

/* Fetch a gcode thumbnail PNG/JPEG. `ref` is the gcode path (as stored in pp_file_t.thumb
 * for Moonraker rows) — metadata is queried, then the largest thumbnail is downloaded. */
esp_err_t moonraker_fetch_thumb(const pp_printer_t *pr, const char *gcode_path,
                                uint8_t **out, int *out_len);

/* Best-effort webcam JPEG via /server/webcams/list snapshot_url (or legacy webcam path). */
esp_err_t moonraker_fetch_snapshot(const pp_printer_t *pr, uint8_t **out, int *out_len);

/* Print control. */
esp_err_t moonraker_print(const pp_printer_t *pr, const char *path);  /* /printer/print/start */
esp_err_t moonraker_pause(const pp_printer_t *pr);                    /* /printer/print/pause  */
esp_err_t moonraker_resume(const pp_printer_t *pr);                   /* /printer/print/resume */
esp_err_t moonraker_stop(const pp_printer_t *pr);                     /* /printer/print/cancel */
esp_err_t moonraker_upload(const pp_printer_t *pr, const char *local_path, const char *dest_name);
esp_err_t moonraker_gcode(const pp_printer_t *pr, const char *gcode); /* /printer/gcode/script */
esp_err_t moonraker_emergency_stop(const pp_printer_t *pr);            /* POST /printer/emergency_stop */

/* Console: recent gcode responses from Moonraker. */
#define PP_GCODE_LOG_MAX 24
#define PP_GCODE_LOG_LINE 96
typedef struct {
    int  count;
    char lines[PP_GCODE_LOG_MAX][PP_GCODE_LOG_LINE];
} pp_gcode_log_t;
esp_err_t moonraker_gcode_store(const pp_printer_t *pr, pp_gcode_log_t *out);

/* Macro discovery via /printer/gcode/help (filtered to likely user macros). */
#define PP_MACRO_MAX 40
#define PP_MACRO_NAME_LEN 40
typedef struct {
    int  count;
    char names[PP_MACRO_MAX][PP_MACRO_NAME_LEN];
} pp_macro_list_t;
esp_err_t moonraker_list_macros(const pp_printer_t *pr, pp_macro_list_t *out);

/* Endstop report (name + open|TRIGGERED). Includes AFC sensors when registered. */
#define PP_ENDSTOP_MAX      32
#define PP_ENDSTOP_NAME_LEN 24
#define PP_ENDSTOP_STATE_LEN 16
typedef struct {
    int  count;
    char name[PP_ENDSTOP_MAX][PP_ENDSTOP_NAME_LEN];
    char state[PP_ENDSTOP_MAX][PP_ENDSTOP_STATE_LEN];
} pp_endstop_list_t;
esp_err_t moonraker_query_endstops(const pp_printer_t *pr, pp_endstop_list_t *out);

/* LED / neopixel / led_effect objects from /printer/objects/list + color_data query. */
#define PP_LED_MAX      16
#define PP_LED_NAME_LEN 32
#define PP_LED_TYPE_LEN 16
typedef enum { PP_LED_KIND_COLOR = 0, PP_LED_KIND_EFFECT = 1 } pp_led_kind_t;
typedef struct {
    char    name[PP_LED_NAME_LEN];   /* SET_LED LED= / SET_LED_EFFECT EFFECT= */
    char    type[PP_LED_TYPE_LEN];   /* led, neopixel, led_effect, … */
    uint8_t kind;
    bool    pwm;                     /* [led …] PWM (often white_pin only) */
    bool    dimmable;                /* brightness slider (false for Bambu on/off lights) */
    bool    on;                      /* any channel > 0, or led_effect enabled */
    bool    on_known;                /* true if `on` came from printer status */
    uint8_t r, g, b, w;              /* 0..255 from first pixel */
    int     pixels;
} pp_led_t;
typedef struct {
    int     count;
    pp_led_t items[PP_LED_MAX];
} pp_led_list_t;
esp_err_t moonraker_list_leds(const pp_printer_t *pr, pp_led_list_t *out);
esp_err_t moonraker_set_led(const pp_printer_t *pr, const char *name,
                            float r, float g, float b, float w);
esp_err_t moonraker_set_led_effect(const pp_printer_t *pr, const char *name, bool stop);

/* AFC (BoxTurtle etc.): query lane status; change/unload via BT_* macros. */
esp_err_t moonraker_get_afc(const pp_printer_t *pr, pp_afc_t *out);
esp_err_t moonraker_afc_change(const pp_printer_t *pr, int lane_num);  /* BT_CHANGE_TOOL LANE=N */
esp_err_t moonraker_afc_unload(const pp_printer_t *pr);                /* BT_TOOL_UNLOAD */
esp_err_t moonraker_afc_eject(const pp_printer_t *pr, int lane_num);   /* BT_LANE_EJECT LANE=N */
esp_err_t moonraker_afc_move(const pp_printer_t *pr, int lane_num, int distance_mm);
esp_err_t moonraker_afc_prep(const pp_printer_t *pr);                  /* BT_PREP */
esp_err_t moonraker_afc_resume(const pp_printer_t *pr);                /* BT_RESUME */
esp_err_t moonraker_afc_clear(const pp_printer_t *pr);                 /* AFC_CLEAR_MESSAGE */
