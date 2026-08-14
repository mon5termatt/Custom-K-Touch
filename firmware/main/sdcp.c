/* Chitubox SDCP V3.0.0 — resin printer status monitoring.
 * Spec: CBD SDCP(Smart Device Control Protocol)_V3.0.0_EN.md
 * Discovery UDP M99999 → :3000; status via ws://IP:3030/websocket Cmd 0. */
#include "sdcp.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "cJSON.h"
#include "i18n.h"

static const char *TAG = "sdcp";

#define SDCP_PREFIX      "sdcp:"
#define SDCP_DISC_PORT   3000
#define SDCP_WS_PORT     3030
#define SDCP_DISC_MS     2500
#define SDCP_WAIT_MS     6000
#define SDCP_RX_CAP      16384

/* Synthetic dialog_ids for the Prusa-style attention banner (Connect uses real ids). */
#define SDCP_DID_PRINT   900000   /* + PrintInfo.ErrorNumber */
#define SDCP_DID_XFER    910000   /* + sdcp/error ErrorCode */
#define SDCP_DID_NOTICE  920000   /* + sdcp/notice Type */

/* Machine CurrentStatus values (spec). */
enum {
    SDCP_MS_IDLE = 0,
    SDCP_MS_PRINTING = 1,
    SDCP_MS_FILE_XFER = 2,
    SDCP_MS_EXPOSURE_TEST = 3,
    SDCP_MS_DEVICES_TEST = 4,
};

/* PrintInfo.Status sub-status (spec). */
enum {
    SDCP_PS_IDLE = 0,
    SDCP_PS_HOMING = 1,
    SDCP_PS_DROPPING = 2,
    SDCP_PS_EXPOSURING = 3,
    SDCP_PS_LIFTING = 4,
    SDCP_PS_PAUSING = 5,
    SDCP_PS_PAUSED = 6,
    SDCP_PS_STOPPING = 7,
    SDCP_PS_STOPPED = 8,
    SDCP_PS_COMPLETE = 9,
    SDCP_PS_FILE_CHECK = 10,
};

bool sdcp_is(const pp_printer_t *pr)
{
    return pr && strncmp(pr->host, SDCP_PREFIX, 5) == 0 && pr->host[5];
}

static const char *sdcp_ip(const pp_printer_t *pr)
{
    return pr->host + 5;   /* skip "sdcp:" */
}

/* ---------- UDP discovery (M99999 → :3000) ---------- */

typedef struct {
    char mainboard_id[40];
    char brand_id[40];
    char name[40];
    char machine[40];
    char firmware[24];
    char proto[16];
    int  release_film_max;   /* from discovery Attributes; -1 if absent */
} sdcp_disc_t;

/* ReleaseFilmMax comes from sdcp/attributes (Cmd 1), not status — cache per MainboardID. */
#define SDCP_FILM_CACHE_N 4
typedef struct {
    char mid[40];
    int  film_max;
} sdcp_film_cache_t;
static sdcp_film_cache_t s_film_cache[SDCP_FILM_CACHE_N];

static int sdcp_film_max_get(const char *mid)
{
    if (!mid || !mid[0]) return -1;
    for (int i = 0; i < SDCP_FILM_CACHE_N; i++) {
        if (s_film_cache[i].mid[0] && strcmp(s_film_cache[i].mid, mid) == 0)
            return s_film_cache[i].film_max;
    }
    return -1;
}

static void sdcp_film_max_set(const char *mid, int film_max)
{
    if (!mid || !mid[0] || film_max <= 0) return;
    int free_i = -1;
    for (int i = 0; i < SDCP_FILM_CACHE_N; i++) {
        if (s_film_cache[i].mid[0] && strcmp(s_film_cache[i].mid, mid) == 0) {
            s_film_cache[i].film_max = film_max;
            return;
        }
        if (free_i < 0 && !s_film_cache[i].mid[0]) free_i = i;
    }
    int i = (free_i >= 0) ? free_i : 0;
    strlcpy(s_film_cache[i].mid, mid, sizeof(s_film_cache[i].mid));
    s_film_cache[i].film_max = film_max;
}

static void sdcp_apply_film_max(pp_status_t *out, const char *mid)
{
    if (!out) return;
    if (out->release_film_max > 0) {
        sdcp_film_max_set(mid && mid[0] ? mid : out->uuid, out->release_film_max);
        return;
    }
    int cached = sdcp_film_max_get(mid && mid[0] ? mid : out->uuid);
    if (cached > 0) out->release_film_max = cached;
}

static esp_err_t sdcp_discover(const char *ip, sdcp_disc_t *out)
{
    memset(out, 0, sizeof(*out));
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return ESP_FAIL;

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct timeval tv = { .tv_sec = SDCP_DISC_MS / 1000, .tv_usec = (SDCP_DISC_MS % 1000) * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SDCP_DISC_PORT);
    if (inet_aton(ip, &dest.sin_addr) == 0) {
        /* hostname: resolve */
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM }, *res = NULL;
        if (getaddrinfo(ip, NULL, &hints, &res) != 0 || !res) {
            close(sock);
            return ESP_FAIL;
        }
        dest.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }

    const char *msg = "M99999";
    if (sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        close(sock);
        return ESP_FAIL;
    }

    char buf[1024];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &flen);
    close(sock);
    if (n <= 0) {
        ESP_LOGW(TAG, "discovery timeout from %s", ip);
        return ESP_ERR_TIMEOUT;
    }
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return ESP_FAIL;

    const cJSON *id = cJSON_GetObjectItem(root, "Id");
    if (cJSON_IsString(id) && id->valuestring)
        strlcpy(out->brand_id, id->valuestring, sizeof(out->brand_id));

    const cJSON *data = cJSON_GetObjectItem(root, "Data");
    /* Some firmwares nest under Data.Attributes */
    const cJSON *attrs = data;
    if (cJSON_IsObject(data)) {
        const cJSON *a = cJSON_GetObjectItem(data, "Attributes");
        if (cJSON_IsObject(a)) attrs = a;
    }
    if (cJSON_IsObject(attrs)) {
        const cJSON *v;
        if ((v = cJSON_GetObjectItem(attrs, "MainboardID")) && cJSON_IsString(v))
            strlcpy(out->mainboard_id, v->valuestring, sizeof(out->mainboard_id));
        if ((v = cJSON_GetObjectItem(attrs, "Name")) && cJSON_IsString(v))
            strlcpy(out->name, v->valuestring, sizeof(out->name));
        if ((v = cJSON_GetObjectItem(attrs, "MachineName")) && cJSON_IsString(v))
            strlcpy(out->machine, v->valuestring, sizeof(out->machine));
        if ((v = cJSON_GetObjectItem(attrs, "FirmwareVersion")) && cJSON_IsString(v))
            strlcpy(out->firmware, v->valuestring, sizeof(out->firmware));
        if ((v = cJSON_GetObjectItem(attrs, "ProtocolVersion")) && cJSON_IsString(v))
            strlcpy(out->proto, v->valuestring, sizeof(out->proto));
        if (!out->brand_id[0] && (v = cJSON_GetObjectItem(attrs, "Id")) && cJSON_IsString(v))
            strlcpy(out->brand_id, v->valuestring, sizeof(out->brand_id));
        out->release_film_max = -1;
        if ((v = cJSON_GetObjectItem(attrs, "ReleaseFilmMax")) && cJSON_IsNumber(v) && v->valueint > 0) {
            out->release_film_max = v->valueint;
            sdcp_film_max_set(out->mainboard_id, out->release_film_max);
        }
    }
    cJSON_Delete(root);

    if (!out->mainboard_id[0]) {
        ESP_LOGW(TAG, "discovery missing MainboardID");
        return ESP_FAIL;
    }
    /* v1 is WebSocket V3 only */
    if (out->proto[0] && strncmp(out->proto, "V3", 2) != 0) {
        ESP_LOGW(TAG, "unsupported ProtocolVersion %s (need V3)", out->proto);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

/* ---------- attention dialog (Prusa-style banner) ---------- */

typedef struct {
    char mid[40];
    int  dialog_id;
    char title[32];
    char text[160];
    bool is_error;
    bool active;
    bool dismissed;
} sdcp_attn_t;

static sdcp_attn_t s_attn;

static const char *sdcp_print_error_text(int n)
{
    switch (n) {
    case 1: return "File MD5 check failed";
    case 2: return "File read failed";
    case 3: return "Resolution mismatch";
    case 4: return "Format mismatch";
    case 5: return "Machine model mismatch";
    default: return "Print error";
    }
}

static const char *sdcp_xfer_error_text(int n)
{
    switch (n) {
    case 1: return "File transfer MD5 check failed";
    case 2: return "File format is incorrect";
    default: return "Printer error";
    }
}

static int sdcp_json_intish(const cJSON *v, int fallback)
{
    if (!v) return fallback;
    if (cJSON_IsNumber(v)) return v->valueint;
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0])
        return atoi(v->valuestring);
    return fallback;
}

static void sdcp_attn_remember(const char *mid, int dialog_id,
                               const char *title, const char *text, bool is_error)
{
    if (!dialog_id) return;
    if (s_attn.dismissed && s_attn.dialog_id == dialog_id &&
        mid && mid[0] && strcmp(s_attn.mid, mid) == 0)
        return;
    if (s_attn.dialog_id != dialog_id) s_attn.dismissed = false;
    if (mid && mid[0]) strlcpy(s_attn.mid, mid, sizeof(s_attn.mid));
    s_attn.dialog_id = dialog_id;
    strlcpy(s_attn.title, title && title[0] ? title : tr(STR_ATTENTION), sizeof(s_attn.title));
    strlcpy(s_attn.text, text ? text : "", sizeof(s_attn.text));
    s_attn.is_error = is_error;
    s_attn.active = true;
}

static void sdcp_attn_apply(pp_status_t *out, int dialog_id,
                            const char *title, const char *text, bool is_error)
{
    if (!out || !dialog_id) return;
    if (s_attn.dismissed && s_attn.dialog_id == dialog_id) return;

    out->dialog_id = dialog_id;
    strlcpy(out->dialog_title, title && title[0] ? title : tr(STR_ATTENTION),
            sizeof(out->dialog_title));
    strlcpy(out->dialog_text, text ? text : "", sizeof(out->dialog_text));
    strlcpy(out->dialog_btns[0], tr(STR_OK), sizeof(out->dialog_btns[0]));
    out->dialog_btn_count = 1;
    if (is_error) strlcpy(out->state, "ATTENTION", sizeof(out->state));

    sdcp_attn_remember(out->uuid[0] ? out->uuid : s_attn.mid, dialog_id,
                       out->dialog_title, out->dialog_text, is_error);
}

static void sdcp_attn_merge_pending(pp_status_t *out, const char *mid)
{
    if (!out || out->dialog_id || !s_attn.active || s_attn.dismissed) return;
    if (mid && mid[0] && s_attn.mid[0] && strcmp(s_attn.mid, mid) != 0) return;
    sdcp_attn_apply(out, s_attn.dialog_id, s_attn.title, s_attn.text, s_attn.is_error);
}

esp_err_t sdcp_dialog_dismiss(int dialog_id)
{
    if (!dialog_id) return ESP_ERR_INVALID_ARG;
    if (dialog_id >= SDCP_DID_PRINT) {
        s_attn.dialog_id = dialog_id;
        s_attn.dismissed = true;
        ESP_LOGI(TAG, "attention dismissed id=%d", dialog_id);
    }
    return ESP_OK;
}

/* ---------- status JSON → pp_status_t ---------- */

static int machine_primary(const cJSON *cur)
{
    /* CurrentStatus is an array; prefer PRINTING, else first entry. */
    if (!cJSON_IsArray(cur) || cJSON_GetArraySize(cur) < 1) return SDCP_MS_IDLE;
    int best = SDCP_MS_IDLE;
    bool have = false;
    const cJSON *e;
    cJSON_ArrayForEach(e, cur) {
        if (!cJSON_IsNumber(e)) continue;
        int v = e->valueint;
        if (!have || v == SDCP_MS_PRINTING) { best = v; have = true; }
        if (v == SDCP_MS_PRINTING) break;
    }
    return have ? best : SDCP_MS_IDLE;
}

static void map_status(const cJSON *status, pp_status_t *out, const char *ip)
{
    const cJSON *ms = cJSON_GetObjectItem(status, "CurrentStatus");
    int machine = machine_primary(ms);
    const cJSON *pi = cJSON_GetObjectItem(status, "PrintInfo");
    int print_st = 0, err = 0, cur_layer = -1, tot_layer = -1;
    int64_t cur_ticks = -1, tot_ticks = -1;
    const char *fname = NULL;
    const char *task_id = NULL;

    if (cJSON_IsObject(pi)) {
        const cJSON *v;
        if ((v = cJSON_GetObjectItem(pi, "Status")) && cJSON_IsNumber(v)) print_st = v->valueint;
        if ((v = cJSON_GetObjectItem(pi, "ErrorNumber")) && cJSON_IsNumber(v)) err = v->valueint;
        if ((v = cJSON_GetObjectItem(pi, "CurrentLayer")) && cJSON_IsNumber(v)) cur_layer = v->valueint;
        if ((v = cJSON_GetObjectItem(pi, "TotalLayer")) && cJSON_IsNumber(v)) tot_layer = v->valueint;
        if ((v = cJSON_GetObjectItem(pi, "CurrentTicks")) && cJSON_IsNumber(v)) cur_ticks = (int64_t)v->valuedouble;
        if ((v = cJSON_GetObjectItem(pi, "TotalTicks")) && cJSON_IsNumber(v)) tot_ticks = (int64_t)v->valuedouble;
        if ((v = cJSON_GetObjectItem(pi, "Filename")) && cJSON_IsString(v)) fname = v->valuestring;
        if ((v = cJSON_GetObjectItem(pi, "TaskId")) && cJSON_IsString(v) && v->valuestring[0])
            task_id = v->valuestring;
    }

    /* Coarse UI state vocabulary. */
    const char *state = "IDLE";
    bool has_job = false;
    switch (machine) {
    case SDCP_MS_FILE_XFER:
        state = "BUSY";
        break;
    case SDCP_MS_EXPOSURE_TEST:
    case SDCP_MS_DEVICES_TEST:
        state = "PREPARING";
        break;
    case SDCP_MS_PRINTING:
        has_job = true;
        if (print_st == SDCP_PS_PAUSED || print_st == SDCP_PS_PAUSING) state = "PAUSED";
        else if (print_st == SDCP_PS_STOPPED || print_st == SDCP_PS_STOPPING) state = "STOPPED";
        else if (print_st == SDCP_PS_COMPLETE) { state = "FINISHED"; }
        else if (print_st == SDCP_PS_FILE_CHECK) state = "PREPARING";
        else state = "PRINTING";
        break;
    default:
        if (print_st == SDCP_PS_COMPLETE) { state = "FINISHED"; has_job = true; }
        else if (print_st == SDCP_PS_STOPPED) { state = "STOPPED"; has_job = true; }
        else state = "READY";
        break;
    }
    if (err != 0) {
        sdcp_attn_apply(out, SDCP_DID_PRINT + err, tr(STR_ATTENTION),
                        sdcp_print_error_text(err), true);
        if (!out->dialog_id)
            strlcpy(out->state, state, sizeof(out->state));  /* dismissed while error persists */
    } else {
        strlcpy(out->state, state, sizeof(out->state));
        /* Print error cleared on printer — allow the same code to reappear later. */
        if (s_attn.active && s_attn.dialog_id >= SDCP_DID_PRINT &&
            s_attn.dialog_id < SDCP_DID_XFER) {
            s_attn.active = false;
            s_attn.dismissed = false;
            s_attn.dialog_id = 0;
        }
    }
    out->has_job = has_job;

    out->current_layer = cur_layer;
    out->total_layer = tot_layer;
    if (tot_layer > 0 && cur_layer >= 0)
        out->progress = (cur_layer * 100.0f) / (float)tot_layer;
    else if (tot_ticks > 0 && cur_ticks >= 0)
        out->progress = (cur_ticks * 100.0f) / (float)tot_ticks;
    else
        out->progress = 0;

    if (cur_ticks >= 0) out->time_printing = (int)(cur_ticks / 1000);
    else out->time_printing = 0;
    if (tot_ticks > 0 && cur_ticks >= 0 && tot_ticks >= cur_ticks)
        out->time_remaining = (int)((tot_ticks - cur_ticks) / 1000);
    else
        out->time_remaining = -1;

    if (fname && fname[0]) {
        const char *base = strrchr(fname, '/');
        strlcpy(out->job_name, base ? base + 1 : fname, sizeof(out->job_name));
    }

    /* Cover preview: TaskId → history_image; otherwise shared /thumb.jpg while a job is active.
     * Elegoo/Chitu V3 often serve the current cover at /thumb.jpg. */
    if (ip && ip[0] && (has_job || (task_id && task_id[0]))) {
        if (task_id && task_id[0])
            snprintf(out->job_thumb, sizeof(out->job_thumb),
                     "http://%s/board-resource/history_image/%s.png", ip, task_id);
        else
            snprintf(out->job_thumb, sizeof(out->job_thumb), "http://%s/thumb.jpg", ip);
    }

    const cJSON *t;
    if ((t = cJSON_GetObjectItem(status, "TempOfUVLED")) && cJSON_IsNumber(t))
        out->temp_nozzle = (float)t->valuedouble;
    if ((t = cJSON_GetObjectItem(status, "TempOfBox")) && cJSON_IsNumber(t))
        out->temp_bed = (float)t->valuedouble;
    if ((t = cJSON_GetObjectItem(status, "TempTargetBox")) && cJSON_IsNumber(t))
        out->target_bed = (float)t->valuedouble;
    if ((t = cJSON_GetObjectItem(status, "ReleaseFilm")) && cJSON_IsNumber(t))
        out->release_film = t->valueint;
    /* Some firmwares may echo max in status; normally it arrives via attributes. */
    if ((t = cJSON_GetObjectItem(status, "ReleaseFilmMax")) && cJSON_IsNumber(t) && t->valueint > 0)
        out->release_film_max = t->valueint;
}

/* ---------- WebSocket one-shot RPC (status / list / print) ---------- */

typedef enum {
    SDCP_OP_STATUS = 0,
    SDCP_OP_LIST,
    SDCP_OP_PRINT,
    SDCP_OP_SIMPLE,   /* pause/stop/resume — Cmd + empty Data, wait Ack */
} sdcp_op_t;

typedef struct {
    SemaphoreHandle_t done;
    esp_websocket_client_handle_t client;
    char *acc;
    int   acc_len;
    bool  ok;
    bool  sent_cmd;
    sdcp_op_t op;
    char  mainboard_id[40];
    char  brand_id[40];
    char  request_id[33];
    char  ip[48];
    /* STATUS */
    pp_status_t *out_status;
    /* LIST */
    pp_file_t *files;
    int max_files;
    int *file_count;
    char list_url[48];
    /* PRINT */
    char print_file[160];
    /* PRINT / SIMPLE */
    int  ack;         /* -1 pending, else Ack code */
    int  simple_cmd;  /* Cmd number for SDCP_OP_SIMPLE */
} sdcp_ws_ctx_t;

static void sdcp_make_ids(char *rid, size_t rn, uint32_t *ts_out)
{
    uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    if (ts_out) *ts_out = ts;
    snprintf(rid, rn, "%08lx%08lx",
             (unsigned long)(esp_timer_get_time() & 0xffffffffu),
             (unsigned long)(ts ^ 0xA5A5A5A5u));
}

static void sdcp_parse_file_list(sdcp_ws_ctx_t *ctx, const cJSON *inner)
{
    if (!ctx->files || !ctx->file_count) return;
    const cJSON *fl = cJSON_GetObjectItem(inner, "FileList");
    if (!cJSON_IsArray(fl)) return;
    const cJSON *it;
    cJSON_ArrayForEach(it, fl) {
        if (*ctx->file_count >= ctx->max_files) break;
        const char *name = NULL;
        int type = 1;   /* default file */
        int64_t used = -1;
        if (cJSON_IsString(it)) {
            name = it->valuestring;
        } else if (cJSON_IsObject(it)) {
            const cJSON *v;
            if ((v = cJSON_GetObjectItem(it, "name")) && cJSON_IsString(v)) name = v->valuestring;
            else if ((v = cJSON_GetObjectItem(it, "Name")) && cJSON_IsString(v)) name = v->valuestring;
            if ((v = cJSON_GetObjectItem(it, "type")) && cJSON_IsNumber(v)) type = v->valueint;
            if ((v = cJSON_GetObjectItem(it, "usedSize")) && cJSON_IsNumber(v)) used = (int64_t)v->valuedouble;
            else if ((v = cJSON_GetObjectItem(it, "FileSize")) && cJSON_IsNumber(v)) used = (int64_t)v->valuedouble;
        }
        if (!name || !name[0]) continue;
        if (type == 0) continue;   /* folder — UI only shows printable files */

        pp_file_t *f = &ctx->files[*ctx->file_count];
        memset(f, 0, sizeof(*f));
        strlcpy(f->path, name, sizeof(f->path));
        const char *base = strrchr(name, '/');
        strlcpy(f->display, base ? base + 1 : name, sizeof(f->display));
        f->is_print = true;
        f->is_folder = false;
        if (used >= 0) {
            if (used < 1024) snprintf(f->meta, sizeof(f->meta), "%lld B", (long long)used);
            else if (used < 1024 * 1024) snprintf(f->meta, sizeof(f->meta), "%.1f KB", (float)used / 1024.0f);
            else snprintf(f->meta, sizeof(f->meta), "%.1f MB", (float)used / (1024.0f * 1024.0f));
        }
        (*ctx->file_count)++;
    }
    ctx->ok = true;
    xSemaphoreGive(ctx->done);
}

static void sdcp_handle_push(sdcp_ws_ctx_t *ctx, const char *tstr, const cJSON *root)
{
    const cJSON *d = cJSON_GetObjectItem(root, "Data");
    const cJSON *inner = cJSON_IsObject(d) ? cJSON_GetObjectItem(d, "Data") : NULL;
    if (!cJSON_IsObject(inner)) return;

    const char *mid = ctx->mainboard_id;
    const cJSON *midj = cJSON_IsObject(d) ? cJSON_GetObjectItem(d, "MainboardID") : NULL;
    if (cJSON_IsString(midj) && midj->valuestring && midj->valuestring[0])
        mid = midj->valuestring;

    if (strstr(tstr, "sdcp/error/") == tstr) {
        int code = sdcp_json_intish(cJSON_GetObjectItem(inner, "ErrorCode"), 0);
        if (code <= 0) return;
        int did = SDCP_DID_XFER + code;
        const char *text = sdcp_xfer_error_text(code);
        sdcp_attn_remember(mid, did, tr(STR_ATTENTION), text, true);
        if (ctx->op == SDCP_OP_STATUS && ctx->out_status)
            sdcp_attn_apply(ctx->out_status, did, tr(STR_ATTENTION), text, true);
        ESP_LOGW(TAG, "sdcp/error ErrorCode=%d", code);
        return;
    }

    if (strstr(tstr, "sdcp/notice/") == tstr) {
        int type = sdcp_json_intish(cJSON_GetObjectItem(inner, "Type"), 0);
        /* Type 1 = history sync success — not worth an attention banner. */
        if (type == 1) return;
        const cJSON *msg = cJSON_GetObjectItem(inner, "Message");
        char text[160] = {0};
        if (cJSON_IsString(msg) && msg->valuestring)
            strlcpy(text, msg->valuestring, sizeof(text));
        else if (cJSON_IsObject(msg) || cJSON_IsArray(msg)) {
            char *raw = cJSON_PrintUnformatted(msg);
            if (raw) { strlcpy(text, raw, sizeof(text)); free(raw); }
        }
        if (!text[0]) snprintf(text, sizeof(text), "Notification (type %d)", type);
        int did = SDCP_DID_NOTICE + (type > 0 ? type : 1);
        sdcp_attn_remember(mid, did, tr(STR_ATTENTION), text, false);
        if (ctx->op == SDCP_OP_STATUS && ctx->out_status)
            sdcp_attn_apply(ctx->out_status, did, tr(STR_ATTENTION), text, false);
        ESP_LOGI(TAG, "sdcp/notice type=%d", type);
    }
}

static void sdcp_handle_attributes(sdcp_ws_ctx_t *ctx, const cJSON *root)
{
    const cJSON *attrs = cJSON_GetObjectItem(root, "Attributes");
    if (!cJSON_IsObject(attrs)) {
        const cJSON *d = cJSON_GetObjectItem(root, "Data");
        if (cJSON_IsObject(d)) attrs = cJSON_GetObjectItem(d, "Attributes");
    }
    if (!cJSON_IsObject(attrs)) return;

    const char *mid = ctx->mainboard_id;
    const cJSON *midj = cJSON_GetObjectItem(root, "MainboardID");
    if ((!midj || !cJSON_IsString(midj)) && cJSON_IsObject(cJSON_GetObjectItem(root, "Data")))
        midj = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "Data"), "MainboardID");
    if (cJSON_IsString(midj) && midj->valuestring && midj->valuestring[0])
        mid = midj->valuestring;

    const cJSON *v = cJSON_GetObjectItem(attrs, "ReleaseFilmMax");
    int film_max = sdcp_json_intish(v, 0);
    if (film_max > 0) {
        sdcp_film_max_set(mid, film_max);
        if (ctx->out_status) ctx->out_status->release_film_max = film_max;
        ESP_LOGI(TAG, "ReleaseFilmMax=%d", film_max);
    }
}

static void sdcp_try_parse_msg(sdcp_ws_ctx_t *ctx, const char *data, int len)
{
    if (!data || len <= 0) return;
    if (len == 4 && (strncmp(data, "pong", 4) == 0 || strncmp(data, "ping", 4) == 0)) return;

    cJSON *root = cJSON_Parse(data);
    if (!root) return;

    const cJSON *topic = cJSON_GetObjectItem(root, "Topic");
    const char *tstr = (cJSON_IsString(topic) && topic->valuestring) ? topic->valuestring : "";

    /* Proactive push topics can arrive on any WS session. */
    if (strstr(tstr, "sdcp/error/") == tstr || strstr(tstr, "sdcp/notice/") == tstr)
        sdcp_handle_push(ctx, tstr, root);
    if (strstr(tstr, "sdcp/attributes/") == tstr)
        sdcp_handle_attributes(ctx, root);

    if (ctx->op == SDCP_OP_STATUS) {
        if (strstr(tstr, "sdcp/status/") == tstr) {
            const cJSON *st = cJSON_GetObjectItem(root, "Status");
            if (cJSON_IsObject(st) && ctx->out_status) {
                map_status(st, ctx->out_status, ctx->ip);
                sdcp_apply_film_max(ctx->out_status, ctx->mainboard_id);
                ctx->ok = true;
                xSemaphoreGive(ctx->done);
            }
        } else if (strstr(tstr, "sdcp/response/") == tstr) {
            const cJSON *d = cJSON_GetObjectItem(root, "Data");
            const cJSON *inner = cJSON_IsObject(d) ? cJSON_GetObjectItem(d, "Data") : NULL;
            const cJSON *ack = cJSON_IsObject(inner) ? cJSON_GetObjectItem(inner, "Ack") : NULL;
            if (cJSON_IsNumber(ack) && ack->valueint != 0)
                ESP_LOGW(TAG, "status Cmd Ack=%d", ack->valueint);
        }
        if (!ctx->ok) {
            const cJSON *st = cJSON_GetObjectItem(root, "Status");
            if (cJSON_IsObject(st) && ctx->out_status) {
                map_status(st, ctx->out_status, ctx->ip);
                sdcp_apply_film_max(ctx->out_status, ctx->mainboard_id);
                ctx->ok = true;
                xSemaphoreGive(ctx->done);
            }
        }
        cJSON_Delete(root);
        return;
    }

    /* LIST / PRINT — wait for response topic (or any packet carrying Ack/FileList). */
    const cJSON *d = cJSON_GetObjectItem(root, "Data");
    const cJSON *inner = cJSON_IsObject(d) ? cJSON_GetObjectItem(d, "Data") : NULL;
    if (!cJSON_IsObject(inner)) {
        /* Some firmwares put FileList/Ack at Data level. */
        if (cJSON_IsObject(d)) inner = d;
    }

    if (ctx->op == SDCP_OP_LIST && cJSON_IsObject(inner)) {
        if (cJSON_GetObjectItem(inner, "FileList")) {
            sdcp_parse_file_list(ctx, inner);
            cJSON_Delete(root);
            return;
        }
        /* Ack-only success with no FileList → empty directory. */
        const cJSON *ack = cJSON_GetObjectItem(inner, "Ack");
        if (cJSON_IsNumber(ack) && ack->valueint == 0) {
            ctx->ok = true;
            xSemaphoreGive(ctx->done);
            cJSON_Delete(root);
            return;
        }
    }

    if ((ctx->op == SDCP_OP_PRINT || ctx->op == SDCP_OP_SIMPLE) && cJSON_IsObject(inner)) {
        const cJSON *ack = cJSON_GetObjectItem(inner, "Ack");
        if (cJSON_IsNumber(ack)) {
            ctx->ack = ack->valueint;
            if (ctx->ack != 0)
                ESP_LOGW(TAG, "Cmd Ack=%d", ctx->ack);
            ctx->ok = (ctx->ack == 0);
            xSemaphoreGive(ctx->done);
        }
    }
    cJSON_Delete(root);
}

static void sdcp_send_cmd(sdcp_ws_ctx_t *ctx, int cmd)
{
    if (!ctx->client) return;
    char req[384], rid[33];
    const char *id = ctx->brand_id[0] ? ctx->brand_id : ctx->mainboard_id;
    uint32_t ts = 0;
    sdcp_make_ids(rid, sizeof(rid), &ts);
    snprintf(req, sizeof(req),
             "{\"Id\":\"%s\",\"Data\":{\"Cmd\":%d,\"Data\":{},\"RequestID\":\"%s\","
             "\"MainboardID\":\"%s\",\"TimeStamp\":%lu,\"From\":0},"
             "\"Topic\":\"sdcp/request/%s\"}",
             id, cmd, rid, ctx->mainboard_id, (unsigned long)ts, ctx->mainboard_id);
    esp_websocket_client_send_text(ctx->client, req, strlen(req), pdMS_TO_TICKS(2000));
}

static void sdcp_send_request(sdcp_ws_ctx_t *ctx)
{
    if (!ctx->client) return;
    char req[512];
    const char *id = ctx->brand_id[0] ? ctx->brand_id : ctx->mainboard_id;
    uint32_t ts = 0;
    sdcp_make_ids(ctx->request_id, sizeof(ctx->request_id), &ts);

    if (ctx->op == SDCP_OP_STATUS) {
        /* Cmd 0 = status; Cmd 1 = attributes (ReleaseFilmMax lives here). */
        sdcp_send_cmd(ctx, 0);
        if (sdcp_film_max_get(ctx->mainboard_id) <= 0)
            sdcp_send_cmd(ctx, 1);
        ctx->sent_cmd = true;
        return;
    } else if (ctx->op == SDCP_OP_LIST) {
        snprintf(req, sizeof(req),
                 "{\"Id\":\"%s\",\"Data\":{\"Cmd\":258,\"Data\":{\"Url\":\"%s\"},"
                 "\"RequestID\":\"%s\",\"MainboardID\":\"%s\",\"TimeStamp\":%lu,\"From\":0},"
                 "\"Topic\":\"sdcp/request/%s\"}",
                 id, ctx->list_url, ctx->request_id, ctx->mainboard_id,
                 (unsigned long)ts, ctx->mainboard_id);
    } else if (ctx->op == SDCP_OP_SIMPLE) {
        snprintf(req, sizeof(req),
                 "{\"Id\":\"%s\",\"Data\":{\"Cmd\":%d,\"Data\":{},\"RequestID\":\"%s\","
                 "\"MainboardID\":\"%s\",\"TimeStamp\":%lu,\"From\":0},"
                 "\"Topic\":\"sdcp/request/%s\"}",
                 id, ctx->simple_cmd, ctx->request_id, ctx->mainboard_id,
                 (unsigned long)ts, ctx->mainboard_id);
    } else { /* PRINT */
        snprintf(req, sizeof(req),
                 "{\"Id\":\"%s\",\"Data\":{\"Cmd\":128,\"Data\":{\"Filename\":\"%s\",\"StartLayer\":0},"
                 "\"RequestID\":\"%s\",\"MainboardID\":\"%s\",\"TimeStamp\":%lu,\"From\":0},"
                 "\"Topic\":\"sdcp/request/%s\"}",
                 id, ctx->print_file, ctx->request_id, ctx->mainboard_id,
                 (unsigned long)ts, ctx->mainboard_id);
    }
    esp_websocket_client_send_text(ctx->client, req, strlen(req), pdMS_TO_TICKS(2000));
    ctx->sent_cmd = true;
}

static void sdcp_ws_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)base;
    sdcp_ws_ctx_t *ctx = handler_args;
    esp_websocket_event_data_t *e = event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        sdcp_send_request(ctx);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (e->op_code == 0x1 || e->op_code == 0x2) {
            if (!ctx->acc) break;
            if (e->payload_offset == 0) ctx->acc_len = 0;
            int need = e->payload_offset + e->data_len;
            if (need >= SDCP_RX_CAP) break;
            if (e->data_ptr && e->data_len > 0) {
                memcpy(ctx->acc + e->payload_offset, e->data_ptr, e->data_len);
                if (e->payload_offset + e->data_len > ctx->acc_len)
                    ctx->acc_len = e->payload_offset + e->data_len;
            }
            if (e->payload_offset + e->data_len >= e->payload_len && ctx->acc_len > 0) {
                ctx->acc[ctx->acc_len] = '\0';
                sdcp_try_parse_msg(ctx, ctx->acc, ctx->acc_len);
            }
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
    case WEBSOCKET_EVENT_DISCONNECTED:
        if (!ctx->ok) xSemaphoreGive(ctx->done);
        break;
    default:
        break;
    }
}

static esp_err_t sdcp_resolve(const pp_printer_t *pr, sdcp_disc_t *disc, bool want_attrs)
{
    memset(disc, 0, sizeof(*disc));

    /* Cached MainboardID skips UDP on the hot path — but Name/Machine/FW only come from
     * discovery, so refresh attributes when the caller still needs identity. */
    if (pr->uuid[0] && !want_attrs) {
        strlcpy(disc->mainboard_id, pr->uuid, sizeof(disc->mainboard_id));
        return ESP_OK;
    }

    esp_err_t r = sdcp_discover(sdcp_ip(pr), disc);
    if (r == ESP_OK) return ESP_OK;

    /* Discovery failed (timeout etc.) — still usable for WS if we already know the ID. */
    if (pr->uuid[0]) {
        strlcpy(disc->mainboard_id, pr->uuid, sizeof(disc->mainboard_id));
        return ESP_OK;
    }
    return r;
}

static esp_err_t sdcp_ws_rpc(const char *ip, const sdcp_disc_t *disc, sdcp_ws_ctx_t *ctx)
{
    strlcpy(ctx->mainboard_id, disc->mainboard_id, sizeof(ctx->mainboard_id));
    strlcpy(ctx->brand_id, disc->brand_id[0] ? disc->brand_id : disc->mainboard_id, sizeof(ctx->brand_id));
    if (ip) strlcpy(ctx->ip, ip, sizeof(ctx->ip));
    ctx->done = xSemaphoreCreateBinary();
    if (!ctx->done) return ESP_FAIL;
    ctx->acc = heap_caps_malloc(SDCP_RX_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx->acc) ctx->acc = malloc(SDCP_RX_CAP);
    if (!ctx->acc) { vSemaphoreDelete(ctx->done); return ESP_FAIL; }

    char uri[80];
    snprintf(uri, sizeof(uri), "ws://%s:%d/websocket", ip, SDCP_WS_PORT);

    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .network_timeout_ms = 4000,
        .disable_auto_reconnect = true,
        .buffer_size = 4096,
    };
    esp_websocket_client_handle_t cl = esp_websocket_client_init(&cfg);
    if (!cl) {
        free(ctx->acc);
        vSemaphoreDelete(ctx->done);
        return ESP_FAIL;
    }
    ctx->client = cl;
    esp_websocket_register_events(cl, WEBSOCKET_EVENT_ANY, sdcp_ws_event, ctx);
    esp_err_t st = esp_websocket_client_start(cl);
    if (st == ESP_OK) {
        xSemaphoreTake(ctx->done, pdMS_TO_TICKS(SDCP_WAIT_MS));
        /* Only linger for attributes when we still need ReleaseFilmMax — holding the
         * socket longer burns one of the printer's scarce WebSocket slots (~5). */
        if (ctx->op == SDCP_OP_STATUS && ctx->ok &&
            ctx->out_status && ctx->out_status->release_film_max <= 0 &&
            sdcp_film_max_get(ctx->mainboard_id) <= 0) {
            for (int i = 0; i < 10; i++) {
                if (ctx->out_status->release_film_max > 0) break;
                if (sdcp_film_max_get(ctx->mainboard_id) > 0) {
                    sdcp_apply_film_max(ctx->out_status, ctx->mainboard_id);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            if (ctx->out_status)
                sdcp_apply_film_max(ctx->out_status, ctx->mainboard_id);
        } else if (ctx->op == SDCP_OP_STATUS && ctx->ok && ctx->out_status) {
            sdcp_apply_film_max(ctx->out_status, ctx->mainboard_id);
        }
    }

    esp_websocket_client_stop(cl);
    esp_websocket_client_destroy(cl);

    bool ok = ctx->ok;
    free(ctx->acc);
    ctx->acc = NULL;
    vSemaphoreDelete(ctx->done);
    ctx->done = NULL;
    ctx->client = NULL;
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t sdcp_get_status_of(const pp_printer_t *pr, pp_status_t *out)
{
    return sdcp_get_status_ex(pr, out, false);
}

esp_err_t sdcp_get_status_ex(const pp_printer_t *pr, pp_status_t *out, bool refresh_identity)
{
    if (!sdcp_is(pr) || !out) return ESP_FAIL;
    memset(out, 0, sizeof(*out));
    out->current_layer = -1;
    out->total_layer = -1;
    out->release_film = -1;
    out->release_film_max = -1;
    out->time_remaining = -1;
    out->has_control = false;

    sdcp_disc_t disc = {0};
    if (sdcp_resolve(pr, &disc, refresh_identity) != ESP_OK || !disc.mainboard_id[0])
        return ESP_FAIL;

    sdcp_ws_ctx_t ctx = {0};
    ctx.op = SDCP_OP_STATUS;
    ctx.out_status = out;
    esp_err_t r = sdcp_ws_rpc(sdcp_ip(pr), &disc, &ctx);
    /* One quick retry — printers with a 5-slot WS limit often reject a reconnect
     * that races a previous close (TIME_WAIT / "too many client"). */
    if (r != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(400));
        memset(out, 0, sizeof(*out));
        out->current_layer = -1;
        out->total_layer = -1;
        out->release_film = -1;
        out->release_film_max = -1;
        out->time_remaining = -1;
        ctx = (sdcp_ws_ctx_t){0};
        ctx.op = SDCP_OP_STATUS;
        ctx.out_status = out;
        r = sdcp_ws_rpc(sdcp_ip(pr), &disc, &ctx);
    }
    if (r != ESP_OK) return r;

    out->online = true;
    out->is_cloud = false;
    out->has_control = false;
    strlcpy(out->uuid, disc.mainboard_id, sizeof(out->uuid));
    if (disc.machine[0]) strlcpy(out->model, disc.machine, sizeof(out->model));
    else if (disc.name[0]) strlcpy(out->model, disc.name, sizeof(out->model));
    if (disc.firmware[0]) strlcpy(out->firmware, disc.firmware, sizeof(out->firmware));
    strlcpy(out->printer_name, pr->name, sizeof(out->printer_name));
    if (disc.release_film_max > 0)
        sdcp_film_max_set(disc.mainboard_id, disc.release_film_max);
    sdcp_apply_film_max(out, disc.mainboard_id);
    /* Apply any push error/notice captured on this or a prior WS session. */
    if (s_attn.mid[0] == '\0') strlcpy(s_attn.mid, disc.mainboard_id, sizeof(s_attn.mid));
    sdcp_attn_merge_pending(out, disc.mainboard_id);
    return ESP_OK;
}

static esp_err_t sdcp_list_url(const pp_printer_t *pr, const sdcp_disc_t *disc,
                               const char *url, pp_file_t *arr, int max, int *count)
{
    sdcp_ws_ctx_t ctx = {0};
    ctx.op = SDCP_OP_LIST;
    ctx.files = arr;
    ctx.max_files = max;
    ctx.file_count = count;
    strlcpy(ctx.list_url, url, sizeof(ctx.list_url));
    return sdcp_ws_rpc(sdcp_ip(pr), disc, &ctx);
}

esp_err_t sdcp_list(const pp_printer_t *pr, pp_file_t *arr, int max, int *count)
{
    if (!sdcp_is(pr) || !arr || !count || max <= 0) return ESP_FAIL;
    *count = 0;

    sdcp_disc_t disc = {0};
    if (sdcp_resolve(pr, &disc, false) != ESP_OK || !disc.mainboard_id[0]) return ESP_FAIL;

    /* Local first, then USB — both are common on resin machines. */
    esp_err_t r1 = sdcp_list_url(pr, &disc, "/local", arr, max, count);
    if (*count < max) {
        int n0 = *count;
        esp_err_t r2 = sdcp_list_url(pr, &disc, "/usb", arr, max, count);
        /* Prefer success if either root answered. Deduplicate by path. */
        if (r2 == ESP_OK || r1 == ESP_OK) {
            for (int i = n0; i < *count; i++) {
                bool dup = false;
                for (int j = 0; j < n0; j++) {
                    if (strcmp(arr[i].path, arr[j].path) == 0) { dup = true; break; }
                }
                if (dup) {
                    memmove(&arr[i], &arr[i + 1], (size_t)(*count - i - 1) * sizeof(pp_file_t));
                    (*count)--;
                    i--;
                }
            }
            return ESP_OK;
        }
        return (r1 == ESP_OK) ? ESP_OK : r2;
    }
    return r1;
}

esp_err_t sdcp_print(const pp_printer_t *pr, const char *filename)
{
    if (!sdcp_is(pr) || !filename || !filename[0]) return ESP_FAIL;

    sdcp_disc_t disc = {0};
    if (sdcp_resolve(pr, &disc, false) != ESP_OK || !disc.mainboard_id[0]) return ESP_FAIL;

    sdcp_ws_ctx_t ctx = {0};
    ctx.op = SDCP_OP_PRINT;
    ctx.ack = -1;
    strlcpy(ctx.print_file, filename, sizeof(ctx.print_file));
    /* Reject paths that would break the JSON string (quotes / control chars). */
    for (const char *p = ctx.print_file; *p; p++) {
        if (*p == '"' || *p == '\\' || (unsigned char)*p < 0x20) {
            ESP_LOGW(TAG, "print path has unsafe chars");
            return ESP_ERR_INVALID_ARG;
        }
    }
    return sdcp_ws_rpc(sdcp_ip(pr), &disc, &ctx);
}

static esp_err_t sdcp_simple_cmd(const pp_printer_t *pr, int cmd)
{
    if (!sdcp_is(pr)) return ESP_FAIL;
    sdcp_disc_t disc = {0};
    if (sdcp_resolve(pr, &disc, false) != ESP_OK || !disc.mainboard_id[0]) return ESP_FAIL;

    sdcp_ws_ctx_t ctx = {0};
    ctx.op = SDCP_OP_SIMPLE;
    ctx.simple_cmd = cmd;
    ctx.ack = -1;
    return sdcp_ws_rpc(sdcp_ip(pr), &disc, &ctx);
}

esp_err_t sdcp_pause(const pp_printer_t *pr)  { return sdcp_simple_cmd(pr, 129); }
esp_err_t sdcp_stop(const pp_printer_t *pr)   { return sdcp_simple_cmd(pr, 130); }
esp_err_t sdcp_resume(const pp_printer_t *pr) { return sdcp_simple_cmd(pr, 131); }

typedef struct { uint8_t *buf; int len; int cap; } sdcp_http_acc_t;

static esp_err_t sdcp_http_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    sdcp_http_acc_t *a = e->user_data;
    if (!a || !e->data || e->data_len <= 0) return ESP_OK;
    int need = a->len + e->data_len;
    if (need > 512 * 1024) return ESP_ERR_NO_MEM;
    if (need > a->cap) {
        int nc = a->cap ? a->cap * 2 : 4096;
        while (nc < need) nc *= 2;
        uint8_t *nb = realloc(a->buf, nc);
        if (!nb) return ESP_ERR_NO_MEM;
        a->buf = nb;
        a->cap = nc;
    }
    memcpy(a->buf + a->len, e->data, e->data_len);
    a->len += e->data_len;
    return ESP_OK;
}

static esp_err_t sdcp_http_get_url(const char *url, uint8_t **out, int *out_len)
{
    if (!url || !url[0] || !out || !out_len) return ESP_FAIL;
    *out = NULL;
    *out_len = 0;

    sdcp_http_acc_t acc = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 6000,
        .event_handler = sdcp_http_event,
        .user_data = &acc,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    esp_err_t err = esp_http_client_perform(c);
    int sc = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || sc != 200 || !acc.buf || acc.len < 8) {
        free(acc.buf);
        return ESP_FAIL;
    }
    /* Accept PNG or JPEG. */
    bool png = (acc.len >= 8 && memcmp(acc.buf, "\x89PNG\r\n\x1a\n", 8) == 0);
    bool jpg = (acc.len >= 3 && acc.buf[0] == 0xFF && acc.buf[1] == 0xD8);
    if (!png && !jpg) {
        free(acc.buf);
        return ESP_FAIL;
    }
    *out = acc.buf;
    *out_len = acc.len;
    return ESP_OK;
}

static void sdcp_abs_url(const char *ip, const char *path_or_url, char *out, size_t n)
{
    if (!path_or_url || !path_or_url[0]) { out[0] = '\0'; return; }
    if (strncmp(path_or_url, "http://", 7) == 0 || strncmp(path_or_url, "https://", 8) == 0) {
        strlcpy(out, path_or_url, n);
        return;
    }
    if (path_or_url[0] == '/')
        snprintf(out, n, "http://%s%s", ip, path_or_url);
    else
        snprintf(out, n, "http://%s/%s", ip, path_or_url);
}

esp_err_t sdcp_fetch_thumb(const pp_printer_t *pr, const char *ref, uint8_t **out, int *out_len)
{
    if (!sdcp_is(pr) || !ref || !ref[0] || !out || !out_len) return ESP_FAIL;
    *out = NULL;
    *out_len = 0;

    const char *ip = sdcp_ip(pr);
    char url[192];
    sdcp_abs_url(ip, ref, url, sizeof(url));
    if (sdcp_http_get_url(url, out, out_len) == ESP_OK) return ESP_OK;

    /* Fallbacks used by Elegoo/Chitu V3 firmwares. */
    char alt[96];
    snprintf(alt, sizeof(alt), "http://%s/thumb.jpg", ip);
    if (strcmp(url, alt) != 0 && sdcp_http_get_url(alt, out, out_len) == ESP_OK) return ESP_OK;
    snprintf(alt, sizeof(alt), "http://%s/thumb.png", ip);
    if (sdcp_http_get_url(alt, out, out_len) == ESP_OK) return ESP_OK;
    return ESP_FAIL;
}

