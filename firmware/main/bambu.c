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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "bambu";

#define BAMBU_RX_BUF   4096          /* esp-mqtt internal RX buffer (internal RAM)            */
#define BAMBU_ACC_CAP  16384         /* PSRAM accumulator for a fragmented report payload    */
#define BAMBU_WAIT_MS  6000          /* status fetch: wait for the printer's report          */
#define BAMBU_PUB_MS   1500          /* command: wait for connect + publish to flush         */

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

    out->has_control = true;   /* pause/stop/gcode all available over MQTT */
    cJSON_Delete(root);
    return true;
}

/* Rough model name from the serial prefix (best-effort; Bambu doesn't put it in the report). */
static const char *model_from_serial(const char *serial)
{
    if (!serial || strlen(serial) < 3) return "Bambu Lab";
    if (!strncmp(serial, "00M", 3)) return "Bambu Lab X1";
    if (!strncmp(serial, "00W", 3)) return "Bambu Lab X1C";
    if (!strncmp(serial, "03W", 3)) return "Bambu Lab X1E";
    if (!strncmp(serial, "01S", 3)) return "Bambu Lab P1S";
    if (!strncmp(serial, "01P", 3)) return "Bambu Lab P1P";
    if (!strncmp(serial, "039", 3)) return "Bambu Lab A1";
    if (!strncmp(serial, "030", 3)) return "Bambu Lab A1 mini";
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
    cfg.buffer.out_size = 1024;

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
