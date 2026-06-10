#!/usr/bin/env python3
"""USB serial logger for Prusa-Touch.

Reads the ESP-IDF console over USB and accumulates it to disk, timestamped, with a Prusa Connect
auth subset broken out separately. Opens the port WITHOUT toggling DTR/RTS, so attaching the
logger does not reset the device. Reconnects if the USB drops.

For headless / battery use, prefer netlog.py (same idea over WiFi, no cable).

Usage:
    python seriallog.py [PORT]    # default: COM4   (e.g. /dev/ttyACM0 on Linux)

Writes (in the current directory): serial_full.log, serial_connect.log
"""
import serial, datetime, time, sys

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
BAUD = 115200

KEYS = ("connect:", "login", "token", "refresh", "re-auth", "reauth", "csrf",
        "logged out", "expired", "unauthor", " 401", " 403", "authoriz", "oauth")

def ts():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

def open_port():
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = PORT, BAUD, 1
    s.dtr = False; s.rts = False   # don't pulse the auto-reset line on open
    s.open()
    return s

def main():
    ffull = open("serial_full.log", "a", encoding="utf-8", errors="replace")
    fconn = open("serial_connect.log", "a", encoding="utf-8", errors="replace")
    while True:
        try:
            s = open_port()
        except Exception as e:
            ffull.write(f"{ts()} [logger] cannot open {PORT}: {e}\n"); ffull.flush()
            time.sleep(3); continue
        hdr = f"\n==== serial attached {ts()} ({PORT} @ {BAUD}) ====\n"
        ffull.write(hdr); fconn.write(hdr); ffull.flush(); fconn.flush()
        buf = b""
        try:
            while True:
                data = s.read(4096)
                if not data:
                    continue
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    txt = line.decode("utf-8", "replace").rstrip("\r")
                    stamp = ts()
                    ffull.write(f"{stamp} {txt}\n"); ffull.flush()
                    if any(k in txt.lower() for k in KEYS):
                        fconn.write(f"{stamp} {txt}\n"); fconn.flush()
        except Exception as e:
            ffull.write(f"{ts()} [logger] serial dropped: {e}; reconnecting\n"); ffull.flush()
            try: s.close()
            except Exception: pass
            time.sleep(3)

if __name__ == "__main__":
    main()
