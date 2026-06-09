# Prusa Touch

A community touchscreen for your 3D printers, built on the **BigTreeTech K-Touch** (5")
and **Panda Touch** (5"). It works two ways: talk **straight to your printers over your
LAN** (no companion app, no cloud account, no extra hardware), or **sign in to Prusa
Connect** and pull your whole fleet in at once. Either way you get a Prusa Connect-style
fleet dashboard on the desk.

> Independent, open project. **Not affiliated with or endorsed by Prusa Research.**
> "Prusa" and "Prusa Connect" are trademarks of Prusa Research.

## What it does

- **Fleet dashboard.** Every printer at a glance: state, temperatures, progress, the model
  render, and the live gcode thumbnail while a job runs.
- **Printer view.** Tap a printer for the detail screen: status badge, nozzle / heatbed /
  speed / Z, and the current job with progress and ETA.
- **Files, including print-from-USB.** Browse the printer's gcode newest-first with
  thumbnails and start a print with a tap. Plugged a USB stick into the printer? Switch the
  Files header between the printer's storage and the USB drive, the way the stock K-Touch
  screen does.
- **Control.** Preheat presets, set temperatures, jog and home. Controls that a backend
  doesn't expose hide themselves.
- **Needs-attention dialogs.** When a printer wants attention (a heater timeout, filament
  runout, that kind of thing) its dialog shows up on the detail screen with the same buttons
  you'd see in Connect, so you can clear it from the touchscreen.
- **Prusa Connect (optional).** Sign in once and the whole fleet appears, including
  Buddy-embedded printers you'd otherwise only reach through the cloud. The device quietly
  learns each printer's LAN address and PrusaLink key, so it keeps working locally even if
  your Connect sign-in later expires.
- **Portrait or landscape.** Rotate the UI to match how you mounted the screen. Landscape and
  portrait, each with a 180° flip.
- **Multi-printer.** Add as many as you like, on the screen or from the built-in web UI.
- **Network onboarding.** Pick your Wi-Fi on-device; with no known network it raises its own
  `PrusaTouch-XXXX` hotspot so you can set it up from a phone.
- **OTA updates.** Flash new firmware from its web page, or turn on automatic updates (off by
  default) to have it pull releases from GitHub on its own.

## Supported printers

- **Prusa** over **PrusaLink** - MK4 / MK4S / MK3.5 / MK3.9 / MINI / CORE One / XL
  (Buddy-embedded PrusaLink), and Pi-hosted PrusaLink. Add the printer's IP + API key.
- **Klipper** over **Moonraker** - anything you'd reach with Fluidd or Mainsail. Add the host
  with port **7125** (e.g. `192.168.1.50:7125`); the backend is auto-detected.
- **Prusa Connect** - any printer on your Prusa account, added all at once by signing in (see
  [Connecting to Prusa Connect](#connecting-to-prusa-connect)).

## Hardware

| | |
|---|---|
| Board | BigTreeTech K-Touch (5") / Panda Touch (7") - ESP32-S3, 8 MB PSRAM, 16 MB flash |
| Display | 800×480 IPS, GT911 capacitive touch |

## Install (prebuilt image)

Grab the latest **`prusa-touch-full.bin`** from the [Releases page](https://github.com/nomadsgalaxy/Prusa-Connect-Touch/releases).

### Option A: Web Serial (easiest, all platforms)
No command line. Use a Chromium-based browser (Chrome, Edge, Brave).
1. Open the [ESP Web Flasher](https://espressif.github.io/esptool-js/).
2. Connect the touchscreen over USB-C.
3. Click **Connect** and pick the serial port (see "Finding your port" below).
4. Set the address to `0x0`, select your `prusa-touch-full.bin`.
5. Click **Program**.
6. Disconnect once you see:
   ```
   File  md5: xxxxxxxxxxxxxxxx
   Flash md5: xxxxxxxxxxxxxxxx
   Hash of data verified.
   Leaving...
   Hard resetting via RTS pin...
   ```

### Option B: Windows GUI
Use the [ESP32 Download Tool](https://www.espressif.com/en/support/download/other-tools).
1. Pick **ESP32-S3** and **WorkMode: Develop**.
2. Check the first row, select the `.bin`, set the address to `0x0`.
3. Pick the **COM port**, set baud to `460800`.
4. Click **Start**.

### Option C: Command line (esptool)
1. `pip install esptool`
2. Connect the screen and flash:
   ```bash
   esptool.py --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 prusa-touch-full.bin
   ```

#### Finding your `<PORT>`
- **Windows:** *Device Manager* under "Ports (COM & LPT)", something like `COM3`.
- **macOS:** `ls /dev/cu.*`, look for `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.
- **Linux:** `ls /dev/ttyACM*`, usually `/dev/ttyACM0`.

### First boot
1. First boot shows a Wi-Fi setup page.
2. Pick your network on-screen, or join the `PrusaTouch-XXXX` hotspot from your phone and open `http://192.168.4.1`.
3. Once connected, add printers one of two ways: enter each printer's IP + API key, or sign
   in to Prusa Connect to pull them all in (next section).

## Connecting to Prusa Connect

Signing in adds **every printer on your Prusa account in one step**: no hunting for IP
addresses or API keys, and it reaches the Buddy-embedded printers (MK4, MINI, CORE One, XL)
you'd otherwise only see in the cloud.

1. Find the touchscreen's address. It's on the device's **Settings → About** screen, e.g.
   `http://192.168.0.42/`. Open that in a browser on the same network.
2. Click the **Account** tab, then **Link Account**.
3. Enter your Prusa account email and password. With two-factor on, you'll be asked for your
   6-digit code next.
4. Done. Your printers show up on the dashboard within a few seconds. Tap one for live
   status, files, and control.

> By default the password is only used to sign in and isn't kept, just the resulting session
> token, same as the Prusa Connect website. Tick **"Stay signed in"** and the device also
> saves your password (in flash) so it can re-link on its own if the session ever fully
> expires. Fine for a device you control; leave it off if someone else could read the flash.
> Either way the session token already lives in flash, so treat the device as holding account
> access.

### Staying connected when your sign-in expires

Connect sessions don't last forever. The device **learns each printer's LAN address and
PrusaLink key** the first time it sees the printer after you link your account. If the Connect
sign-in later lapses, it falls back to talking to each printer **directly over your LAN**, and
status / files / control keep working.

If you ticked **"Stay signed in"**, it goes further and **re-links automatically** with your
saved login, no action needed. (Not possible with two-factor accounts, which need a fresh code
each time.)

Otherwise you'll see a one-line banner on the dashboard:

> ⚠ *Prusa Connect sign-in expired. Reconnect from http://&lt;device-ip&gt;/ → Account.
> Local printers stay reachable.*

To restore the cloud link, open the device's web page, go to the **Account** tab, and
**Link Account** again. You never type credentials on the touchscreen; re-auth happens on a
real keyboard.

---

> [!IMPORTANT]
> **Back up the stock firmware first** - flashing replaces BigTreeTech's firmware.
> `esptool.py -p <PORT> -b 460800 read_flash 0 0x1000000 ktouch_stock_backup.bin`
> If the device won't enter download mode on its own, hold **BOOT** while tapping **RESET**.

> [!NOTE]
> The full `prusa-touch-full.bin` writes from `0x0` and **resets stored settings** (Wi-Fi,
> linked account, configured printers). Great for a first install, but re-flashing it later
> wipes your config. To update an existing device, use the in-app **OTA** (Firmware tab /
> automatic updates), which keeps your settings.

## Build from source

See [`firmware/README.md`](firmware/README.md). With ESP-IDF or its Docker image:

```bash
cd firmware && idf.py set-target esp32s3 && idf.py build
```

[`firmware/scripts/`](firmware/scripts/) wraps the build / flash / capture / reset workflow,
and [`firmware/sim/`](firmware/sim/) is a desktop build of the UI that renders the real screens
to PNG in seconds, handy for layout work without flashing.

## Developer API

The device exposes a small HTTP API (status, fleet, printer config, Wi-Fi, OTA, a live screen
mirror, remote UI navigation) for integrations and troubleshooting. See
[`docs/API.md`](docs/API.md).

## License

[OCL v1.1 + SWAtt v1](LICENSE), with attribution in [`NOTICE`](NOTICE). Built on the
MIT-licensed [`PandaTouch_IDF`](https://github.com/bigtreetech/PandaTouch_IDF) BSP and
[LVGL](https://lvgl.io).
