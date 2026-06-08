#pragma once
/* Prusa-Touch — Prusa Connect cloud backend.
 *
 * Implements the reverse-engineered Prusa Connect API using OAuth2 + PKCE.
 * Allows managing multiple printers through a single Prusa Account.
 */
#include "esp_err.h"
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

/* Load/Save tokens from NVS. Called by app_state. */
void prusa_connect_init(void);
