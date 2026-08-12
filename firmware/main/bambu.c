/* Prusa-Touch — Bambu Lab LAN backend. See bambu.h.
 *
 * Clean-room implementation from the public reverse-engineered protocol (OpenBambuAPI,
 * ha-bambulab/pybambu). MQTT over TLS to the printer's own broker; the printer pushes a JSON
 * status report, and we publish small JSON command objects back. */
#include "bambu.h"
#include "bambu_cloud.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_tls.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "bambu";

#define BAMBU_RX_BUF   4096          /* esp-mqtt internal RX buffer (internal RAM)            */
#define BAMBU_ACC_CAP  16384         /* PSRAM accumulator for a fragmented report payload    */
#define BAMBU_WAIT_MS  6000          /* status fetch: wait for the printer's report          */
#define BAMBU_PUB_MS   1500          /* command: wait for connect + publish to flush         */

static esp_err_t tls_write_all(esp_tls_t *tls, const void *data, size_t n);
static esp_err_t tls_read_all(esp_tls_t *tls, void *data, size_t n);

bool bambu_is(const pp_printer_t *pr)
{
    return pr && (strncmp(pr->host, "bambu:", 6) == 0 || strncmp(pr->host, "bambucloud:", 11) == 0);
}
static bool bambu_is_cloud(const pp_printer_t *pr) { return strncmp(pr->host, "bambucloud:", 11) == 0; }

/* Pick broker URI + MQTT credentials for a LAN or cloud Bambu printer.
 * Returns false if it's a cloud printer but the account isn't signed in. */
static bool session_params(const pp_printer_t *pr, char *uri, size_t un,
                           const char **user, const char **pass)
{
    if (bambu_is_cloud(pr)) {
        if (!bambu_cloud_is_authed()) return false;
        snprintf(uri, un, "mqtts://us.mqtt.bambulab.com:8883");
        *user = bambu_cloud_mqtt_user();
        *pass = bambu_cloud_token();
    } else {
        snprintf(uri, un, "mqtts://%s:8883", pr->host + 6);   /* skip "bambu:" */
        *user = "bblp";
        *pass = pr->api_key;
    }
    return true;
}

/* ---------- report parsing (the printer -> us JSON) ---------- */

/* Map Bambu's gcode_state to our coarse state[] vocabulary. */
static const char *map_state(const char *gs)
{
    if (!gs) return "IDLE";
    if (!strcmp(gs, "RUNNING"))  return "PRINTING";
    if (!strcmp(gs, "PAUSE"))    return "PAUSED";
    if (!strcmp(gs, "FINISH"))   return "FINISHED";
    if (!strcmp(gs, "FAILED"))   return "ERROR";
    /* IDLE / PREPARE / SLICING / INIT / UNKNOWN */
    return "IDLE";
}

static double num(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : 0.0;
}

/* Parse one report payload into *out. Returns true if it carried a usable status (a print
 * object with at least the nozzle temperature, which every full pushall response includes). */
static bool parse_report(const char *json, int len, pp_status_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;
    cJSON *p = cJSON_GetObjectItem(root, "print");
    if (!cJSON_IsObject(p) || !cJSON_HasObjectItem(p, "nozzle_temper")) { cJSON_Delete(root); return false; }

    out->temp_nozzle   = (float)num(p, "nozzle_temper");
    out->target_nozzle = (float)num(p, "nozzle_target_temper");
    out->temp_bed      = (float)num(p, "bed_temper");
    out->target_bed    = (float)num(p, "bed_target_temper");
    out->progress      = (float)num(p, "mc_percent");
    out->time_remaining = (int)num(p, "mc_remaining_time") * 60;   /* minutes -> seconds */

    const cJSON *gs = cJSON_GetObjectItem(p, "gcode_state");
    const char *state = map_state(cJSON_IsString(gs) ? gs->valuestring : NULL);
    strlcpy(out->state, state, sizeof(out->state));
    out->has_job = (!strcmp(state, "PRINTING") || !strcmp(state, "PAUSED"));

    /* Speed: spd_mag is a percentage when present; otherwise leave 100. */
    int spd = (int)num(p, "spd_mag");
    out->speed = spd > 0 ? spd : 100;

    const cJSON *sub = cJSON_GetObjectItem(p, "subtask_name");
    const cJSON *gf  = cJSON_GetObjectItem(p, "gcode_file");
    const char *jn = cJSON_IsString(sub) && sub->valuestring[0] ? sub->valuestring
                   : (cJSON_IsString(gf) ? gf->valuestring : "");
    if (jn && jn[0]) strlcpy(out->job_name, jn, sizeof(out->job_name));
    if (out->has_job && out->job_name[0])
        strlcpy(out->job_thumb, out->job_name, sizeof(out->job_thumb));

    out->has_control = true;   /* pause/stop/gcode all available over MQTT */
    cJSON_Delete(root);
    return true;
}

/* Rough model name from the serial prefix (best-effort; Bambu doesn't put it in the report).
 * Prefixes from https://wiki.bambulab.com/en/general/find-sn */
static const char *model_from_serial(const char *serial)
{
    if (!serial || strlen(serial) < 3) return "Bambu Lab";
    if (!strncmp(serial, "00M", 3)) return "Bambu Lab X1C";
    if (!strncmp(serial, "03W", 3)) return "Bambu Lab X1E";
    if (!strncmp(serial, "20P", 3)) return "Bambu Lab X2D";
    if (!strncmp(serial, "01P", 3)) return "Bambu Lab P1S";
    if (!strncmp(serial, "01S", 3)) return "Bambu Lab P1P";
    if (!strncmp(serial, "22E", 3)) return "Bambu Lab P2S";
    if (!strncmp(serial, "039", 3)) return "Bambu Lab A1";
    if (!strncmp(serial, "030", 3)) return "Bambu Lab A1 mini";
    if (!strncmp(serial, "26A", 3)) return "Bambu Lab A2L";
    return "Bambu Lab";
}

/* ---------- one MQTT session ---------- */

typedef enum { BAMBU_FETCH, BAMBU_PUBLISH } bambu_mode_t;

typedef struct {
    bambu_mode_t mode;
    const char  *serial;
    const char  *payload;     /* PUBLISH mode: the request JSON */
    pp_status_t *out;         /* FETCH mode: filled from the report */
    SemaphoreHandle_t done;
    volatile bool got;        /* FETCH: report parsed; PUBLISH: publish accepted */
    char *acc;                /* PSRAM accumulator for fragmented FETCH payloads */
    int   acc_len;
} bambu_ctx_t;

static void on_mqtt(void *args, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    bambu_ctx_t *c = args;
    esp_mqtt_event_handle_t e = data;
    char topic[96];

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        if (c->mode == BAMBU_FETCH) {
            snprintf(topic, sizeof(topic), "device/%s/report", c->serial);
            esp_mqtt_client_subscribe(e->client, topic, 0);
            /* Force a full state push (the printer otherwise only sends deltas). */
            snprintf(topic, sizeof(topic), "device/%s/request", c->serial);
            esp_mqtt_client_publish(e->client, topic,
                "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}",
                0, 0, 0);
        } else {
            snprintf(topic, sizeof(topic), "device/%s/request", c->serial);
            int mid = esp_mqtt_client_publish(e->client, topic, c->payload, 0, 1, 0);
            c->got = (mid >= 0);
            xSemaphoreGive(c->done);   /* command flushed; we're done */
        }
        break;

    case MQTT_EVENT_DATA:
        if (c->mode != BAMBU_FETCH || !c->acc) break;
        /* esp-mqtt splits a payload larger than BAMBU_RX_BUF across events; accumulate by offset. */
        if (e->current_data_offset == 0) c->acc_len = 0;
        if (c->acc_len + e->data_len < BAMBU_ACC_CAP) {
            memcpy(c->acc + c->acc_len, e->data, e->data_len);
            c->acc_len += e->data_len;
        }
        if (e->current_data_offset + e->data_len >= e->total_data_len) {
            if (parse_report(c->acc, c->acc_len, c->out)) { c->got = true; xSemaphoreGive(c->done); }
            c->acc_len = 0;
        }
        break;

    case MQTT_EVENT_ERROR:
    case MQTT_EVENT_DISCONNECTED:
        xSemaphoreGive(c->done);   /* fail fast instead of waiting out the timeout */
        break;
    default: break;
    }
}

/* Run a single MQTT session against broker `uri` (mqtts://host:8883) with the given credentials. */
static esp_err_t mqtt_session(const char *uri, const char *user, const char *pass,
                              const char *serial, bambu_mode_t mode, const char *payload,
                              pp_status_t *out)
{
    bambu_ctx_t ctx = { .mode = mode, .serial = serial, .payload = payload, .out = out };
    ctx.done = xSemaphoreCreateBinary();
    if (!ctx.done) return ESP_FAIL;
    if (mode == BAMBU_FETCH) {
        ctx.acc = heap_caps_malloc(BAMBU_ACC_CAP, MALLOC_CAP_SPIRAM);
        if (!ctx.acc) { vSemaphoreDelete(ctx.done); return ESP_FAIL; }
    }

    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = uri;
    cfg.broker.verification.skip_cert_common_name_check = true;   /* printer uses a self-signed cert */
    cfg.credentials.username = user;
    cfg.credentials.authentication.password = pass;
    cfg.session.keepalive = 20;
    cfg.session.disable_clean_session = false;
    cfg.network.timeout_ms = 4000;
    cfg.network.disable_auto_reconnect = true;   /* we own the lifecycle, one shot */
    cfg.buffer.size = BAMBU_RX_BUF;
    cfg.buffer.out_size = 2048;

    esp_mqtt_client_handle_t cl = esp_mqtt_client_init(&cfg);
    if (!cl) { if (ctx.acc) free(ctx.acc); vSemaphoreDelete(ctx.done); return ESP_FAIL; }
    esp_mqtt_client_register_event(cl, ESP_EVENT_ANY_ID, on_mqtt, &ctx);

    esp_err_t started = esp_mqtt_client_start(cl);
    if (started == ESP_OK) {
        TickType_t wait = pdMS_TO_TICKS(mode == BAMBU_FETCH ? BAMBU_WAIT_MS : BAMBU_PUB_MS);
        xSemaphoreTake(ctx.done, wait);
    }
    esp_mqtt_client_stop(cl);
    esp_mqtt_client_destroy(cl);

    bool ok = ctx.got;
    if (ctx.acc) free(ctx.acc);
    vSemaphoreDelete(ctx.done);
    return ok ? ESP_OK : ESP_FAIL;
}

/* ---------- public LAN API ---------- */

esp_err_t bambu_get_status_of(const pp_printer_t *pr, pp_status_t *out)
{
    if (!bambu_is(pr) || !pr->uuid[0]) return ESP_FAIL;
    memset(out, 0, sizeof(*out));
    out->current_layer = -1;
    out->total_layer   = -1;
    char uri[64]; const char *user, *pass;
    if (!session_params(pr, uri, sizeof(uri), &user, &pass)) return ESP_FAIL;

    esp_err_t r = mqtt_session(uri, user, pass, pr->uuid, BAMBU_FETCH, NULL, out);
    if (r != ESP_OK) return r;

    out->online = true;
    out->is_cloud = bambu_is_cloud(pr);
    strlcpy(out->printer_name, pr->name, sizeof(out->printer_name));
    strlcpy(out->model, model_from_serial(pr->uuid), sizeof(out->model));
    strlcpy(out->uuid, pr->uuid, sizeof(out->uuid));
    return ESP_OK;
}

static esp_err_t bambu_cmd(const pp_printer_t *pr, const char *payload)
{
    if (!bambu_is(pr) || !pr->uuid[0]) return ESP_FAIL;
    char uri[64]; const char *user, *pass;
    if (!session_params(pr, uri, sizeof(uri), &user, &pass)) return ESP_FAIL;
    return mqtt_session(uri, user, pass, pr->uuid, BAMBU_PUBLISH, payload, NULL);
}

esp_err_t bambu_pause(const pp_printer_t *pr)
{ return bambu_cmd(pr, "{\"print\":{\"sequence_id\":\"1\",\"command\":\"pause\",\"param\":\"\"}}"); }
esp_err_t bambu_resume(const pp_printer_t *pr)
{ return bambu_cmd(pr, "{\"print\":{\"sequence_id\":\"1\",\"command\":\"resume\",\"param\":\"\"}}"); }
esp_err_t bambu_stop(const pp_printer_t *pr)
{ return bambu_cmd(pr, "{\"print\":{\"sequence_id\":\"1\",\"command\":\"stop\",\"param\":\"\"}}"); }

esp_err_t bambu_gcode(const pp_printer_t *pr, const char *gcode)
{
    /* Bambu has no temp command — temps/home/etc. go as raw G-code via gcode_line. */
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"print\":{\"sequence_id\":\"1\",\"command\":\"gcode_line\",\"param\":\"%s\\n\"}}", gcode);
    return bambu_cmd(pr, payload);
}

/* ---------- FTPS (implicit TLS :990) for LAN file list + /image/ thumbs ---------- */

static esp_err_t ftps_write_cmd(esp_tls_t *tls, const char *cmd)
{
    char line[320];
    int n = snprintf(line, sizeof(line), "%s\r\n", cmd);
    if (n <= 0 || n >= (int)sizeof(line)) return ESP_FAIL;
    return tls_write_all(tls, line, (size_t)n);
}

/* Read one FTP reply. Handles multi-line (NNN-... / NNN ...). Returns reply code or -1. */
static int ftps_read_reply(esp_tls_t *tls, char *msg, size_t msg_n)
{
    if (msg && msg_n) msg[0] = '\0';
    char acc[512];
    int acc_len = 0;
    int64_t deadline = esp_timer_get_time() + 10000000LL; /* 10 s */

    while (esp_timer_get_time() < deadline) {
        if (acc_len >= (int)sizeof(acc) - 1) break;
        int r = esp_tls_conn_read(tls, acc + acc_len, sizeof(acc) - 1 - acc_len);
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (r <= 0) break;
        acc_len += r;
        acc[acc_len] = '\0';

        char *start = acc;
        for (;;) {
            char *nl = strchr(start, '\n');
            if (!nl) {
                int keep = (int)strlen(start);
                memmove(acc, start, keep + 1);
                acc_len = keep;
                break;
            }
            *nl = '\0';
            if (nl > start && nl[-1] == '\r') nl[-1] = '\0';
            if (strlen(start) >= 3 &&
                start[0] >= '0' && start[0] <= '9' &&
                start[1] >= '0' && start[1] <= '9' &&
                start[2] >= '0' && start[2] <= '9') {
                int c = (start[0] - '0') * 100 + (start[1] - '0') * 10 + (start[2] - '0');
                if (start[3] == ' ' || start[3] == '\0') {
                    if (msg && msg_n) strlcpy(msg, start, msg_n);
                    return c;
                }
            }
            start = nl + 1;
            if (start >= acc + acc_len) {
                acc_len = 0;
                acc[0] = '\0';
                break;
            }
        }
    }
    return -1;
}

static esp_err_t ftps_cmd(esp_tls_t *tls, const char *cmd, int expect, char *msg, size_t msg_n)
{
    if (ftps_write_cmd(tls, cmd) != ESP_OK) return ESP_FAIL;
    int code = ftps_read_reply(tls, msg, msg_n);
    if (code != expect) {
        const char *shown = (strncmp(cmd, "PASS ", 5) == 0) ? "PASS ****" : cmd;
        ESP_LOGW(TAG, "FTPS '%s' -> %d (want %d)", shown, code, expect);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool ftps_parse_pasv(const char *msg, char *ip, size_t ip_n, int *port)
{
    if (!msg || !ip || !port) return false;
    const char *p = strchr(msg, '(');
    if (!p) return false;
    p++;
    if (p[0] == '|') {
        while (*p == '|') p++;
        *port = atoi(p);
        strlcpy(ip, "", ip_n);
        return *port > 0;
    }
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) return false;
    snprintf(ip, ip_n, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = p1 * 256 + p2;
    return *port > 0;
}

static esp_tls_t *ftps_open_data(esp_tls_t *ctrl, const char *host_ip)
{
    char msg[256], data_ip[32];
    int data_port = 0;
    /* PASV only — EPSV often fails on P1 and costs a round-trip. */
    if (ftps_cmd(ctrl, "PASV", 227, msg, sizeof(msg)) != ESP_OK) return NULL;
    if (!ftps_parse_pasv(msg, data_ip, sizeof(data_ip), &data_port)) return NULL;
    if (!data_ip[0]) strlcpy(data_ip, host_ip, sizeof(data_ip));

    esp_tls_cfg_t cfg = {0};
    cfg.skip_common_name = true;
    cfg.timeout_ms = 12000;
    esp_tls_t *data = esp_tls_init();
    if (!data) return NULL;
    if (esp_tls_conn_new_sync(data_ip, (int)strlen(data_ip), data_port, &cfg, data) != 1) {
        ESP_LOGW(TAG, "FTPS data connect %s:%d failed", data_ip, data_port);
        esp_tls_conn_destroy(data);
        return NULL;
    }
    return data;
}

static esp_err_t ftps_read_all_data(esp_tls_t *data, uint8_t *buf, size_t cap, size_t *got)
{
    size_t n = 0;
    int64_t deadline = esp_timer_get_time() + 20000000LL;
    while (n < cap && esp_timer_get_time() < deadline) {
        int r = esp_tls_conn_read(data, buf + n, cap - n);
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (r <= 0) break;
        n += (size_t)r;
    }
    if (got) *got = n;
    return n > 0 ? ESP_OK : ESP_FAIL;
}

/* RETR path (optional REST offset). Reads up to cap bytes into buf. */
static esp_err_t ftps_retr(esp_tls_t *ctrl, const char *host_ip, const char *path,
                           uint32_t offset, uint8_t *buf, size_t cap, size_t *got)
{
    if (got) *got = 0;
    char msg[256], cmd[200];
    (void)ftps_cmd(ctrl, "TYPE I", 200, msg, sizeof(msg));
    if (offset > 0) {
        snprintf(cmd, sizeof(cmd), "REST %lu", (unsigned long)offset);
        if (ftps_cmd(ctrl, cmd, 350, msg, sizeof(msg)) != ESP_OK) return ESP_FAIL;
    }
    esp_tls_t *data = ftps_open_data(ctrl, host_ip);
    if (!data) return ESP_FAIL;

    snprintf(cmd, sizeof(cmd), "RETR /%s", path[0] == '/' ? path + 1 : path);
    if (ftps_write_cmd(ctrl, cmd) != ESP_OK) {
        esp_tls_conn_destroy(data);
        return ESP_FAIL;
    }
    int code = ftps_read_reply(ctrl, msg, sizeof(msg));
    if (code != 150 && code != 125) {
        ESP_LOGW(TAG, "FTPS %s -> %d", cmd, code);
        esp_tls_conn_destroy(data);
        return ESP_FAIL;
    }
    esp_err_t rr = ftps_read_all_data(data, buf, cap, got);
    esp_tls_conn_destroy(data);
    (void)ftps_read_reply(ctrl, msg, sizeof(msg)); /* 226 */
    return rr;
}

static bool bambu_printable_name(const char *name)
{
    if (!name || !name[0]) return false;
    size_t n = strlen(name);
    if (n < 5) return false;
    /* Only project files (.3mf / .gcode.3mf). Plain .gcode in /cache are
     * extracted plate dumps with misleading dates — not what you print from. */
    const char *dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, ".3mf");
}

static void bambu_add_file(pp_file_t *arr, int max, int *count,
                          const char *dir, const char *name, uint32_t mtime)
{
    if (!arr || !count || *count >= max || !name) return;
    const char *base = name;
    char path[160];
    if (name[0] == '/') {
        strlcpy(path, name + 1, sizeof(path));
        base = strrchr(name, '/');
        base = base ? base + 1 : name;
    } else if (dir && dir[0] && strcmp(dir, "/") != 0) {
        snprintf(path, sizeof(path), "%.64s/%.90s", dir, name);
    } else {
        strlcpy(path, name, sizeof(path));
    }
    if (!bambu_printable_name(base)) return;
    for (int i = 0; i < *count; i++) {
        if (strcmp(arr[i].path, path) == 0) {
            /* Keep the newer stamp if we see the same path twice. */
            if (mtime > arr[i].mtime) arr[i].mtime = mtime;
            return;
        }
    }
    pp_file_t *f = &arr[*count];
    memset(f, 0, sizeof(*f));
    strlcpy(f->path, path, sizeof(f->path));
    strlcpy(f->display, base, sizeof(f->display));
    f->is_print = true;
    f->mtime = mtime;
    f->thumb[0] = '\0';
    if (mtime > 0) {
        time_t ts = (time_t)mtime;
        struct tm tmv;
        gmtime_r(&ts, &tmv);
        strftime(f->meta, sizeof(f->meta), "%b %d, %Y", &tmv);
    } else {
        const char *slash = strrchr(path, '/');
        if (slash) snprintf(f->meta, sizeof(f->meta), "%.*s", (int)(slash - path), path);
        else strlcpy(f->meta, "sd", sizeof(f->meta));
    }
    (*count)++;
}

static int mon_index(const char *mon)
{
    static const char *ms[] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(mon, ms[i], 3) == 0) return i;
    }
    return -1;
}

static bool is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* Civil UTC → unix seconds. Avoid mktime(): on ESP it often fails / needs TZ and
 * then every mtime is 0 so newest-first qsort becomes a no-op (raw FTP order). */
static uint32_t utc_ymdhmm_to_epoch(int year, int mon /*0-11*/, int day, int hh, int mm)
{
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (year < 1970 || year > 2100 || mon < 0 || mon > 11 || day < 1 || day > 31) return 0;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return 0;
    uint32_t days = 0;
    for (int y = 1970; y < year; y++) days += is_leap(y) ? 366u : 365u;
    for (int m = 0; m < mon; m++) {
        days += (uint32_t)mdays[m];
        if (m == 1 && is_leap(year)) days++;
    }
    days += (uint32_t)(day - 1);
    return days * 86400u + (uint32_t)hh * 3600u + (uint32_t)mm * 60u;
}

/* Parse FTP LIST date field ("Jun 18 00:27" or "Jun 17 2025") to epoch seconds. */
static uint32_t ftp_list_mtime(const char *mon, int day, const char *tod)
{
    int mi = mon_index(mon);
    if (mi < 0 || day < 1 || day > 31 || !tod || !tod[0]) return 0;

    int year = 0, hh = 0, mm = 0;
    if (strchr(tod, ':')) {
        /* Recent files: FTP omits the year. Resolve against the device clock
         * with the usual ~6‑month window (same idea as Unix ls / pybambu). */
        time_t now = time(NULL);
        struct tm nowtm;
        if (now <= (time_t)100000 || !gmtime_r(&now, &nowtm)) return 0;
        sscanf(tod, "%d:%d", &hh, &mm);
        year = nowtm.tm_year + 1900;
        int now_ord = nowtm.tm_mon * 31 + nowtm.tm_mday;
        int file_ord = mi * 31 + day;
        int delta_days = file_ord - now_ord;
        if (delta_days > 190) year -= 1;
        else if (delta_days < -190) year += 1;
    } else {
        year = atoi(tod);
        if (year < 1970 || year > 2100) return 0;
    }
    return utc_ymdhmm_to_epoch(year, mi, day, hh, mm);
}

/* Skip N whitespace-separated fields; return remainder (filename, may contain spaces). */
static const char *ftp_skip_fields(const char *line, int n)
{
    const char *p = line;
    for (int i = 0; i < n; i++) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) return NULL;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    return *p ? p : NULL;
}

static bool parse_ftp_list_line(const char *line, char *name, size_t name_n, uint32_t *mtime)
{
    if (!line || line[0] != '-') return false; /* files only (skip dirs/links) */
    char mon[8] = {0}, tod[16] = {0};
    unsigned day = 0;
    /* permissions nlinks user group size month day time-or-year name... */
    if (sscanf(line, "%*s %*s %*s %*s %*s %7s %u %15s", mon, &day, tod) < 3) return false;
    const char *fn = ftp_skip_fields(line, 8);
    if (!fn) return false;
    strlcpy(name, fn, name_n);
    if (mtime) *mtime = ftp_list_mtime(mon, (int)day, tod);
    return name[0] != '\0';
}

static int file_cmp_mtime_desc(const void *a, const void *b)
{
    uint32_t x = ((const pp_file_t *)a)->mtime, y = ((const pp_file_t *)b)->mtime;
    return (x < y) - (x > y);
}

static esp_err_t ftps_list_into(esp_tls_t *ctrl, const char *host_ip, const char *dir,
                                pp_file_t *arr, int max, int *count)
{
    char msg[256];
    (void)ftps_cmd(ctrl, "TYPE A", 200, msg, sizeof(msg));
    esp_tls_t *data = ftps_open_data(ctrl, host_ip);
    if (!data) return ESP_FAIL;

    char cmd[96];
    if (dir && dir[0] && strcmp(dir, "/") != 0)
        snprintf(cmd, sizeof(cmd), "LIST /%s", dir);
    else
        snprintf(cmd, sizeof(cmd), "LIST");

    if (ftps_write_cmd(ctrl, cmd) != ESP_OK) {
        esp_tls_conn_destroy(data);
        return ESP_FAIL;
    }
    int code = ftps_read_reply(ctrl, msg, sizeof(msg));
    if (code != 150 && code != 125) {
        ESP_LOGW(TAG, "FTPS %s -> %d", cmd, code);
        esp_tls_conn_destroy(data);
        return ESP_FAIL;
    }

    char *body = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) body = malloc(32768);
    if (!body) { esp_tls_conn_destroy(data); return ESP_FAIL; }

    size_t blen = 0;
    (void)ftps_read_all_data(data, (uint8_t *)body, 32767, &blen);
    body[blen] = '\0';
    esp_tls_conn_destroy(data);
    (void)ftps_read_reply(ctrl, msg, sizeof(msg));

    const char *dir_key = (dir && strcmp(dir, "/") != 0) ? dir : "";
    char *save = NULL;
    for (char *line = strtok_r(body, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line) continue;
        char name[160];
        uint32_t mtime = 0;
        if (!parse_ftp_list_line(line, name, sizeof(name), &mtime)) continue;
        bambu_add_file(arr, max, count, dir_key, name, mtime);
    }
    free(body);
    return ESP_OK;
}

static esp_tls_t *bambu_ftps_login(const pp_printer_t *pr, const char **out_ip)
{
    if (!pr || !bambu_is(pr) || bambu_is_cloud(pr) || !pr->api_key[0]) return NULL;
    if (strncmp(pr->host, "bambu:", 6) != 0) return NULL;
    const char *ip = pr->host + 6;
    if (!ip[0]) return NULL;
    if (out_ip) *out_ip = ip;

    esp_tls_cfg_t cfg = {0};
    cfg.skip_common_name = true;
    cfg.timeout_ms = 12000;
    esp_tls_t *tls = esp_tls_init();
    if (!tls) return NULL;
    if (esp_tls_conn_new_sync(ip, (int)strlen(ip), 990, &cfg, tls) != 1) {
        ESP_LOGW(TAG, "FTPS connect to %s:990 failed", ip);
        esp_tls_conn_destroy(tls);
        return NULL;
    }
    char msg[256];
    if (ftps_read_reply(tls, msg, sizeof(msg)) != 220) { esp_tls_conn_destroy(tls); return NULL; }
    if (ftps_cmd(tls, "USER bblp", 331, msg, sizeof(msg)) != ESP_OK) { esp_tls_conn_destroy(tls); return NULL; }
    char pass[80];
    snprintf(pass, sizeof(pass), "PASS %s", pr->api_key);
    if (ftps_cmd(tls, pass, 230, msg, sizeof(msg)) != ESP_OK) { esp_tls_conn_destroy(tls); return NULL; }
    (void)ftps_cmd(tls, "PBSZ 0", 200, msg, sizeof(msg));
    (void)ftps_cmd(tls, "PROT P", 200, msg, sizeof(msg));
    return tls;
}

esp_err_t bambu_list(const pp_printer_t *pr, pp_file_t *arr, int max, int *count)
{
    if (count) *count = 0;
    if (!pr || !arr || max <= 0 || !count) return ESP_FAIL;
    const char *ip = NULL;
    esp_tls_t *tls = bambu_ftps_login(pr, &ip);
    if (!tls) return ESP_FAIL;

    /* Printables live on SD root as *.gcode.3mf. /cache is extracted plate .gcode
     * dumps (+ .bbl) — wrong names/dates for the file browser. */
    (void)ftps_list_into(tls, ip, "/", arr, max, count);

    /* Stash 3mf path as thumb ref; fetch pulls Metadata/plate_1_small.png from it. */
    for (int i = 0; i < *count; i++) {
        if (!arr[i].thumb[0])
            strlcpy(arr[i].thumb, arr[i].path, sizeof(arr[i].thumb));
    }

    if (*count > 1) qsort(arr, (size_t)*count, sizeof(pp_file_t), file_cmp_mtime_desc);

    (void)ftps_write_cmd(tls, "QUIT");
    esp_tls_conn_destroy(tls);
    ESP_LOGI(TAG, "FTPS listed %d root .3mf file(s)", *count);
    return ESP_OK;
}

static uint32_t zip_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t zip_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Bambu .gcode.3mf puts Metadata/plate_1_small.png near the start (after a small
 * plate_1.png). Local headers use data descriptors (sizes 0); P1 FTPS rejects REST,
 * so we only pull a prefix and parse forward — never the whole multi‑MB project. */
static bool zip_extract_plate_small(const uint8_t *data, size_t len, uint8_t **out, int *out_len)
{
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!data || len < 64) return false;

    size_t off = 0;
    while (off + 30 <= len) {
        if (!(data[off] == 0x50 && data[off + 1] == 0x4b && data[off + 2] == 0x03 && data[off + 3] == 0x04))
            return false;
        uint16_t flags = zip_u16(data + off + 6);
        uint16_t method = zip_u16(data + off + 8);
        uint32_t comp = zip_u32(data + off + 18);
        uint16_t fn_len = zip_u16(data + off + 26);
        uint16_t ex_len = zip_u16(data + off + 28);
        if (off + 30u + fn_len + ex_len > len) return false;
        const char *name = (const char *)(data + off + 30);
        size_t data_start = off + 30u + fn_len + ex_len;
        size_t next = 0;
        uint32_t payload = comp;

        if (payload == 0 && (flags & 0x8)) {
            bool found = false;
            for (size_t i = data_start; i + 16 <= len; i++) {
                if (!(data[i] == 0x50 && data[i + 1] == 0x4b && data[i + 2] == 0x07 && data[i + 3] == 0x08))
                    continue;
                uint32_t c = zip_u32(data + i + 8);
                if (i == data_start + (size_t)c) {
                    payload = c;
                    next = i + 16;
                    found = true;
                    break;
                }
            }
            if (!found) return false; /* need a larger prefix */
        } else {
            if (data_start + (size_t)payload > len) return false;
            next = data_start + (size_t)payload;
            if (flags & 0x8) {
                if (next + 16 <= len && data[next] == 0x50 && data[next + 1] == 0x4b &&
                    data[next + 2] == 0x07 && data[next + 3] == 0x08)
                    next += 16;
                else if (next + 12 <= len)
                    next += 12;
            }
        }

        if (fn_len == 26 && !strncasecmp(name, "Metadata/plate_1_small.png", 26)) {
            if (method != 0 || payload < 32 || data_start + payload > len) return false;
            if (!(data[data_start] == 0x89 && data[data_start + 1] == 'P')) return false;
            uint8_t *png = heap_caps_malloc(payload, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!png) png = malloc(payload);
            if (!png) return false;
            memcpy(png, data + data_start, payload);
            if (out) *out = png; else free(png);
            if (out_len) *out_len = (int)payload;
            return true;
        }
        off = next;
    }
    return false;
}

/* Download a PNG preview. `path` is either image/<id>.png or a root .3mf. */
esp_err_t bambu_fetch_thumb(const pp_printer_t *pr, const char *path, uint8_t **out, int *out_len)
{
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!pr || !path || !path[0]) return ESP_FAIL;
    const char *rel = path[0] == '/' ? path + 1 : path;

    const char *ip = NULL;
    esp_tls_t *tls = bambu_ftps_login(pr, &ip);
    if (!tls) return ESP_FAIL;

    esp_err_t rc = ESP_FAIL;
    char msg[256], cmd[220];

    if (strncmp(rel, "image/", 6) == 0) {
        snprintf(cmd, sizeof(cmd), "SIZE /%s", rel);
        if (ftps_cmd(tls, cmd, 213, msg, sizeof(msg)) != ESP_OK) goto done;
        unsigned long fsz = strtoul(msg + 4, NULL, 10);
        if (fsz < 32 || fsz > 400ul * 1024ul) goto done;

        uint8_t *buf = heap_caps_malloc(fsz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = malloc(fsz);
        if (!buf) goto done;
        size_t got = 0;
        if (ftps_retr(tls, ip, rel, 0, buf, fsz, &got) != ESP_OK || got < 32) {
            free(buf);
            goto done;
        }
        if (!(got >= 8 && buf[0] == 0x89 && buf[1] == 'P')) {
            free(buf);
            goto done;
        }
        if (out) *out = buf; else free(buf);
        if (out_len) *out_len = (int)got;
        rc = ESP_OK;
        goto done;
    }

    /* Stream a prefix of the .3mf and stop as soon as plate_1_small is parseable.
     * P1 FTPS has no REST, so we always start at byte 0; plate PNGs sit near the front. */
    const size_t prefix_cap = 512 * 1024;
    uint8_t *prefix = heap_caps_malloc(prefix_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!prefix) prefix = malloc(prefix_cap);
    if (!prefix) goto done;

    (void)ftps_cmd(tls, "TYPE I", 200, msg, sizeof(msg));
    esp_tls_t *data = ftps_open_data(tls, ip);
    if (!data) { free(prefix); goto done; }
    snprintf(cmd, sizeof(cmd), "RETR /%s", rel);
    if (ftps_write_cmd(tls, cmd) != ESP_OK) {
        esp_tls_conn_destroy(data);
        free(prefix);
        goto done;
    }
    int code = ftps_read_reply(tls, msg, sizeof(msg));
    if (code != 150 && code != 125) {
        esp_tls_conn_destroy(data);
        free(prefix);
        goto done;
    }

    size_t got = 0;
    size_t last_try = 0;
    uint8_t *png = NULL;
    int png_len = 0;
    int64_t deadline = esp_timer_get_time() + 20000000LL;
    while (got < prefix_cap && esp_timer_get_time() < deadline) {
        int r = esp_tls_conn_read(data, prefix + got, prefix_cap - got);
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (r <= 0) break;
        got += (size_t)r;
        if (got >= 4096 && (got - last_try >= 8192 || got == prefix_cap)) {
            last_try = got;
            if (zip_extract_plate_small(prefix, got, &png, &png_len)) break;
        }
    }
    if (!png && got >= 64)
        (void)zip_extract_plate_small(prefix, got, &png, &png_len);

    esp_tls_conn_destroy(data);
    (void)ftps_read_reply(tls, msg, sizeof(msg)); /* 226 / 426 */
    free(prefix);

    if (!png) goto done;
    if (out) *out = png; else free(png);
    if (out_len) *out_len = png_len;
    rc = ESP_OK;

done:
    (void)ftps_write_cmd(tls, "QUIT");
    esp_tls_conn_destroy(tls);
    if (rc == ESP_OK) ESP_LOGI(TAG, "thumb OK %s (%d bytes)", rel, out_len ? *out_len : 0);
    else ESP_LOGW(TAG, "thumb fail %s", rel);
    return rc;
}

esp_err_t bambu_print(const pp_printer_t *pr, const char *path)
{
    if (!pr || !path || !path[0] || !bambu_is(pr)) return ESP_FAIL;
    const char *rel = (path[0] == '/') ? path + 1 : path;
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;

    const char *ext = strrchr(base, '.');
    bool is_3mf = ext && !strcasecmp(ext, ".3mf");

    cJSON *root = cJSON_CreateObject();
    cJSON *print = cJSON_CreateObject();
    if (!root || !print) { cJSON_Delete(root); return ESP_FAIL; }
    cJSON_AddItemToObject(root, "print", print);
    cJSON_AddStringToObject(print, "sequence_id", "1");

    if (is_3mf) {
        char url[200];
        snprintf(url, sizeof(url), "ftp:///%s", rel);
        cJSON_AddStringToObject(print, "command", "project_file");
        cJSON_AddStringToObject(print, "param", "Metadata/plate_1.gcode");
        cJSON_AddStringToObject(print, "project_id", "0");
        cJSON_AddStringToObject(print, "profile_id", "0");
        cJSON_AddStringToObject(print, "task_id", "0");
        cJSON_AddStringToObject(print, "subtask_id", "0");
        cJSON_AddStringToObject(print, "subtask_name", base);
        cJSON_AddStringToObject(print, "file", base);
        cJSON_AddStringToObject(print, "url", url);
        cJSON_AddStringToObject(print, "md5", "");
        cJSON_AddBoolToObject(print, "timelapse", false);
        cJSON_AddStringToObject(print, "bed_type", "auto");
        cJSON_AddBoolToObject(print, "bed_levelling", true);
        cJSON_AddBoolToObject(print, "flow_cali", false);
        cJSON_AddBoolToObject(print, "vibration_cali", false);
        cJSON_AddBoolToObject(print, "layer_inspect", false);
        cJSON_AddBoolToObject(print, "use_ams", true);
    } else {
        cJSON_AddStringToObject(print, "command", "gcode_file");
        cJSON_AddStringToObject(print, "param", rel);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return ESP_FAIL;
    ESP_LOGI(TAG, "print %s", rel);
    esp_err_t err = bambu_cmd(pr, payload);
    free(payload);
    return err;
}

/* Write exactly n bytes (or fail). */
static esp_err_t tls_write_all(esp_tls_t *tls, const void *data, size_t n)
{
    const uint8_t *p = data;
    size_t left = n;
    while (left) {
        int w = esp_tls_conn_write(tls, p, left);
        if (w == ESP_TLS_ERR_SSL_WANT_READ || w == ESP_TLS_ERR_SSL_WANT_WRITE) continue;
        if (w <= 0) return ESP_FAIL;
        p += w; left -= (size_t)w;
    }
    return ESP_OK;
}

/* Read exactly n bytes (or fail). */
static esp_err_t tls_read_all(esp_tls_t *tls, void *data, size_t n)
{
    uint8_t *p = data;
    size_t left = n;
    while (left) {
        int r = esp_tls_conn_read(tls, p, left);
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) continue;
        if (r <= 0) return ESP_FAIL;
        p += r; left -= (size_t)r;
    }
    return ESP_OK;
}

esp_err_t bambu_fetch_snapshot(const pp_printer_t *pr, uint8_t **out, int *out_len)
{
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    /* LAN only — cloud camera uses a different (TUTK) path we don't implement. */
    if (!pr || !bambu_is(pr) || bambu_is_cloud(pr) || !pr->api_key[0]) return ESP_FAIL;
    if (strncmp(pr->host, "bambu:", 6) != 0) return ESP_FAIL;
    const char *ip = pr->host + 6;
    if (!ip[0]) return ESP_FAIL;

    /* OpenBambuAPI video.md — P1/A1 JPEG stream on TLS :6000. */
    esp_tls_cfg_t cfg = {0};
    cfg.skip_common_name = true;   /* printer self-signed; sdkconfig skips verify */
    cfg.timeout_ms = 8000;

    esp_tls_t *tls = esp_tls_init();
    if (!tls) return ESP_FAIL;
    if (esp_tls_conn_new_sync(ip, (int)strlen(ip), 6000, &cfg, tls) != 1) {
        ESP_LOGW(TAG, "camera TLS connect to %s:6000 failed", ip);
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    /* Auth packet: 16-byte header + 32-byte user + 32-byte pass. */
    uint8_t auth[80];
    memset(auth, 0, sizeof(auth));
    auth[0] = 0x40;                         /* payload size 0x40 (LE) */
    auth[4] = 0x00; auth[5] = 0x30;         /* type 0x3000 (LE) */
    strlcpy((char *)(auth + 16), "bblp", 32);
    strlcpy((char *)(auth + 48), pr->api_key, 32);

    esp_err_t rc = ESP_FAIL;
    if (tls_write_all(tls, auth, sizeof(auth)) != ESP_OK) goto done;

    uint8_t hdr[16];
    if (tls_read_all(tls, hdr, sizeof(hdr)) != ESP_OK) goto done;
    uint32_t payload = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                       ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    /* Sanity: JPEG frames are typically 20 KB..400 KB. */
    if (payload < 1000 || payload > 512 * 1024) {
        ESP_LOGW(TAG, "camera bad payload size %lu", (unsigned long)payload);
        goto done;
    }

    uint8_t *buf = heap_caps_malloc(payload, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(payload);
    if (!buf) goto done;
    if (tls_read_all(tls, buf, payload) != ESP_OK) { free(buf); goto done; }
    if (payload < 2 || buf[0] != 0xFF || buf[1] != 0xD8) {
        ESP_LOGW(TAG, "camera payload is not JPEG");
        free(buf);
        goto done;
    }
    if (out) *out = buf; else free(buf);
    if (out_len) *out_len = (int)payload;
    rc = ESP_OK;

done:
    esp_tls_conn_destroy(tls);
    return rc;
}
