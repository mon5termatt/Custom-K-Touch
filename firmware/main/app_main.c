/*
 * Prusa-Touch — functional prototype.
 *
 * Flow: bring up display (BTT MIT BSP) -> connect WiFi -> start the network task
 * that polls PrusaLink and drains UI commands -> show a Prusa-themed status screen
 * with live temps/progress and pause/resume/stop + a file browser to start prints.
 *
 * Configure WiFi + printer in `idf.py menuconfig` -> "Prusa-Touch Configuration".
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"

#include "pandatouch_display.h"
#include "lvgl.h"

#include "pandaprusa.h"
#include "pandaprusa_theme.h"
#include "wifi.h"
#include "prusalink.h"
#include "printer_store.h"
#include "app_state.h"
#include "prefs.h"
#include "ota_update.h"
#include "web.h"
#include "ui.h"

static const char *TAG = "prusa-touch";

void app_main(void)
{
    ESP_LOGI(TAG, "Prusa-Touch %s starting", PP_FW_VERSION);

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);   /* fail loudly on any other NVS init error */

    prefs_load();   /* user prefs (sort/filter/logo) — before the UI is built */

    /* Display + LVGL + GT911 (BSP registers the touch indev). */
    if (pt_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "pt_display_init failed — check panel pins/timings in pandatouch_board.h");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    PT_LVGL_SCOPE_LOCK() {
        ui_init();
    }
    for (uint32_t pct = 0; pct <= 100; pct += 10) {
        pt_backlight_set(pct);
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    /* Bring up state + polling; update the boot screen as we go. */
    ui_boot_update(10, "Initializing store...");
    printer_store_init();
    vTaskDelay(pdMS_TO_TICKS(100));

    ui_boot_update(30, "Connecting WiFi...");
    wifi_init_start();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ui_boot_update(50, "Starting services...");
    prusalink_init();
    app_state_start();
    vTaskDelay(pdMS_TO_TICKS(100));

    ui_boot_update(80, "Starting web UI...");
    web_start();   /* on-device web UI: settings + status + firmware OTA */
    ota_update_start_auto();   /* opt-in background auto-updater (off by default) */
    vTaskDelay(pdMS_TO_TICKS(100));

    ui_boot_update(100, "Ready");
    vTaskDelay(pdMS_TO_TICKS(500));
    ui_request_screen("dash");

    /* We reached a healthy run state — confirm this app so the bootloader won't
     * roll back (relevant after an OTA when rollback is enabled). No-op otherwise. */
    esp_ota_mark_app_valid_cancel_rollback();

    ESP_LOGI(TAG, "Prusa-Touch running");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
