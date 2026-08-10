#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * USB HID keyboard on the USB-A OTG port.
 *
 * Requires the USB host library already running (pt_usb_start). Installs the
 * Espressif HID host class driver alongside MSC — plug a keyboard *or* a stick
 * (or a hub for both).
 *
 * Nav keys: Esc / Home → fleet home. Arrows cycle focus among objects marked
 * with ui_kb_focus_add(); Enter activates the focused control. Printable keys
 * go to a focused textarea (console gcode entry).
 */
bool usb_hid_kb_start(void);
bool usb_hid_kb_is_connected(void);

/** Mark a clickable control as keyboard-focusable (orange outline when focused). */
void ui_kb_focus_add(lv_obj_t *obj);
/** Move keyboard focus to an object (LVGL thread only). */
void ui_kb_focus_set(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif
