# Prusa Touch

A community touchscreen for your 3D printers, built on the **BigTreeTech K-Touch** (5")
and **Panda Touch** (7"). It connects straight to your printers over your LAN — no
companion app, no cloud account, no extra hardware — and gives you a **Prusa
Connect-style fleet dashboard** right on the desk.

> Independent, open project — **not affiliated with or endorsed by Prusa Research.**
> "Prusa" and "Prusa Connect" are trademarks of Prusa Research.

## What it does

- **Fleet dashboard** — every printer at a glance: state, temperatures, progress, and
  the model render, with the live gcode thumbnail while a job is running.
- **Printer view** — tap a printer for the full detail screen: status badge, nozzle /
  heatbed / speed / Z telemetry, and the current job with progress + ETA.
- **Files** — browse the printer's gcode, newest first, with thumbnails, and start a
  print with a tap. The list is always for the selected printer.
- **Control** — preheat presets, set temperatures, jog and home (where the printer
  allows it; controls hide themselves on backends that don't expose them).
- **Multi-printer** — add as many as you like, on the screen or from the built-in web UI.
- **Just works on the network** — Wi-Fi onboarding on-device; if there's no known
  network it opens its own `PrusaTouch-XXXX` hotspot so you can set it up from a phone.
- **Updates over the air** — pulls new firmware from GitHub Releases; also flashable
  from its web page.

## Supported printers

- **Prusa** over **PrusaLink** — MK4 / MK4S / MK3.5 / MK3.9 / MINI / CORE One / XL
  (Buddy-embedded PrusaLink), and Pi-hosted PrusaLink. Add the printer's IP + API key.
- **Klipper** over **Moonraker** — anything you'd reach with Fluidd or Mainsail. Add
  the host with port **7125** (e.g. `192.168.1.50:7125`); the backend is auto-detected.

## Hardware

| | |
|---|---|
| Board | BigTreeTech K-Touch (5") / Panda Touch (7") — ESP32-S3, 8 MB PSRAM, 16 MB flash |
| Display | 800×480 IPS, GT911 capacitive touch |

## Install (prebuilt image)

1. Download the latest **`prusa-touch-full.bin`** from the
   [Releases page](https://github.com/nomadsgalaxy/Prusa-Connect-Touch/releases).
2. Install [esptool](https://github.com/espressif/esptool) (`pip install esptool`).
3. Connect the touchscreen over USB-C and flash the full image at offset `0x0`:

   ```bash
   esptool.py --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 prusa-touch-full.bin
   ```

   `<PORT>` is e.g. `/dev/ttyACM0` (Linux), `/dev/cu.usbserial-*` (macOS) or `COM5`
   (Windows). If it doesn't enter download mode on its own, hold **BOOT** and tap reset.
4. On first boot, pick your Wi-Fi (or join the `PrusaTouch-XXXX` hotspot and open
   `http://192.168.4.1`), then add a printer.

> **Back up the stock firmware first** — flashing replaces BigTreeTech's firmware.
> `esptool.py -p <PORT> -b 460800 read_flash 0 0x1000000 ktouch_stock_backup.bin`.

## Build from source

See [`firmware/README.md`](firmware/README.md). In short, with ESP-IDF (or its Docker
image):

```bash
cd firmware && idf.py set-target esp32s3 && idf.py build
```

## Developer API

The device exposes a small HTTP API (status, fleet, printer config, Wi-Fi, OTA, a live
screen mirror, and remote UI navigation) — useful for integrations and troubleshooting.
See [`docs/API.md`](docs/API.md).

## License

[OCL v1.1 + SWAtt v1](LICENSE) — see [`NOTICE`](NOTICE) for attribution. Built on the
MIT-licensed [`PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF) BSP and
[LVGL](https://lvgl.io).
