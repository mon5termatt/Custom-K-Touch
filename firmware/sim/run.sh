#!/usr/bin/env bash
# Build the host LVGL simulator and render screens to BMP (headless, no SDL/X needed).
# Runs inside the ESP-IDF image purely for its host gcc + cmake. Usage:
#   ./run.sh                      # build + render the default screen set (both orientations)
#   ./run.sh <screen> <W> <H>     # render one screen at WxH
set -e
cd "$(dirname "$0")"

mkdir -p build out
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" 2>&1 | tail -3

render() { ./build/pt_sim "$1" "$2" "$3" "out/$4"; }

if [ -n "$1" ]; then
  render "$1" "${2:-800}" "${3:-480}" "${1}_${2:-800}x${3:-480}.bmp"
else
  # Default sweep: the key screens in landscape AND portrait for side-by-side review.
  for s in dash status files printers control wifi about; do
    render "$s" 800 480 "${s}_land.bmp"
    render "$s" 480 800 "${s}_port.bmp"
  done
fi
echo "done -> sim/out/"
