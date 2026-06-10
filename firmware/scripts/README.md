# Developer scripts

The toolset used to build, flash, and debug Prusa-Touch — wrapped so anyone can replicate the
workflow. Everything runs through the pinned ESP-IDF Docker image (`espressif/idf:v5.3.1`), so
the only host prerequisites are **Docker**, **Python** (with `pyserial` + `Pillow`), and a
USB-serial connection to the board.

Run them from anywhere — each script resolves the firmware directory itself.

| script | what it does |
|---|---|
| `build.sh` | Build the firmware in the IDF Docker image. |
| `flash.sh [PORT] [BAUD]` | **Per-partition** flash over serial — preserves NVS (Wi-Fi creds, linked account, printers). See the warning in the file about the merged image. |
| `capture.sh [IP] [OUT.png] [SCREEN]` | Save a live mirror of the panel via the device's HTTP test API (optionally navigating to a screen first). |
| `reset.sh [PORT]` | Pulse a hardware reset over RTS without re-flashing (clears a wedged HTTP worker). |
| `decode-backtrace.sh 0x... 0x...` | Turn a panic backtrace into `file:line` using the current ELF. |
| `netlog.py [IP]` | **Live console over WiFi** — poll the device log ring and mirror it to disk (no USB). |
| `seriallog.py [PORT]` | Live console over USB, mirrored to disk; attaches without resetting the device. |

Typical loop:

```bash
scripts/build.sh
scripts/flash.sh COM4
scripts/capture.sh 192.168.0.203 dash.png dash
```

## Live logging (console capture)

Two ways to record the device's ESP-IDF console to disk over a long run — useful for watching
slow-moving behaviour like the Prusa Connect auth/token lifecycle.

**Over WiFi — `netlog.py` (no cable, works on battery).** The firmware tees its entire console
into a **fixed 64 KB rolling ring buffer** in PSRAM — bounded memory that never grows — and serves
it at `GET /api/log?since=<seq>` (with `X-Log-Head` / `X-Log-Oldest` sequence headers for
incremental, gap-aware polling). `netlog.py` polls that endpoint and accumulates the **full,
unbounded** history on your computer:

```bash
python scripts/netlog.py 192.168.0.203
# -> netlog_full.log      (everything, timestamped)
# -> netlog_connect.log   (the Prusa Connect / auth subset)
```

It survives device reboots (detects the sequence reset and resyncs) and Wi-Fi blips. The ring is
sized so a 2 s poll never falls behind; if it ever does (e.g. the poller was off for minutes), the
gap is flagged in the log rather than silently dropped.

**Over USB — `seriallog.py`.** Same idea via the serial port; opens without toggling DTR/RTS so
attaching the logger doesn't reset the device:

```bash
python scripts/seriallog.py COM4
# -> serial_full.log, serial_connect.log
```

## UI simulator

For **layout iteration without hardware**, see [`../sim/`](../sim/README.md) — a headless
desktop build of the real `ui.c` screens that renders to PNG in seconds (great for the
portrait/landscape work). Confirm final results on the device.
