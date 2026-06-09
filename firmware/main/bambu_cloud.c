/* Prusa-Touch — Bambu Lab cloud account layer (ALPHA). See bambu_cloud.h. */
#include "bambu_cloud.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "bambu_cloud";

#define API_BASE "https://api.bambulab.com"
#define NS       "bambu"
#define KEY_TOK  "tok"
#define KEY_UID  "uid"
/* Looking like the official client materially helps get past Cloudflare (per pybambu). */
#define BC_UA    "bambu_network_agent/01.09.05.01"

static char s_token[2300];   /* JWT access token */
static char s_uid[24];       /* numeric user id  */
static char s_user[28];      /* "u_<uid>" cached for MQTT */

bool        bambu_cloud_is_authed(void) { return s_token[0] != '\0' && s_uid[0] != '\0'; }
const char *bambu_cloud_token(void)     { return s_token; }
const char *bambu_cloud_mqtt_user(void) { return s_user; }

static void cache_user(void) { if (s_uid[0]) snprintf(s_user, sizeof(s_user), "u_%s", s_uid); else s_user[0] = '\0'; }

/* ---------- HTTP (TLS via the cert bundle; api.bambulab.com uses a public CA) ---------- */

typedef struct { char *buf; int len; int cap; } acc_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        acc_t *a = e->user_data;
        if (a->len + e->data_len + 1 > a->cap) {
            int nc = a->cap ? a->cap * 2 : 2048;
            while (nc < a->len + e->data_len + 1) nc *= 2;
            char *nb = realloc(a->buf, nc);
            if (!nb) return ESP_OK;   /* drop extra data rather than crash */
            a->buf = nb; a->cap = nc;
        }
        memcpy(a->buf + a->len, e->data, e->data_len);
        a->len += e->data_len;
        a->buf[a->len] = '\0';
    }
    return ESP_OK;
}

/* Returns the malloc'd response body (caller frees) and the HTTP status via *status. NULL on error. */
static char *cloud_http(esp_http_client_method_t m, const char *url, const char *bearer,
                        const char *json_body, int *status)
{
    acc_t acc = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = m,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = BC_UA,
        .timeout_ms = 15000,
        .event_handler = on_http,
        .user_data = &acc,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { if (acc.buf) free(acc.buf); return NULL; }
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "X-BBL-Client-Name", "OrcaSlicer");
    esp_http_client_set_header(c, "X-BBL-Client-Type", "slicer");
    if (bearer && bearer[0]) {
        char h[2360]; snprintf(h, sizeof(h), "Bearer %s", bearer);
        esp_http_client_set_header(c, "Authorization", h);
    }
    if (json_body) esp_http_client_set_post_field(c, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (status) *status = st;
    if (err != ESP_OK) { if (acc.buf) free(acc.buf); return NULL; }
    return acc.buf;   /* may be NULL if the body was empty */
}

/* ---------- token + uid persistence ---------- */

static void save_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (s_token[0]) nvs_set_str(h, KEY_TOK, s_token); else nvs_erase_key(h, KEY_TOK);
    if (s_uid[0])   nvs_set_str(h, KEY_UID, s_uid);   else nvs_erase_key(h, KEY_UID);
    nvs_commit(h);
    nvs_close(h);
}

void bambu_cloud_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(s_token); nvs_get_str(h, KEY_TOK, s_token, &n);
    n = sizeof(s_uid); nvs_get_str(h, KEY_UID, s_uid, &n);
    nvs_close(h);
    cache_user();
    if (bambu_cloud_is_authed()) ESP_LOGI(TAG, "loaded saved Bambu cloud token (uid %s)", s_uid);
}

/* GET the account's numeric uid (the JWT no longer carries it reliably). */
static bool derive_uid(void)
{
    int st = 0;
    char *body = cloud_http(HTTP_METHOD_GET, API_BASE "/v1/design-user-service/my/preference", s_token, NULL, &st);
    bool ok = false;
    if (body && st == 200) {
        cJSON *j = cJSON_Parse(body);
        cJSON *uid = j ? cJSON_GetObjectItem(j, "uid") : NULL;
        if (cJSON_IsNumber(uid)) { snprintf(s_uid, sizeof(s_uid), "%lld", (long long)uid->valuedouble); ok = true; }
        else if (cJSON_IsString(uid) && uid->valuestring[0]) { strlcpy(s_uid, uid->valuestring, sizeof(s_uid)); ok = true; }
        if (j) cJSON_Delete(j);
    }
    if (body) free(body);
    cache_user();
    return ok;
}

/* Store a token (from login or import), derive the uid, persist. */
static esp_err_t accept_token(const char *token)
{
    strlcpy(s_token, token, sizeof(s_token));
    if (!derive_uid()) { s_token[0] = '\0'; s_uid[0] = '\0'; cache_user(); return ESP_FAIL; }
    save_creds();
    return ESP_OK;
}

esp_err_t bambu_cloud_set_token(const char *token)
{
    if (!token || !token[0]) return ESP_FAIL;
    return accept_token(token);
}

void bambu_cloud_logout(void)
{
    s_token[0] = s_uid[0] = s_user[0] = '\0';
    save_creds();
}

/* ---------- login ---------- */

/* Parse a /user/login response: token -> accept; "verifyCode" -> NEED_CODE; else FAIL. */
static bc_status_t handle_login_resp(char *body)
{
    if (!body) return BC_FAIL;
    bc_status_t r = BC_FAIL;
    cJSON *j = cJSON_Parse(body);
    if (j) {
        cJSON *tok = cJSON_GetObjectItem(j, "accessToken");
        cJSON *lt  = cJSON_GetObjectItem(j, "loginType");
        if (cJSON_IsString(tok) && tok->valuestring[0]) {
            r = (accept_token(tok->valuestring) == ESP_OK) ? BC_OK : BC_FAIL;
        } else if (cJSON_IsString(lt) && strcmp(lt->valuestring, "verifyCode") == 0) {
            r = BC_NEED_CODE;
        }
        cJSON_Delete(j);
    }
    return r;
}

bc_status_t bambu_cloud_login(const char *email, const char *password)
{
    if (!email || !password) return BC_FAIL;
    /* cJSON-build so the fields are escaped safely */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "account", email);
    cJSON_AddStringToObject(o, "password", password);
    cJSON_AddStringToObject(o, "apiError", "");
    char *bs = cJSON_PrintUnformatted(o); cJSON_Delete(o);
    if (!bs) return BC_FAIL;

    int st = 0;
    char *resp = cloud_http(HTTP_METHOD_POST, API_BASE "/v1/user-service/user/login", NULL, bs, &st);
    free(bs);
    bc_status_t r = handle_login_resp(resp);
    if (resp) free(resp);

    if (r == BC_NEED_CODE) {
        /* Ask Bambu to email the login code. */
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "email", email);
        cJSON_AddStringToObject(c, "type", "codeLogin");
        char *cs = cJSON_PrintUnformatted(c); cJSON_Delete(c);
        if (cs) {
            int s2 = 0;
            char *r2 = cloud_http(HTTP_METHOD_POST, API_BASE "/v1/user-service/user/sendemail/code", NULL, cs, &s2);
            if (r2) free(r2);
            free(cs);
        }
    }
    return r;
}

bc_status_t bambu_cloud_submit_code(const char *email, const char *code)
{
    if (!email || !code) return BC_FAIL;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "account", email);
    cJSON_AddStringToObject(o, "code", code);
    char *bs = cJSON_PrintUnformatted(o); cJSON_Delete(o);
    if (!bs) return BC_FAIL;
    int st = 0;
    char *resp = cloud_http(HTTP_METHOD_POST, API_BASE "/v1/user-service/user/login", NULL, bs, &st);
    free(bs);
    bc_status_t r = handle_login_resp(resp);
    if (resp) free(resp);
    return r;
}

/* ---------- device list ---------- */

int bambu_cloud_list_devices(pp_printer_t *out, int max)
{
    if (!bambu_cloud_is_authed() || !out || max <= 0) return 0;
    int st = 0;
    char *body = cloud_http(HTTP_METHOD_GET, API_BASE "/v1/iot-service/api/user/bind", s_token, NULL, &st);
    int n = 0;
    if (body && st == 200) {
        cJSON *j = cJSON_Parse(body);
        cJSON *devs = j ? cJSON_GetObjectItem(j, "devices") : NULL;
        if (cJSON_IsArray(devs)) {
            cJSON *d;
            cJSON_ArrayForEach(d, devs) {
                if (n >= max) break;
                cJSON *id   = cJSON_GetObjectItem(d, "dev_id");
                cJSON *name = cJSON_GetObjectItem(d, "name");
                cJSON *ac   = cJSON_GetObjectItem(d, "dev_access_code");
                if (!cJSON_IsString(id) || !id->valuestring[0]) continue;
                pp_printer_t *p = &out[n];
                memset(p, 0, sizeof(*p));
                snprintf(p->host, sizeof(p->host), "bambucloud:%s", id->valuestring);
                strlcpy(p->uuid, id->valuestring, sizeof(p->uuid));
                strlcpy(p->name, (cJSON_IsString(name) && name->valuestring[0]) ? name->valuestring : id->valuestring, sizeof(p->name));
                if (cJSON_IsString(ac)) strlcpy(p->api_key, ac->valuestring, sizeof(p->api_key));
                p->port = 8883;
                n++;
            }
        }
        if (j) cJSON_Delete(j);
    }
    if (body) free(body);
    ESP_LOGI(TAG, "cloud device list: %d printer(s)", n);
    return n;
}
