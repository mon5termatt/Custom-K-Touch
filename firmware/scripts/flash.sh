#!/usr/bin/env bash
# Per-partition flash over USB serial. Writes each image to its own offset, which leaves the
# NVS partition (0x9000) UNTOUCHED — so Wi-Fi credentials, the linked Prusa account, and your
# configured printers all survive the flash.
#
# Do NOT flash the merged prusa-touch-full.bin at 0x0 on a configured device: merge-bin fills
# the gap that contains NVS with 0xFF, wiping your settings. The merged image is for fresh
# installs only; existing devices should update via the in-app OTA.
#
# Usage: scripts/flash.sh [PORT] [BAUD]    e.g. scripts/flash.sh COM4 460800
#        (defaults: PORT=COM4, BAUD=460800)
set -e
FW="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-COM4}"
BAUD="${2:-460800}"
cd "$FW"
python -m esptool --chip esp32s3 -p "$PORT" -b "$BAUD" --before default_reset --after hard_reset \
  write_flash --flash_mode dout --flash_size 16MB --flash_freq 80m \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0xe000  build/ota_data_initial.bin \
  0x10000 build/prusa-touch.bin
