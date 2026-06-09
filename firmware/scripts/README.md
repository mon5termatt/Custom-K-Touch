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

Typical loop:

```bash
scripts/build.sh
scripts/flash.sh COM4
scripts/capture.sh 192.168.0.203 dash.png dash
```

## UI simulator

For **layout iteration without hardware**, see [`../sim/`](../sim/README.md) — a headless
desktop build of the real `ui.c` screens that renders to PNG in seconds (great for the
portrait/landscape work). Confirm final results on the device.
