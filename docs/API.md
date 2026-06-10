# Prusa Touch — device HTTP API

The firmware runs a small HTTP server on the device (port 80) for status, configuration,
OTA, and troubleshooting. All endpoints are also reachable over the Wi-Fi provisioning
hotspot at `http://192.168.4.1` when no network is configured.

Base URL: `http://<device-ip>/` (find the IP on the device, or use `prusatouch.local`
if mDNS resolves on your network).

> These endpoints are unauthenticated and intended for a trusted LAN. Don't expose the
> device to the internet.

## Status & info

### `GET /api/info`
Device + firmware health. Handy first stop when troubleshooting.
```json
{ "name":"Prusa Connect Touch", "fw":"0.2.0-proto", "idf":"v5.3.1",
  "model":"BTT K-Touch / Panda Touch (ESP32-S3)", "screen":"800x480",
  "heap_free":5700000, "psram_free":5600000, "uptime_s":1234 }
```
`heap_free` trending down over time → a leak; `uptime_s` resetting unexpectedly → a crash/reboot.

### `GET /api/status`
The **active** printer's latest poll.
```json
{ "name":"Apollo", "online":true, "state":"PRINTING", "nozzle":215, "tnozzle":215,
  "bed":60, "tbed":60, "has_job":true, "progress":42, "job":"part.gcode" }
```
`online:false` → the last poll failed (wrong IP/API key, printer off, or network). `state`
is the raw backend state (`IDLE`/`PRINTING`/`PAUSED`/`FINISHED`/`ERROR`/...).

### `GET /api/fleet`
One entry per configured printer (drives the dashboard).
```json
[ { "name":"Apollo", "online":true, "state":"PRINTING", "nozzle":215, "bed":60,
    "printing":true, "progress":42 }, ... ]
```

## Printer management

### `GET /api/printers`
```json
[ { "i":0, "name":"Apollo", "host":"192.168.1.50", "active":true, "haskey":true }, ... ]
```
`haskey:true` but `online:false` in `/api/status` → the stored API key is likely wrong
(re-enter it). For Klipper/Moonraker printers set the host with port `7125`
(e.g. `192.168.1.50:7125`); the backend is auto-detected.

### `POST /api/printers`  — add a printer
Body: `{ "name":"Apollo", "host":"192.168.1.50", "key":"<api-key>" }`

### `POST /api/printers/update`  — edit (key blank = keep existing)
Body: `{ "i":0, "name":"...", "host":"...", "key":"..." }`

### `POST /api/printers/remove`
Body: `{ "i":0 }`

### `POST /api/printers/active`  — set the active printer
Body: `{ "i":0 }`

## Wi-Fi

### `POST /api/wifi`
Body: `{ "ssid":"MyNetwork", "pass":"secret" }` — saves to NVS and (re)connects. If no
known network is found at boot, the device opens an open `PrusaTouch-XXXX` SoftAP at
`192.168.4.1` so you can post here.

## UI / screen (debug + automated testing)

### `GET /api/screen.bmp`
Returns the live 800×480 panel framebuffer as a BMP (RGB565→24-bit). Great for remote
inspection / screenshots. Convert to PNG with any image tool.

### `GET /api/ui`
`{ "screen":"dash" }` — the currently shown screen.

### `GET /api/ui/nav?screen=<name>`
Navigate the on-device UI remotely. `<name>` ∈
`dash | status | control | files | printers | prefs | wifi | about`.
Navigation is async (marshaled to the LVGL thread), so the reply's `current` reflects the
*pre-nav* screen — re-query `/api/ui` (or grab `/api/screen.bmp`) ~0.5 s later to confirm.
```
GET /api/ui/nav?screen=files   ->  { "ok":true, "requested":"files", "current":"dash" }
```
Pair these two endpoints to script UI walkthroughs: nav → wait → screen.bmp.

### `GET /api/log?since=<seq>`  — console log over WiFi
The device tees its entire ESP-IDF console into a fixed **64 KB rolling ring buffer** in PSRAM
(bounded memory — it overwrites oldest, never grows) and serves it here, so you get "serial over
the network" with no USB cable. Returns the console bytes from sequence `since` to newest as
`text/plain`, plus headers:
- `X-Log-Head` — the current write sequence; pass it as the next `?since=` for incremental polling.
- `X-Log-Oldest` — the oldest sequence still buffered; if your `since` is below it you missed some
  (there was a gap — poll more often or the ring wrapped).

```
GET /api/log?since=0     ->  (recent console text)   X-Log-Head: 4372  X-Log-Oldest: 0
GET /api/log?since=4372  ->  (only what's new since)  X-Log-Head: 4510
```
Use [`scripts/netlog.py`](../firmware/scripts/netlog.py) to poll this and accumulate the full
history to disk — handy for watching the Prusa Connect auth/token lifecycle over a long run.

## Firmware update (OTA)

Updates pull the standalone **app** image (`prusa-touch-app.bin`) from the latest GitHub
Release and flash it into the inactive OTA slot via `esp_https_ota`, with an app-identity
check and a bootloader rollback safety net. Manual updates (below) work regardless of the
auto-update setting; **automatic** updates are opt-in (off by default) — enable
*Preferences → Automatic firmware updates* on the device. When enabled, the device checks
GitHub roughly every 6 hours and applies any release with a newer version tag.

### `GET /api/update/check`
Checks the GitHub Releases of `nomadsgalaxy/Prusa-Connect-Touch` for a newer build.

### `POST /api/update/apply`
Triggers the self-update (download + flash via `esp_https_ota`, with an app-identity check
and rollback safety net). Unaffected by the auto-update opt-in.

### `POST /update`
Manual firmware upload — POST a raw `prusa-touch.bin` app image (multipart/binary) to flash
the inactive OTA slot. Always keep the USB-C recovery path available.

## Troubleshooting checklist

| Symptom | Check |
|---|---|
| Printer shows offline | `GET /api/status` → `online:false`; verify IP/port and API key; for Klipper use `:7125` |
| CONTROL button missing | Expected on PrusaLink (Buddy doesn't run remote gcode); shown for Klipper/Moonraker |
| No thumbnails | PrusaLink large thumbnails are PNG at `refs.thumbnail`; Moonraker thumbnails aren't wired yet |
| Screen frozen / reboots | `GET /api/info` for `uptime_s` resets + `heap_free`; capture serial @115200 for the panic backtrace |
| Can't reach the device | Join the `PrusaTouch-XXXX` hotspot and open `http://192.168.4.1` |
