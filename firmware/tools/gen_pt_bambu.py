#!/usr/bin/env python3
"""Download Bambu printer icons from bambuddy and emit LVGL ARGB8888 assets.

Source: https://github.com/maziggy/bambuddy/tree/main/static/img/printers
"""
from pathlib import Path
from urllib.request import urlretrieve

from PIL import Image

SIZE = 96
ROOT = Path(__file__).resolve().parents[1]
ICON_DIR = ROOT / "tools" / "bambu"
OUT_C = ROOT / "main" / "pt_bambu_icons.c"
OUT_H = ROOT / "main" / "pt_bambu.h"
BASE_URL = "https://raw.githubusercontent.com/maziggy/bambuddy/main/static/img/printers"

# (symbol suffix, remote filename)
ICONS = [
    ("default", "default.png"),
    ("x1c", "x1c.png"),
    ("x1e", "x1e.png"),
    ("x2d", "x2d.png"),
    ("p1p", "p1p.png"),
    ("p1s", "p1s.png"),
    ("a1", "a1.png"),
    ("a1mini", "a1mini.png"),
    ("a2l", "a2l.png"),
]


def rgba_to_bgra(data: bytearray, canvas: Image.Image) -> None:
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = canvas.getpixel((x, y))
            data.extend((b, g, r, a))


def png_to_map(png: Path) -> bytearray:
    src = Image.open(png).convert("RGBA")
    max_side = SIZE - 4
    scale = min(max_side / src.width, max_side / src.height)
    nw, nh = max(1, int(src.width * scale)), max(1, int(src.height * scale))
    src = src.resize((nw, nh), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    canvas.paste(src, ((SIZE - nw) // 2, (SIZE - nh) // 2), src)
    data = bytearray()
    rgba_to_bgra(data, canvas)
    assert len(data) == SIZE * SIZE * 4
    return data


def fmt_bytes(data: bytearray) -> str:
    lines = []
    row = []
    for byte in data:
        row.append(f"0x{byte:02x}")
        if len(row) == 12:
            lines.append("    " + ",".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ",".join(row))
    return "\n".join(lines)


def main() -> None:
    ICON_DIR.mkdir(parents=True, exist_ok=True)
    c_parts = [
        "/* Bambu Lab printer icons - from maziggy/bambuddy (static/img/printers). */",
        "#define LV_LVGL_H_INCLUDE_SIMPLE 1",
        "",
        '#if defined(LV_LVGL_H_INCLUDE_SIMPLE)',
        '#include "lvgl.h"',
        "#elif defined(LV_BUILD_TEST)",
        '#include "../lvgl.h"',
        "#else",
        '#include "lvgl/lvgl.h"',
        "#endif",
        "",
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
        "#define LV_ATTRIBUTE_MEM_ALIGN",
        "#endif",
        "",
        "#ifndef LV_ATTRIBUTE_PT_BAMBU",
        "#define LV_ATTRIBUTE_PT_BAMBU",
        "#endif",
        "",
    ]
    h_parts = [
        "/* Bambu Lab printer icons - from maziggy/bambuddy (static/img/printers). */",
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
    ]

    for sym, fname in ICONS:
        local = ICON_DIR / fname
        if not local.is_file():
            url = f"{BASE_URL}/{fname}"
            print(f"Downloading {url}")
            urlretrieve(url, local)
        data = png_to_map(local)
        body = fmt_bytes(data)
        map_name = f"pt_bambu_{sym}_map"
        dsc_name = f"pt_bambu_{sym}"
        c_parts += [
            f"static const",
            f"LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_PT_BAMBU",
            f"uint8_t {map_name}[] = {{",
            body,
            "};",
            "",
            f"const lv_image_dsc_t {dsc_name} = {{",
            "  .header.magic = LV_IMAGE_HEADER_MAGIC,",
            "  .header.cf = LV_COLOR_FORMAT_ARGB8888,",
            "  .header.flags = 0,",
            f"  .header.w = {SIZE},",
            f"  .header.h = {SIZE},",
            f"  .header.stride = {SIZE * 4},",
            f"  .data_size = sizeof({map_name}),",
            f"  .data = {map_name},",
            "};",
            "",
        ]
        h_parts.append(f"extern const lv_image_dsc_t {dsc_name};")

    OUT_C.write_text("\n".join(c_parts) + "\n", newline="\n")
    OUT_H.write_text("\n".join(h_parts) + "\n", newline="\n")
    print(f"Wrote {OUT_C} and {OUT_H} ({len(ICONS)} icons)")


if __name__ == "__main__":
    main()
