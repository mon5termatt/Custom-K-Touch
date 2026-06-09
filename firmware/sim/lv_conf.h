/* Host LVGL config for the Prusa-Touch desktop simulator (headless render-to-BMP).
 * Mirrors the device's feature set (16-bit colour, Montserrat fonts, flex/grid, the
 * widgets ui.c uses) but uses the C-library allocator + a manual tick. NOT used on device
 * (that build's lv_conf is generated from Kconfig/sdkconfig). */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ---- colour ---- */
#define LV_COLOR_DEPTH 16

/* ---- memory / stdlib: use the real C library on host ---- */
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING   LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_CLIB

/* ---- OS / tick: single-threaded host, tick supplied via lv_tick_set_cb ---- */
#define LV_USE_OS LV_OS_NONE

/* ---- drawing ---- */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_SUPPORT_RGB565    1
#define LV_DRAW_SW_SUPPORT_RGB565A8  1
#define LV_DRAW_SW_SUPPORT_RGB888    1
#define LV_DRAW_SW_SUPPORT_XRGB8888  1
#define LV_DRAW_SW_SUPPORT_ARGB8888  1
#define LV_DRAW_SW_SUPPORT_L8        1
#define LV_DRAW_SW_SUPPORT_AL88      1
#define LV_DRAW_SW_SUPPORT_A8        1
#define LV_DRAW_SW_SUPPORT_I1        1
#define LV_DRAW_SW_COMPLEX           1

/* ---- image decoders (so ui.c's decoder calls link; thumbnails won't load w/o net) ---- */
#define LV_USE_LODEPNG 1
#define LV_USE_TJPGD   1
#define LV_BIN_DECODER_RAM_LOAD 1

/* ---- in-memory FS (device uses letter 'M' = 77) ---- */
#define LV_USE_FS_MEMFS 1
#define LV_FS_MEMFS_LETTER 'M'

/* ---- layouts ---- */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* ---- widgets used by ui.c ---- */
#define LV_USE_LABEL       1
#define LV_USE_IMAGE       1
#define LV_USE_IMAGEBUTTON 1
#define LV_USE_BUTTON      1
#define LV_USE_KEYBOARD    1
#define LV_USE_TEXTAREA    1
#define LV_USE_DROPDOWN    1
#define LV_USE_SWITCH      1
#define LV_USE_LIST        1
#define LV_USE_SPINNER     1
#define LV_USE_BAR         1
#define LV_USE_QRCODE      1
#define LV_USE_MSGBOX      1

/* ---- themes ---- */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE  1

/* ---- fonts (match ui.c) ---- */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* FontAwesome symbols (LV_SYMBOL_*) are merged into the Montserrat fonts on device. */
#define LV_USE_FONT_COMPRESSED 0

/* ---- misc ---- */
#define LV_USE_OBSERVER 1
#define LV_USE_LOG 0

#endif /* LV_CONF_H */
