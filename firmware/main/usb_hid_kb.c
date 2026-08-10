/*
 * USB HID keyboard host — experimental.
 *
 * Shares the USB host stack started by pt_usb_start() (MSC). Boot-protocol
 * keyboards only (same as Espressif's hid host example).
 */
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"

#include "lvgl.h"
#include "pandatouch_display.h"

#include "usb_hid_kb.h"

static const char *TAG = "usb_hid_kb";

#define KEY_Q_LEN 32

typedef struct {
    uint32_t key; /* LV_KEY_* or ASCII */
    bool pressed;
} kb_evt_t;

static QueueHandle_t s_key_q;
static lv_indev_t *s_indev;
static lv_group_t *s_group;
static volatile bool s_connected;
static bool s_started;

/* US layout boot-protocol map (Espressif HID example). */
static const uint8_t s_keycode2ascii[57][2] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {'a', 'A'}, {'b', 'B'}, {'c', 'C'}, {'d', 'D'}, {'e', 'E'},
    {'f', 'F'}, {'g', 'G'}, {'h', 'H'}, {'i', 'I'}, {'j', 'J'},
    {'k', 'K'}, {'l', 'L'}, {'m', 'M'}, {'n', 'N'}, {'o', 'O'},
    {'p', 'P'}, {'q', 'Q'}, {'r', 'R'}, {'s', 'S'}, {'t', 'T'},
    {'u', 'U'}, {'v', 'V'}, {'w', 'W'}, {'x', 'X'}, {'y', 'Y'},
    {'z', 'Z'},
    {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'},
    {'6', '^'}, {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'},
    {'\r', '\r'}, {0, 0}, {'\b', '\b'}, {'\t', '\t'}, {' ', ' '},
    {'-', '_'}, {'=', '+'}, {'[', '{'}, {']', '}'}, {'\\', '|'},
    {'\\', '|'}, {';', ':'}, {'\'', '"'}, {'`', '~'},
    {',', '<'}, {'.', '>'}, {'/', '?'},
};

static void push_key(uint32_t key, bool pressed)
{
    if (!s_key_q || !key) return;
    kb_evt_t e = { .key = key, .pressed = pressed };
    xQueueSend(s_key_q, &e, 0);
}

static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    kb_evt_t e;
    if (xQueueReceive(s_key_q, &e, 0) == pdTRUE) {
        data->key = e.key;
        data->state = e.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = (uxQueueMessagesWaiting(s_key_q) > 0);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static bool shift_mod(uint8_t modifier)
{
    return (modifier & (HID_LEFT_SHIFT | HID_RIGHT_SHIFT)) != 0;
}

static bool key_to_lv(uint8_t modifier, uint8_t key_code, uint32_t *out)
{
    if (key_code == HID_KEY_ENTER) {
        *out = LV_KEY_ENTER;
        return true;
    }
    if (key_code == HID_KEY_DEL /* Backspace in boot HID */) {
        *out = LV_KEY_BACKSPACE;
        return true;
    }
    if (key_code == HID_KEY_TAB) {
        *out = LV_KEY_NEXT;
        return true;
    }
    if (key_code == HID_KEY_RIGHT) {
        *out = LV_KEY_RIGHT;
        return true;
    }
    if (key_code == HID_KEY_LEFT) {
        *out = LV_KEY_LEFT;
        return true;
    }
    if (key_code == HID_KEY_DOWN) {
        *out = LV_KEY_DOWN;
        return true;
    }
    if (key_code == HID_KEY_UP) {
        *out = LV_KEY_UP;
        return true;
    }
    if (key_code == HID_KEY_ESC) {
        *out = LV_KEY_ESC;
        return true;
    }
    if (key_code >= HID_KEY_A && key_code <= HID_KEY_SLASH) {
        uint8_t ch = s_keycode2ascii[key_code][shift_mod(modifier) ? 1 : 0];
        if (!ch) return false;
        if (ch == '\r') {
            *out = LV_KEY_ENTER;
            return true;
        }
        if (ch == '\b') {
            *out = LV_KEY_BACKSPACE;
            return true;
        }
        *out = (uint32_t)ch;
        return true;
    }
    return false;
}

static inline bool key_in(const uint8_t *keys, uint8_t key, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (keys[i] == key) return true;
    }
    return false;
}

static void keyboard_report(const uint8_t *data, int length)
{
    if (length < (int)sizeof(hid_keyboard_input_report_boot_t)) return;
    const hid_keyboard_input_report_boot_t *rep =
        (const hid_keyboard_input_report_boot_t *)data;

    static uint8_t prev[HID_KEYBOARD_KEY_MAX];
    uint8_t mod = rep->modifier.val;

    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if (prev[i] > HID_KEY_ERROR_UNDEFINED &&
            !key_in(rep->key, prev[i], HID_KEYBOARD_KEY_MAX)) {
            uint32_t lv;
            if (key_to_lv(0, prev[i], &lv)) {
                push_key(lv, false);
            }
        }
        if (rep->key[i] > HID_KEY_ERROR_UNDEFINED &&
            !key_in(prev, rep->key[i], HID_KEYBOARD_KEY_MAX)) {
            uint32_t lv;
            if (key_to_lv(mod, rep->key[i], &lv)) {
                ESP_LOGI(TAG, "key 0x%02x -> lv 0x%" PRIx32, rep->key[i], lv);
                push_key(lv, true);
                push_key(lv, false); /* LVGL textareas want a press+release pair */
            }
        }
    }
    memcpy(prev, rep->key, HID_KEYBOARD_KEY_MAX);
}

static void iface_cb(hid_host_device_handle_t handle,
                     const hid_host_interface_event_t event,
                     void *arg)
{
    (void)arg;
    uint8_t buf[64];
    size_t len = 0;
    hid_host_dev_params_t params;
    if (hid_host_device_get_params(handle, &params) != ESP_OK) return;

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        if (hid_host_device_get_raw_input_report_data(handle, buf, sizeof(buf), &len) != ESP_OK) {
            break;
        }
        if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE &&
            params.proto == HID_PROTOCOL_KEYBOARD) {
            keyboard_report(buf, (int)len);
        }
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "keyboard disconnected");
        s_connected = false;
        hid_host_device_close(handle);
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "HID transfer error");
        break;
    default:
        break;
    }
}

static void device_event(hid_host_device_handle_t handle,
                         const hid_host_driver_event_t event,
                         void *arg)
{
    (void)arg;
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;

    hid_host_dev_params_t params;
    if (hid_host_device_get_params(handle, &params) != ESP_OK) return;

    ESP_LOGI(TAG, "HID connected proto=%d subclass=%d", (int)params.proto, (int)params.sub_class);

    const hid_host_device_config_t cfg = {
        .callback = iface_cb,
        .callback_arg = NULL,
    };
    if (hid_host_device_open(handle, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_device_open failed");
        return;
    }
    if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
        hid_class_request_set_protocol(handle, HID_REPORT_PROTOCOL_BOOT);
        if (params.proto == HID_PROTOCOL_KEYBOARD) {
            hid_class_request_set_idle(handle, 0, 0);
            s_connected = true;
        }
    }
    if (hid_host_device_start(handle) != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_device_start failed");
        hid_host_device_close(handle);
        s_connected = false;
    }
}

static void hid_driver_cb(hid_host_device_handle_t handle,
                          const hid_host_driver_event_t event,
                          void *arg)
{
    /* hid_host callbacks are not on the LVGL thread — handle device open here. */
    device_event(handle, event, arg);
}

bool usb_hid_kb_is_connected(void)
{
    return s_connected;
}

bool usb_hid_kb_start(void)
{
    if (s_started) return true;

    s_key_q = xQueueCreate(KEY_Q_LEN, sizeof(kb_evt_t));
    if (!s_key_q) {
        ESP_LOGE(TAG, "key queue alloc failed");
        return false;
    }

    const hid_host_driver_config_t cfg = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_driver_cb,
        .callback_arg = NULL,
    };
    esp_err_t err = hid_host_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_install failed: %s (is USB host up?)", esp_err_to_name(err));
        vQueueDelete(s_key_q);
        s_key_q = NULL;
        return false;
    }

    PT_LVGL_SCOPE_LOCK() {
        s_group = lv_group_create();
        lv_group_set_default(s_group);
        s_indev = lv_indev_create();
        if (s_indev) {
            lv_indev_set_type(s_indev, LV_INDEV_TYPE_KEYPAD);
            lv_indev_set_read_cb(s_indev, keypad_read_cb);
            lv_indev_set_group(s_indev, s_group);
        }
    }

    s_started = true;
    ESP_LOGI(TAG, "HID keyboard host ready — plug a keyboard into USB-A");
    return true;
}
