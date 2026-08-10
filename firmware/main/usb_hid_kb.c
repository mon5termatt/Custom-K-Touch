/*
 * USB HID keyboard host — experimental.
 *
 * Shares the USB host stack started by pt_usb_start() (MSC). Boot-protocol
 * keyboards only. Esc/Home → fleet; arrows/Enter → on-screen focus; ASCII →
 * focused textarea.
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
#include "pandaprusa_theme.h"
#include "ui.h"

#include "usb_hid_kb.h"

static const char *TAG = "usb_hid_kb";

#define KEY_Q_LEN 32
#define NAV_MAX   48

typedef enum {
    NAV_HOME = 1,
    NAV_NEXT,
    NAV_PREV,
    NAV_ENTER,
    NAV_SLOT, /* arg packed: high bits unused, low byte = 1..9 */
} nav_op_t;

typedef struct {
    uint32_t key; /* LV_KEY_* or ASCII — for textarea only */
    bool pressed;
} kb_evt_t;

static QueueHandle_t s_key_q;
static lv_indev_t *s_indev;
static lv_group_t *s_group;
static lv_obj_t *s_kb_focus;
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

void ui_kb_focus_add(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_outline_width(obj, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(obj, PP_ORANGE, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(obj, 3, LV_STATE_FOCUSED);
}

static void kb_set_focus(lv_obj_t *o); /* fwd */

void ui_kb_focus_set(lv_obj_t *obj)
{
    if (obj) ui_kb_focus_add(obj);
    kb_set_focus(obj);
}

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

static bool is_textarea(lv_obj_t *o)
{
    return o && lv_obj_check_type(o, &lv_textarea_class);
}

static void kb_set_focus(lv_obj_t *o)
{
    if (s_kb_focus && lv_obj_is_valid(s_kb_focus)) {
        lv_obj_remove_state(s_kb_focus, LV_STATE_FOCUSED);
    }
    s_kb_focus = o;
    if (!o || !lv_obj_is_valid(o)) {
        s_kb_focus = NULL;
        return;
    }
    lv_obj_add_state(o, LV_STATE_FOCUSED);
    lv_obj_scroll_to_view(o, LV_ANIM_ON);
    if (is_textarea(o) && s_group) {
        if (!lv_obj_get_group(o)) lv_group_add_obj(s_group, o);
        lv_group_focus_obj(o);
        lv_group_set_editing(s_group, true);
    } else if (s_group) {
        lv_group_set_editing(s_group, false);
    }
}

static void collect_nav(lv_obj_t *parent, lv_obj_t **out, int *n, int max)
{
    if (!parent || *n >= max) return;
    uint32_t cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *c = lv_obj_get_child(parent, i);
        if (!c || lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_USER_1) &&
            !lv_obj_has_state(c, LV_STATE_DISABLED) &&
            *n < max) {
            out[(*n)++] = c;
        }
        collect_nav(c, out, n, max);
    }
}

static void nav_step(int delta)
{
    lv_obj_t *list[NAV_MAX];
    int n = 0;
    collect_nav(lv_screen_active(), list, &n, NAV_MAX);
    if (n <= 0) return;

    int cur = -1;
    for (int i = 0; i < n; i++) {
        if (list[i] == s_kb_focus) {
            cur = i;
            break;
        }
    }
    int next = (cur < 0) ? 0 : (cur + delta + n * 8) % n;
    kb_set_focus(list[next]);
}

static void nav_enter(void)
{
    if (!s_kb_focus || !lv_obj_is_valid(s_kb_focus)) {
        nav_step(0);
        return;
    }
    if (is_textarea(s_kb_focus)) {
        push_key(LV_KEY_ENTER, true);
        push_key(LV_KEY_ENTER, false);
        return;
    }
    lv_obj_send_event(s_kb_focus, LV_EVENT_CLICKED, NULL);
}

static void nav_home(void)
{
    kb_set_focus(NULL);
    ui_request_screen("dash");
}

static void apply_nav(void *arg)
{
    intptr_t v = (intptr_t)arg;
    nav_op_t op = (nav_op_t)(v & 0xff);
    int slot = (int)((v >> 8) & 0xff);
    switch (op) {
    case NAV_HOME:  nav_home(); break;
    case NAV_NEXT:  nav_step(+1); break;
    case NAV_PREV:  nav_step(-1); break;
    case NAV_ENTER: nav_enter(); break;
    case NAV_SLOT:  ui_kb_dash_select(slot); break;
    default: break;
    }
}

static void schedule_nav(nav_op_t op)
{
    (void)pt_display_schedule_ui(apply_nav, (void *)(intptr_t)op);
}

static void schedule_slot(int one_based)
{
    intptr_t v = (intptr_t)NAV_SLOT | ((intptr_t)one_based << 8);
    (void)pt_display_schedule_ui(apply_nav, (void *)v);
}

static void apply_digit(void *arg)
{
    int d = (int)(intptr_t)arg;
    if (d < 1 || d > 9) return;
    /* Fleet: jump to printer N. Elsewhere: type the digit (console, etc.). */
    if (!strcmp(ui_current_screen(), "dash")) {
        ui_kb_dash_select(d);
        return;
    }
    uint32_t ch = (uint32_t)('0' + d);
    push_key(ch, true);
    push_key(ch, false);
}

static void schedule_digit(int one_based)
{
    (void)pt_display_schedule_ui(apply_digit, (void *)(intptr_t)one_based);
}

static void handle_special_press(uint8_t key_code)
{
    if (key_code == HID_KEY_ESC || key_code == HID_KEY_HOME) {
        schedule_nav(NAV_HOME);
        return;
    }
    if (key_code == HID_KEY_ENTER || key_code == HID_KEY_KEYPAD_ENTER) {
        schedule_nav(NAV_ENTER);
        return;
    }
    if (key_code == HID_KEY_TAB || key_code == HID_KEY_RIGHT || key_code == HID_KEY_DOWN) {
        schedule_nav(NAV_NEXT);
        return;
    }
    if (key_code == HID_KEY_LEFT || key_code == HID_KEY_UP) {
        schedule_nav(NAV_PREV);
        return;
    }
    if (key_code >= HID_KEY_1 && key_code <= HID_KEY_9) {
        schedule_digit((int)(key_code - HID_KEY_1 + 1));
        return;
    }
    if (key_code >= HID_KEY_KEYPAD_1 && key_code <= HID_KEY_KEYPAD_9) {
        schedule_digit((int)(key_code - HID_KEY_KEYPAD_1 + 1));
        return;
    }
}

static bool key_to_text(uint8_t modifier, uint8_t key_code, uint32_t *out)
{
    /* Text entry only — nav keys are handled separately. */
    if (key_code == HID_KEY_DEL) {
        *out = LV_KEY_BACKSPACE;
        return true;
    }
    if (key_code >= HID_KEY_A && key_code <= HID_KEY_SLASH) {
        if (key_code == HID_KEY_ENTER || key_code == HID_KEY_TAB || key_code == HID_KEY_ESC) {
            return false;
        }
        uint8_t ch = s_keycode2ascii[key_code][shift_mod(modifier) ? 1 : 0];
        if (!ch || ch == '\r' || ch == '\t') return false;
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

static bool is_nav_keycode(uint8_t k)
{
    return k == HID_KEY_ESC || k == HID_KEY_HOME || k == HID_KEY_ENTER ||
           k == HID_KEY_KEYPAD_ENTER || k == HID_KEY_TAB ||
           k == HID_KEY_RIGHT || k == HID_KEY_LEFT ||
           k == HID_KEY_DOWN || k == HID_KEY_UP ||
           (k >= HID_KEY_1 && k <= HID_KEY_9) ||
           (k >= HID_KEY_KEYPAD_1 && k <= HID_KEY_KEYPAD_9);
}

static void keyboard_report(const uint8_t *data, int length)
{
    if (length < (int)sizeof(hid_keyboard_input_report_boot_t)) return;
    const hid_keyboard_input_report_boot_t *rep =
        (const hid_keyboard_input_report_boot_t *)data;

    static uint8_t prev[HID_KEYBOARD_KEY_MAX];
    uint8_t mod = rep->modifier.val;

    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if (rep->key[i] > HID_KEY_ERROR_UNDEFINED &&
            !key_in(prev, rep->key[i], HID_KEYBOARD_KEY_MAX)) {
            uint8_t kc = rep->key[i];
            if (is_nav_keycode(kc)) {
                ESP_LOGI(TAG, "nav key 0x%02x", kc);
                handle_special_press(kc);
            } else {
                uint32_t lv;
                if (key_to_text(mod, kc, &lv)) {
                    ESP_LOGI(TAG, "text 0x%02x -> 0x%" PRIx32, kc, lv);
                    push_key(lv, true);
                    push_key(lv, false);
                }
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
    ESP_LOGI(TAG, "HID keyboard ready — Esc/Home=fleet, arrows/Enter=focus, type in Console");
    return true;
}
