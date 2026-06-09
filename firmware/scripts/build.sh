#!/usr/bin/env bash
# Build the firmware in the pinned ESP-IDF Docker image (no local IDF install needed).
# Usage: scripts/build.sh   (run from the firmware/ directory or anywhere — it cd's itself)
set -e
FW="$(cd "$(dirname "$0")/.." && pwd)"
docker run --rm -v "$FW:/project" -w /project espressif/idf:v5.3.1 idf.py build
echo "Build artifacts in $FW/build/ (prusa-touch.bin)."
