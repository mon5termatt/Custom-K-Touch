#!/usr/bin/env bash
# Grab a live mirror of the 800x480 panel over the device's HTTP test API and save it as PNG.
# Handy for reviewing UI changes without a camera. Requires Python + Pillow for the PNG step
# (the device serves a 24-bit BMP).
#
# Usage: scripts/capture.sh [DEVICE_IP] [OUT.png] [SCREEN]
#   SCREEN (optional) navigates first: dash|status|files|printers|control|wifi|about
set -e
IP="${1:-192.168.0.203}"
OUT="${2:-screen.png}"
SCREEN="${3:-}"
[ -n "$SCREEN" ] && curl -s -m 8 "http://$IP/api/ui/nav?screen=$SCREEN" >/dev/null && sleep 1
TMP="$(mktemp --suffix=.bmp)"
curl -s -m 40 "http://$IP/api/screen.bmp" -o "$TMP"
python -c "from PIL import Image; Image.open('$TMP').save('$OUT')"
rm -f "$TMP"
echo "saved $OUT"
