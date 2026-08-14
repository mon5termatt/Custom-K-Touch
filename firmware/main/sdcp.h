#pragma once
/* Chitubox SDCP V3.0.0 resin printer backend (CBD Smart Device Control Protocol).
 *
 * Official spec: CBD SDCP V3.0.0 — WebSocket on :3030, UDP discovery on :3000.
 * Config: host = "sdcp:<ip>", uuid = MainboardID (filled after first discovery).
 *
 * Status (Cmd 0), file list (Cmd 258), start print (Cmd 128),
 * pause (129) / stop (130) / resume (131). */
#include "pandaprusa.h"
#include "esp_err.h"

bool      sdcp_is(const pp_printer_t *pr);   /* host starts with "sdcp:" */
esp_err_t sdcp_get_status_of(const pp_printer_t *pr, pp_status_t *out);
/* Like get_status_of; when refresh_identity is true, UDP-discovers Name/Machine/FW
 * even if MainboardID is already cached (needed after reboot). */
esp_err_t sdcp_get_status_ex(const pp_printer_t *pr, pp_status_t *out, bool refresh_identity);
esp_err_t sdcp_list(const pp_printer_t *pr, pp_file_t *arr, int max, int *count);
esp_err_t sdcp_print(const pp_printer_t *pr, const char *filename);
esp_err_t sdcp_pause(const pp_printer_t *pr);
esp_err_t sdcp_stop(const pp_printer_t *pr);
esp_err_t sdcp_resume(const pp_printer_t *pr);
/* Dismiss the local attention banner (same UX as Connect dialog OK). */
esp_err_t sdcp_dialog_dismiss(int dialog_id);
/* Download a cover/preview image. `ref` is an absolute http URL or a printer-relative path. */
esp_err_t sdcp_fetch_thumb(const pp_printer_t *pr, const char *ref, uint8_t **out, int *out_len);
