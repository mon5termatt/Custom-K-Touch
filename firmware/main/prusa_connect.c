/* Prusa-Touch — Prusa Connect (OAuth2 + API). */
#include "prusa_connect.h"
#include "printer_store.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "esp_random.h"

static const char *TAG = "connect";

#define PRUSA_AUTH_URL     "https://account.prusa3d.com/o/authorize/"
#define PRUSA_TOKEN_URL    "https://account.prusa3d.com/o/token/"
#define PRUSA_LOGIN_URL    "https://account.prusa3d.com/login/"
#define PRUSA_ACCOUNT_URL  "https://account.prusa3d.com"
#define PRUSA_CLIENT_ID    "MRHTlZhZqkNrrQ6FUPtjyusAz8nc59ErHXP8XkS4"
#define PRUSA_REDIRECT_URI "https://connect.prusa3d.com/login/auth-callback"
#define PRUSA_MOBILE_API   "https://connect-mobile-api.prusa3d.com"
#define PRUSA_USER_AGENT   "PrusaTouch/0.3.3"

/* NVS keys */
#define NS                 "pp"
#define KEY_AT             "conn_at"   /* access token  */
#define KEY_RT             "conn_rt"   /* refresh token */

/* Internal state */
static char s_at[2048];
static char s_rt[256];
static char s_pkce_verifier[128];
static char s_pkce_challenge[128];
static char s_cookies[2048];
static char s_csrf[128];
static char s_next[256];
static char s_totp_url[128];

/* ---- response accumulator (heap) ---- */
typedef struct {
    char  *buf;
    int    len;
    int    cap;
} resp_t;

static esp_err_t http_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    resp_t *r = (resp_t *)e->user_data;
    if (!r) return ESP_OK;
    int need = r->len + e->data_len + 1;
    if (need > 512 * 1024) return ESP_ERR_NO_MEM;
    if (need > r->cap) {
        int nc = r->cap ? r->cap : 2048;
        while (nc < need) nc *= 2;
        char *nb = realloc(r->buf, nc);
        if (!nb) return ESP_ERR_NO_MEM;
        r->buf = nb; r->cap = nc;
    }
    memcpy(r->buf + r->len, e->data, e->data_len);
    r->len += e->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

/* ---- helpers ---- */

static void base64url_encode(const uint8_t *src, size_t slen, char *dst, size_t dlen)
{
    size_t out_len = 0;
    mbedtls_base64_encode((unsigned char *)dst, dlen, &out_len, src, slen);
    /* URL-safe mapping: + -> -, / -> _ */
    for (char *p = dst; *p; p++) {
        if (*p == '+') *p = '-';
        else if (*p == '/') *p = '_';
        else if (*p == '=') { *p = '\0'; break; }   /* no padding */
    }
}

static void generate_pkce(void)
{
    uint8_t rnd[32];
    esp_fill_random(rnd, 32);
    base64url_encode(rnd, 32, s_pkce_verifier, sizeof(s_pkce_verifier));

    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)s_pkce_verifier, strlen(s_pkce_verifier), hash, 0);
    base64url_encode(hash, 32, s_pkce_challenge, sizeof(s_pkce_challenge));
}

static char *url_encode(const char *in)
{
    if (!in) return NULL;
    char *out = malloc(strlen(in) * 3 + 1);
    if (!out) return NULL;
    char *p = out;
    while (*in) {
        if (isalnum((int)*in) || *in == '-' || *in == '_' || *in == '.' || *in == '~') {
            *p++ = *in;
        } else {
            p += sprintf(p, "%%%02X", (unsigned char)*in);
        }
        in++;
    }
    *p = '\0';
    return out;
}

static char *extract_field(const char *html, const char *name)
{
    char search[64];
    snprintf(search, sizeof(search), "name=\"%s\"", name);
    const char *p = strstr(html, search);
    if (!p) return NULL;
    p = strstr(p, "value=\"");
    if (!p) return NULL;
    p += 7;
    const char *end = strchr(p, '"');
    if (!end) return NULL;
    return strndup(p, end - p);
}

static void merge_cookies(esp_http_client_handle_t c)
{
    /* esp_http_client_get_header only returns the LAST instance of a header.
     * To get all Set-Cookie, we'd need to use the event handler or a custom parser.
     * Since we only care about sessionid and csrftoken, and they usually come in separate
     * redirects, we'll just append what we find. */
    char *hdr = NULL;
    esp_http_client_get_header(c, "Set-Cookie", &hdr);
    if (hdr && hdr[0]) {
        int semi = 0;
        char *p = strchr(hdr, ';');
        if (p) semi = p - hdr;
        else semi = strlen(hdr);
        
        if (s_cookies[0]) strlcat(s_cookies, "; ", sizeof(s_cookies));
        strncat(s_cookies, hdr, semi);
    }
}

/* -------------------------------------------------------------------------- */

typedef struct {
    int code;
    char *body;
    char location[256];
} http_resp_t;

static http_resp_t do_http(const char *method, const char *url, const char *ct, const char *body, bool with_auth)
{
    http_resp_t r = {0};
    resp_t acc = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = (!strcmp(method, "POST")) ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = PRUSA_USER_AGENT,
        .timeout_ms = 15000,
        .event_handler = http_event,
        .user_data = &acc,
        .buffer_size_tx = 4096,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return r;

    if (s_cookies[0]) esp_http_client_set_header(client, "Cookie", s_cookies);
    if (with_auth && s_at[0]) {
        char hdr[2100]; snprintf(hdr, sizeof(hdr), "Bearer %s", s_at);
        esp_http_client_set_header(client, "Authorization", hdr);
    }
    if (ct) esp_http_client_set_header(client, "Content-Type", ct);
    esp_http_client_set_header(client, "Referer", PRUSA_ACCOUNT_URL "/login/");

    if (body) esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        r.code = esp_http_client_get_status_code(client);
        r.body = acc.buf;
        char *loc = NULL;
        esp_http_client_get_header(client, "Location", &loc);
        if (loc) strlcpy(r.location, loc, sizeof(r.location));

        /* Scrape cookies - multiple Set-Cookie headers require manual parsing or a hack.
         * The ESP-IDF client only returns the last one via get_header.
         * We'll need to use the event loop for this to be robust. */
    } else {
        free(acc.buf);
    }
    esp_http_client_cleanup(client);
    return r;
}

/* Ported followRedirects */
static http_resp_t follow_redirects(http_resp_t r, int max)
{
    while (max-- > 0 && (r.code >= 301 && r.code <= 307)) {
        if (!r.location[0]) break;
        char next_url[512];
        if (strncmp(r.location, "http", 4) != 0) {
            snprintf(next_url, sizeof(next_url), "%s%s", PRUSA_ACCOUNT_URL, r.location);
        } else {
            strlcpy(next_url, r.location, sizeof(next_url));
        }
        if (strstr(next_url, "auth-callback") && strstr(next_url, "code=")) return r;
        
        free(r.body);
        r = do_http("GET", next_url, NULL, NULL, false);
    }
    return r;
}

/* -------------------------------------------------------------------------- */

static pp_connect_status_t try_exchange_code(http_resp_t r)
{
    const char *code_ptr = strstr(r.location, "code=");
    if (!code_ptr && r.body) code_ptr = strstr(r.body, "code=");
    if (!code_ptr) return PP_CONNECT_ERROR;
    
    char code[128];
    strlcpy(code, code_ptr + 5, sizeof(code));
    char *amp = strchr(code, '&'); if (amp) *amp = '\0';
    
    char *enc_uri = url_encode(PRUSA_REDIRECT_URI);
    char body[1024];
    snprintf(body, sizeof(body), 
             "grant_type=authorization_code&client_id=%s&code=%s&code_verifier=%s&redirect_uri=%s",
             PRUSA_CLIENT_ID, code, s_pkce_verifier, enc_uri);
    free(enc_uri);

    http_resp_t tr = do_http("POST", PRUSA_TOKEN_URL, "application/x-www-form-urlencoded", body, false);
    if (tr.code == 200 && tr.body) {
        cJSON *j = cJSON_Parse(tr.body);
        if (j) {
            cJSON *at = cJSON_GetObjectItem(j, "access_token");
            cJSON *rt = cJSON_GetObjectItem(j, "refresh_token");
            if (cJSON_IsString(at)) strlcpy(s_at, at->valuestring, sizeof(s_at));
            if (cJSON_IsString(rt)) strlcpy(s_rt, rt->valuestring, sizeof(s_rt));
            
            nvs_handle_t h;
            if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_str(h, KEY_AT, s_at);
                nvs_set_str(h, KEY_RT, s_rt);
                nvs_commit(h); nvs_close(h);
            }
            cJSON_Delete(j);
            free(tr.body);
            return PP_CONNECT_AUTH_OK;
        }
    }
    free(tr.body);
    return PP_CONNECT_AUTH_FAILED;
}

pp_connect_status_t prusa_connect_login(const char *email, const char *password)
{
    s_cookies[0] = s_at[0] = s_rt[0] = '\0';
    generate_pkce();
    
    char *enc_uri = url_encode(PRUSA_REDIRECT_URI);
    char url[512];
    snprintf(url, sizeof(url), "%s?response_type=code&client_id=%s&redirect_uri=%s&code_challenge_method=S256&code_challenge=%s",
             PRUSA_AUTH_URL, PRUSA_CLIENT_ID, enc_uri, s_pkce_challenge);
    free(enc_uri);

    http_resp_t r = follow_redirects(do_http("GET", url, NULL, NULL, false), 10);
    if (!r.body) return PP_CONNECT_ERROR;

    char *csrf = extract_field(r.body, "csrfmiddlewaretoken");
    char *next = extract_field(r.body, "next");
    if (csrf) strlcpy(s_csrf, csrf, sizeof(s_csrf));
    if (next) strlcpy(s_next, next, sizeof(s_next));
    free(csrf); free(next);

    if (!s_csrf[0]) { free(r.body); return PP_CONNECT_ERROR; }

    char *e_email = url_encode(email);
    char *e_pass = url_encode(password);
    char *e_next = url_encode(s_next);
    char lbody[1024];
    snprintf(lbody, sizeof(lbody), "csrfmiddlewaretoken=%s&next=%s&email=%s&password=%s",
             s_csrf, e_next, e_email, e_pass);
    free(e_email); free(e_pass); free(e_next);

    free(r.body);
    r = follow_redirects(do_http("POST", PRUSA_ACCOUNT_URL "/login/", "application/x-www-form-urlencoded", lbody, false), 10);

    if (r.body && (strstr(r.body, "/login/totp/") || strstr(r.location, "/login/totp/"))) {
        char *c2 = extract_field(r.body, "csrfmiddlewaretoken");
        if (c2) strlcpy(s_csrf, c2, sizeof(s_csrf));
        free(c2);
        strlcpy(s_totp_url, PRUSA_ACCOUNT_URL "/login/totp/", sizeof(s_totp_url));
        free(r.body);
        return PP_CONNECT_NEED_TOTP;
    }

    if (r.body && strstr(r.body, "invalid-feedback")) { free(r.body); return PP_CONNECT_AUTH_FAILED; }

    pp_connect_status_t res = try_exchange_code(r);
    free(r.body);
    return res;
}

pp_connect_status_t prusa_connect_submit_totp(const char *code)
{
    char *e_next = url_encode(s_next);
    char body[512];
    snprintf(body, sizeof(body), "csrfmiddlewaretoken=%s&next=%s&otp_token=%s",
             s_csrf, e_next, code);
    free(e_next);

    http_resp_t r = follow_redirects(do_http("POST", s_totp_url, "application/x-www-form-urlencoded", body, false), 10);
    pp_connect_status_t res = try_exchange_code(r);
    free(r.body);
    return res;
}

bool prusa_connect_is_authenticated(void) { return s_at[0] != '\0' || s_rt[0] != '\0'; }

esp_err_t prusa_connect_refresh_token(void)
{
    if (!s_rt[0]) return ESP_FAIL;
    char body[512];
    snprintf(body, sizeof(body), "grant_type=refresh_token&client_id=%s&refresh_token=%s",
             PRUSA_CLIENT_ID, s_rt);
    
    http_resp_t r = do_http("POST", PRUSA_TOKEN_URL, "application/x-www-form-urlencoded", body, false);
    if (r.code == 200 && r.body) {
        cJSON *j = cJSON_Parse(r.body);
        if (j) {
            cJSON *at = cJSON_GetObjectItem(j, "access_token");
            cJSON *rt = cJSON_GetObjectItem(j, "refresh_token");
            if (cJSON_IsString(at)) strlcpy(s_at, at->valuestring, sizeof(s_at));
            if (cJSON_IsString(rt)) strlcpy(s_rt, rt->valuestring, sizeof(s_rt));
            
            nvs_handle_t h;
            if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_str(h, KEY_AT, s_at);
                nvs_set_str(h, KEY_RT, s_rt);
                nvs_commit(h); nvs_close(h);
            }
            cJSON_Delete(j);
            free(r.body);
            return ESP_OK;
        }
    }
    free(r.body);
    return ESP_FAIL;
}

/* -------------------------------------------------------------------------- */

static float jnum(const cJSON *o, const char *k, float def)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(n) ? (float)n->valuedouble : def;
}

esp_err_t prusa_connect_get_fleet(pp_status_t *arr, int max, int *count)
{
    *count = 0;
    http_resp_t r = do_http("GET", "https://connect.prusa3d.com/app/printers?limit=64&offset=0", NULL, NULL, true);
    if (r.code == 401 && prusa_connect_refresh_token() == ESP_OK) {
        free(r.body);
        r = do_http("GET", "https://connect.prusa3d.com/app/printers?limit=64&offset=0", NULL, NULL, true);
    }
    if (r.code != 200 || !r.body) { free(r.body); return ESP_FAIL; }

    cJSON *root = cJSON_Parse(r.body);
    free(r.body);
    if (!root) return ESP_FAIL;

    cJSON *printers = cJSON_GetObjectItemCaseSensitive(root, "printers");
    cJSON *p = NULL;
    cJSON_ArrayForEach(p, printers) {
        if (*count >= max) break;
        pp_status_t *s = &arr[*count];
        memset(s, 0, sizeof(*s));
        
        cJSON *uuid = cJSON_GetObjectItemCaseSensitive(p, "uuid");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(p, "name");
        cJSON *type = cJSON_GetObjectItemCaseSensitive(p, "printer_type");
        cJSON *fw = cJSON_GetObjectItemCaseSensitive(p, "firmware");
        cJSON *st = cJSON_GetObjectItemCaseSensitive(p, "printer_state");
        cJSON *conn = cJSON_GetObjectItemCaseSensitive(p, "connection_state");

        if (cJSON_IsString(uuid)) strlcpy(s->uuid, uuid->valuestring, sizeof(s->uuid));
        if (cJSON_IsString(name)) strlcpy(s->printer_name, name->valuestring, sizeof(s->printer_name));
        if (cJSON_IsString(type)) strlcpy(s->model, type->valuestring, sizeof(s->model));
        if (cJSON_IsString(fw)) strlcpy(s->firmware, fw->valuestring, sizeof(s->firmware));
        if (cJSON_IsString(st)) strlcpy(s->state, st->valuestring, sizeof(s->state));
        s->online = (cJSON_IsString(conn) && strcmp(conn->valuestring, "OFFLINE") != 0);

        cJSON *tel = cJSON_GetObjectItemCaseSensitive(p, "telemetry");
        if (tel) {
            s->temp_nozzle = jnum(tel, "temp_nozzle", 0);
            s->target_nozzle = jnum(tel, "temp_nozzle_target", 0);
            s->temp_bed = jnum(tel, "temp_bed", 0);
            s->target_bed = jnum(tel, "temp_bed_target", 0);
            s->speed = (int)jnum(tel, "speed", 0);
        }

        cJSON *job = cJSON_GetObjectItemCaseSensitive(p, "job_info");
        if (cJSON_IsObject(job)) {
            s->has_job = true;
            s->progress = jnum(job, "progress", 0);
            s->time_remaining = (int)jnum(job, "time_remaining", -1);
            cJSON *file = cJSON_GetObjectItemCaseSensitive(job, "file");
            if (file) {
                cJSON *dn = cJSON_GetObjectItemCaseSensitive(file, "display_name");
                if (cJSON_IsString(dn)) strlcpy(s->job_name, dn->valuestring, sizeof(s->job_name));
            }
        }
        (*count)++;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t connect_post(const char *uuid, const char *action)
{
    char url[256]; snprintf(url, sizeof(url), "https://connect.prusa3d.com/app/printers/%s/%s", uuid, action);
    http_resp_t r = do_http("POST", url, "application/json", "{}", true);
    free(r.body);
    return (r.code >= 200 && r.code < 300) ? ESP_OK : ESP_FAIL;
}

esp_err_t prusa_connect_pause(const char *uuid)  { return connect_post(uuid, "pause"); }
esp_err_t prusa_connect_resume(const char *uuid) { return connect_post(uuid, "resume"); }
esp_err_t prusa_connect_stop(const char *uuid)   { return connect_post(uuid, "cancel"); }

esp_err_t prusa_connect_gcode(const char *uuid, const char *gcode)
{
    char url[256]; snprintf(url, sizeof(url), "https://connect.prusa3d.com/app/printers/%s/gcode", uuid);
    char body[256]; snprintf(body, sizeof(body), "{\"gcode\":\"%s\"}", gcode);
    http_resp_t r = do_http("POST", url, "application/json", body, true);
    free(r.body);
    return (r.code >= 200 && r.code < 300) ? ESP_OK : ESP_FAIL;
}

void prusa_connect_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_at); nvs_get_str(h, KEY_AT, s_at, &sz);
        sz = sizeof(s_rt); nvs_get_str(h, KEY_RT, s_rt, &sz);
        nvs_close(h);
    }
}
