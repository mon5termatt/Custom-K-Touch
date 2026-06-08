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

#include "pandaprusa.h"   /* PP_FW_VERSION */

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
                if (cJSON_IsString(name) && cJSON_IsString(dl) &&
                    strstr(name->valuestring, ".bin")) {
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

void ota_update_apply(const char *bin_url)
{
    if (!bin_url || !bin_url[0]) return;
    ESP_LOGI(TAG, "OTA from %s", bin_url);
    esp_http_client_config_t http = {
        .url = bin_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };
    esp_err_t e = esp_https_ota(&cfg);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "OTA ok — rebooting");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(e));
    }
}
