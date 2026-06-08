#pragma once
/* Prusa-Touch — PrusaLink HTTP client (modern /api/v1, HTTP Digest auth). */
#include <stdint.h>
#include "esp_err.h"
#include "pandaprusa.h"

/* Authenticated GET of a raw blob (gcode thumbnail / camera snapshot) on the active
 * printer. Returns ESP_OK + a malloc'd buffer (caller frees *out). */
esp_err_t prusalink_get_blob(const char *path, uint8_t **out, int *out_len);
esp_err_t prusalink_get_blob_of(const pp_printer_t *pr, const char *path, uint8_t **out, int *out_len);

/* No persistent state to set up today, but keep an init hook for symmetry. */
esp_err_t prusalink_init(void);

/* GET /api/v1/status (+ /api/v1/job for the file name) for the ACTIVE printer. */
esp_err_t prusalink_get_status(pp_status_t *out);

/* Same, for a SPECIFIC printer (used to poll the whole fleet for the dashboard).
 * Does not touch the active-storage cache. */
esp_err_t prusalink_get_status_of(const pp_printer_t *pr, pp_status_t *out);

/* One-shot printer identity from GET /api/version: friendly model (derived from
 * the hostname) + firmware version (may be empty on older MINI PrusaLink). */
esp_err_t prusalink_get_info(const pp_printer_t *pr, char *model, size_t ml,
                             char *fw, size_t fl, bool *has_control);

/* Job control. job_id comes from the latest status. */
esp_err_t prusalink_pause(int job_id);   /* PUT  /api/v1/job/{id}/pause   */
esp_err_t prusalink_resume(int job_id);  /* PUT  /api/v1/job/{id}/resume  */
esp_err_t prusalink_stop(int job_id);    /* DEL  /api/v1/job/{id}         */

/* List a folder on the configured storage (root if path is NULL/empty). */
esp_err_t prusalink_list(const char *path, pp_file_t *arr, int max, int *count);

/* Start printing a stored file: POST /api/v1/files/{storage}/{path}. */
esp_err_t prusalink_print(const char *path);

/* Send raw G-code (OctoPrint-style /api/printer/command). */
esp_err_t prusalink_gcode(const char *gcode);
