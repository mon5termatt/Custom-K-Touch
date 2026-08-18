/* Prusa-Touch — Moonraker (Klipper) client. See moonraker.h. */
#include "moonraker.h"
#include "prefs.h"

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

static esp_err_t do_request_to(const pp_printer_t *pr, esp_http_client_method_t method,
                               const char *path, resp_t *resp, int *status_code, int timeout_ms)
{
    esp_http_client_config_t cfg = {
        .host = pr->host,
        .port = pr->port ? pr->port : MOONRAKER_PORT_DEFAULT,
        .path = path,
        .method = method,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 6000,
        .event_handler = http_event,
        .user_data = resp,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    if (pr->api_key[0]) esp_http_client_set_header(c, "X-Api-Key", pr->api_key);
    esp_err_t err = esp_http_client_perform(c);
    int sc = esp_http_client_get_status_code(c);
    if (status_code) *status_code = sc;
    if (err != ESP_OK) ESP_LOGW(TAG, "%s -> %s", path, esp_err_to_name(err));
    esp_http_client_cleanup(c);
    return err;
}

static esp_err_t do_request(const pp_printer_t *pr, esp_http_client_method_t method,
                            const char *path, resp_t *resp, int *status_code)
{
    return do_request_to(pr, method, path, resp, status_code, 6000);
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

/* Percent-encode a URL path, keeping '/' as a separator (spaces, unicode, etc.). */
static void path_encode(const char *in, char *out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < n; p++) {
        unsigned char c = *p;
        if (c == '/' ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
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
    out->current_layer  = -1;
    out->total_layer    = -1;

    const char *path = "/printer/objects/query?extruder&heater_bed&print_stats"
                       "&toolhead&gcode_move&virtual_sdcard&display_status&webhooks";
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
        /* Prefer Klipper host state when shutdown/error so the fault screen can open. */
        cJSON *wh = cJSON_GetObjectItemCaseSensitive(st, "webhooks");
        if (wh) {
            cJSON *ws = cJSON_GetObjectItemCaseSensitive(wh, "state");
            if (cJSON_IsString(ws) && ws->valuestring) {
                if (!strcmp(ws->valuestring, "shutdown") || !strcmp(ws->valuestring, "error"))
                    kstate = "error";
            }
        }
        strlcpy(out->state, map_state(kstate), sizeof(out->state));
        out->has_job = (!strcmp(out->state, "PRINTING") || !strcmp(out->state, "PAUSED"));
        if (out->has_job && out->job_name[0])
            strlcpy(out->job_thumb, out->job_name, sizeof(out->job_thumb));

        /* Progress normalization:
         * - virtual_sdcard.progress is usually 0..1
         * - display_status.progress is often 0..100
         * Mixing these makes derived ETA go negative/blank. */
        float prog = 0;
        if (vsd) prog = jnum(vsd, "progress", 0);
        if (prog <= 0 && ds) prog = jnum(ds, "progress", 0);
        if (prog > 1.0f) prog /= 100.0f;      /* accept 0..100 -> 0..1 */
        if (prog < 0.0f) prog = 0.0f;
        if (prog > 1.0f) prog = 1.0f;
        out->progress = prog * 100.0f;

        /* Estimate remaining from elapsed print time + progress (Moonraker gives no ETA). */
        if (out->has_job && ps) {
            float dur = jnum(ps, "print_duration", 0);
            if (prog > 0.01f && dur > 0) out->time_remaining = (int)(dur * (1.0f - prog) / prog);
            out->time_printing = (int)dur;

            /* Layer progress (if virtual_sdcard + slicer emit SET_PRINT_STATS_INFO). */
            cJSON *info = cJSON_GetObjectItemCaseSensitive(ps, "info");
            if (info) {
                cJSON *cl = cJSON_GetObjectItemCaseSensitive(info, "current_layer");
                cJSON *tl = cJSON_GetObjectItemCaseSensitive(info, "total_layer");
                if (cJSON_IsNumber(cl)) out->current_layer = (int)cl->valueint;
                if (cJSON_IsNumber(tl)) out->total_layer = (int)tl->valueint;
            }
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

static int file_cmp_mtime_desc(const void *a, const void *b)
{
    uint32_t x = ((const pp_file_t *)a)->mtime, y = ((const pp_file_t *)b)->mtime;
    return (x < y) - (x > y);
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
        /* Thumb fetch uses metadata later; stash the gcode path as the thumb ref. */
        strlcpy(f->thumb, f->path, sizeof(f->thumb));
        (*count)++;
    }
    cJSON_Delete(root);
    if (*count > 1) qsort(arr, (size_t)*count, sizeof(pp_file_t), file_cmp_mtime_desc);
    return ESP_OK;
}

/* GET arbitrary path and return body bytes (caller frees). Optional port override
 * (0 = use printer's Moonraker port). Webcam proxies often live on :80 while the API is :7125. */
static esp_err_t get_bytes_port(const pp_printer_t *pr, const char *path, int port,
                                uint8_t **out, int *out_len)
{
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!pr || !path) return ESP_FAIL;
    resp_t r = {0};
    esp_http_client_config_t cfg = {
        .host = pr->host,
        .port = port > 0 ? port : (pr->port ? pr->port : MOONRAKER_PORT_DEFAULT),
        .path = path,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .event_handler = http_event,
        .user_data = &r,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    if (pr->api_key[0]) esp_http_client_set_header(c, "X-Api-Key", pr->api_key);
    esp_err_t err = esp_http_client_perform(c);
    int sc = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || sc != 200 || !r.buf || r.len <= 0) {
        free(r.buf);
        return ESP_FAIL;
    }
    /* Reject non-image HTML error pages that some proxies return as 200. */
    if (r.len >= 3 && (uint8_t)r.buf[0] == 0xFF && (uint8_t)r.buf[1] == 0xD8) {
        /* JPEG */
    } else if (r.len >= 8 && memcmp(r.buf, "\x89PNG\r\n\x1a\n", 8) == 0) {
        /* PNG */
    } else {
        free(r.buf);
        return ESP_FAIL;
    }
    if (out) *out = (uint8_t *)r.buf; else free(r.buf);
    if (out_len) *out_len = r.len;
    return ESP_OK;
}

static esp_err_t get_bytes(const pp_printer_t *pr, const char *path, uint8_t **out, int *out_len)
{
    return get_bytes_port(pr, path, 0, out, out_len);
}

esp_err_t moonraker_fetch_thumb(const pp_printer_t *pr, const char *gcode_path,
                                uint8_t **out, int *out_len)
{
    if (!pr || !gcode_path || !gcode_path[0] || !out || !out_len) return ESP_FAIL;
    *out = NULL; *out_len = 0;

    char enc[220], meta_path[280];
    const char *rel = (gcode_path[0] == '/') ? gcode_path + 1 : gcode_path;
    q_encode(rel, enc, sizeof(enc));

    /* Prefer /server/files/thumbnails — thumbnail_path is relative to gcodes root
     * (correct for nested folders). Fall back to metadata.relative_path + dirname. */
    char best_rel[200] = {0};   /* path under gcodes/, e.g. sub/.thumbs/x.png */
    snprintf(meta_path, sizeof(meta_path), "/server/files/thumbnails?filename=%s", enc);

    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, meta_path, &r, &sc) == ESP_OK && sc == 200 && r.buf) {
        cJSON *root = cJSON_Parse(r.buf);
        free(r.buf); r.buf = NULL;
        if (root) {
            /* Result is a bare array of {width,height,size,thumbnail_path}. */
            cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "result");
            if (!cJSON_IsArray(arr)) arr = root;
            int best_area = -1;
            cJSON *it = NULL;
            cJSON_ArrayForEach(it, arr) {
                cJSON *tp = cJSON_GetObjectItemCaseSensitive(it, "thumbnail_path");
                int w = (int)jnum(it, "width", 0);
                int h = (int)jnum(it, "height", 0);
                if (!cJSON_IsString(tp) || !tp->valuestring) continue;
                int area = w * h;
                if (area >= best_area) {
                    best_area = area;
                    strlcpy(best_rel, tp->valuestring, sizeof(best_rel));
                }
            }
            cJSON_Delete(root);
        }
    } else {
        free(r.buf); r.buf = NULL;
    }

    if (!best_rel[0]) {
        snprintf(meta_path, sizeof(meta_path), "/server/files/metadata?filename=%s", enc);
        r = (resp_t){0};
        sc = 0;
        if (do_request(pr, HTTP_METHOD_GET, meta_path, &r, &sc) != ESP_OK || sc != 200 || !r.buf) {
            ESP_LOGW(TAG, "thumb meta fail %s sc=%d", rel, sc);
            free(r.buf);
            return ESP_FAIL;
        }
        cJSON *root = cJSON_Parse(r.buf);
        free(r.buf);
        if (!root) return ESP_FAIL;

        cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
        cJSON *thumbs = res ? cJSON_GetObjectItemCaseSensitive(res, "thumbnails") : NULL;
        const char *best = NULL;
        int best_area = -1;
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, thumbs) {
            cJSON *rp = cJSON_GetObjectItemCaseSensitive(it, "relative_path");
            int w = (int)jnum(it, "width", 0);
            int h = (int)jnum(it, "height", 0);
            if (!cJSON_IsString(rp) || !rp->valuestring) continue;
            int area = w * h;
            if (area >= best_area) { best_area = area; best = rp->valuestring; }
        }
        if (!best) {
            ESP_LOGW(TAG, "thumb none for %s", rel);
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        /* relative_path is relative to the gcode's parent directory, not gcodes/. */
        const char *slash = strrchr(rel, '/');
        if (slash) {
            snprintf(best_rel, sizeof(best_rel), "%.*s/%s",
                     (int)(slash - rel), rel, best);
        } else {
            strlcpy(best_rel, best, sizeof(best_rel));
        }
        cJSON_Delete(root);
    }

    char thumb_url[360], thumb_enc[400];
    snprintf(thumb_url, sizeof(thumb_url), "/server/files/gcodes/%s", best_rel);
    path_encode(thumb_url, thumb_enc, sizeof(thumb_enc));
    ESP_LOGI(TAG, "thumb get %s", thumb_enc);
    esp_err_t ge = get_bytes(pr, thumb_enc, out, out_len);
    if (ge != ESP_OK) ESP_LOGW(TAG, "thumb download fail %s", thumb_enc);
    return ge;
}

esp_err_t moonraker_fetch_snapshot(const pp_printer_t *pr, uint8_t **out, int *out_len)
{
    if (!pr || !out || !out_len) return ESP_FAIL;
    *out = NULL; *out_len = 0;

    char path[300] = "/webcam/?action=snapshot";
    int abs_port = 0;   /* set when snapshot_url is an absolute http://host:port/... */

    /* Prefer configured webcams list (Moonraker >= 0.8). */
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/server/webcams/list", &r, &sc) == ESP_OK &&
        sc == 200 && r.buf) {
        cJSON *root = cJSON_Parse(r.buf);
        free(r.buf); r.buf = NULL;
        if (root) {
            cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
            cJSON *webcams = res ? cJSON_GetObjectItemCaseSensitive(res, "webcams") : NULL;
            int want = prefs_webcam_index();
            int idx = 0;
            cJSON *it = NULL;
            cJSON_ArrayForEach(it, webcams) {
                cJSON *en = cJSON_GetObjectItemCaseSensitive(it, "enabled");
                if (cJSON_IsBool(en) && !cJSON_IsTrue(en)) continue;
                if (idx++ < want) continue;
                cJSON *snap = cJSON_GetObjectItemCaseSensitive(it, "snapshot_url");
                if (!cJSON_IsString(snap) || !snap->valuestring || !snap->valuestring[0]) continue;
                const char *url = snap->valuestring;
                if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
                    /* Absolute URL — use path (+ port if present). Host stays the printer. */
                    const char *after = strstr(url, "://");
                    after = after ? after + 3 : NULL;
                    const char *slash = after ? strchr(after, '/') : NULL;
                    if (!slash) continue;
                    abs_port = 80;
                    if (after) {
                        const char *colon = memchr(after, ':', (size_t)(slash - after));
                        if (colon) abs_port = atoi(colon + 1);
                    }
                    strlcpy(path, slash, sizeof(path));
                } else {
                    strlcpy(path, url, sizeof(path));
                }
                break;
            }
            cJSON_Delete(root);
        }
    } else {
        free(r.buf);
    }

    /* Relative webcam URLs are usually served by the machine's front-end proxy (:80),
     * not Moonraker (:7125). Try Moonraker port first, then :80. */
    int mr_port = pr->port ? pr->port : MOONRAKER_PORT_DEFAULT;
    if (abs_port > 0) {
        if (get_bytes_port(pr, path, abs_port, out, out_len) == ESP_OK) return ESP_OK;
    }
    if (get_bytes_port(pr, path, mr_port, out, out_len) == ESP_OK) return ESP_OK;
    if (mr_port != 80 && get_bytes_port(pr, path, 80, out, out_len) == ESP_OK) return ESP_OK;
    /* Common alternate path when webcams/list is empty/misconfigured. */
    if (strcmp(path, "/webcam/?action=snapshot") != 0) {
        if (get_bytes_port(pr, "/webcam/?action=snapshot", 80, out, out_len) == ESP_OK) return ESP_OK;
    }
    return ESP_FAIL;
}

/* POST helpers (Moonraker actions are simple POSTs, no body needed).
 * 3s timeout — control taps should fail fast rather than wait for a dead host. */
static esp_err_t post(const pp_printer_t *pr, const char *path)
{
    int sc = 0;
    esp_err_t err = do_request_to(pr, HTTP_METHOD_POST, path, NULL, &sc, 3000);
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

esp_err_t moonraker_upload(const pp_printer_t *pr, const char *local_path, const char *dest_name)
{
    FILE *f = fopen(local_path, "rb");
    if (!f) return ESP_FAIL;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    const char *boundary = "----KlipperTouchBoundary";
    char preamble[256];
    int preamble_len = snprintf(preamble, sizeof(preamble),
                                "--%s\r\n"
                                "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                                "Content-Type: application/octet-stream\r\n\r\n",
                                boundary, dest_name);

    char epilogue[128];
    int epilogue_len = snprintf(epilogue, sizeof(epilogue), "\r\n--%s--\r\n", boundary);

    long total_size = preamble_len + file_size + epilogue_len;

    esp_http_client_config_t http_cfg = {
        .host = pr->host,
        .port = pr->port ? pr->port : MOONRAKER_PORT_DEFAULT,
        .path = "/server/files/upload",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&http_cfg);
    if (!c) { fclose(f); return ESP_FAIL; }

    if (pr->api_key[0]) esp_http_client_set_header(c, "X-Api-Key", pr->api_key);
    char ct[64];
    snprintf(ct, sizeof(ct), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(c, "Content-Type", ct);

    esp_err_t err = esp_http_client_open(c, total_size);
    if (err != ESP_OK) {
        esp_http_client_cleanup(c);
        fclose(f);
        return ESP_FAIL;
    }

    esp_http_client_write(c, preamble, preamble_len);

    char *buf = malloc(4096);
    if (!buf) { esp_http_client_cleanup(c); fclose(f); return ESP_ERR_NO_MEM; }

    while (!feof(f)) {
        size_t read = fread(buf, 1, 4096, f);
        if (read > 0) {
            if (esp_http_client_write(c, buf, read) < 0) {
                err = ESP_FAIL;
                break;
            }
        }
    }
    free(buf);
    fclose(f);

    if (err == ESP_OK) {
        esp_http_client_write(c, epilogue, epilogue_len);
        esp_http_client_fetch_headers(c);
        int sc = esp_http_client_get_status_code(c);
        if (sc < 200 || sc >= 300) err = ESP_FAIL;
    }

    esp_http_client_cleanup(c);
    return err;
}

esp_err_t moonraker_gcode(const pp_printer_t *pr, const char *gcode)
{
    if (!gcode || !gcode[0]) return ESP_FAIL;
    char enc[160], url[220];
    q_encode(gcode, enc, sizeof(enc));
    snprintf(url, sizeof(url), "/printer/gcode/script?script=%s", enc);
    return post(pr, url);
}

static void jstr(const cJSON *o, const char *k, char *dst, size_t n)
{
    if (!dst || !n) return;
    dst[0] = '\0';
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsString(v) && v->valuestring) strlcpy(dst, v->valuestring, n);
}

static bool jbool(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsTrue(v);
}

esp_err_t moonraker_get_afc(const pp_printer_t *pr, pp_afc_t *out)
{
    if (!out) return ESP_FAIL;
    memset(out, 0, sizeof(*out));

    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/printer/objects/query?AFC", &r, &sc) != ESP_OK ||
        sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_OK;   /* unreachable / no AFC — present stays false */
    }

    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_OK;

    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (st) st = cJSON_GetObjectItemCaseSensitive(st, "status");
    cJSON *afc = st ? cJSON_GetObjectItemCaseSensitive(st, "AFC") : NULL;
    if (!cJSON_IsObject(afc)) { cJSON_Delete(root); return ESP_OK; }

    char lane_names[PP_AFC_MAX_LANES][12];
    int nnames = 0;
    cJSON *lanes = cJSON_GetObjectItemCaseSensitive(afc, "lanes");
    if (cJSON_IsArray(lanes)) {
        cJSON *it;
        cJSON_ArrayForEach(it, lanes) {
            if (!cJSON_IsString(it) || !it->valuestring || nnames >= PP_AFC_MAX_LANES) continue;
            strlcpy(lane_names[nnames], it->valuestring, sizeof(lane_names[0]));
            nnames++;
        }
    }
    /* Require at least one lane — empty/stub AFC objects are not "installed". */
    if (nnames == 0) { cJSON_Delete(root); return ESP_OK; }

    out->present = true;
    out->error = jbool(afc, "error_state");
    jstr(afc, "current_state", out->state, sizeof(out->state));
    if (!out->state[0]) strlcpy(out->state, "Idle", sizeof(out->state));
    jstr(afc, "current_lane", out->current, sizeof(out->current));
    if (!out->current[0]) jstr(afc, "current_load", out->current, sizeof(out->current));
    cJSON_Delete(root);

    /* Batch-query AFC_stepper <name> for each lane. */
    char path[480];
    size_t off = 0;
    off += (size_t)snprintf(path + off, sizeof(path) - off, "/printer/objects/query");
    for (int i = 0; i < nnames && off + 48 < sizeof(path); i++) {
        char enc[64];
        char key[32];
        /* Build "AFC_stepper <name>" without snprintf+%s (avoids -Wformat-truncation). */
        memcpy(key, "AFC_stepper ", 12);
        strlcpy(key + 12, lane_names[i], sizeof(key) - 12);
        q_encode(key, enc, sizeof(enc));
        off += (size_t)snprintf(path + off, sizeof(path) - off, "%c%s", i ? '&' : '?', enc);
    }

    r = (resp_t){0};
    if (do_request(pr, HTTP_METHOD_GET, path, &r, &sc) != ESP_OK || sc != 200 || !r.buf) {
        free(r.buf);
        /* Still present — lane list from AFC object, detail unavailable. */
        for (int i = 0; i < nnames; i++) {
            strlcpy(out->lanes[i].name, lane_names[i], sizeof(out->lanes[i].name));
            out->lanes[i].num = i + 1;
        }
        out->n = nnames;
        return ESP_OK;
    }

    root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_OK;
    st = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (st) st = cJSON_GetObjectItemCaseSensitive(st, "status");

    for (int i = 0; i < nnames; i++) {
        pp_afc_lane_t *L = &out->lanes[out->n];
        strlcpy(L->name, lane_names[i], sizeof(L->name));
        L->num = i + 1;

        char key[32];
        memcpy(key, "AFC_stepper ", 12);
        strlcpy(key + 12, lane_names[i], sizeof(key) - 12);
        cJSON *ls = st ? cJSON_GetObjectItemCaseSensitive(st, key) : NULL;
        if (cJSON_IsObject(ls)) {
            int num = (int)jnum(ls, "lane", (float)(i + 1));
            if (num > 0) L->num = num;
            jstr(ls, "map", L->map, sizeof(L->map));
            jstr(ls, "material", L->material, sizeof(L->material));
            jstr(ls, "color", L->color, sizeof(L->color));
            jstr(ls, "filament_status", L->status, sizeof(L->status));
            bool prep = jbool(ls, "prep");
            bool load = jbool(ls, "load");
            L->ready = prep && load;
            L->tool_loaded = jbool(ls, "tool_loaded");
            if (L->tool_loaded && !out->current[0])
                strlcpy(out->current, L->name, sizeof(out->current));
        }
        out->n++;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t moonraker_afc_change(const pp_printer_t *pr, int lane_num)
{
    if (lane_num < 1 || lane_num > 99) return ESP_FAIL;
    char g[40];
    snprintf(g, sizeof(g), "BT_CHANGE_TOOL LANE=%d", lane_num);
    return moonraker_gcode(pr, g);
}

esp_err_t moonraker_afc_unload(const pp_printer_t *pr)
{
    return moonraker_gcode(pr, "BT_TOOL_UNLOAD");
}

esp_err_t moonraker_emergency_stop(const pp_printer_t *pr)
{
    return post(pr, "/printer/emergency_stop");
}

static bool gcode_msg_is_temp(const char *m)
{
    if (!m || !m[0]) return false;
    /* Skip leading "ok " / "Recv: " / whitespace */
    while (*m == ' ' || *m == '\t') m++;
    if (!strncmp(m, "ok ", 3)) m += 3;
    else if (!strncmp(m, "Recv: ", 6)) m += 6;
    while (*m == ' ' || *m == '\t') m++;
    /* Classic Marlin/Klipper heater report starts with T: then a digit */
    if (m[0] == 'T' && m[1] == ':') {
        const char *p = m + 2;
        if (*p == '-' || *p == '+') p++;
        if (*p >= '0' && *p <= '9') return true;
    }
    return false;
}

esp_err_t moonraker_gcode_store(const pp_printer_t *pr, pp_gcode_log_t *out)
{
    if (!out) return ESP_FAIL;
    memset(out, 0, sizeof(*out));
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/server/gcode_store?count=64", &r, &sc) != ESP_OK ||
        sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_FAIL;
    cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *gcode = res ? cJSON_GetObjectItemCaseSensitive(res, "gcode_store") : NULL;
    if (cJSON_IsArray(gcode)) {
        cJSON *it;
        cJSON_ArrayForEach(it, gcode) {
            if (out->count >= PP_GCODE_LOG_MAX) break;
            cJSON *msg = cJSON_GetObjectItemCaseSensitive(it, "message");
            if (!cJSON_IsString(msg) || !msg->valuestring) continue;
            const char *m = msg->valuestring;
            /* Suppress heater/temp poll spam: "ok T:…", "T:25.1 /0.0 B:…" */
            if (gcode_msg_is_temp(m)) continue;
            strlcpy(out->lines[out->count], m, PP_GCODE_LOG_LINE);
            out->count++;
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t moonraker_list_macros(const pp_printer_t *pr, pp_macro_list_t *out)
{
    if (!out) return ESP_FAIL;
    memset(out, 0, sizeof(*out));
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/printer/gcode/help", &r, &sc) != ESP_OK ||
        sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_FAIL;
    cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsObject(res)) {
        for (cJSON *it = res->child; it; it = it->next) {
            if (out->count >= PP_MACRO_MAX) break;
            const char *name = it->string;
            if (!name || !name[0]) continue;
            /* Skip private macros (Klipper convention: leading underscore). */
            if (name[0] == '_') continue;
            /* Skip noisy builtins; keep user macros and common AFC/BT_ helpers. */
            if (!strncmp(name, "SET_", 4) || !strncmp(name, "GET_", 4) ||
                !strncmp(name, "QUERY_", 6) || !strncmp(name, "DUMP_", 5) ||
                !strncmp(name, "BED_MESH_", 9) || !strncmp(name, "TUNING_", 7) ||
                !strncmp(name, "ACCELEROMETER_", 14) || !strncmp(name, "TEST_", 5) ||
                !strncmp(name, "DELAYED_", 8) || !strcmp(name, "RESPOND") ||
                !strcmp(name, "SAVE_CONFIG") || !strcmp(name, "RESTART") ||
                !strcmp(name, "FIRMWARE_RESTART") || !strcmp(name, "STATUS") ||
                !strcmp(name, "HELP") || !strcmp(name, "M112") ||
                (name[0] == 'M' && name[1] >= '0' && name[1] <= '9') ||
                (name[0] == 'G' && name[1] >= '0' && name[1] <= '9'))
                continue;
            strlcpy(out->names[out->count], name, PP_MACRO_NAME_LEN);
            out->count++;
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t moonraker_query_endstops(const pp_printer_t *pr, pp_endstop_list_t *out)
{
    if (!out) return ESP_FAIL;
    memset(out, 0, sizeof(*out));
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/printer/query_endstops/status", &r, &sc) != ESP_OK ||
        sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_FAIL;
    cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsObject(res)) {
        for (cJSON *it = res->child; it && out->count < PP_ENDSTOP_MAX; it = it->next) {
            if (!it->string) continue;
            const char *val = cJSON_IsString(it) ? it->valuestring : "?";
            strlcpy(out->name[out->count], it->string, PP_ENDSTOP_NAME_LEN);
            strlcpy(out->state[out->count], val ? val : "?", PP_ENDSTOP_STATE_LEN);
            out->count++;
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t moonraker_afc_eject(const pp_printer_t *pr, int lane_num)
{
    if (lane_num < 1 || lane_num > 99) return ESP_FAIL;
    char g[40];
    snprintf(g, sizeof(g), "BT_LANE_EJECT LANE=%d", lane_num);
    return moonraker_gcode(pr, g);
}

esp_err_t moonraker_afc_move(const pp_printer_t *pr, int lane_num, int distance_mm)
{
    if (lane_num < 1 || lane_num > 99) return ESP_FAIL;
    char g[48];
    snprintf(g, sizeof(g), "BT_LANE_MOVE LANE=%d DISTANCE=%d", lane_num, distance_mm);
    return moonraker_gcode(pr, g);
}

esp_err_t moonraker_afc_prep(const pp_printer_t *pr)   { return moonraker_gcode(pr, "BT_PREP"); }
esp_err_t moonraker_afc_resume(const pp_printer_t *pr) { return moonraker_gcode(pr, "BT_RESUME"); }
esp_err_t moonraker_afc_clear(const pp_printer_t *pr)  { return moonraker_gcode(pr, "AFC_CLEAR_MESSAGE"); }

static uint8_t led_u8(float v)
{
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    return (uint8_t)(v * 255.f + 0.5f);
}

static bool led_name_ok(const char *n)
{
    if (!n || !n[0] || n[0] == '_') return false;
    for (const char *p = n; *p; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

/* Match "neopixel screen" / "led case" / "led_effect rainbow". Longer prefixes first. */
static bool led_parse_object(const char *obj, char *type, size_t tl, char *name, size_t nl,
                             uint8_t *kind, bool *pwm)
{
    static const struct { const char *pfx; uint8_t kind; bool pwm; } T[] = {
        { "led_effect ", PP_LED_KIND_EFFECT, false },
        { "neopixel ",   PP_LED_KIND_COLOR,  false },
        { "dotstar ",    PP_LED_KIND_COLOR,  false },
        { "pca9533 ",    PP_LED_KIND_COLOR,  false },
        { "pca9632 ",    PP_LED_KIND_COLOR,  false },
        { "AFC_led ",    PP_LED_KIND_COLOR,  false },
        { "afc_led ",    PP_LED_KIND_COLOR,  false },
        { "led ",        PP_LED_KIND_COLOR,  true  },
    };
    if (!obj) return false;
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        size_t n = strlen(T[i].pfx);
        if (strncmp(obj, T[i].pfx, n) != 0) continue;
        const char *nm = obj + n;
        if (!led_name_ok(nm)) return false;
        if (type && tl) strlcpy(type, T[i].pfx, tl); /* includes trailing space; trim below */
        if (type && tl) {
            size_t L = strlen(type);
            if (L && type[L - 1] == ' ') type[L - 1] = '\0';
        }
        if (name && nl) strlcpy(name, nm, nl);
        if (kind) *kind = T[i].kind;
        if (pwm) *pwm = T[i].pwm;
        return true;
    }
    return false;
}

static bool led_json_truthy(const cJSON *v)
{
    if (cJSON_IsTrue(v)) return true;
    if (cJSON_IsNumber(v) && v->valuedouble != 0) return true;
    return false;
}

static void led_fill_color(pp_led_t *L, const cJSON *st)
{
    if (!L || !st) return;
    char key[48];
    snprintf(key, sizeof(key), "%s %s", L->type, L->name);
    cJSON *o = cJSON_GetObjectItemCaseSensitive(st, key);
    if (!cJSON_IsObject(o)) return;
    cJSON *en = cJSON_GetObjectItemCaseSensitive(o, "enabled");
    if (!en) en = cJSON_GetObjectItemCaseSensitive(o, "running");
    if (en && (cJSON_IsBool(en) || cJSON_IsNumber(en))) {
        L->on = led_json_truthy(en);
        L->on_known = true;
    }
    cJSON *cd = cJSON_GetObjectItemCaseSensitive(o, "color_data");
    if (!cJSON_IsArray(cd) || cJSON_GetArraySize(cd) < 1) return;
    L->pixels = cJSON_GetArraySize(cd);
    cJSON *p0 = cJSON_GetArrayItem(cd, 0);
    if (!cJSON_IsArray(p0)) return;
    int n = cJSON_GetArraySize(p0);
    float ch[4] = {0, 0, 0, 0};
    for (int i = 0; i < n && i < 4; i++) {
        cJSON *c = cJSON_GetArrayItem(p0, i);
        if (cJSON_IsNumber(c)) ch[i] = (float)c->valuedouble;
    }
    L->r = led_u8(ch[0]);
    L->g = led_u8(ch[1]);
    L->b = led_u8(ch[2]);
    L->w = led_u8(ch[3]);
    if (!L->on_known && L->kind != PP_LED_KIND_EFFECT) {
        L->on = (L->r || L->g || L->b || L->w);
        L->on_known = true;
    }
}

esp_err_t moonraker_list_leds(const pp_printer_t *pr, pp_led_list_t *out)
{
    if (!out) return ESP_FAIL;
    memset(out, 0, sizeof(*out));
    resp_t r = {0};
    int sc = 0;
    if (do_request(pr, HTTP_METHOD_GET, "/printer/objects/list", &r, &sc) != ESP_OK ||
        sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_FAIL;
    cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "result");
    cJSON *objs = res ? cJSON_GetObjectItemCaseSensitive(res, "objects") : NULL;
    if (cJSON_IsArray(objs)) {
        cJSON *it;
        cJSON_ArrayForEach(it, objs) {
            if (out->count >= PP_LED_MAX) break;
            if (!cJSON_IsString(it) || !it->valuestring) continue;
            pp_led_t *L = &out->items[out->count];
            if (!led_parse_object(it->valuestring, L->type, sizeof(L->type),
                                  L->name, sizeof(L->name), &L->kind, &L->pwm))
                continue;
            L->dimmable = (L->kind != PP_LED_KIND_EFFECT);
            out->count++;
        }
    }
    cJSON_Delete(root);
    if (out->count == 0) return ESP_OK;

    char path[768];
    size_t off = (size_t)snprintf(path, sizeof(path), "/printer/objects/query");
    for (int i = 0; i < out->count && off + 64 < sizeof(path); i++) {
        char key[48], enc[64];
        snprintf(key, sizeof(key), "%s %s", out->items[i].type, out->items[i].name);
        q_encode(key, enc, sizeof(enc));
        off += (size_t)snprintf(path + off, sizeof(path) - off, "%c%s", i ? '&' : '?', enc);
    }
    r = (resp_t){0};
    if (do_request(pr, HTTP_METHOD_GET, path, &r, &sc) != ESP_OK || sc != 200 || !r.buf) {
        free(r.buf);
        return ESP_OK;   /* names still useful without color_data */
    }
    root = cJSON_Parse(r.buf);
    free(r.buf);
    if (!root) return ESP_OK;
    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (st) st = cJSON_GetObjectItemCaseSensitive(st, "status");
    if (cJSON_IsObject(st)) {
        for (int i = 0; i < out->count; i++)
            led_fill_color(&out->items[i], st);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t moonraker_set_led(const pp_printer_t *pr, const char *name,
                            float r, float g, float b, float w)
{
    if (!led_name_ok(name)) return ESP_FAIL;
    r = (r < 0.f) ? 0.f : (r > 1.f ? 1.f : r);
    g = (g < 0.f) ? 0.f : (g > 1.f ? 1.f : g);
    b = (b < 0.f) ? 0.f : (b > 1.f ? 1.f : b);
    w = (w < 0.f) ? 0.f : (w > 1.f ? 1.f : w);
    char gcode[128];
    snprintf(gcode, sizeof(gcode),
             "SET_LED LED=%s RED=%.3f GREEN=%.3f BLUE=%.3f WHITE=%.3f TRANSMIT=1",
             name, r, g, b, w);
    return moonraker_gcode(pr, gcode);
}

esp_err_t moonraker_set_led_effect(const pp_printer_t *pr, const char *name, bool stop)
{
    if (!led_name_ok(name)) return ESP_FAIL;
    char gcode[96];
    if (stop) snprintf(gcode, sizeof(gcode), "SET_LED_EFFECT EFFECT=%s STOP=1", name);
    else      snprintf(gcode, sizeof(gcode), "SET_LED_EFFECT EFFECT=%s", name);
    return moonraker_gcode(pr, gcode);
}
