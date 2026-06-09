#pragma once
/* Prusa-Touch — Bambu Lab CLOUD account layer (ALPHA).
 *
 * Logs in to a Bambu account (or imports an access token acquired off-device), enumerates the
 * account's printers, and hands MQTT credentials to bambu.c. The cloud MQTT report/request
 * protocol is IDENTICAL to LAN, so bambu.c reuses its parser + commands verbatim; only the broker
 * (us.mqtt.bambulab.com:8883) and auth (username "u_<uid>", password = accessToken) differ.
 *
 * Reality check (why this is Alpha): Bambu's login is fronted by Cloudflare, which an ESP32 can't
 * pass reliably, and the refresh-token endpoint is dead — so token-import is the dependable path
 * and the token must be re-pasted every ~90 days. On-device email/password login is best-effort. */
#include "pandaprusa.h"
#include "esp_err.h"

typedef enum { BC_OK = 0, BC_NEED_CODE, BC_FAIL } bc_status_t;

void        bambu_cloud_init(void);                 /* load saved token/uid from NVS */
bool        bambu_cloud_is_authed(void);
const char *bambu_cloud_mqtt_user(void);            /* "u_<uid>" (or "") for the cloud broker */
const char *bambu_cloud_token(void);                /* accessToken (or "") */

/* Email/password login (best-effort; may be blocked by Cloudflare). Returns BC_NEED_CODE if the
 * account wants an emailed verification code — then call bambu_cloud_submit_code(). */
bc_status_t bambu_cloud_login(const char *email, const char *password);
bc_status_t bambu_cloud_submit_code(const char *email, const char *code);

/* Token-import: paste an accessToken obtained off-device. Derives the uid and persists. */
esp_err_t   bambu_cloud_set_token(const char *token);
void        bambu_cloud_logout(void);

/* Enumerate the account's bound printers into out[] as ready-to-store entries
 * (host="bambucloud:<serial>", uuid=serial, api_key=LAN access code). Returns the count. */
int         bambu_cloud_list_devices(pp_printer_t *out, int max);
