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

/* Print control. */
esp_err_t moonraker_print(const pp_printer_t *pr, const char *path);  /* /printer/print/start */
esp_err_t moonraker_pause(const pp_printer_t *pr);                    /* /printer/print/pause  */
esp_err_t moonraker_resume(const pp_printer_t *pr);                   /* /printer/print/resume */
esp_err_t moonraker_stop(const pp_printer_t *pr);                     /* /printer/print/cancel */
esp_err_t moonraker_upload(const pp_printer_t *pr, const char *local_path, const char *dest_name);
esp_err_t moonraker_gcode(const pp_printer_t *pr, const char *gcode); /* /printer/gcode/script */
