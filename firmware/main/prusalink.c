/* Prusa-Touch — PrusaLink client implementation. */
#include "prusalink.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "sdkconfig.h"

#include "printer_store.h"

static const char *TAG = "prusalink";

/* Active storage name, auto-detected from /api/v1/status (e.g. "usb" or "local").
 * Seeded from Kconfig until the first successful status poll. */
static char s_storage[16];

/* ---- response accumulator (heap) ---- */
typedef struct {
    char  *buf;
    int    len;
    int    cap;
} resp_t;

static esp_err_t http_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    resp_t *r = (resp_t *)e->user_data;
    if (!r) {
        return ESP_OK;
    }
    /* Skip the body of the 401 Digest challenge — only keep the final reply. */
    if (esp_http_client_get_status_code(e->client) == 401) {
        return ESP_OK;
    }
    int need = r->len + e->data_len + 1;
    if (need > 600 * 1024) return ESP_ERR_NO_MEM;   /* bound (camera PNG etc.) */
    if (need > r->cap) {
        int newcap = r->cap ? r->cap : 1024;
        while (newcap < need) newcap *= 2;
        char *nb = realloc(r->buf, newcap);
        if (!nb) {
            return ESP_ERR_NO_MEM;
        }
        r->buf = nb;
        r->cap = newcap;
    }
    memcpy(r->buf + r->len, e->data, e->data_len);
    r->len += e->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

/*
 * Perform one request. method/path required. On success returns ESP_OK and sets
 * *status_code; if resp != NULL, the body is collected into resp->buf (caller
 * frees resp->buf). post_body may be NULL.
 */
static esp_err_t do_request(const pp_printer_t *pr, esp_http_client_method_t method,
                            const char *path, const char *post_body, resp_t *resp,
                            int *status_code)
{
    const bool use_apikey = (pr->api_key[0] != '\0');
    esp_http_client_config_t cfg = {
        .host = pr->host,
        .port = pr->port ? pr->port : 80,
        .path = path,
        .method = method,
        /* API key (X-Api-Key) is preferred; Digest is the fallback. */
        .auth_type = use_apikey ? HTTP_AUTH_TYPE_NONE : HTTP_AUTH_TYPE_DIGEST,
        .username = CONFIG_PP_PRINTER_USER,
        .password = CONFIG_PP_PRINTER_PASSWORD,
        .timeout_ms = 6000,
        .event_handler = http_event,
        .user_data = resp,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return ESP_FAIL;
    }
    if (use_apikey) {
        esp_http_client_set_header(c, "X-Api-Key", pr->api_key);
    }
    if (post_body) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, post_body, strlen(post_body));
    }
    esp_err_t err = esp_http_client_perform(c);
    int sc = esp_http_client_get_status_code(c);
    if (status_code) {
        *status_code = sc;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s -> transport err %s", path, esp_err_to_name(err));
    } else if (sc >= 400) {
        ESP_LOGW(TAG, "%s -> HTTP %d", path, sc);
    }
    esp_http_client_cleanup(c);
    return err;
}

static float json_num(const cJSON *o, const char *key, float def)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(n) ? (float)n->valuedouble : def;
}

esp_err_t prusalink_init(void)
{
    strlcpy(s_storage, CONFIG_PP_STORAGE, sizeof(s_storage));
    ESP_LOGI(TAG, "target http://%s:%d (storage \"%s\")",
             CONFIG_PP_PRINTER_HOST, CONFIG_PP_PRINTER_PORT, s_storage);
    return ESP_OK;
}

static esp_err_t fetch_status(const pp_printer_t *pr, pp_status_t *out, bool update_storage)
{
    memset(out, 0, sizeof(*out));
    out->time_remaining = -1;

    resp_t r = {0};
    int sc = 0;
    esp_err_t err = do_request(pr, HTTP_METHOD_GET, "/api/v1/status", NULL, &r, &sc);
    if (err != ESP_OK || sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) {
        return ESP_FAIL;
    }

    /* Auto-detect the active storage (e.g. "usb"); only for the active printer. */
    if (update_storage) {
        cJSON *storage = cJSON_GetObjectItemCaseSensitive(root, "storage");
        if (storage) {
            cJSON *sn = cJSON_GetObjectItemCaseSensitive(storage, "name");
            if (cJSON_IsString(sn) && sn->valuestring && sn->valuestring[0]) {
                strlcpy(s_storage, sn->valuestring, sizeof(s_storage));
            }
        }
    }

    cJSON *printer = cJSON_GetObjectItemCaseSensitive(root, "printer");
    if (printer) {
        cJSON *st = cJSON_GetObjectItemCaseSensitive(printer, "state");
        if (cJSON_IsString(st) && st->valuestring) {
            strlcpy(out->state, st->valuestring, sizeof(out->state));
        }
        out->temp_nozzle   = json_num(printer, "temp_nozzle", 0);
        out->target_nozzle = json_num(printer, "target_nozzle", 0);
        out->temp_bed      = json_num(printer, "temp_bed", 0);
        out->target_bed    = json_num(printer, "target_bed", 0);
        out->speed         = (int)json_num(printer, "speed", 0);
        out->axis_z        = json_num(printer, "axis_z", 0);
    }

    cJSON *job = cJSON_GetObjectItemCaseSensitive(root, "job");
    if (cJSON_IsObject(job)) {
        out->has_job = true;
        out->job_id = (int)json_num(job, "id", 0);
        out->progress = json_num(job, "progress", 0);
        out->time_remaining = (int)json_num(job, "time_remaining", -1);
        out->time_printing = (int)json_num(job, "time_printing", 0);
    }
    cJSON_Delete(root);

    /* The status job block has no file name; fetch /api/v1/job for display_name. */
    if (out->has_job) {
        resp_t jr = {0};
        int jsc = 0;
        if (do_request(pr, HTTP_METHOD_GET, "/api/v1/job", NULL, &jr, &jsc) == ESP_OK &&
            jsc == 200 && jr.buf) {
            cJSON *jroot = cJSON_Parse(jr.buf);
            if (jroot) {
                cJSON *file = cJSON_GetObjectItemCaseSensitive(jroot, "file");
                if (file) {
                    cJSON *dn = cJSON_GetObjectItemCaseSensitive(file, "display_name");
                    if (!cJSON_IsString(dn)) {
                        dn = cJSON_GetObjectItemCaseSensitive(file, "name");
                    }
                    if (cJSON_IsString(dn) && dn->valuestring) {
                        strlcpy(out->job_name, dn->valuestring, sizeof(out->job_name));
                    }

                    cJSON *refs = cJSON_GetObjectItemCaseSensitive(file, "refs");
                    if (refs) {
                        cJSON *tn = cJSON_GetObjectItemCaseSensitive(refs, "thumbnail");
                        if (cJSON_IsString(tn) && tn->valuestring) {
                            strlcpy(out->job_thumb, tn->valuestring, sizeof(out->job_thumb));
                        }
                    }
                }
                cJSON_Delete(jroot);
            }
        }
        free(jr.buf);
    }

    out->online = true;
    return ESP_OK;
}

esp_err_t prusalink_get_status(pp_status_t *out)
{
    pp_printer_t pr;
    if (!printer_store_active_get(&pr)) { memset(out, 0, sizeof(*out)); return ESP_FAIL; }
    return fetch_status(&pr, out, true);
}

esp_err_t prusalink_get_status_of(const pp_printer_t *pr, pp_status_t *out)
{
    return fetch_status(pr, out, false);
}

/* Map a (default) PrusaLink hostname to a friendly Connect-style model name. */
static void model_from_hostname(const char *host, char *out, size_t n)
{
    char lo[64];
    size_t i = 0;
    for (; host && host[i] && i + 1 < sizeof(lo); i++) {
        char c = host[i];
        lo[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    lo[i] = '\0';

    const char *m;
    if (strstr(lo, "core-one-l") || strstr(lo, "coreonel"))      m = "Original Prusa CORE One L";
    else if (strstr(lo, "core-one") || strstr(lo, "coreone"))    m = "Original Prusa CORE One";
    else if (strstr(lo, "mk4s"))                                  m = "Original Prusa MK4S";
    else if (strstr(lo, "mk4"))                                   m = "Original Prusa MK4";
    else if (strstr(lo, "mk3.5") || strstr(lo, "mk35"))           m = "Original Prusa MK3.5";
    else if (strstr(lo, "mk3"))                                   m = "Original Prusa MK3";
    else if (strstr(lo, "mini"))                                  m = "Original Prusa MINI";
    else if (strstr(lo, "xl"))                                    m = "Original Prusa XL";
    else m = (host && host[0]) ? host : "Prusa printer";
    strlcpy(out, m, n);
}

esp_err_t prusalink_get_info(const pp_printer_t *pr, char *model, size_t ml,
                             char *fw, size_t fl, char *uuid, size_t ul, bool *has_control)
{
    if (model && ml) model[0] = '\0';
    if (fw && fl) fw[0] = '\0';
    if (uuid && ul) uuid[0] = '\0';
    if (has_control) *has_control = false;

    /* 1. Get model/firmware via /api/version */
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/api/version", NULL, &r, &sc) == ESP_OK &&
        sc == 200 && r.buf) {
        cJSON *root = cJSON_Parse(r.buf);
        if (root) {
            cJSON *hn = cJSON_GetObjectItemCaseSensitive(root, "hostname");
            if (model && ml) {
                model_from_hostname(cJSON_IsString(hn) ? hn->valuestring : "", model, ml);
            }
            cJSON *f = cJSON_GetObjectItemCaseSensitive(root, "firmware");
            if (fw && fl && cJSON_IsString(f) && f->valuestring) {
                strlcpy(fw, f->valuestring, fl);
            }
            cJSON_Delete(root);
        }
    }
    free(r.buf);

    /* 2. Get UUID/fingerprint via /api/v1/info */
    r.buf = NULL; r.len = r.cap = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/api/v1/info", NULL, &r, &sc) == ESP_OK &&
        sc == 200 && r.buf) {
        cJSON *root = cJSON_Parse(r.buf);
        if (root) {
            cJSON *fp = cJSON_GetObjectItemCaseSensitive(root, "fingerprint");
            if (!cJSON_IsString(fp)) fp = cJSON_GetObjectItemCaseSensitive(root, "uuid");
            if (uuid && ul && cJSON_IsString(fp) && fp->valuestring) {
                strlcpy(uuid, fp->valuestring, ul);
            }
            cJSON_Delete(root);
        }
    }
    free(r.buf);

    return ESP_OK;
}

static esp_err_t job_action(esp_http_client_method_t m, int job_id, const char *suffix)
{
    pp_printer_t pr;
    if (!printer_store_active_get(&pr)) return ESP_FAIL;
    char path[64];
    if (suffix) {
        snprintf(path, sizeof(path), "/api/v1/job/%d/%s", job_id, suffix);
    } else {
        snprintf(path, sizeof(path), "/api/v1/job/%d", job_id);
    }
    int sc = 0;
    esp_err_t err = do_request(&pr, m, path, NULL, NULL, &sc);
    if (err == ESP_OK && (sc == 204 || sc == 200)) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t prusalink_pause(int job_id)  { return job_action(HTTP_METHOD_PUT, job_id, "pause"); }
esp_err_t prusalink_resume(int job_id) { return job_action(HTTP_METHOD_PUT, job_id, "resume"); }
esp_err_t prusalink_stop(int job_id)   { return job_action(HTTP_METHOD_DELETE, job_id, NULL); }

/* Percent-encode a path, preserving '/' segment separators and unreserved chars. */
static void url_encode_path(const char *in, char *out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < n; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out[o++] = c;
        } else {
            out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

esp_err_t prusalink_print(const char *path)
{
    pp_printer_t pr;
    if (!printer_store_active_get(&pr)) return ESP_FAIL;
    char url[300], enc[220];
    const char *rel = (path && path[0] == '/') ? path + 1 : (path ? path : "");
    url_encode_path(rel, enc, sizeof(enc));
    snprintf(url, sizeof(url), "/api/v1/files/%s/%s", s_storage, enc);
    int sc = 0;
    esp_err_t err = do_request(&pr, HTTP_METHOD_POST, url, "", NULL, &sc);
    return (err == ESP_OK && (sc == 204 || sc == 200)) ? ESP_OK : ESP_FAIL;
}

esp_err_t prusalink_upload(const char *local_path, const char *dest_name)
{
    pp_printer_t pr;
    if (!printer_store_active_get(&pr)) return ESP_FAIL;

    FILE *f = fopen(local_path, "rb");
    if (!f) return ESP_FAIL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char url[300], enc[220];
    url_encode_path(dest_name, enc, sizeof(enc));
    snprintf(url, sizeof(url), "/api/v1/files/%s/%s", s_storage, enc);

    const bool use_apikey = (pr.api_key[0] != '\0');
    esp_http_client_config_t cfg = {
        .host = pr.host,
        .port = pr.port ? pr.port : 80,
        .path = url,
        .method = HTTP_METHOD_PUT,
        .auth_type = use_apikey ? HTTP_AUTH_TYPE_NONE : HTTP_AUTH_TYPE_DIGEST,
        .username = CONFIG_PP_PRINTER_USER,
        .password = CONFIG_PP_PRINTER_PASSWORD,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { fclose(f); return ESP_FAIL; }

    if (use_apikey) esp_http_client_set_header(c, "X-Api-Key", pr.api_key);
    esp_http_client_set_header(c, "Content-Type", "application/octet-stream");

    esp_err_t err = esp_http_client_open(c, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(c);
        fclose(f);
        return ESP_FAIL;
    }

    char *buf = malloc(4096);
    if (!buf) { esp_http_client_cleanup(c); fclose(f); return ESP_ERR_NO_MEM; }

    int total_sent = 0;
    while (!feof(f)) {
        size_t read = fread(buf, 1, 4096, f);
        if (read > 0) {
            int sent = esp_http_client_write(c, buf, read);
            if (sent < 0) {
                err = ESP_FAIL;
                break;
            }
            total_sent += sent;
        }
    }
    free(buf);
    fclose(f);

    if (err == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int sc = esp_http_client_get_status_code(c);
        if (sc < 200 || sc >= 300) err = ESP_FAIL;
    }

    esp_http_client_cleanup(c);
    return err;
}

esp_err_t prusalink_gcode(const char *gcode)
{
    pp_printer_t pr;
    if (!printer_store_active_get(&pr)) return ESP_FAIL;
    if (!gcode || !gcode[0]) return ESP_FAIL;

    char body[200];
    snprintf(body, sizeof(body), "{\"command\": \"%s\"}", gcode);

    int sc = 0;
    esp_err_t err = do_request(&pr, HTTP_METHOD_POST, "/api/printer/command", body, NULL, &sc);
    return (err == ESP_OK && (sc == 204 || sc == 200)) ? ESP_OK : ESP_FAIL;
}

/* Authenticated GET of a raw blob (PNG thumbnail / camera snapshot) on the active
 * printer. On success returns ESP_OK and a malloc'd buffer (caller frees *out). */
esp_err_t prusalink_get_blob(const char *path, uint8_t **out, int *out_len)
{
    pp_printer_t pr;
    if (!printer_store_active_get(&pr)) return ESP_FAIL;
    return prusalink_get_blob_of(&pr, path, out, out_len);
}

esp_err_t prusalink_get_blob_of(const pp_printer_t *pr, const char *path, uint8_t **out, int *out_len)
{
    *out = NULL; *out_len = 0;
    resp_t r = {0};
    int sc = 0;
    esp_err_t err = do_request(pr, HTTP_METHOD_GET, path, NULL, &r, &sc);
    if (err == ESP_OK && sc == 200 && r.buf && r.len > 0) {
        *out = (uint8_t *)r.buf;
        *out_len = r.len;
        return ESP_OK;
    }
    free(r.buf);
    return ESP_FAIL;
}

/* Slicers encode the filament in the file name (e.g. "..._PETG_COREONE_1h7m.gcode").
 * Pull it out for a Connect-style row meta line. Returns NULL if not found. */
static const char *find_material(const char *disp)
{
    static const char *mats[] = {"PETG","PLA","ASA","ABS","TPU","PVB","HIPS","PCCF","PC","PA","FLEX",0};
    char up[120];
    size_t i = 0;
    for (; disp[i] && i + 1 < sizeof(up); i++) {
        char c = disp[i];
        up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    up[i] = '\0';
    for (int k = 0; mats[k]; k++) {
        if (strstr(up, mats[k])) return mats[k];
    }
    return NULL;
}

/* Newest-first sort comparator (by m_timestamp). */
static int file_cmp_mtime_desc(const void *a, const void *b)
{
    uint32_t x = ((const pp_file_t *)a)->mtime, y = ((const pp_file_t *)b)->mtime;
    return (x < y) - (x > y);   /* +1 if a older, -1 if a newer */
}

/* Build "<date>  ·  <material>" from a file's m_timestamp + name. */
static void build_file_meta(const cJSON *child, const char *disp, char *out, size_t n)
{
    char date[24] = {0};
    const cJSON *mt = cJSON_GetObjectItemCaseSensitive(child, "m_timestamp");
    if (cJSON_IsNumber(mt) && mt->valuedouble > 0) {
        time_t ts = (time_t)mt->valuedouble;
        struct tm tmv;
        gmtime_r(&ts, &tmv);
        strftime(date, sizeof(date), "%b %d, %Y", &tmv);
    }
    const char *mat = find_material(disp);
    if (date[0] && mat)      snprintf(out, n, "%s   -   %s", date, mat);
    else if (date[0])        strlcpy(out, date, n);
    else if (mat)            strlcpy(out, mat, n);
}

esp_err_t prusalink_list(const char *path, pp_file_t *arr, int max, int *count)
{
    *count = 0;
    pp_printer_t pr;
    if (!printer_store_active_get(&pr)) return ESP_FAIL;
    /* Cloud printer: list files from its local PrusaLink (IP + key learned from Connect),
     * so the file browser works for Connect-managed printers too. */
    if (strncmp(pr.host, "cloud:", 6) == 0 && pr.local_host[0])
        strlcpy(pr.host, pr.local_host, sizeof(pr.host));
    char url[256];
    const char *p = (path && path[0]) ? path : "/";
    /* Always include the path; just guarantee a single leading slash. */
    snprintf(url, sizeof(url), "/api/v1/files/%s%s%s", s_storage,
             (p[0] == '/') ? "" : "/", p);

    resp_t r = {0};
    int sc = 0;
    esp_err_t err = do_request(&pr, HTTP_METHOD_GET, url, NULL, &r, &sc);
    if (err != ESP_OK || sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *children = cJSON_GetObjectItemCaseSensitive(root, "children");
    cJSON *child = NULL;
    cJSON_ArrayForEach(child, children) {
        if (*count >= max) break;
        cJSON *type = cJSON_GetObjectItemCaseSensitive(child, "type");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(child, "name");
        cJSON *disp = cJSON_GetObjectItemCaseSensitive(child, "display_name");
        if (!cJSON_IsString(name)) continue;

        pp_file_t *f = &arr[*count];
        memset(f, 0, sizeof(*f));
        strlcpy(f->path, name->valuestring, sizeof(f->path));
        strlcpy(f->display,
                cJSON_IsString(disp) ? disp->valuestring : name->valuestring,
                sizeof(f->display));
        const char *t = cJSON_IsString(type) ? type->valuestring : "";
        f->is_folder = (strcmp(t, "FOLDER") == 0);
        f->is_print  = (strcmp(t, "PRINT_FILE") == 0);
        cJSON *refs = cJSON_GetObjectItemCaseSensitive(child, "refs");
        if (refs) {
            cJSON *th = cJSON_GetObjectItemCaseSensitive(refs, "thumbnail");
            if (!cJSON_IsString(th)) th = cJSON_GetObjectItemCaseSensitive(refs, "icon");
            if (cJSON_IsString(th) && th->valuestring) strlcpy(f->thumb, th->valuestring, sizeof(f->thumb));
        }
        cJSON *mt = cJSON_GetObjectItemCaseSensitive(child, "m_timestamp");
        if (cJSON_IsNumber(mt) && mt->valuedouble > 0) f->mtime = (uint32_t)mt->valuedouble;
        if (f->is_print) build_file_meta(child, f->display, f->meta, sizeof(f->meta));
        (*count)++;
    }
    cJSON_Delete(root);

    /* Newest files first (Connect's default "Latest" sort). */
    if (*count > 1) qsort(arr, *count, sizeof(pp_file_t), file_cmp_mtime_desc);
    return ESP_OK;
}
