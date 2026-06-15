idf_component_register(
    SRCS
        "app_main.c"
        "wifi.c"
        "prusalink.c"
        "moonraker.c"
        "printer_store.c"
        "prefs.c"
        "skin.c"
        "app_state.c"
        "web.c"
        "ota_update.c"
        "ui.c"
        "prusa_connect.c"
        "bambu.c"
        "bambu_cloud.c"
        "netlog.c"
        "pt_core_one.c"
        "pt_core_one_l.c"
        "pt_mini.c"
        "pt_mk4s.c"
        "pt_xl.c"
        "pt_ic_nozzle.c"
        "pt_ic_bed.c"
        "pt_ic_speed.c"
        "pt_fluidd.c"
    INCLUDE_DIRS "."
    REQUIRES
        PandaTouch_IDF      # display + GT911 touch + backlight + USB-MSC (BTT MIT BSP)
        lvgl                # UI toolkit (v9)
        nvs_flash           # persistent settings
        esp_wifi            # station connectivity
        esp_netif
        esp_event
        esp_http_client     # PrusaLink REST + HTTP Digest auth
        esp_http_server     # on-device web UI
        app_update          # firmware OTA (esp_ota_*)
        esp_https_ota       # GitHub Releases self-update
        esp-tls             # TLS + certificate bundle for GitHub HTTPS
        mqtt                # Bambu Lab LAN backend (MQTT over TLS to the printer broker)
        esp_lcd             # read panel framebuffer for /api/screen.bmp
        esp_timer           # uptime for /api/info
        json                # cJSON parsing
)
