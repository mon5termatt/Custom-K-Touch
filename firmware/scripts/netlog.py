#!/usr/bin/env python3
"""Network log poller for Prusa-Touch — "serial over WiFi".

The device tees its entire ESP-IDF console into a fixed 64 KB rolling ring buffer (bounded
memory) and exposes it at GET /api/log?since=<seq>. This script polls that endpoint and
accumulates the full, unbounded console history on your computer — no USB cable needed.

Usage:
    python netlog.py [device-ip-or-host]      # default: prusatouch.local

Writes (in the current directory):
    netlog_full.log      every console line, timestamped (ANSI stripped)
    netlog_connect.log   the Prusa Connect / auth subset (handy for auth debugging)

Survives device reboots (detects the sequence reset and resyncs) and network blips.
"""
import urllib.request, time, datetime, re, sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "prusatouch.local"
URL  = f"http://{HOST}/api/log"
POLL = 2.0   # seconds; the device ring (64 KB) holds many minutes, so this never falls behind

# A line is mirrored to netlog_connect.log if it mentions the connect component or any auth signal.
KEYS = ("connect:", "login", "token", "refresh", "re-auth", "reauth", "csrf",
        "logged out", "expired", "unauthor", " 401", " 403", "authoriz", "oauth")
ANSI = re.compile(r"\x1b\[[0-9;]*m")

def ts():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

def main():
    ffull = open("netlog_full.log", "a", encoding="utf-8", errors="replace")
    fconn = open("netlog_connect.log", "a", encoding="utf-8", errors="replace")
    hdr = f"\n==== netlog started {ts()} -> {HOST} ====\n"
    ffull.write(hdr); fconn.write(hdr); ffull.flush(); fconn.flush()

    since, partial = 0, ""
    while True:
        try:
            with urllib.request.urlopen(f"{URL}?since={since}", timeout=8) as r:
                body = r.read().decode("utf-8", "replace")
                head = int(r.headers.get("X-Log-Head", since))
                oldest = int(r.headers.get("X-Log-Oldest", 0))
            if head < since:                     # device rebooted -> sequence reset
                note = f"{ts()} [netlog] head reset {since}->{head} (device rebooted?)\n"
                ffull.write(note); fconn.write(note); ffull.flush(); fconn.flush()
                partial = ""
            elif since and since < oldest:        # we fell behind the ring -> data gap
                note = f"{ts()} [netlog] GAP — {oldest - since} bytes lost (poller fell behind)\n"
                ffull.write(note); fconn.write(note); ffull.flush(); fconn.flush()
            since = head
            partial += body
            while "\n" in partial:
                line, partial = partial.split("\n", 1)
                txt = ANSI.sub("", line).rstrip("\r")
                if not txt:
                    continue
                stamp = ts()
                ffull.write(f"{stamp} {txt}\n"); ffull.flush()
                if any(k in txt.lower() for k in KEYS):
                    fconn.write(f"{stamp} {txt}\n"); fconn.flush()
        except Exception as e:
            ffull.write(f"{ts()} [netlog] poll error: {e}\n"); ffull.flush()
            time.sleep(3)
            continue
        time.sleep(POLL)

if __name__ == "__main__":
    main()
