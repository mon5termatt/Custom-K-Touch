/* Prusa-Touch — state, polling, command worker. */
#include "app_state.h"
#include "prusalink.h"
#include "moonraker.h"
#include "printer_store.h"
#include "prefs.h"
#include "wifi.h"
#include "ui.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "pandatouch_display.h"   /* pt_display_schedule_ui */

#include "esp_attr.h"

static const char *TAG = "app_state";

/* wifi_scan() writes up to WIFI_MAX_SCAN entries into pp_wifi_list_t.ssids
 * (sized PP_WIFI_MAX_SCAN); enforce the invariant at compile time. */
_Static_assert(WIFI_MAX_SCAN <= PP_WIFI_MAX_SCAN, "wifi scan cap exceeds ssids[] size");

static pp_status_t      s_status;                  /* active printer (detail screen) */
static EXT_RAM_ATTR pp_status_t      s_cache[PP_MAX_PRINTERS];   /* fleet cache (dashboard)        */
static EXT_RAM_ATTR char             s_info_model[PP_MAX_PRINTERS][28];  /* lazy /api/version cache */
static EXT_RAM_ATTR char             s_info_fw[PP_MAX_PRINTERS][24];
static EXT_RAM_ATTR bool             s_info_control[PP_MAX_PRINTERS];
static EXT_RAM_ATTR uint8_t          s_backend[PP_MAX_PRINTERS];          /* pp_backend_t, auto-detected */
static int              s_cache_count;

/* Detect (and cache) whether a printer speaks PrusaLink or Moonraker. Probe runs
 * once per printer on the net task. NOTE: if a Moonraker printer is unreachable at
 * first contact it defaults to PrusaLink until the cache resets (printer edit / reboot). */
static pp_backend_t detect_backend(int i, const pp_printer_t *pr)
{
    if (i < 0 || i >= PP_MAX_PRINTERS) return PP_BK_PRUSALINK;
    if (s_backend[i] == PP_BK_UNKNOWN) {
        s_backend[i] = moonraker_probe(pr) ? PP_BK_MOONRAKER : PP_BK_PRUSALINK;
    }
    return (pp_backend_t)s_backend[i];
}

/* Send a gcode line to whichever backend the active printer speaks. */
static esp_err_t be_gcode(pp_backend_t bk, const pp_printer_t *pr, const char *g)
{
    return (bk == PP_BK_MOONRAKER) ? moonraker_gcode(pr, g) : prusalink_gcode(g);
}
static int              s_poll_idx;                 /* round-robin cursor             */
static SemaphoreHandle_t s_lock;
static QueueHandle_t    s_cmds;

void app_state_get(pp_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

void app_state_printers_changed(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_cache, 0, sizeof(s_cache));
    memset(s_info_model, 0, sizeof(s_info_model));   /* re-fetch identity after edits */
    memset(s_info_fw, 0, sizeof(s_info_fw));
    memset(s_info_control, 0, sizeof(s_info_control));
    memset(s_backend, 0, sizeof(s_backend));   /* re-detect backend after edits */
    s_cache_count = printer_store_count();
    s_poll_idx = 0;
    xSemaphoreGive(s_lock);
}

void app_state_get_fleet(pp_status_t *arr, int max, int *count)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_cache_count;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) arr[i] = s_cache[i];
    *count = n;
    xSemaphoreGive(s_lock);
}

void app_state_post_cmd(pp_cmd_kind_t kind, const char *path)
{
    pp_cmd_t cmd = { .kind = kind };
    if (path) {
        strlcpy(cmd.path, path, sizeof(cmd.path));
    }
    if (s_cmds) {
        xQueueSend(s_cmds, &cmd, 0);
    }
}

void app_state_select_printer(int index)
{
    pp_cmd_t cmd = { .kind = PP_CMD_SET_PRINTER, .index = index };
    if (s_cmds) {
        xQueueSend(s_cmds, &cmd, 0);
    }
}

void app_state_refresh_dashboard(void)
{
    pp_cmd_t cmd = { .kind = PP_CMD_DASH_REFRESH };
    if (s_cmds) xQueueSend(s_cmds, &cmd, 0);
}

void app_state_set_pref(pp_pref_kind_t pref, int value)
{
    /* Packed so the NVS write happens on this task, not the PSRAM-stack LVGL task. */
    pp_cmd_t cmd = { .kind = PP_CMD_SET_PREF, .index = ((int)pref << 8) | (value & 0xFF) };
    if (s_cmds) xQueueSend(s_cmds, &cmd, 0);
}

void app_state_wifi_scan(void)
{
    pp_cmd_t cmd = { .kind = PP_CMD_WIFI_SCAN };
    if (s_cmds) xQueueSend(s_cmds, &cmd, 0);
}

void app_state_wifi_connect(const char *ssid, const char *pass)
{
    pp_cmd_t cmd = { .kind = PP_CMD_WIFI_CONNECT };
    strlcpy(cmd.path, ssid ? ssid : "", sizeof(cmd.path));
    strlcpy(cmd.arg2, pass ? pass : "", sizeof(cmd.arg2));
    if (s_cmds) xQueueSend(s_cmds, &cmd, 0);
}

void app_state_fetch_thumb(const char *ref)
{
    pp_cmd_t cmd = { .kind = PP_CMD_THUMB };
    strlcpy(cmd.path, ref ? ref : "", sizeof(cmd.path));
    if (s_cmds) xQueueSend(s_cmds, &cmd, 0);
}

void app_state_fetch_thumb_dash(const char *ref, int idx)
{
    pp_cmd_t cmd = { .kind = PP_CMD_THUMB_DASH, .index = idx };
    strlcpy(cmd.path, ref ? ref : "", sizeof(cmd.path));
    if (s_cmds) xQueueSend(s_cmds, &cmd, 0);
}

/* Push a heap copy of the current status to the LVGL thread. */
static void publish_status(void)
{
    pp_status_t *copy = malloc(sizeof(*copy));
    if (!copy) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *copy = s_status;
    xSemaphoreGive(s_lock);
    if (pt_display_schedule_ui(ui_apply_status, copy) != LV_RESULT_OK) {
        free(copy);   /* not enqueued -> applier won't run -> free here */
    }
}

/* Push a heap snapshot of the whole fleet cache to the LVGL thread. */
static void publish_dashboard(void)
{
    pp_dash_t *d = malloc(sizeof(*d));
    if (!d) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    d->count = s_cache_count;
    for (int i = 0; i < s_cache_count && i < PP_MAX_PRINTERS; i++) d->items[i] = s_cache[i];
    xSemaphoreGive(s_lock);
    if (pt_display_schedule_ui(ui_apply_dashboard, d) != LV_RESULT_OK) {
        free(d);
    }
}

/* Poll one printer (by store index) into the cache; also update s_status if it is
 * the active printer. Returns true if that printer's cached data changed (so the
 * dashboard only republishes on real change, not every cycle). Blocking HTTP —
 * runs on the net task only. */
static bool poll_printer(int i)
{
    pp_printer_t pr;
    if (!printer_store_get(i, &pr)) return false;
    int active = printer_store_active();
    pp_backend_t bk = detect_backend(i, &pr);
    pp_status_t fresh;
    if (bk == PP_BK_MOONRAKER) {
        moonraker_get_status_of(&pr, &fresh);
    } else if (i == active) {
        prusalink_get_status(&fresh);          /* active: refreshes s_storage */
    } else {
        prusalink_get_status_of(&pr, &fresh);  /* fleet poll: leaves s_storage */
    }
    strlcpy(fresh.printer_name, pr.name, sizeof(fresh.printer_name));

    /* Identity (model/firmware/control): one blocking fetch per printer, then reuse cache.
     * The unlocked emptiness check is a benign race (at worst one extra fetch). */
    if (fresh.online && i >= 0 && i < PP_MAX_PRINTERS && s_info_model[i][0] == '\0') {
        char m[28] = {0}, fw[24] = {0};
        bool ctl = false;
        bool got;
        if (bk == PP_BK_MOONRAKER) {
            got = (moonraker_get_info(&pr, m, sizeof(m), fw, sizeof(fw)) == ESP_OK);
            ctl = true;   /* Klipper always accepts gcode/print control */
        } else {
            got = (prusalink_get_info(&pr, m, sizeof(m), fw, sizeof(fw), &ctl) == ESP_OK);
        }
        if (got && m[0]) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(s_info_model[i], m, sizeof(s_info_model[i]));
            strlcpy(s_info_fw[i], fw, sizeof(s_info_fw[i]));
            s_info_control[i] = ctl;
            xSemaphoreGive(s_lock);
        }
    }

    /* Hydrate the polled status from our cached identity row. */
    if (i >= 0 && i < PP_MAX_PRINTERS) {
        strlcpy(fresh.model, s_info_model[i], sizeof(fresh.model));
        strlcpy(fresh.firmware, s_info_fw[i], sizeof(fresh.firmware));
        fresh.has_control = s_info_control[i];
    }

    bool changed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (i >= 0 && i < PP_MAX_PRINTERS) {
        changed = (memcmp(&s_cache[i], &fresh, sizeof(fresh)) != 0);
        s_cache[i] = fresh;
    }
    s_cache_count = printer_store_count();
    if (i == active) s_status = fresh;
    xSemaphoreGive(s_lock);
    return changed;
}

/* Poll the active printer and publish both views (after a command, or on switch). */
static void poll_active_and_publish(void)
{
    int a = printer_store_active();
    if (a >= 0) {
        poll_printer(a);
        publish_status();
    }
    publish_dashboard();
}

static void run_command(const pp_cmd_t *cmd)
{
    int job_id;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    job_id = s_status.job_id;
    xSemaphoreGive(s_lock);

    /* Resolve the active printer's backend so control/file commands hit the right API. */
    pp_printer_t apr;
    bool have_apr = printer_store_active_get(&apr);
    int aidx = printer_store_active();
    pp_backend_t abk = (have_apr && aidx >= 0) ? detect_backend(aidx, &apr) : PP_BK_PRUSALINK;
    bool moon = (abk == PP_BK_MOONRAKER);

    switch (cmd->kind) {
    case PP_CMD_PAUSE:  moon ? moonraker_pause(&apr)  : prusalink_pause(job_id);  break;
    case PP_CMD_RESUME: moon ? moonraker_resume(&apr) : prusalink_resume(job_id); break;
    case PP_CMD_STOP:   moon ? moonraker_stop(&apr)   : prusalink_stop(job_id);   break;
    case PP_CMD_PRINT:  moon ? moonraker_print(&apr, cmd->path) : prusalink_print(cmd->path); break;
    case PP_CMD_SET_PRINTER:
        printer_store_set_active(cmd->index);
        break;
    case PP_CMD_WIFI_SCAN: {
        pp_wifi_list_t *wl = malloc(sizeof(*wl));
        if (wl) {
            wl->count = wifi_scan(wl->ssids, WIFI_MAX_SCAN);
            if (pt_display_schedule_ui(ui_apply_wifi_list, wl) != LV_RESULT_OK) {
                free(wl);
            }
        }
        return;   /* no status poll needed after a scan */
    }
    case PP_CMD_WIFI_CONNECT:
        wifi_save_and_connect(cmd->path, cmd->arg2);
        return;
    case PP_CMD_THUMB: {
        if (!moon && cmd->path[0]) {   /* Moonraker thumbnails not wired yet */
            uint8_t *buf = NULL; int len = 0;
            if (prusalink_get_blob(cmd->path, &buf, &len) == ESP_OK) {
                pp_image_t *im = malloc(sizeof(*im));
                if (im) {
                    im->data = buf; im->len = len;
                    if (pt_display_schedule_ui(ui_apply_thumb, im) != LV_RESULT_OK) { free(buf); free(im); }
                } else {
                    free(buf);
                }
            }
        }
        return;
    }
    case PP_CMD_THUMB_DASH: {
        if (cmd->path[0]) {
            uint8_t *buf = NULL; int len = 0;
            /* Fetching a dashboard thumbnail for printer cmd->index (PrusaLink only). */
            pp_printer_t pr;
            if (printer_store_get(cmd->index, &pr) &&
                detect_backend(cmd->index, &pr) != PP_BK_MOONRAKER) {
                if (prusalink_get_blob_of(&pr, cmd->path, &buf, &len) == ESP_OK) {
                    pp_image_t *im = malloc(sizeof(*im));
                    pp_thumb_dash_t *td = malloc(sizeof(*td));
                    if (im && td) {
                        im->data = buf; im->len = len;
                        td->image = im; td->index = cmd->index;
                        /* Use the wrapper to pass data to the LVGL thread. */
                        if (pt_display_schedule_ui(ui_apply_thumb_dash, td) != LV_RESULT_OK) {
                            free(buf); free(im); free(td);
                        }
                    } else {
                        free(buf); free(im); free(td);
                    }
                }
            }
        }
        return;
    }
    case PP_CMD_LIST: {
        pp_file_list_t *list = malloc(sizeof(*list));
        if (list) {
            list->count = 0;
            esp_err_t lr = moon ? moonraker_list(&apr, list->items, PP_MAX_FILES, &list->count)
                                : prusalink_list("/", list->items, PP_MAX_FILES, &list->count);
            if (lr == ESP_OK) {
                if (pt_display_schedule_ui(ui_apply_files, list) != LV_RESULT_OK) {
                    free(list);
                }
            } else {
                free(list);
            }
        }
        break;
    }
    case PP_CMD_GCODE:
        be_gcode(abk, &apr, cmd->path);
        break;
    case PP_CMD_PREHEAT: {
        int m = cmd->index;
        if (m == 0) { be_gcode(abk, &apr, "M104 S215"); be_gcode(abk, &apr, "M140 S60"); }
        else if (m == 1) { be_gcode(abk, &apr, "M104 S230"); be_gcode(abk, &apr, "M140 S85"); }
        else if (m == 2) { be_gcode(abk, &apr, "M104 S260"); be_gcode(abk, &apr, "M140 S100"); }
        else if (m == 3) { be_gcode(abk, &apr, "M104 S0"); be_gcode(abk, &apr, "M140 S0"); }
        break;
    }
    case PP_CMD_DASH_REFRESH:
        publish_dashboard();
        return;
    case PP_CMD_SET_PREF: {
        int pref = cmd->index >> 8, val = cmd->index & 0xFF;   /* NVS write on net task */
        if (pref == PP_PREF_SORT) { prefs_set_sort((pp_sort_t)val); publish_dashboard(); }
        else if (pref == PP_PREF_HIDE_OFFLINE) { prefs_set_hide_offline(val != 0); publish_dashboard(); }
        else if (pref == PP_PREF_LOGO) {
            prefs_set_logo((pp_logo_t)val);
            pt_display_schedule_ui(ui_apply_logo, NULL);   /* relayout on the LVGL task */
        }
        else if (pref == PP_PREF_AUTOUPDATE) { prefs_set_auto_update(val != 0); }
        return;
    }
    }
    /* Reflect the effect of the command quickly on the active printer. */
    poll_active_and_publish();
}

static void net_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_PP_POLL_INTERVAL_MS);
    for (;;) {
        int n = printer_store_count();
        if (n > 0) {
            int i = s_poll_idx % n;
            s_poll_idx++;
            bool changed = poll_printer(i);
            if (i == printer_store_active()) publish_status();
            if (changed) publish_dashboard();   /* only rebuild cards on change */
        } else {
            /* No printers configured yet. */
            xSemaphoreTake(s_lock, portMAX_DELAY);
            memset(&s_status, 0, sizeof(s_status));
            s_status.time_remaining = -1;
            strlcpy(s_status.printer_name, "No printer", sizeof(s_status.printer_name));
            s_cache_count = 0;
            xSemaphoreGive(s_lock);
            publish_status();
            publish_dashboard();
        }
        pp_cmd_t cmd;
        if (xQueueReceive(s_cmds, &cmd, period) == pdTRUE) {
            ESP_LOGI(TAG, "command %d", cmd.kind);
            run_command(&cmd);
        }
    }
}

void app_state_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_cmds = xQueueCreate(8, sizeof(pp_cmd_t));
    memset(&s_status, 0, sizeof(s_status));
    memset(s_cache, 0, sizeof(s_cache));
    s_status.time_remaining = -1;
    s_cache_count = 0;
    s_poll_idx = 0;
    xTaskCreate(net_task, "pp_net", 8192, NULL, 5, NULL);
}
