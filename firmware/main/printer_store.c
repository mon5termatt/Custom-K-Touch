/* Prusa-Touch — runtime printer store (NVS-backed, mutex-protected). */
#include "printer_store.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "esp_attr.h"

static const char *TAG = "printers";
#define NS         "pp"
#define KEY_LIST   "printers"
#define KEY_COUNT  "count"
#define KEY_ACTIVE "active"
#define KEY_VER    "store_ver"
#define STORE_VER  2            /* bump if pp_printer_t layout changes */

static EXT_RAM_BSS_ATTR pp_printer_t      s_list[PP_MAX_PRINTERS];
static int               s_count;
static int               s_active = -1;
static SemaphoreHandle_t s_mtx;

#define LOCK()   do { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); } while (0)
#define UNLOCK() do { if (s_mtx) xSemaphoreGive(s_mtx); } while (0)

/* Legacy entries often stored "host:7125" in host with port left at 80. Split that
 * into host + port so HTTP clients don't DNS-lookup "10.0.0.x:7125". */
static bool migrate_host_port(pp_printer_t *p)
{
    if (!p || !p->host[0]) return false;
    if (strncmp(p->host, "cloud:", 6) == 0 || strncmp(p->host, "bambu:", 6) == 0 ||
        strncmp(p->host, "bambucloud:", 11) == 0) return false;
    if (p->host[0] == '[') {
        char *rb = strchr(p->host, ']');
        if (!rb || rb[1] != ':' || !rb[2]) return false;
        int port = atoi(rb + 2);
        if (port <= 0 || port >= 65536) return false;
        size_t n = (size_t)(rb - p->host - 1);
        memmove(p->host, p->host + 1, n);
        p->host[n] = '\0';
        p->port = port;
        return true;
    }
    char *colon = strrchr(p->host, ':');
    if (!colon || colon == p->host || strchr(p->host, ':') != colon) return false;
    for (const char *d = colon + 1; *d; d++)
        if (*d < '0' || *d > '9') return false;
    if (!colon[1]) return false;
    int port = atoi(colon + 1);
    if (port <= 0 || port >= 65536) return false;
    *colon = '\0';
    p->port = port;
    return true;
}

/* Persist current state. Caller must hold s_mtx. */
static void save_locked(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, KEY_VER, STORE_VER);

    /* Split the printer list into chunks of 16 to avoid the ~4KB NVS blob limit. */
    for (int i = 0; i < (PP_MAX_PRINTERS + 15) / 16; i++) {
        char key[16];
        snprintf(key, sizeof(key), "plist_%d", i);
        int start = i * 16;
        if (start < s_count) {
            int count = s_count - start;
            if (count > 16) count = 16;
            nvs_set_blob(h, key, &s_list[start], count * sizeof(pp_printer_t));
        } else {
            nvs_erase_key(h, key);
        }
    }

    nvs_set_i32(h, KEY_COUNT, s_count);
    nvs_set_i32(h, KEY_ACTIVE, s_active);
    nvs_commit(h);
    nvs_close(h);
}

static void load_locked(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    int32_t ver = 0, cnt = 0, act = -1;
    nvs_get_i32(h, KEY_VER, &ver);
    if (ver != STORE_VER) {           /* schema mismatch -> ignore old blob */
        ESP_LOGW(TAG, "store schema %ld != %d; starting empty", (long)ver, STORE_VER);
        nvs_close(h);
        return;
    }
    nvs_get_i32(h, KEY_COUNT, &cnt);
    nvs_get_i32(h, KEY_ACTIVE, &act);
    bool need_save = false;
    if (cnt > 0 && cnt <= PP_MAX_PRINTERS) {
        s_count = 0;
        for (int i = 0; i < (PP_MAX_PRINTERS + 15) / 16; i++) {
            char key[16];
            snprintf(key, sizeof(key), "plist_%d", i);
            size_t sz = 16 * sizeof(pp_printer_t);
            if (nvs_get_blob(h, key, &s_list[s_count], &sz) == ESP_OK) {
                s_count += (sz / sizeof(pp_printer_t));
            }
        }
        /* Fallback for legacy single-blob storage if no chunks found. */
        if (s_count == 0) {
            size_t sz = cnt * sizeof(pp_printer_t);
            if (nvs_get_blob(h, KEY_LIST, s_list, &sz) == ESP_OK) {
                s_count = (int)(sz / sizeof(pp_printer_t));
            }
        }
        s_active = (act >= 0 && act < s_count) ? act : 0;

        /* One-time cleanup of duplicate hosts left over from before add-time dedup
         * existed (e.g. repeated "Add All from Team"). Keep the first of each host. */
        int w = 0;
        for (int r = 0; r < s_count; r++) {
            bool dup = false;
            for (int k = 0; k < w; k++)
                if (s_list[r].host[0] && strcmp(s_list[k].host, s_list[r].host) == 0) { dup = true; break; }
            if (!dup) { if (w != r) s_list[w] = s_list[r]; w++; }
            else if (s_active == r) s_active = 0;
        }
        if (w != s_count) { s_count = w; need_save = true; }
        if (s_active >= s_count) s_active = s_count ? 0 : -1;

        for (int i = 0; i < s_count; i++) {
            if (migrate_host_port(&s_list[i])) {
                ESP_LOGI(TAG, "migrated printer[%d] host/port -> %s:%d", i, s_list[i].host, s_list[i].port);
                need_save = true;
            }
        }
    }
    nvs_close(h);
    if (need_save) save_locked();
}

void printer_store_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    LOCK();
    load_locked();
    if (s_count == 0 && CONFIG_PP_PRINTER_HOST[0] &&
        strcmp(CONFIG_PP_PRINTER_HOST, "192.168.1.50") != 0) {
        pp_printer_t p = {0};
        strlcpy(p.name, "Printer", sizeof(p.name));
        strlcpy(p.host, CONFIG_PP_PRINTER_HOST, sizeof(p.host));
        p.port = CONFIG_PP_PRINTER_PORT;
        strlcpy(p.api_key, CONFIG_PP_PRINTER_APIKEY, sizeof(p.api_key));
        s_list[0] = p;
        s_count = 1;
        s_active = 0;
        save_locked();
        ESP_LOGI(TAG, "seeded printer from Kconfig: %s", p.host);
    }
    ESP_LOGI(TAG, "loaded %d printer(s), active=%d", s_count, s_active);
    UNLOCK();
}

int printer_store_count(void)
{
    LOCK(); int n = s_count; UNLOCK(); return n;
}

bool printer_store_get(int idx, pp_printer_t *out)
{
    bool ok = false;
    LOCK();
    if (out && idx >= 0 && idx < s_count) { *out = s_list[idx]; ok = true; }
    UNLOCK();
    return ok;
}

bool printer_store_active_get(pp_printer_t *out)
{
    bool ok = false;
    LOCK();
    if (out && s_active >= 0 && s_active < s_count) { *out = s_list[s_active]; ok = true; }
    UNLOCK();
    return ok;
}

int printer_store_active(void)
{
    LOCK(); int a = s_active; UNLOCK(); return a;
}

void printer_store_set_active(int idx)
{
    LOCK();
    if (idx >= 0 && idx < s_count) { s_active = idx; save_locked(); }
    UNLOCK();
}

int printer_store_add(const pp_printer_t *p)
{
    int idx = -1;
    LOCK();
    if (p) {
        /* De-dup by host: re-adding the same printer (e.g. a cloud:<uuid> from
         * "Add All from Team") returns the existing entry instead of stacking a
         * duplicate. Callers treat the returned idx as success either way. */
        for (int i = 0; i < s_count; i++) {
            if (p->host[0] && strcmp(s_list[i].host, p->host) == 0) { idx = i; break; }
        }
        if (idx < 0 && s_count < PP_MAX_PRINTERS) {
            s_list[s_count] = *p;
            /* port 0 = backend default (Moonraker 7125 / PrusaLink 80) */
            idx = s_count++;
            if (s_active < 0) s_active = idx;
            save_locked();
        }
    }
    UNLOCK();
    return idx;
}

bool printer_store_update(int idx, const pp_printer_t *p)
{
    bool ok = false;
    LOCK();
    if (p && idx >= 0 && idx < s_count) {
        s_list[idx] = *p;
        save_locked();
        ok = true;
    }
    UNLOCK();
    return ok;
}

void printer_store_remove(int idx)
{
    LOCK();
    if (idx >= 0 && idx < s_count) {
        for (int i = idx; i < s_count - 1; i++) s_list[i] = s_list[i + 1];
        s_count--;
        if (s_active >= s_count) s_active = s_count - 1;
        save_locked();
    }
    UNLOCK();
}

void printer_store_clear(void)
{
    LOCK();
    s_count = 0;
    s_active = -1;
    save_locked();
    UNLOCK();
}
