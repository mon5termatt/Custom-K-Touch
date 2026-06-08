/* Prusa-Touch — Moonraker (Klipper) client. See moonraker.h. */
#include "moonraker.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "moonraker";

#define MOONRAKER_PORT_DEFAULT 7125

/* ---- response accumulator (heap) ---- */
typedef struct { char *buf; int len; int cap; } resp_t;

static esp_err_t http_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    resp_t *r = (resp_t *)e->user_data;
    if (!r) return ESP_OK;
    int need = r->len + e->data_len + 1;
    if (need > 600 * 1024) return ESP_ERR_NO_MEM;
    if (need > r->cap) {
        int nc = r->cap ? r->cap : 1024;
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

static esp_err_t do_request(const pp_printer_t *pr, esp_http_client_method_t method,
                            const char *path, resp_t *resp, int *status_code)
{
    esp_http_client_config_t cfg = {
        .host = pr->host,
        .port = pr->port ? pr->port : MOONRAKER_PORT_DEFAULT,
        .path = path,
        .method = method,
        .timeout_ms = 6000,
        .event_handler = http_event,
        .user_data = resp,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    /* Moonraker may run open (trusted clients) or behind an API key. */
    if (pr->api_key[0]) esp_http_client_set_header(c, "X-Api-Key", pr->api_key);
    esp_err_t err = esp_http_client_perform(c);
    int sc = esp_http_client_get_status_code(c);
    if (status_code) *status_code = sc;
    if (err != ESP_OK) ESP_LOGW(TAG, "%s -> %s", path, esp_err_to_name(err));
    esp_http_client_cleanup(c);
    return err;
}

static float jnum(const cJSON *o, const char *k, float def)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(n) ? (float)n->valuedouble : def;
}

/* Percent-encode a string for use as a query value. */
static void q_encode(const char *in, char *out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < n; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = c;
        } else {
            out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

bool moonraker_probe(const pp_printer_t *pr)
{
    resp_t r = {0};
    int sc = 0;
    esp_err_t err = do_request(pr, HTTP_METHOD_GET, "/printer/info", &r, &sc);
    bool ok = false;
    if (err == ESP_OK && sc == 200 && r.buf) {
        /* Moonraker wraps everything in {"result": {...}}. */
        ok = (strstr(r.buf, "\"result\"") != NULL) &&
             (strstr(r.buf, "klippy") != NULL || strstr(r.buf, "state") != NULL ||
              strstr(r.buf, "hostname") != NULL);
    }
    free(r.buf);
    return ok;
}

esp_err_t moonraker_get_info(const pp_printer_t *pr, char *model, size_t ml,
                             char *fw, size_t fl)
{
    if (model && ml) model[0] = '\0';
    if (fw && fl) fw[0] = '\0';
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/printer/info", &r, &sc) != ESP_OK ||
        sc != 200 || !r.buf) { free(r.buf); return ESP_FAIL; }
    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_FAIL;
    cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (res) {
        cJSON *hn = cJSON_GetObjectItemCaseSensitive(res, "hostname");
        if (model && ml) {
            if (cJSON_IsString(hn) && hn->valuestring[0])
                snprintf(model, ml, "Klipper (%s)", hn->valuestring);
            else
                strlcpy(model, "Klipper printer", ml);
        }
        cJSON *sv = cJSON_GetObjectItemCaseSensitive(res, "software_version");
        if (fw && fl && cJSON_IsString(sv) && sv->valuestring)
            strlcpy(fw, sv->valuestring, fl);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

/* Map Klipper print_stats.state -> our pp_status_t.state vocabulary. */
static const char *map_state(const char *s)
{
    if (!s) return "IDLE";
    if (!strcmp(s, "printing"))  return "PRINTING";
    if (!strcmp(s, "paused"))    return "PAUSED";
    if (!strcmp(s, "complete"))  return "FINISHED";
    if (!strcmp(s, "error"))     return "ERROR";
    if (!strcmp(s, "cancelled")) return "STOPPED";
    return "IDLE";   /* "standby" */
}

esp_err_t moonraker_get_status_of(const pp_printer_t *pr, pp_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->time_remaining = -1;

    const char *path = "/printer/objects/query?extruder&heater_bed&print_stats"
                       "&toolhead&gcode_move&virtual_sdcard&display_status";
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, path, &r, &sc) != ESP_OK || sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_FAIL;

    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (st) st = cJSON_GetObjectItemCaseSensitive(st, "status");
    if (st) {
        cJSON *ext = cJSON_GetObjectItemCaseSensitive(st, "extruder");
        cJSON *bed = cJSON_GetObjectItemCaseSensitive(st, "heater_bed");
        cJSON *ps  = cJSON_GetObjectItemCaseSensitive(st, "print_stats");
        cJSON *th  = cJSON_GetObjectItemCaseSensitive(st, "toolhead");
        cJSON *gm  = cJSON_GetObjectItemCaseSensitive(st, "gcode_move");
        cJSON *vsd = cJSON_GetObjectItemCaseSensitive(st, "virtual_sdcard");
        cJSON *ds  = cJSON_GetObjectItemCaseSensitive(st, "display_status");

        if (ext) { out->temp_nozzle = jnum(ext, "temperature", 0); out->target_nozzle = jnum(ext, "target", 0); }
        if (bed) { out->temp_bed = jnum(bed, "temperature", 0); out->target_bed = jnum(bed, "target", 0); }
        if (gm)  out->speed = (int)(jnum(gm, "speed_factor", 1.0f) * 100.0f + 0.5f);
        if (th) {
            cJSON *pos = cJSON_GetObjectItemCaseSensitive(th, "position");
            if (cJSON_IsArray(pos) && cJSON_GetArraySize(pos) >= 3) {
                cJSON *z = cJSON_GetArrayItem(pos, 2);
                if (cJSON_IsNumber(z)) out->axis_z = (float)z->valuedouble;
            }
        }

        const char *kstate = "standby";
        if (ps) {
            cJSON *s = cJSON_GetObjectItemCaseSensitive(ps, "state");
            if (cJSON_IsString(s)) kstate = s->valuestring;
            cJSON *fn = cJSON_GetObjectItemCaseSensitive(ps, "filename");
            if (cJSON_IsString(fn) && fn->valuestring) strlcpy(out->job_name, fn->valuestring, sizeof(out->job_name));
        }
        strlcpy(out->state, map_state(kstate), sizeof(out->state));
        out->has_job = (!strcmp(out->state, "PRINTING") || !strcmp(out->state, "PAUSED"));

        float prog = 0;
        if (vsd) prog = jnum(vsd, "progress", 0);
        if (prog <= 0 && ds) prog = jnum(ds, "progress", 0);
        out->progress = prog * 100.0f;

        /* Estimate remaining from elapsed print time + progress (Moonraker gives no ETA). */
        if (out->has_job && ps) {
            float dur = jnum(ps, "print_duration", 0);
            if (prog > 0.01f && dur > 0) out->time_remaining = (int)(dur * (1.0f - prog) / prog);
            out->time_printing = (int)dur;
        }
        out->has_control = true;   /* Klipper always accepts gcode/print control */
        out->online = true;
    }
    cJSON_Delete(root);
    return out->online ? ESP_OK : ESP_FAIL;
}

/* Slicer encodes filament in the name; reuse the same heuristic as PrusaLink rows. */
static const char *find_material(const char *disp)
{
    static const char *mats[] = {"PETG","PLA","ASA","ABS","TPU","PVB","HIPS","PCCF","PC","PA","FLEX",0};
    char up[120]; size_t i = 0;
    for (; disp[i] && i + 1 < sizeof(up); i++) { char c = disp[i]; up[i] = (c>='a'&&c<='z')?(char)(c-32):c; }
    up[i] = '\0';
    for (int k = 0; mats[k]; k++) if (strstr(up, mats[k])) return mats[k];
    return NULL;
}

static void build_meta(uint32_t mtime, const char *disp, char *out, size_t n)
{
    char date[24] = {0};
    if (mtime > 0) {
        time_t ts = (time_t)mtime; struct tm tmv; gmtime_r(&ts, &tmv);
        strftime(date, sizeof(date), "%b %d, %Y", &tmv);
    }
    const char *mat = find_material(disp);
    if (date[0] && mat) snprintf(out, n, "%s   -   %s", date, mat);
    else if (date[0])   strlcpy(out, date, n);
    else if (mat)       strlcpy(out, mat, n);
}

esp_err_t moonraker_list(const pp_printer_t *pr, pp_file_t *arr, int max, int *count)
{
    *count = 0;
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/server/files/list?root=gcodes", &r, &sc) != ESP_OK ||
        sc != 200 || !r.buf) { free(r.buf); return ESP_FAIL; }
    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_FAIL;
    cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, res) {
        if (*count >= max) break;
        cJSON *path = cJSON_GetObjectItemCaseSensitive(it, "path");
        if (!cJSON_IsString(path)) continue;
        pp_file_t *f = &arr[*count];
        memset(f, 0, sizeof(*f));
        strlcpy(f->path, path->valuestring, sizeof(f->path));
        /* display = basename of the path */
        const char *base = strrchr(path->valuestring, '/');
        strlcpy(f->display, base ? base + 1 : path->valuestring, sizeof(f->display));
        f->is_print = true;
        f->mtime = (uint32_t)jnum(it, "modified", 0);
        build_meta(f->mtime, f->display, f->meta, sizeof(f->meta));
        /* thumbnails require a per-file metadata call; left empty for now */
        (*count)++;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST helpers (Moonraker actions are simple POSTs, no body needed). */
static esp_err_t post(const pp_printer_t *pr, const char *path)
{
    int sc = 0;
    esp_err_t err = do_request(pr, HTTP_METHOD_POST, path, NULL, &sc);
    return (err == ESP_OK && (sc == 200 || sc == 204)) ? ESP_OK : ESP_FAIL;
}

esp_err_t moonraker_pause(const pp_printer_t *pr)  { return post(pr, "/printer/print/pause"); }
esp_err_t moonraker_resume(const pp_printer_t *pr) { return post(pr, "/printer/print/resume"); }
esp_err_t moonraker_stop(const pp_printer_t *pr)   { return post(pr, "/printer/print/cancel"); }

esp_err_t moonraker_print(const pp_printer_t *pr, const char *path)
{
    char enc[220], url[300];
    const char *rel = (path && path[0] == '/') ? path + 1 : (path ? path : "");
    q_encode(rel, enc, sizeof(enc));
    snprintf(url, sizeof(url), "/printer/print/start?filename=%s", enc);
    return post(pr, url);
}

esp_err_t moonraker_gcode(const pp_printer_t *pr, const char *gcode)
{
    if (!gcode || !gcode[0]) return ESP_FAIL;
    char enc[160], url[220];
    q_encode(gcode, enc, sizeof(enc));
    snprintf(url, sizeof(url), "/printer/gcode/script?script=%s", enc);
    return post(pr, url);
}
