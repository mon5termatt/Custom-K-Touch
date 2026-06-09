# Prusa Touch

A community touchscreen for your 3D printers, built on the **BigTreeTech K-Touch** (5")
and **Panda Touch** (5"). It works two ways: connect **straight to your printers over
your LAN** — no companion app, no cloud account, no extra hardware — or **sign in to
Prusa Connect** and pull your whole fleet in at once. Either way you get a **Prusa
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
- **Prusa Connect (optional)** — sign in once and your whole fleet appears automatically,
  including Buddy-embedded printers you'd otherwise reach only through the cloud. The
  device quietly learns each printer's LAN address + PrusaLink key, so it keeps working
  locally even if your Connect sign-in later expires.
- **Multi-printer** — add as many as you like, on the screen or from the built-in web UI.
- **Just works on the network** — Wi-Fi onboarding on-device; if there's no known
  network it opens its own `PrusaTouch-XXXX` hotspot so you can set it up from a phone.
- **Updates over the air** — flash new firmware from its web page any time, or opt in to
  **automatic updates** (off by default) to have it pull new releases from GitHub on its own.

## Supported printers

- **Prusa** over **PrusaLink** — MK4 / MK4S / MK3.5 / MK3.9 / MINI / CORE One / XL
  (Buddy-embedded PrusaLink), and Pi-hosted PrusaLink. Add the printer's IP + API key.
- **Klipper** over **Moonraker** — anything you'd reach with Fluidd or Mainsail. Add
  the host with port **7125** (e.g. `192.168.1.50:7125`); the backend is auto-detected.
- **Prusa Connect** — any printer linked to your Prusa account, added all at once by
  signing in (see [Connecting to Prusa Connect](#connecting-to-prusa-connect) below).

## Hardware

| | |
|---|---|
| Board | BigTreeTech K-Touch (5") / Panda Touch (7") — ESP32-S3, 8 MB PSRAM, 16 MB flash |
| Display | 800×480 IPS, GT911 capacitive touch |

## Install (prebuilt image)

Get the latest **`prusa-touch-full.bin`** from the [Releases page](https://github.com/nomadsgalaxy/Prusa-Connect-Touch/releases).

### Option A: Web Serial (Easiest - All Platforms)
No command line required. Use a Chromium-based browser (Chrome, Edge, or Brave).
1. Go to the [ESP Web Flasher](https://espressif.github.io/esptool-js/).
2. Connect your touchscreen to your computer via USB-C.
3. Click **Connect**, select the serial port (see "Finding your Port" below).
4. Set the address to `0x0` and select your downloaded `prusa-touch-full.bin`.
5. Click **Program**.
6. Click Disconnect once the code says;
   ```
   File  md5: xxxxxxxxxxxxxxxx
   Flash md5: xxxxxxxxxxxxxxxx
   Hash of data verified.
   Leaving...
   Hard resetting via RTS pin...
   ```

### Option B: Windows GUI
Use the [ESP32 Download Tool](https://www.espressif.com/en/support/download/other-tools).
1. Select **ESP32-S3** and **WorkMode: Develop**.
2. Check the first row, select the `.bin` file, and set the address to `0x0`.
3. Select the correct **COM port** and set the baud rate to `460800`.
4. Click **Start**.

### Option C: Command Line (esptool)
Best for power users on any platform.

1. Install `esptool`: `pip install esptool`.
2. Connect the screen and flash:
   ```bash
   esptool.py --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 prusa-touch-full.bin
   ```

#### Finding your `<PORT>`
- **Windows:** Check *Device Manager* under "Ports (COM & LPT)". It will be something like `COM3`.
- **macOS:** Open Terminal and run `ls /dev/cu.*`. Look for `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.
- **Linux:** Open Terminal and run `ls /dev/ttyACM*`. Usually it is `/dev/ttyACM0`.

### First Boot & Setup
1. On first boot, the screen will show a Wi-Fi setup page.
2. Either pick your network on-screen, or join the `PrusaTouch-XXXX` hotspot from your phone and open `http://192.168.4.1`.
3. Once connected, add printers one of two ways: enter each printer's IP address + API
   key, **or** sign in to Prusa Connect to pull them all in at once (next section).

## Connecting to Prusa Connect

Signing in to Prusa Connect adds **every printer on your Prusa account in one step** —
no hunting for IP addresses or API keys, and it reaches Buddy-embedded printers (MK4,
MINI, CORE One, XL) that you'd otherwise only see in the cloud.

1. Find the touchscreen's address. It's shown on the device's **Settings → About**
   screen, e.g. `http://192.168.0.42/`. Open that page in a browser on the same network.
2. Click the **Account** tab, then **Link Account**.
3. Enter your Prusa account **email and password**. If you use two-factor
   authentication, you'll be asked for your 6-digit code next.
4. That's it — your printers appear on the dashboard within a few seconds. Tap any one
   for live status, files, and control.

> Your password is used only to sign in and is **never stored on the device** — only the
> resulting session token is kept, exactly like the Prusa Connect website.

### Staying connected when your sign-in expires

Prusa Connect sessions don't last forever. To keep working through an expiry, the device
**automatically learns each printer's local LAN address and PrusaLink key** the first
time it sees the printer after you link your account. If your Connect sign-in later
lapses, it transparently falls back to talking to each printer **directly over your LAN**
— status, files, and control keep working.

When that happens you'll also see a one-line banner on the dashboard:

> ⚠ *Prusa Connect sign-in expired. Reconnect from http://&lt;device-ip&gt;/ → Account.
> Local printers stay reachable.*

To restore the cloud link, just open the device's web page, go to the **Account** tab,
and **Link Account** again. You never have to type your credentials on the touchscreen
itself — re-authentication always happens from the comfort of a real keyboard.

---

> [!IMPORTANT]
> **Back up the stock firmware first** — flashing replaces BigTreeTech's firmware.
> `esptool.py -p <PORT> -b 460800 read_flash 0 0x1000000 ktouch_stock_backup.bin`
> If the device doesn't enter download mode on its own, hold the **BOOT** button while tapping **RESET**.

> [!NOTE]
> The full `prusa-touch-full.bin` writes from `0x0` and **resets stored settings** (Wi-Fi,
> linked Prusa account, configured printers) — perfect for a first install, but it means
> re-flashing it later wipes your config. **To update an existing device, use the in-app
> OTA** (Firmware tab / automatic updates), which keeps all your settings.

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
