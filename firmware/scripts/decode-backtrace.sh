#!/usr/bin/env bash
# Decode a Guru Meditation / panic backtrace into source file:line. Paste the addresses from
# the serial "Backtrace: 0x... 0x..." line as arguments. Uses the toolchain in the IDF image
# and the current build/klipper-touch.elf.
#
# Usage: scripts/decode-backtrace.sh 0x42019e13 0x42033b86 ...
set -e
FW="$(cd "$(dirname "$0")/.." && pwd)"
ADDRS="${*//0x/0x}"   # pass through; strip the "0x...:0x..." stack-ptr suffix yourself if present
docker run --rm -v "$FW:/project" -w /project espressif/idf:v5.3.1 \
  bash -c "xtensa-esp-elf-addr2line -pfiaC -e build/klipper-touch.elf $ADDRS" 2>&1 \
  | grep -vE 'Detecting|Checking|Python|Adding|/opt/esp|Done|idf.py|Requirement|being checked|Go to|^$'
