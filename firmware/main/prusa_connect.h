#pragma once
/* Prusa-Touch — Prusa Connect cloud backend.
 *
 * Implements the reverse-engineered Prusa Connect API using OAuth2 + PKCE.
 * Allows managing multiple printers through a single Prusa Account.
 */
#include "esp_err.h"
#include "cJSON.h"
#include "pandaprusa.h"

typedef enum {
    PP_CONNECT_IDLE,
    PP_CONNECT_NEED_TOTP,
    PP_CONNECT_AUTH_OK,
    PP_CONNECT_AUTH_FAILED,
    PP_CONNECT_ERROR
} pp_connect_status_t;

/* Start the OAuth2 login flow. Scrapes CSRF/Next and submits credentials. */
pp_connect_status_t prusa_connect_login(const char *email, const char *password);

/* Second leg of login if 2FA is required. */
pp_connect_status_t prusa_connect_submit_totp(const char *code);

/* Check if we have a valid (or refreshable) session. */
bool prusa_connect_is_authenticated(void);

/* Forget the account: wipe tokens + default team from RAM and NVS. */
void prusa_connect_logout(void);

/* Feasibility probe against the farm GraphQL API with our device token. */
void prusa_connect_farm_probe(char *out, int outlen);

/* Control-path probe: sends {command:<cmd>,args:[]} to a printer via commands/sync
 * (use only no-op/safe commands on idle printers) to discover valid command names. */
void prusa_connect_ctrl_probe(const char *uuid, const char *cmd, char *out, int outlen);

/* Farm: org-scoped data via the GraphQL API. Each returns the malloc'd response
 * body (caller frees) or NULL on failure. Pass NULL/"" to use the stored org. */
char *prusa_connect_get_farm_stats(const char *org);
char *prusa_connect_get_orders(const char *org);
char *prusa_connect_graphql_raw(const char *body);   /* arbitrary GraphQL (schema probe) */
/* One-time per-printer fetch of LAN IP + PrusaLink API key (for the local fallback). */
esp_err_t prusa_connect_get_printer_net(const char *uuid, char *ip, int iplen, char *key, int keylen);
char *prusa_connect_get_printer_raw(const char *uuid);   /* raw per-printer JSON (debug; caller frees) */

/* Persisted Farm organization UUID (settable from the web UI; used by the touch). */
void prusa_connect_set_org(const char *org);
const char *prusa_connect_get_org(void);

/* Ensure the access token is fresh (refreshes if close to expiry). */
esp_err_t prusa_connect_refresh_token(void);

/* Fetch the fleet dashboard from Connect: GET /app/printers.
 * Populates out_arr with up to max_printers. */
esp_err_t prusa_connect_get_fleet(pp_status_t *out_arr, int max_printers, int *out_count);

/* Printer control via Connect (UUID is stored in pp_printer_t.host for Connect backends). */
esp_err_t prusa_connect_pause(const char *uuid);
esp_err_t prusa_connect_resume(const char *uuid);
esp_err_t prusa_connect_stop(const char *uuid);
esp_err_t prusa_connect_gcode(const char *uuid, const char *gcode);

/* Fetch a webcam snapshot JPEG for a cloud printer. On ESP_OK, *out is a malloc'd
 * buffer of *out_len bytes (caller frees). ESP_FAIL if the printer has no camera. */
esp_err_t prusa_connect_fetch_snapshot(const char *uuid, uint8_t **out, int *out_len);

/* Dedicated control commands for modern Connect printers (kwargs envelope). Used for
 * cloud backends; Klipper/Moonraker keep raw gcode. Temps in °C; move distances in mm
 * (relative); feedrate mm/min. */
esp_err_t prusa_connect_set_nozzle_temp(const char *uuid, int temp_c);
esp_err_t prusa_connect_set_bed_temp(const char *uuid, int temp_c);
esp_err_t prusa_connect_home(const char *uuid, const char *axes);
esp_err_t prusa_connect_move(const char *uuid, int feedrate, float x, float y);
esp_err_t prusa_connect_move_z(const char *uuid, int feedrate, float distance);

/* Fetch teams/organizations for Farm Mode. */
esp_err_t prusa_connect_get_teams(cJSON **out_json);

/* Fetch printers for a specific team. */
esp_err_t prusa_connect_get_team_printers(const char *team_id, pp_status_t *out_arr, int max, int *out_count);

/* Default Team ID management. */
void prusa_connect_set_default_team(const char *team_id);
const char* prusa_connect_get_default_team(void);

/* Load/Save tokens from NVS. Called by app_state. */
void prusa_connect_init(void);
