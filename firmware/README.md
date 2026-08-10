# Klipper Touch — firmware (build from source)

Native, open firmware for the BigTreeTech **K-Touch** (and its sibling Panda Touch).
**Moonraker/Klipper first**, with optional PrusaLink, Prusa Connect, and Bambu — fleet
dashboard on the desk, no bridge hardware.

If you just want to flash it, grab the prebuilt image from the
[Releases page](https://github.com/mon5termatt/Custom-K-Touch/releases) and
follow the flashing steps in the [root README](../README.md). This document is for
building from source.

- **MCU:** ESP32-S3, 8 MB octal PSRAM, 16 MB flash
- **Display:** 800×480 RGB565 (16-bit, DE mode)
- **Touch:** GT911 (I²C0)
- **Stack:** ESP-IDF ≥ 5.1, LVGL v9, BigTreeTech's MIT `PandaTouch_IDF` BSP (vendored)
- **License:** OCL v1.1 + SWAtt v1 (see `../LICENSE`, `../NOTICE`). Third-party
  components keep their own licenses; "Prusa" / "Prusa Connect" are Prusa Research
  trademarks — this is an independent project, not affiliated with Prusa Research.

## Layout

```
firmware/
  CMakeLists.txt            top-level project
  partitions.csv            dual-OTA layout (app0/app1 + otadata)
  sdkconfig.defaults        octal PSRAM, 16 MB QIO, LVGL fonts + custom malloc
  components/
    PandaTouch_IDF/         vendored MIT BSP (display, GT911, backlight, USB-MSC)
  main/
    app_main.c              boot: display -> wifi -> state -> web server
    prusalink.c             PrusaLink client (status/job/files/thumbnails/control)
    moonraker.c             Moonraker (Klipper) client
    app_state.c             per-printer polling + command worker + backend dispatch
    ui.c                    LVGL screens (dashboard, detail, files, control, wifi, about)
    web.c                   on-device web UI + OTA + screen mirror
    printer_store.c         NVS-backed multi-printer store
    wifi.c / ota_update.c   Wi-Fi onboarding + SoftAP fallback, GitHub-release OTA
```

## Build

With the ESP-IDF toolchain (≥ 5.1) installed and exported:

```bash
cd firmware
idf.py set-target esp32s3      # first time only
idf.py build
idf.py -p <PORT> flash monitor # PORT e.g. /dev/ttyACM0 or COM5
```

No local toolchain? Build in the official Docker image:

```bash
cd firmware
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.1 \
  bash -c "idf.py set-target esp32s3 && idf.py build"
```

LVGL is pulled automatically by the IDF component manager (`main/idf_component.yml`);
the BSP is vendored under `components/`.

## Configuration

Printers and Wi-Fi are configured **on the device** — either on the touchscreen
(Settings → Add printer / Wi-Fi) or from the on-device web UI at `http://<device-ip>/`.
There's nothing to hard-code. `idf.py menuconfig` → **Prusa-Touch Configuration** only
sets optional build-time defaults.

If no known network is reachable, the device opens a `PrusaTouch-XXXX` Wi-Fi hotspot
at `http://192.168.4.1` so you can enter credentials.

## Flashing notes

- **USB-C (recommended, recoverable):** the K-Touch flashes over USB-C. If it doesn't
  auto-enter download mode, hold **BOOT** while tapping reset.
- **OTA over Wi-Fi:** once running, the device self-updates from GitHub Releases and
  accepts manual firmware uploads from its web UI.

### Back up the stock firmware first

Flashing replaces BigTreeTech's stock firmware. Keep a way back — dump the whole flash
so you can restore it byte-for-byte, and note that BTT also publishes the stock images
and a recovery tool in the [K-Touch repo](https://github.com/bigtreetech/K-Touch):

```bash
esptool.py -p <PORT> -b 460800 read_flash 0 0x1000000 ktouch_stock_backup.bin
```

## Pinout note (K-Touch vs Panda Touch)

The BSP pin map (`components/PandaTouch_IDF/include/`) is documented for the 7" Panda
Touch. The 5" K-Touch is the same family but may differ on a few panel GPIOs or timings;
if the image is blank/garbled/shifted, tune the `PT_LCD_*` defines there first.
