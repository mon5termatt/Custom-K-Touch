# Prusa Connect Touch

Native, open firmware for the BigTreeTech **K-Touch** (and its sibling Panda Touch)
that talks to **PrusaLink** directly, with a **Prusa Connect-style fleet dashboard** —
no bridge, no extra hardware on your network. (Internal build/binary name:
`prusa-touch`.)

- **MCU:** ESP32-S3, 8 MB octal PSRAM, 16 MB flash
- **Display:** 800×480 RGB565 (16-bit, DE mode)
- **Touch:** GT911 (I²C0)
- **Stack:** ESP-IDF ≥ 5.1, LVGL v9, BigTreeTech's MIT `PandaTouch_IDF` BSP
- **Repo:** github.com/nomadsgalaxy/Prusa-Connect-Touch
- **License:** OCL v1.1 + SWAtt v1 (see `../LICENSE`, `../NOTICE`). Third-party
  components keep their own licenses; "Prusa"/"Prusa Connect" are Prusa trademarks —
  independent project, not affiliated with Prusa Research.

## Layout

```
firmware/
  CMakeLists.txt            top-level project
  sdkconfig.defaults        octal PSRAM, 16MB QIO, LVGL custom malloc (from BSP) + ours
  components/
    PandaTouch_IDF/         vendored MIT BSP (display, GT911, backlight, USB-MSC)
  main/
    app_main.c              Milestone 1: bring-up (display + backlight + touch test)
    pandaprusa_theme.h      Prusa brand color tokens for the LVGL UI
    CMakeLists.txt / idf_component.yml
```

## Configure

`idf.py menuconfig` → **Prusa-Touch Configuration**: set WiFi SSID/password, the
printer's PrusaLink host/IP + port, the Digest username (`maker`) and password (the
one PrusaLink shows under Settings → Network), and the storage name (`local`/`usb`).

## Build & flash

Requires the ESP-IDF toolchain (≥ 5.1). With it installed and exported:

```bash
cd firmware
idf.py set-target esp32s3      # first time only
idf.py build
idf.py -p <PORT> flash monitor # PORT e.g. COM5 (Windows) or /dev/ttyACM0
```

No local toolchain? Build in Docker (what this repo is validated against):

```bash
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.1 \
  bash -c "idf.py set-target esp32s3 && idf.py build"
```

### Flashing options

1. **USB-C (safest, recoverable):** the K-Touch flashes over USB-C (CH340K
   bridge). If it doesn't auto-enter download mode, hold BOOT while tapping reset.
2. **OTA over WiFi (no teardown):** stock firmware runs a web "Settings Manager"
   on the LAN with an **`/update`** firmware-upload page (seen at
   `http://<screen-ip>/`). This *may* accept our app image once the partition
   layout is confirmed compatible — to be validated before relying on it. Always
   keep the USB-C recovery path available.

## ⚠️ Back up stock firmware before first flash

Flashing this replaces BigTreeTech's stock firmware. Keep a way back:

- The original binaries are saved in `../spike/fw/` (K-Touch v1.1.0
  `firmware.bin` + `product.img`), and all versions remain in BTT's repo.
- Recommended: dump the whole stock flash first so you can restore byte-for-byte:
  ```bash
  esptool.py -p <PORT> -b 460800 read_flash 0 0x1000000 ktouch_stock_backup.bin
  ```
- BTT also publishes a recovery tool (`flash_download_tool`) in the K-Touch repo.

## Milestone status

- [x] **M1 — bring-up:** project skeleton, vendored BSP, display + backlight + GT911.
- [x] **M2 — PrusaLink client:** `prusalink.c` — esp_http_client + `X-Api-Key`
  (Digest fallback) + cJSON; status, job, pause/resume/stop, file list, print start;
  storage auto-detected from the status response. **Validated against real CORE One
  printers** (auth + JSON shapes confirmed by direct API calls).
- [x] **M3 — status UI:** `ui.c` — Prusa-themed status screen (state, nozzle/bed
  temps, job name, progress bar, ETA, connection indicator).
- [x] **M4 — control + files:** pause/resume/stop and a file browser that starts
  prints (commands dispatched off the UI thread via `app_state.c`).
- [x] **M5a — multi-printer (runtime):** NVS-backed, mutex-protected printer store;
  on-screen picker + "Add printer" form (name/IP/API key) with keyboard.
- [x] **M5b — WiFi onboarding:** on-screen scan + password entry (NVS); no compiled creds.
- [x] **M5c — Connect dashboard:** poll-all fleet cache + card grid (state badge,
  temps, progress) + persistent bottom nav, to `../UI_DESIGN.md` (live Connect tokens).
- [x] **M5d — Web UI + self-OTA:** `esp_http_server` page (Status/Printers/WiFi/
  Firmware) + OTA upload; BTT-matched OTA partition layout.
- [x] **M5e — GitHub auto-updater:** checks the repo's Releases, self-flashes via
  `esp_https_ota`; app-identity check + rollback safety net.
- [x] Adversarial review (25 bugs) fixed; About screen (SWAtt attribution).
- [x] Flashed + booted on real 5" K-Touch hardware (display + GT911 + WiFi confirmed).
- [ ] **M5f — dual Connect+Link config + fallback** (per-printer prefer + failover).
- [ ] **M6+ — depth:** gcode thumbnails, camera snapshots, preheat/temp set,
  jog/home/babystep, fan/speed/flow, gcode console, i18n, USB print, brand fonts.

> **Compile-validated** in `espressif/idf:v5.3.1` and the PrusaLink client is
> **verified against live CORE One printers**. The earlier (pre-dashboard) build ran
> on real 5" hardware. Pending on-hardware re-verification of the dashboard + controls.
> Threading: the BSP owns the LVGL task; a `pp_net` task polls PrusaLink and runs
> commands; UI updates are marshalled back via `pt_display_schedule_ui`.

## Pinout note (K-Touch vs Panda Touch)

The BSP's pin map (`components/PandaTouch_IDF/include/pandatouch_board.h`) is
documented for the 7" Panda Touch. The 5" K-Touch is the same family but may differ
on a few panel GPIOs or timings. If M1 shows a blank/garbled/shifted image, tune the
`PT_LCD_*` defines there first. Confirmed-correct values for the K-Touch should be
recorded back into that header.
