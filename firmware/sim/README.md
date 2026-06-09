# Prusa-Touch UI simulator

A headless desktop build of the **real `main/ui.c` screens** that renders to PNG/BMP — so
you can iterate on layout (especially the portrait/landscape work) in seconds without
flashing hardware. The hardware and network layers are stubbed; mock fleet data is injected
so the screens look realistic.

It is **layout-accurate, not a full emulator**: it renders the same LVGL widget tree the
device builds, at the same logical resolution (800×480 landscape, 480×800 portrait), using
the same fonts/theme/assets. It does **not** run the cloud client, touch input, or the
device's 90°/270° framebuffer rotation (the sim just creates the window at the target size).
Always confirm the final result on the device.

## Run it

Everything runs inside the ESP-IDF Docker image (only for its host gcc + cmake — no SDL/X11
needed, output is a BMP file):

```bash
cd firmware
docker run --rm -v "$PWD:/project" -w /project/sim espressif/idf:v5.3.1 \
  bash -c "cmake -S . -B build >/dev/null && cmake --build build -j4 && \
           ./build/pt_sim status 480 800 out/status_port.bmp"
```

`pt_sim <screen> <W> <H> <out.bmp>`:

- `<screen>`: `dash` · `status` · `files` · `printers` (Settings) · `control` · `wifi` ·
  `about` · `prefs` · `farm`
- `<W> <H>`: `800 480` for landscape, `480 800` for portrait
- output is a 24-bit BMP (convert with any tool, e.g. `python -c "from PIL import Image;
  Image.open('out/x.bmp').save('out/x.png')"`)

`./run.sh` with no args builds once and renders every screen in both orientations into
`sim/out/`.

## How it's wired

| file | role |
|---|---|
| `sim_main.c` | LVGL init, mock fleet, build UI, navigate, render one settled frame → BMP |
| `sim_stubs.c` | stubs for every non-LVGL symbol `ui.c` calls (app_state / printer_store / wifi / prefs / pt_display) + a mock printer store + `strlcpy`/`strlcat` (absent on glibc < 2.38) |
| `lv_conf.h` | host LVGL config mirroring the device's feature set (16-bit colour, Montserrat fonts, flex/grid, the widgets `ui.c` uses) |
| `shim/` | tiny host headers standing in for `esp_attr.h` / `esp_heap_caps.h` / `esp_err.h` / `esp_lcd_panel_ops.h` |
| `CMakeLists.txt` | compiles `ui.c` + the `pt_*.c` assets + all of LVGL (minus the platform drivers) |

To change what the screens show (printer states, the attention dialog, etc.), edit the mock
builders in `sim_main.c`. `build/` and `out/` are git-ignored.
