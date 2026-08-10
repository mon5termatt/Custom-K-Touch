#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * USB HID keyboard on the USB-A OTG port.
 *
 * Requires the USB host library already running (pt_usb_start). Installs the
 * Espressif HID host class driver alongside MSC — plug a keyboard *or* a stick
 * (or a hub for both). Keys are fed into LVGL as a keypad indev so focused
 * textareas (e.g. console gcode entry) receive them.
 */
bool usb_hid_kb_start(void);
bool usb_hid_kb_is_connected(void);

#ifdef __cplusplus
}
#endif
