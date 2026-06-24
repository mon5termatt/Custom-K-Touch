/* Prusa-Touch — GitHub Releases auto-updater. */
#include "ota_update.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pandaprusa.h"   /* PP_FW_VERSION */
#include "prefs.h"        /* prefs_auto_update() — opt-in gate */
#include "wifi.h"         /* wifi_is_connected() */

static const char *TAG = "ota_update";

typedef struct { char *buf; int len, cap; } resp_t;

static esp_err_t collect(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    resp_t *r = e->user_data;
    if (!r) return ESP_OK;
    int need = r->len + e->data_len + 1;
    if (need > r->cap) {
        int nc = r->cap ? r->cap : 2048;
        while (nc < need) nc *= 2;
        char *nb = realloc(r->buf, nc);
        if (!nb) return ESP_ERR_NO_MEM;
        r->buf = nb; r->cap = nc;
    }
    memcpy(r->buf + r->len, e->data, e->data_len);
    r->len += e->data_len; r->buf[r->len] = '\0';
    return ESP_OK;
}

/* Parse "[v]MAJOR.MINOR.PATCH[-suffix]" into v[3]; missing parts = 0. */
static void ver_parse(const char *s, int v[3])
{
    v[0] = v[1] = v[2] = 0;
    if (s && (s[0] == 'v' || s[0] == 'V')) s++;
    if (s) sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);
}

/* True if version string a is strictly newer than b (ordered semver compare). */
static bool ver_newer(const char *a, const char *b)
{
    int va[3], vb[3];
    ver_parse(a, va);
    ver_parse(b, vb);
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] > vb[i];
    }
    return false;
}

bool ota_update_check(ota_check_t *out)
{
    memset(out, 0, sizeof(*out));
    strlcpy(out->current, PP_FW_VERSION, sizeof(out->current));

    char url[160];
    snprintf(url, sizeof(url),
             "https://api.github.com/repos/%s/releases/latest", CONFIG_PP_UPDATE_REPO);

    resp_t r = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .event_handler = collect,
        .user_data = &r,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { ESP_LOGW(TAG, "http init failed"); free(r.buf); return false; }
    esp_http_client_set_header(c, "User-Agent", "Prusa-Touch");
    esp_http_client_set_header(c, "Accept", "application/vnd.github+json");
    esp_err_t err = esp_http_client_perform(c);
    int sc = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    bool ok = false;
    if (err == ESP_OK && sc == 200 && r.buf) {
        cJSON *root = cJSON_Parse(r.buf);
        if (root) {
            const cJSON *tag = cJSON_GetObjectItem(root, "tag_name");
            if (cJSON_IsString(tag)) {
                strlcpy(out->latest, tag->valuestring, sizeof(out->latest));
            }
            const cJSON *assets = cJSON_GetObjectItem(root, "assets");
            const cJSON *a = NULL;
            cJSON_ArrayForEach(a, assets) {
                const cJSON *name = cJSON_GetObjectItem(a, "name");
                const cJSON *dl = cJSON_GetObjectItem(a, "browser_download_url");
                if (!cJSON_IsString(name) || !cJSON_IsString(dl)) continue;
                /* esp_https_ota writes ONLY the app partition, so we need the
                 * standalone app image (prusa-touch-app.bin) — never the merged
                 * full image (bootloader@0x0), the bootloader, or the partition
                 * table, all of which also end in ".bin". Match "app.bin" only. */
                if (strstr(name->valuestring, "app.bin")) {
                    strlcpy(out->url, dl->valuestring, sizeof(out->url));
                    break;
                }
            }
            /* "Available" only when the release is a strictly NEWER semver than the
             * running build AND ships a downloadable .bin (so a downgrade tag or a
             * re-tag of the same version is not offered as an update). */
            out->available = out->latest[0] && out->url[0] &&
                             ver_newer(out->latest, out->current);
            cJSON_Delete(root);
            ok = true;
        }
    } else {
        ESP_LOGW(TAG, "release check failed (err=%s http=%d)", esp_err_to_name(err), sc);
    }
    free(r.buf);
    ESP_LOGI(TAG, "current=%s latest=%s available=%d", out->current, out->latest, out->available);
    return ok;
}

#define OTA_IDLE (-1)   /* not running */
#define OTA_ERR  (-2)   /* last attempt failed (distinct from idle so the UI can say so) */
static volatile int s_progress = OTA_IDLE;   /* volatile: read on other tasks (web poll, UI guard) */
static char s_msg[80] = "";   /* last failure reason, surfaced on the web UI */

/* Single-OTA guard: serialize ota_update_apply() so the background auto-updater and a manual
 * on-device "Update now" can never both enter the (non-reentrant) esp_https_ota machine. */
static portMUX_TYPE  s_ota_mux  = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_ota_busy = false;

int ota_update_get_progress(void) { return s_progress; }
const char *ota_update_get_msg(void) { return s_msg; }

static void ota_fail(esp_err_t e)
{
    s_progress = OTA_ERR;
    strlcpy(s_msg, esp_err_to_name(e), sizeof(s_msg));
    s_ota_busy = false;   /* release the single-OTA guard on every failure path */
}

void ota_update_apply(const char *bin_url)
{
    if (!bin_url || !bin_url[0]) return;
    /* Refuse a second concurrent OTA (auto-updater vs manual). Atomic test-and-set. */
    bool busy;
    taskENTER_CRITICAL(&s_ota_mux);
    busy = s_ota_busy;
    if (!busy) s_ota_busy = true;
    taskEXIT_CRITICAL(&s_ota_mux);
    if (busy) { ESP_LOGW(TAG, "OTA already in progress — ignoring duplicate apply"); return; }

    ESP_LOGI(TAG, "OTA from %s", bin_url);
    s_progress = 0; s_msg[0] = '\0';

    esp_http_client_config_t http = {
        .url = bin_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        /* GitHub redirects release assets to a signed CDN URL ~900 chars long, which overflows
         * the 512-byte default rx buffer and fails the OTA. Give it real headroom. */
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };

    esp_https_ota_handle_t ota = NULL;
    esp_err_t e = esp_https_ota_begin(&cfg, &ota);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(e));
        ota_fail(e);
        return;
    }

    int total = esp_https_ota_get_image_size(ota);
    while (1) {
        e = esp_https_ota_perform(ota);
        if (e != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        int read = esp_https_ota_get_image_len_read(ota);
        if (total > 0) {
            s_progress = (read * 100) / total;
        }
    }

    if (e == ESP_OK) {
        /* finish() validates + releases the handle on BOTH success and validate-failure — so we
         * must NOT abort() afterwards (that would touch a freed handle). */
        e = esp_https_ota_finish(ota);
        if (e == ESP_OK) {
            s_progress = 100;
            ESP_LOGI(TAG, "OTA ok — rebooting");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        ESP_LOGE(TAG, "OTA finish/validate failed: %s", esp_err_to_name(e));
        ota_fail(e);
        return;
    }

    /* perform() failed mid-stream: the handle is still live, so abort() to release it. */
    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(e));
    ota_fail(e);
    esp_https_ota_abort(ota);
}

/* Background opt-in auto-updater. Does nothing unless the user has turned on
 * "Automatic updates" in Preferences (default off). When enabled and online, it
 * checks the GitHub release on a slow cadence and OTA-flashes a newer build. */
#define OTA_AUTO_FIRST_DELAY_MS   60000      /* let WiFi associate after boot      */
#define OTA_AUTO_PERIOD_MS        21600000   /* 6 h — well under GitHub's 60 req/h  */

/* vTaskDelay in <=10 min chunks: pdMS_TO_TICKS(ms) computes ms*HZ, which overflows
 * uint32_t for multi-hour values (collapsing a 6 h wait to ~2 min). */
static void ota_sleep_ms(uint32_t ms)
{
    const uint32_t chunk = 600000;   /* 10 min */
    while (ms) {
        uint32_t s = ms < chunk ? ms : chunk;
        vTaskDelay(pdMS_TO_TICKS(s));
        ms -= s;
    }
}

static void ota_auto_task(void *arg)
{
    (void)arg;
    ota_sleep_ms(OTA_AUTO_FIRST_DELAY_MS);
    for (;;) {
        if (prefs_auto_update() && wifi_is_connected()) {
            ota_check_t c;
            if (ota_update_check(&c) && c.available && c.url[0]) {
                ESP_LOGI(TAG, "auto-update enabled: %s -> %s, applying",
                         c.current, c.latest);
                ota_update_apply(c.url);   /* reboots on success */
            }
        }
        ota_sleep_ms(OTA_AUTO_PERIOD_MS);
    }
}

void ota_update_start_auto(void)
{
    xTaskCreate(ota_auto_task, "ota_auto", 8192, NULL, 3, NULL);
}
