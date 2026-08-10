#pragma once
/* Prusa-Touch — Bambu Lab LAN backend.
 *
 * Talks MQTT-over-TLS straight to a Bambu printer's on-board broker (port 8883), the same way
 * the BigTreeTech Panda Touch did. Clean-room from the public protocol (OpenBambuAPI /
 * ha-bambulab) — no Bambu/BTT code. Requires the printer in LAN Mode with Developer Mode on.
 *
 * Config model (pp_printer_t): host = "bambu:<ip>", api_key = <LAN access code>, uuid = <serial>.
 *
 * The poll loop is round-robin (one printer per cycle), so each status fetch is a short
 * connect -> pushall -> read -> disconnect; control is connect -> publish -> disconnect. One
 * TLS-MQTT session at a time, matching how the Connect fleet poll already does TLS per cycle.
 *
 * The mqtt session helpers are parameterized by broker URI + credentials so the planned Bambu
 * Cloud backend can reuse the same report parser and command payloads. */
#include "pandaprusa.h"
#include "esp_err.h"

bool      bambu_is(const pp_printer_t *pr);              /* host starts with "bambu:" */
esp_err_t bambu_get_status_of(const pp_printer_t *pr, pp_status_t *out);
esp_err_t bambu_pause(const pp_printer_t *pr);
esp_err_t bambu_resume(const pp_printer_t *pr);
esp_err_t bambu_stop(const pp_printer_t *pr);
esp_err_t bambu_gcode(const pp_printer_t *pr, const char *gcode);   /* raw M/G-code via gcode_line */

/* One JPEG frame from the P1/A1 LAN camera (TLS TCP :6000). Caller frees *out.
 * X1 RTSPS is not supported here. */
esp_err_t bambu_fetch_snapshot(const pp_printer_t *pr, uint8_t **out, int *out_len);
