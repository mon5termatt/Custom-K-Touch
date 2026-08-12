#!/usr/bin/env python3
"""Convert Klipper logo PNG to LVGL ARGB8888 C asset (pt_klipper.c)."""
from pathlib import Path
from PIL import Image

SIZE = 48
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tools" / "klipper-logo-small.png"
OUT = ROOT / "main" / "pt_klipper.c"

src = Image.open(SRC).convert("RGBA")
max_side = SIZE - 4
scale = min(max_side / src.width, max_side / src.height)
nw, nh = max(1, int(src.width * scale)), max(1, int(src.height * scale))
src = src.resize((nw, nh), Image.Resampling.LANCZOS)
canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
canvas.paste(src, ((SIZE - nw) // 2, (SIZE - nh) // 2), src)

data = bytearray()
for y in range(SIZE):
    for x in range(SIZE):
        r, g, b, a = canvas.getpixel((x, y))
        if a and r < 24 and g < 24 and b < 24:
            a = 0
        data.extend((b, g, r, a))

assert len(data) == SIZE * SIZE * 4

lines = []
row = []
for byte in data:
    row.append(f"0x{byte:02x}")
    if len(row) == 12:
        lines.append("    " + ",".join(row) + ",")
        row = []
if row:
    lines.append("    " + ",".join(row))

body = "\n".join(lines)
content = f"""#define LV_LVGL_H_INCLUDE_SIMPLE 1

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(LV_BUILD_TEST)
#include "../lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_PT_KLIPPER
#define LV_ATTRIBUTE_PT_KLIPPER
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_PT_KLIPPER
uint8_t pt_klipper_map[] = {{
{body}
}};

const lv_image_dsc_t pt_klipper = {{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_ARGB8888,
  .header.flags = 0,
  .header.w = {SIZE},
  .header.h = {SIZE},
  .header.stride = {SIZE * 4},
  .data_size = sizeof(pt_klipper_map),
  .data = pt_klipper_map,
}};
"""

OUT.write_text(content, newline="\n")
print(f"Wrote {OUT} ({len(data)} bytes)")
