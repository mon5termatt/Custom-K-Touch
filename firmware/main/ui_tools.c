/* Klipper Touch — Tools hub and child screens (menu-first Control replacement). */
#include "ui.h"
#include "usb_hid_kb.h"
#include "app_state.h"
#include "moonraker.h"
#include "prefs.h"
#include "i18n.h"
#include "pandaprusa_theme.h"
#include "pandatouch_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "misc/cache/lv_image_cache.h"
#include "libs/tjpgd/tjpgd.h"
#include "esp_heap_caps.h"

static inline int32_t tw(void) { return lv_display_get_horizontal_resolution(lv_display_get_default()); }
static inline int32_t th(void) { return lv_display_get_vertical_resolution(lv_display_get_default()); }
static inline bool t_portrait(void) { return tw() < 600; }

/* ---- screens ---- */
static lv_obj_t *s_hub;
static lv_obj_t *s_move, *s_temp, *s_webcam, *s_afc, *s_console, *s_macros, *s_lights;
static lv_obj_t *s_tune, *s_calib, *s_endstops, *s_pid, *s_zoff, *s_mesh, *s_fault;
static lv_obj_t *s_hub_grid;
static lv_obj_t *s_hub_afc_btn;
static lv_obj_t *s_hub_moon_btns[6]; /* macros, console, tune, calib, lights */

static lv_obj_t *s_console_log;
static lv_obj_t *s_console_ta;
static lv_obj_t *s_macros_grid;
static lv_obj_t *s_leds_grid;
static lv_obj_t *s_leds_status;
static lv_obj_t *s_endstop_grid;
static lv_obj_t *s_endstop_status;
static lv_obj_t *s_fault_msg;

/* Tune / Temp / Z numpad state */
static lv_obj_t *s_tune_speed_slider, *s_tune_flow_slider;
static lv_obj_t *s_tune_speed_val, *s_tune_flow_val;
static lv_obj_t *s_tune_fdm;              /* % speed/flow UI (Moonraker / Connect) */
static lv_obj_t *s_tune_bambu;            /* Silent/Standard/Sport presets */
static lv_obj_t *s_bambu_spd_btns[4];
static int s_tune_speed = 100, s_tune_flow = 100;
static int s_bambu_spd_lvl = 2;           /* 1 Silent … 4 Ludicrous */
static lv_obj_t *s_numpad_modal, *s_numpad_ta;
static float s_numpad_min, s_numpad_max;
static int s_numpad_decimals;
typedef void (*tools_numpad_fn)(float value);
static tools_numpad_fn s_numpad_cb;

/* Webcam widgets (also used by ui_apply_snapshot) */
static lv_obj_t *s_snap_card, *s_snap_img, *s_snap_ph, *s_snap_modal, *s_snap_fs_img;
static lv_image_dsc_t s_snap_dsc;
static uint16_t *s_snap_px;
static int s_snap_w, s_snap_h;
static bool s_snap_want_fs;

/* AFC — KS-inspired lane cards (main panel only; no sensors/filament config) */
static lv_obj_t *s_afc_state_lbl;
static lv_obj_t *s_afc_loaded_lbl;
static lv_obj_t *s_afc_lane_grid;
static lv_obj_t *s_afc_cards[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_name_lbl[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_map_lbl[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_mat_lbl[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_stat_lbl[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_swatch[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_load_btn[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_load_lbl[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_eject_btn[PP_AFC_MAX_LANES];
static lv_obj_t *s_afc_dist_btns[3];
static lv_obj_t *s_afc_unload_btn;
static lv_obj_t *s_afc_footer;            /* Move/Prep row — Klipper AFC only */
static int s_afc_lane_nums[PP_AFC_MAX_LANES];
static bool s_afc_lane_active[PP_AFC_MAX_LANES];
static int s_afc_sel_lane;
static int s_afc_move_mm = 50;
static int s_afc_n_shown;
static bool s_afc_is_ams;
static int s_jog_step = 10;
static int s_extrude_mm = 15;

extern bool ui_locked_block_public(void); /* provided by ui.c */

static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, PP_SURFACE, 0);
    lv_obj_set_style_border_color(c, PP_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 6, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                             void *user_data, lv_obj_t **out_label)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 150, 48);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, PP_SURFACE_HI, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, PP_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    ui_kb_focus_add(b);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, PP_TEXT, 0);
    lv_obj_set_style_text_font(l, PP_F16, 0);
    lv_obj_center(l);
    if (out_label) *out_label = l;
    return b;
}

static lv_obj_t *make_barbtn(lv_obj_t *bar, const char *text, lv_event_cb_t cb,
                             void *user_data, lv_coord_t w)
{
    lv_obj_t *b = lv_button_create(bar);
    lv_obj_set_size(b, w, 40);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, PP_SURFACE_HI, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, PP_WHITE, 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *make_hdr(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 56);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, PP_HEADER, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, title ? title : "");
    lv_obj_set_style_text_color(t, PP_TEXT, 0);
    lv_obj_set_style_text_font(t, PP_F20, 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", back_cb, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);
    return bar;
}

static void go_status(lv_event_t *e)
{
    (void)e;
    ui_request_screen("status");
}

static bool s_back_to_status;

static void tools_go_back(lv_event_t *e)
{
    (void)e;
    if (s_back_to_status) ui_request_screen("status");
    else                  lv_screen_load(s_hub);
}

void ui_tools_set_back_to_status(void) { s_back_to_status = true; }
void ui_tools_set_back_to_hub(void)    { s_back_to_status = false; }
typedef void (*tools_action_fn)(void);
static tools_action_fn s_confirm_fn;

static void on_confirm_ok(lv_event_t *e)
{
    lv_obj_t *m = (lv_obj_t *)lv_event_get_user_data(e);
    tools_action_fn fn = s_confirm_fn;
    s_confirm_fn = NULL;
    if (m) lv_msgbox_close(m);
    if (fn) fn();
}
static void on_confirm_cancel(lv_event_t *e)
{
    lv_obj_t *m = (lv_obj_t *)lv_event_get_user_data(e);
    s_confirm_fn = NULL;
    if (m) lv_msgbox_close(m);
}
static void tools_confirm(const char *title, const char *body, tools_action_fn fn)
{
    s_confirm_fn = fn;
    lv_obj_t *m = lv_msgbox_create(NULL);
    lv_msgbox_add_title(m, title);
    lv_msgbox_add_text(m, body);
    lv_obj_t *ok = lv_msgbox_add_footer_button(m, tr(STR_OK));
    lv_obj_t *no = lv_msgbox_add_footer_button(m, "Cancel");
    lv_obj_add_event_cb(ok, on_confirm_ok, LV_EVENT_CLICKED, m);
    lv_obj_add_event_cb(no, on_confirm_cancel, LV_EVENT_CLICKED, m);
}

static void tools_numpad_close(void)
{
    if (s_numpad_modal) {
        lv_obj_delete(s_numpad_modal);
        s_numpad_modal = NULL;
        s_numpad_ta = NULL;
    }
    s_numpad_cb = NULL;
}

static void on_numpad_cancel(lv_event_t *e)
{
    (void)e;
    tools_numpad_close();
}

static void on_numpad_ready(lv_event_t *e)
{
    (void)e;
    if (!s_numpad_ta) { tools_numpad_close(); return; }
    const char *txt = lv_textarea_get_text(s_numpad_ta);
    float v = txt && txt[0] ? strtof(txt, NULL) : 0.f;
    if (v < s_numpad_min) v = s_numpad_min;
    if (v > s_numpad_max) v = s_numpad_max;
    tools_numpad_fn cb = s_numpad_cb;
    tools_numpad_close();
    if (cb) cb(v);
}

static void tools_numpad_open(const char *title, float initial, float min_v, float max_v,
                              int decimals, tools_numpad_fn cb)
{
    tools_numpad_close();
    s_numpad_cb = cb;
    s_numpad_min = min_v;
    s_numpad_max = max_v;
    s_numpad_decimals = decimals < 0 ? 0 : decimals;

    s_numpad_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_numpad_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_numpad_modal, PP_BG, 0);
    lv_obj_set_style_bg_opa(s_numpad_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_numpad_modal, 0, 0);
    lv_obj_set_style_pad_all(s_numpad_modal, 0, 0);
    lv_obj_clear_flag(s_numpad_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ttl = lv_label_create(s_numpad_modal);
    lv_label_set_text(ttl, title ? title : "Value");
    lv_obj_set_style_text_color(ttl, PP_TEXT, 0);
    lv_obj_set_style_text_font(ttl, PP_F20, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 16);

    s_numpad_ta = lv_textarea_create(s_numpad_modal);
    lv_textarea_set_one_line(s_numpad_ta, true);
    lv_obj_set_width(s_numpad_ta, tw() - 48);
    lv_obj_align(s_numpad_ta, LV_ALIGN_TOP_MID, 0, 56);
    char init[24];
    if (s_numpad_decimals <= 0) snprintf(init, sizeof(init), "%.0f", (double)initial);
    else snprintf(init, sizeof(init), "%.*f", s_numpad_decimals, (double)initial);
    lv_textarea_set_text(s_numpad_ta, init);

    lv_obj_t *kb = lv_keyboard_create(s_numpad_modal);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(kb, s_numpad_ta);
    lv_obj_add_event_cb(kb, on_numpad_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, on_numpad_cancel, LV_EVENT_CANCEL, NULL);

    lv_obj_t *cancel = make_button(s_numpad_modal, "Cancel", on_numpad_cancel, NULL, NULL);
    lv_obj_set_size(cancel, 120, 40);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -16, 8);
}

static void do_estop(void) { app_state_post_cmd(PP_CMD_ESTOP, NULL); }
static void do_unlock(void) { app_state_post_cmd(PP_CMD_UNLOCK, NULL); }
static void do_save_cfg(void) { app_state_post_cmd(PP_CMD_SAVE_CONFIG, NULL); }
static void do_mesh(void) { app_state_post_cmd(PP_CMD_MESH, NULL); }
static void do_klipper_restart(void) { app_state_post_cmd(PP_CMD_KLIPPER_RESTART, NULL); }
static void do_fw_restart(void) { app_state_post_cmd(PP_CMD_FIRMWARE_RESTART, NULL); }

/* Dedicated E-STOP confirm: full-screen, huge Cancel + red Confirm — fast when intentional,
 * hard to trigger by accident (header button alone does not stop). */
static lv_obj_t *s_estop_modal;

static void estop_modal_close(void)
{
    if (s_estop_modal) {
        lv_obj_delete(s_estop_modal);
        s_estop_modal = NULL;
    }
}

static void on_estop_modal_cancel(lv_event_t *e)
{
    (void)e;
    estop_modal_close();
}

static void on_estop_modal_ok(lv_event_t *e)
{
    (void)e;
    estop_modal_close();
    do_estop();
}

void ui_tools_estop(void)
{
    if (ui_locked_block_public()) return;
    if (!app_state_active_is_moonraker()) return;
    if (s_estop_modal) return;

    s_estop_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_estop_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_estop_modal, PP_BG, 0);
    lv_obj_set_style_bg_opa(s_estop_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_estop_modal, 0, 0);
    lv_obj_set_style_pad_all(s_estop_modal, 0, 0);
    lv_obj_clear_flag(s_estop_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_estop_modal);
    lv_label_set_text(title, tr(STR_ESTOP));
    lv_obj_set_style_text_color(title, PP_ERROR, 0);
    lv_obj_set_style_text_font(title, PP_F28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *body = lv_label_create(s_estop_modal);
    lv_label_set_text(body, "Emergency stop the printer?\nMotors and heaters will cut immediately.");
    lv_obj_set_style_text_color(body, PP_TEXT, 0);
    lv_obj_set_style_text_font(body, PP_F16, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(body, tw() - 48);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 100);

    const bool P = t_portrait();
    const int bw = P ? (tw() - 48) : 280;
    const int bh = 96;

    lv_obj_t *cancel = lv_button_create(s_estop_modal);
    lv_obj_set_size(cancel, bw, bh);
    lv_obj_set_style_bg_color(cancel, PP_SURFACE, 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cancel, PP_SURFACE_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(cancel, PP_BORDER, 0);
    lv_obj_set_style_border_width(cancel, 2, 0);
    lv_obj_set_style_radius(cancel, 8, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_add_event_cb(cancel, on_estop_modal_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, PP_TEXT, 0);
    lv_obj_set_style_text_font(cl, PP_F20, 0);
    lv_obj_center(cl);

    lv_obj_t *ok = lv_button_create(s_estop_modal);
    lv_obj_set_size(ok, bw, bh);
    lv_obj_set_style_bg_color(ok, PP_ERROR, 0);
    lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ok, lv_color_darken(PP_ERROR, 30), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ok, 0, 0);
    lv_obj_set_style_radius(ok, 8, 0);
    lv_obj_set_style_shadow_width(ok, 0, 0);
    lv_obj_add_event_cb(ok, on_estop_modal_ok, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ol = lv_label_create(ok);
    lv_label_set_text(ol, tr(STR_ESTOP));
    lv_obj_set_style_text_color(ol, PP_WHITE, 0);
    lv_obj_set_style_text_font(ol, PP_F20, 0);
    lv_obj_center(ol);

    if (P) {
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -16 - bh - 16);
        lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -16);
    } else {
        lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, -(bw / 2 + 12), -32);
        lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, (bw / 2 + 12), -32);
    }
}

/* ---- Coming soon helper (kept for future stubs) ---- */
#if 0
static lv_obj_t *build_placeholder(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, PP_BG, 0);
    make_hdr(scr, title, go_hub);
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, tr(STR_COMING_SOON));
    lv_obj_set_style_text_color(l, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(l, PP_F20, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    return scr;
}
#endif

/* ---- hub ---- */
static void open_scr(lv_event_t *e)
{
    ui_tools_set_back_to_hub();
    lv_obj_t *scr = (lv_obj_t *)lv_event_get_user_data(e);
    if (scr) lv_screen_load(scr);
}

static void open_tune_hub(lv_event_t *e)
{
    (void)e;
    ui_tools_set_back_to_hub();
    ui_tools_open_tune();
}

static void open_webcam(lv_event_t *e)
{
    (void)e;
    ui_tools_set_back_to_hub();
    if (s_snap_ph) lv_label_set_text(s_snap_ph, tr(STR_LOADING_WEBCAM));
    app_state_fetch_snapshot();
    lv_screen_load(s_webcam);
}

static void open_console(lv_event_t *e)
{
    (void)e;
    ui_tools_set_back_to_hub();
    app_state_post_cmd(PP_CMD_GCODE_LOG, NULL);
    lv_screen_load(s_console);
    /* HID keypad indev is created after ui_init — (re)attach entry field when opening. */
    ui_kb_focus_set(s_console_ta);
}

static void open_macros(lv_event_t *e)
{
    (void)e;
    ui_tools_set_back_to_hub();
    app_state_post_cmd(PP_CMD_LIST_MACROS, NULL);
    lv_screen_load(s_macros);
}

static void open_lights(lv_event_t *e)
{
    (void)e;
    ui_tools_set_back_to_hub();
    if (s_leds_status) lv_label_set_text(s_leds_status, tr(STR_LOADING));
    if (s_leds_grid) lv_obj_clean(s_leds_grid);
    app_state_post_cmd(PP_CMD_LIST_LEDS, NULL);
    lv_screen_load(s_lights);
}

static void open_afc(lv_event_t *e)
{
    (void)e;
    ui_tools_set_back_to_hub();
    pp_afc_t afc;
    app_state_get_afc(&afc);
    if (!afc.present) return;
    lv_screen_load(s_afc);
}

static void open_endstops(lv_event_t *e)
{
    (void)e;
    if (s_endstop_status) lv_label_set_text(s_endstop_status, tr(STR_LOADING));
    if (s_endstop_grid) lv_obj_clean(s_endstop_grid);
    app_state_post_cmd(PP_CMD_ENDSTOPS, NULL);
    lv_screen_load(s_endstops);
}

static void on_endstop_refresh(lv_event_t *e)
{
    (void)e;
    if (ui_locked_block_public()) return;
    if (s_endstop_status) lv_label_set_text(s_endstop_status, tr(STR_LOADING));
    if (s_endstop_grid) lv_obj_clean(s_endstop_grid);
    app_state_post_cmd(PP_CMD_ENDSTOPS, NULL);
}

static lv_obj_t *add_hub_tile(lv_obj_t *grid, const char *label, lv_event_cb_t cb,
                              void *ud, int tile_w, int tile_h, lv_obj_t **store)
{
    lv_obj_t *b = lv_button_create(grid);
    lv_obj_set_size(b, tile_w, tile_h);
    lv_obj_set_style_bg_color(b, PP_SURFACE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, PP_SURFACE_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, PP_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label ? label : "");
    lv_obj_set_style_text_color(l, PP_TEXT, 0);
    lv_obj_set_style_text_font(l, PP_F16, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, tile_w - 16);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);

    ui_kb_focus_add(b);
    if (store) *store = b;
    return b;
}

static void fill_hub_tiles(void)
{
    const int cols = t_portrait() ? 2 : 4;
    const int gap = 12;
    const int pad = 16;
    const int tile_w = (tw() - pad * 2 - gap * (cols - 1)) / cols;
    const int tile_h = t_portrait() ? 88 : 100;

    lv_obj_clean(s_hub_grid);
    add_hub_tile(s_hub_grid, tr(STR_MOVE), open_scr, s_move, tile_w, tile_h, NULL);
    add_hub_tile(s_hub_grid, tr(STR_TEMPERATURE), open_scr, s_temp, tile_w, tile_h, NULL);
    add_hub_tile(s_hub_grid, tr(STR_WEBCAM), open_webcam, NULL, tile_w, tile_h, NULL);
    add_hub_tile(s_hub_grid, tr(STR_LIGHTS), open_lights, NULL, tile_w, tile_h, &s_hub_moon_btns[4]);
    add_hub_tile(s_hub_grid, tr(STR_AFC), open_afc, NULL, tile_w, tile_h, &s_hub_afc_btn);
    add_hub_tile(s_hub_grid, tr(STR_MACROS), open_macros, NULL, tile_w, tile_h, &s_hub_moon_btns[0]);
    add_hub_tile(s_hub_grid, tr(STR_CONSOLE), open_console, NULL, tile_w, tile_h, &s_hub_moon_btns[1]);
    add_hub_tile(s_hub_grid, tr(STR_TUNE), open_tune_hub, NULL, tile_w, tile_h, &s_hub_moon_btns[2]);
    add_hub_tile(s_hub_grid, tr(STR_CALIBRATION), open_scr, s_calib, tile_w, tile_h, &s_hub_moon_btns[3]);
    ui_tools_refresh_menu();
    /* Always start hidden — ui_apply_afc reveals after Moonraker detects lanes.
     * Do not call app_state_get_afc here: ui_tools_init runs before app_state_start. */
    if (s_hub_afc_btn) lv_obj_add_flag(s_hub_afc_btn, LV_OBJ_FLAG_HIDDEN);
}

static void build_hub(void)
{
    s_hub = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_hub, PP_BG, 0);
    make_hdr(s_hub, tr(STR_TOOLS), go_status);

    s_hub_grid = lv_obj_create(s_hub);
    lv_obj_set_size(s_hub_grid, LV_PCT(100), th() - 56);
    lv_obj_align(s_hub_grid, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_hub_grid, PP_BG, 0);
    lv_obj_set_style_border_width(s_hub_grid, 0, 0);
    lv_obj_set_style_pad_all(s_hub_grid, 16, 0);
    lv_obj_set_style_pad_row(s_hub_grid, 12, 0);
    lv_obj_set_style_pad_column(s_hub_grid, 12, 0);
    lv_obj_set_flex_flow(s_hub_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_hub_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_hub_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_hub_grid, LV_OBJ_FLAG_SCROLL_ELASTIC);

    fill_hub_tiles();
    if (s_hub_afc_btn) lv_obj_add_flag(s_hub_afc_btn, LV_OBJ_FLAG_HIDDEN);
}

static void rebuild_hub_list(void)
{
    fill_hub_tiles();
}

/* ---- Move ---- */
static void on_jog(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    const char *tok = (const char *)lv_event_get_user_data(e);
    if (tok[0] == 'H') { app_state_post_cmd_n(PP_CMD_HOME, 0, 0, 0); return; }
    if (tok[0] == 'U') {
        tools_confirm(tr(STR_UNLOCK_MOTORS), "Unlock motors?", do_unlock);
        return;
    }
    int axis = (tok[0] == 'X') ? 0 : (tok[0] == 'Y') ? 1 : 2;
    int dist = (tok[1] == '-') ? -s_jog_step : s_jog_step;
    int feed = (axis == 2) ? 600 : 3000;
    app_state_post_cmd_n(PP_CMD_MOVE, axis, dist * 100, feed);
}

static void on_step(lv_event_t *e)
{
    s_jog_step = (int)(intptr_t)lv_event_get_user_data(e);
}

static void on_extrude(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int sign = (int)(intptr_t)lv_event_get_user_data(e);
    app_state_post_cmd_n(PP_CMD_EXTRUDE, 0, sign * s_extrude_mm * 100, 300);
}

static void build_move(void)
{
    s_move = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_move, PP_BG, 0);
    if (t_portrait()) lv_obj_add_flag(s_move, LV_OBJ_FLAG_SCROLLABLE);
    make_hdr(s_move, tr(STR_MOVE), tools_go_back);

    /* Centered cluster: Z column (88) + gap 16 + XY pad (3*76 + 2*10 = 248) = 352 */
    const int bw = 76, bh = 52, gap = 10, z_w = 88;
    const int xy_w = bw * 3 + gap * 2;
    const int cluster_w = z_w + 16 + xy_w;
    const int ox = (tw() - cluster_w) / 2;
    const int oy = 70;

    const int steps[] = {1, 10, 50, 100};
    const int step_row_w = 4 * 70 + 3 * 10;
    const int step_ox = (tw() - step_row_w) / 2;
    for (int i = 0; i < 4; i++) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d", steps[i]);
        lv_obj_t *b = make_button(s_move, buf, on_step, (void *)(intptr_t)steps[i], NULL);
        lv_obj_set_size(b, 70, 40);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, step_ox + i * 80, oy);
    }

    const int pad_x = ox + z_w + 16;
    const int pad_y = oy + 56;

    lv_obj_t *bzp = make_button(s_move, "Z+", on_jog, "Z+", NULL);
    lv_obj_t *bzm = make_button(s_move, "Z-", on_jog, "Z-", NULL);
    lv_obj_set_size(bzp, z_w, bh); lv_obj_set_size(bzm, z_w, bh);
    lv_obj_align(bzp, LV_ALIGN_TOP_LEFT, ox, pad_y);
    lv_obj_align(bzm, LV_ALIGN_TOP_LEFT, ox, pad_y + bh + gap);

    lv_obj_t *byp = make_button(s_move, LV_SYMBOL_UP " Y+", on_jog, "Y+", NULL);
    lv_obj_t *bxm = make_button(s_move, LV_SYMBOL_LEFT " X-", on_jog, "X-", NULL);
    lv_obj_t *home = make_button(s_move, LV_SYMBOL_HOME, on_jog, "HM", NULL);
    lv_obj_t *bxp = make_button(s_move, LV_SYMBOL_RIGHT " X+", on_jog, "X+", NULL);
    lv_obj_t *bym = make_button(s_move, LV_SYMBOL_DOWN " Y-", on_jog, "Y-", NULL);
    lv_obj_set_size(byp, bw, bh); lv_obj_set_size(bxm, bw, bh); lv_obj_set_size(home, bw, bh);
    lv_obj_set_size(bxp, bw, bh); lv_obj_set_size(bym, bw, bh);
    lv_obj_align(byp, LV_ALIGN_TOP_LEFT, pad_x + bw + gap, pad_y);
    lv_obj_align(bxm, LV_ALIGN_TOP_LEFT, pad_x, pad_y + bh + gap);
    lv_obj_align(home, LV_ALIGN_TOP_LEFT, pad_x + bw + gap, pad_y + bh + gap);
    lv_obj_align(bxp, LV_ALIGN_TOP_LEFT, pad_x + 2 * (bw + gap), pad_y + bh + gap);
    lv_obj_align(bym, LV_ALIGN_TOP_LEFT, pad_x + bw + gap, pad_y + 2 * (bh + gap));

    const int bot_y = pad_y + 3 * (bh + gap) + 8;
    const int bot_w = 120 + 12 + 100 + 12 + 100;
    const int bot_ox = (tw() - bot_w) / 2;
    lv_obj_t *unl = make_button(s_move, tr(STR_UNLOCK_MOTORS), on_jog, "UL", NULL);
    lv_obj_set_size(unl, 120, 48);
    lv_obj_align(unl, LV_ALIGN_TOP_LEFT, bot_ox, bot_y);
    lv_obj_t *ex = make_button(s_move, "E+", on_extrude, (void *)(intptr_t)1, NULL);
    lv_obj_t *re = make_button(s_move, "E-", on_extrude, (void *)(intptr_t)-1, NULL);
    lv_obj_set_size(ex, 100, 48); lv_obj_set_size(re, 100, 48);
    lv_obj_align(ex, LV_ALIGN_TOP_LEFT, bot_ox + 132, bot_y);
    lv_obj_align(re, LV_ALIGN_TOP_LEFT, bot_ox + 244, bot_y);
}

/* ---- Temperature ---- */
static void on_preheat(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    app_state_post_cmd_n(PP_CMD_PREHEAT, (int)(intptr_t)lv_event_get_user_data(e), 0, 0);
}
static void on_fan(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    app_state_post_cmd_n(PP_CMD_FAN, 0, (int)(intptr_t)lv_event_get_user_data(e), 0);
}
static void on_set_temp(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int packed = (int)(intptr_t)lv_event_get_user_data(e);
    int which = packed >> 16;
    int temp = packed & 0xFFFF;
    app_state_post_cmd_n(PP_CMD_SET_TEMP, which, temp, 0);
}

static void numpad_set_nozzle(float v)
{
    app_state_post_cmd_n(PP_CMD_SET_TEMP, 0, (int)(v + 0.5f), 0);
}
static void numpad_set_bed(float v)
{
    app_state_post_cmd_n(PP_CMD_SET_TEMP, 1, (int)(v + 0.5f), 0);
}
static void on_temp_numpad_noz(lv_event_t *e)
{
    (void)e;
    if (ui_locked_block_public()) return;
    tools_numpad_open("Nozzle C", 200.f, 0.f, 350.f, 0, numpad_set_nozzle);
}
static void on_temp_numpad_bed(lv_event_t *e)
{
    (void)e;
    if (ui_locked_block_public()) return;
    tools_numpad_open("Bed C", 60.f, 0.f, 150.f, 0, numpad_set_bed);
}

static void build_temp(void)
{
    s_temp = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_temp, PP_BG, 0);
    if (t_portrait()) lv_obj_add_flag(s_temp, LV_OBJ_FLAG_SCROLLABLE);
    make_hdr(s_temp, tr(STR_TEMPERATURE), tools_go_back);

    const char *mats[] = { "PLA", "PETG", "ASA", "Cooldown" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = make_button(s_temp, mats[i], on_preheat, (void *)(intptr_t)i, NULL);
        lv_obj_set_size(b, 160, 50);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 16 + (i % 2) * 180, 70 + (i / 2) * 60);
    }

    lv_obj_t *nl = lv_label_create(s_temp);
    lv_label_set_text(nl, tr(STR_NOZZLE));
    lv_obj_set_style_text_color(nl, PP_TEXT_MUTED, 0);
    lv_obj_align(nl, LV_ALIGN_TOP_LEFT, 16, 210);
    const int noz[] = {0, 180, 200, 220, 240};
    for (int i = 0; i < 5; i++) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d", noz[i]);
        int pack = (0 << 16) | noz[i];
        lv_obj_t *b = make_button(s_temp, buf, on_set_temp, (void *)(intptr_t)pack, NULL);
        lv_obj_set_size(b, 64, 40);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 16 + i * 72, 240);
    }
    lv_obj_t *noz_set = make_button(s_temp, "Set...", on_temp_numpad_noz, NULL, NULL);
    lv_obj_set_size(noz_set, 72, 40);
    lv_obj_align(noz_set, LV_ALIGN_TOP_LEFT, 16 + 5 * 72, 240);

    lv_obj_t *bl = lv_label_create(s_temp);
    lv_label_set_text(bl, tr(STR_BED));
    lv_obj_set_style_text_color(bl, PP_TEXT_MUTED, 0);
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 16, 300);
    const int bed[] = {0, 50, 60, 80, 100};
    for (int i = 0; i < 5; i++) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d", bed[i]);
        int pack = (1 << 16) | bed[i];
        lv_obj_t *b = make_button(s_temp, buf, on_set_temp, (void *)(intptr_t)pack, NULL);
        lv_obj_set_size(b, 64, 40);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 16 + i * 72, 330);
    }
    lv_obj_t *bed_set = make_button(s_temp, "Set...", on_temp_numpad_bed, NULL, NULL);
    lv_obj_set_size(bed_set, 72, 40);
    lv_obj_align(bed_set, LV_ALIGN_TOP_LEFT, 16 + 5 * 72, 330);

    lv_obj_t *fl = lv_label_create(s_temp);
    lv_label_set_text(fl, tr(STR_FAN));
    lv_obj_set_style_text_color(fl, PP_TEXT_MUTED, 0);
    lv_obj_align(fl, LV_ALIGN_TOP_LEFT, 16, 390);
    const int fans[] = {0, 25, 50, 75, 100};
    for (int i = 0; i < 5; i++) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", fans[i]);
        lv_obj_t *b = make_button(s_temp, buf, on_fan, (void *)(intptr_t)fans[i], NULL);
        lv_obj_set_size(b, 64, 40);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 16 + i * 72, 420);
    }
}

/* ---- Webcam (snapshot widgets) ---- */
static void snap_release_pixels(void)
{
    if (s_snap_img) lv_image_set_src(s_snap_img, NULL);
    if (s_snap_fs_img) lv_image_set_src(s_snap_fs_img, NULL);
    if (s_snap_px) {
        lv_image_cache_drop(&s_snap_dsc);
        heap_caps_free(s_snap_px);
        s_snap_px = NULL;
    }
    s_snap_w = s_snap_h = 0;
}

static void snap_fs_hide(void)
{
    if (s_snap_modal) lv_obj_add_flag(s_snap_modal, LV_OBJ_FLAG_HIDDEN);
}

void ui_tools_leave(void)
{
    snap_fs_hide();
    tools_numpad_close();
}

static void snap_fs_show(void)
{
    if (s_snap_modal) lv_obj_clear_flag(s_snap_modal, LV_OBJ_FLAG_HIDDEN);
}

static void on_snap_load(lv_event_t *e)
{
    (void)e;
    s_snap_want_fs = false;   /* Load stays in-page; tap the image for fullscreen */
    if (s_snap_ph) lv_label_set_text(s_snap_ph, tr(STR_LOADING));
    app_state_fetch_snapshot();
}

static void on_snap_preview(lv_event_t *e)
{
    (void)e;
    /* Fullscreen only from the image itself — not the card chrome / empty areas. */
    if (s_snap_px) snap_fs_show();
}

static void on_snap_fs_close(lv_event_t *e) { (void)e; snap_fs_hide(); }

static void build_webcam_overlay(void)
{
    s_snap_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_snap_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_snap_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_snap_modal, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_snap_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_snap_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_snap_modal, on_snap_fs_close, LV_EVENT_CLICKED, NULL);
    s_snap_fs_img = lv_image_create(s_snap_modal);
    lv_obj_center(s_snap_fs_img);
}

static void build_webcam(void)
{
    s_webcam = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_webcam, PP_BG, 0);
    make_hdr(s_webcam, tr(STR_WEBCAM), tools_go_back);

    s_snap_card = make_card(s_webcam, tw() - 32, th() - 120);
    lv_obj_align(s_snap_card, LV_ALIGN_TOP_MID, 0, 70);
    /* Card is display-only; do not fullscreen on empty-area taps. */

    lv_obj_t *cam_btn = make_button(s_snap_card, "Load", on_snap_load, NULL, NULL);
    lv_obj_set_size(cam_btn, 84, 34);
    lv_obj_align(cam_btn, LV_ALIGN_TOP_RIGHT, 0, -4);

    s_snap_ph = lv_label_create(s_snap_card);
    lv_label_set_text(s_snap_ph, tr(STR_TAP_LOAD_CAM));
    lv_obj_set_style_text_color(s_snap_ph, PP_TEXT_MUTED, 0);
    lv_label_set_long_mode(s_snap_ph, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_snap_ph, tw() - 64);
    lv_obj_set_style_text_align(s_snap_ph, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_snap_ph, LV_ALIGN_CENTER, 0, 0);

    s_snap_img = lv_image_create(s_snap_card);
    lv_obj_align(s_snap_img, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_flag(s_snap_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_snap_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_snap_img, on_snap_preview, LV_EVENT_CLICKED, NULL);

    build_webcam_overlay();
}

/* JPEG decode (same approach as former ui.c Control webcam) */
typedef struct {
    const uint8_t *jpg; size_t len, pos;
    uint16_t *rgb; int dw, dh, div;
} snap_jpg_io_t;

static size_t snap_jpg_in(JDEC *jd, uint8_t *buf, size_t n)
{
    snap_jpg_io_t *io = (snap_jpg_io_t *)jd->device;
    if (io->pos >= io->len) return 0;
    if (n > io->len - io->pos) n = io->len - io->pos;
    if (buf) memcpy(buf, io->jpg + io->pos, n);
    io->pos += n;
    return n;
}
static int snap_jpg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    snap_jpg_io_t *io = (snap_jpg_io_t *)jd->device;
    const uint8_t *src = (const uint8_t *)bitmap;
    const int div = io->div > 0 ? io->div : 1;
    for (int y = rect->top; y <= rect->bottom; y++) {
        for (int x = rect->left; x <= rect->right; x++) {
            uint8_t b = src[0], g = src[1], r = src[2];
            src += 3;
            if ((x % div) || (y % div)) continue;
            int dx = x / div, dy = y / div;
            if ((unsigned)dx >= (unsigned)io->dw || (unsigned)dy >= (unsigned)io->dh) continue;
            io->rgb[dy * io->dw + dx] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    return 1;
}

static bool jpeg_dims(const uint8_t *jpg, int len, uint16_t *w, uint16_t *h)
{
    for (int i = 0; i + 9 < len; i++) {
        if (jpg[i] == 0xFF && jpg[i + 1] == 0xC0) {
            *h = (uint16_t)((jpg[i + 5] << 8) | jpg[i + 6]);
            *w = (uint16_t)((jpg[i + 7] << 8) | jpg[i + 8]);
            return *w > 0 && *h > 0;
        }
    }
    return false;
}

static bool snap_decode_jpeg(const uint8_t *jpg, int len, uint16_t jw, uint16_t jh)
{
    int div = 1;
    while (div < 8 && ((jw / div) > tw() || (jh / div) > th())) div *= 2;
    while (div < 8 && (size_t)(jw / div) * (size_t)(jh / div) * 2u > 768u * 1024u) div *= 2;
    int dw = jw / div, dh = jh / div;
    if (dw < 1 || dh < 1) return false;
    uint16_t *px = heap_caps_malloc((size_t)dw * (size_t)dh * 2u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!px) px = heap_caps_malloc((size_t)dw * (size_t)dh * 2u, MALLOC_CAP_DEFAULT);
    if (!px) return false;
    snap_jpg_io_t io = { .jpg = jpg, .len = (size_t)len, .pos = 0, .rgb = px, .dw = dw, .dh = dh, .div = div };
    uint8_t work[4096];
    JDEC jd;
    if (jd_prepare(&jd, snap_jpg_in, work, sizeof(work), &io) != JDR_OK) { heap_caps_free(px); return false; }
    if (jd_decomp(&jd, snap_jpg_out, 0) != JDR_OK) { heap_caps_free(px); return false; }
    s_snap_px = px; s_snap_w = dw; s_snap_h = dh;
    return true;
}

static uint32_t snap_contain_scale(int img_w, int img_h, int box_w, int box_h)
{
    if (img_w < 1 || img_h < 1 || box_w < 1 || box_h < 1) return LV_SCALE_NONE;
    uint32_t sx = ((uint32_t)box_w * LV_SCALE_NONE) / (uint32_t)img_w;
    uint32_t sy = ((uint32_t)box_h * LV_SCALE_NONE) / (uint32_t)img_h;
    uint32_t sc = sx < sy ? sx : sy;
    return sc ? sc : 1;
}

static void snap_bind_images(void)
{
    if (!s_snap_px || s_snap_w < 1 || s_snap_h < 1) return;
    s_snap_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_snap_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_snap_dsc.header.w = (uint32_t)s_snap_w;
    s_snap_dsc.header.h = (uint32_t)s_snap_h;
    s_snap_dsc.header.stride = (uint32_t)s_snap_w * 2u;
    s_snap_dsc.data = (const uint8_t *)s_snap_px;
    s_snap_dsc.data_size = (uint32_t)s_snap_w * (uint32_t)s_snap_h * 2u;
    if (s_snap_img) {
        int box_w = s_snap_card ? (int)lv_obj_get_content_width(s_snap_card) : 340;
        int box_h = s_snap_card ? (int)lv_obj_get_content_height(s_snap_card) - 28 : 110;
        if (box_w < 40) box_w = 40;
        if (box_h < 40) box_h = 40;
        uint32_t sc = snap_contain_scale(s_snap_w, s_snap_h, box_w, box_h);
        lv_image_set_src(s_snap_img, &s_snap_dsc);
        lv_image_set_pivot(s_snap_img, s_snap_w / 2, s_snap_h / 2);
        lv_image_set_scale(s_snap_img, sc);
        lv_obj_clear_flag(s_snap_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_snap_ph) lv_obj_add_flag(s_snap_ph, LV_OBJ_FLAG_HIDDEN);
    if (s_snap_fs_img) {
        uint32_t sc = snap_contain_scale(s_snap_w, s_snap_h, (int)tw(), (int)th());
        lv_image_set_src(s_snap_fs_img, &s_snap_dsc);
        lv_image_set_pivot(s_snap_fs_img, s_snap_w / 2, s_snap_h / 2);
        lv_image_set_scale(s_snap_fs_img, sc);
        lv_obj_align(s_snap_fs_img, LV_ALIGN_CENTER, 0, 0);
    }
}

void ui_apply_snapshot(void *arg)
{
    pp_image_t *im = (pp_image_t *)arg;
    if (!im) return;
    if (!s_snap_img) { free(im->data); free(im); return; }
    bool want_fs = s_snap_want_fs;
    bool fs_was = s_snap_modal && !lv_obj_has_flag(s_snap_modal, LV_OBJ_FLAG_HIDDEN);
    s_snap_want_fs = false;
    snap_release_pixels();
    if (!im->data || im->len <= 0) {
        free(im->data); free(im);
        snap_fs_hide();
        if (s_snap_ph) { lv_label_set_text(s_snap_ph, tr(STR_NO_CAMERA)); lv_obj_clear_flag(s_snap_ph, LV_OBJ_FLAG_HIDDEN); }
        return;
    }
    uint8_t *jpg = im->data; int len = im->len; free(im);
    uint16_t jw = 0, jh = 0;
    if (!jpeg_dims(jpg, len, &jw, &jh) || !snap_decode_jpeg(jpg, len, jw, jh)) {
        free(jpg); snap_fs_hide();
        if (s_snap_ph) { lv_label_set_text(s_snap_ph, tr(STR_SNAPSHOT_UNREADABLE)); lv_obj_clear_flag(s_snap_ph, LV_OBJ_FLAG_HIDDEN); }
        return;
    }
    free(jpg);
    snap_bind_images();
    if (want_fs || fs_was) snap_fs_show();
}

/* ---- AFC (inspired by ArmoredTurtle AFC-Klipper-Screen main panel) ---- */
static lv_color_t afc_parse_color(const char *hex)
{
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return PP_SURFACE_HI;
    unsigned r = 0, g = 0, b = 0;
    sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b);
    return lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static void afc_refresh_sel_style(void)
{
    for (int i = 0; i < PP_AFC_MAX_LANES; i++) {
        if (!s_afc_cards[i] || lv_obj_has_flag(s_afc_cards[i], LV_OBJ_FLAG_HIDDEN)) continue;
        bool sel = (s_afc_lane_nums[i] > 0 && s_afc_lane_nums[i] == s_afc_sel_lane);
        bool act = s_afc_lane_active[i];
        lv_obj_set_style_border_color(s_afc_cards[i],
            sel ? PP_ORANGE : (act ? PP_OK : PP_BORDER), 0);
        lv_obj_set_style_border_width(s_afc_cards[i], sel || act ? 2 : 1, 0);
        lv_obj_set_style_bg_color(s_afc_cards[i],
            sel ? PP_SURFACE_HI : PP_SURFACE, 0);
    }
    for (int i = 0; i < 3; i++) {
        if (!s_afc_dist_btns[i]) continue;
        int mm = (int)(intptr_t)lv_obj_get_user_data(s_afc_dist_btns[i]);
        bool on = (mm == s_afc_move_mm);
        lv_obj_set_style_border_color(s_afc_dist_btns[i], on ? PP_ORANGE : PP_BORDER, 0);
        lv_obj_set_style_border_width(s_afc_dist_btns[i], on ? 2 : 1, 0);
    }
}

static void on_afc_select(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot < 0 || slot >= PP_AFC_MAX_LANES) return;
    if (s_afc_lane_nums[slot] <= 0) return;
    s_afc_sel_lane = s_afc_lane_nums[slot];
    afc_refresh_sel_style();
}

static void on_afc_load_unload(lv_event_t *e)
{
    if (ui_locked_block_public() || s_afc_is_ams) return;
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot < 0 || slot >= PP_AFC_MAX_LANES) return;
    int num = s_afc_lane_nums[slot];
    if (num <= 0) return;
    s_afc_sel_lane = num;
    afc_refresh_sel_style();
    if (s_afc_lane_active[slot])
        app_state_post_cmd(PP_CMD_AFC_UNLOAD, NULL);
    else
        app_state_post_cmd_n(PP_CMD_AFC_CHANGE, num, 0, 0);
}

static void on_afc_eject_lane(lv_event_t *e)
{
    if (ui_locked_block_public() || s_afc_is_ams) return;
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (slot < 0 || slot >= PP_AFC_MAX_LANES) return;
    int num = s_afc_lane_nums[slot];
    if (num <= 0) return;
    s_afc_sel_lane = num;
    afc_refresh_sel_style();
    app_state_post_cmd_n(PP_CMD_AFC_EJECT, num, 0, 0);
}

static void on_afc_unload(lv_event_t *e)
{
    (void)e; if (ui_locked_block_public() || s_afc_is_ams) return;
    app_state_post_cmd(PP_CMD_AFC_UNLOAD, NULL);
}

static void on_afc_move(lv_event_t *e)
{
    if (ui_locked_block_public() || s_afc_is_ams) return;
    int sign = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_afc_sel_lane > 0)
        app_state_post_cmd_n(PP_CMD_AFC_MOVE, s_afc_sel_lane, sign * s_afc_move_mm, 0);
}

static void on_afc_dist(lv_event_t *e)
{
    if (s_afc_is_ams) return;
    s_afc_move_mm = (int)(intptr_t)lv_event_get_user_data(e);
    afc_refresh_sel_style();
}

static void on_afc_prep(lv_event_t *e)
{ (void)e; if (s_afc_is_ams || ui_locked_block_public()) return; app_state_post_cmd(PP_CMD_AFC_PREP, NULL); }
static void on_afc_resume(lv_event_t *e)
{ (void)e; if (s_afc_is_ams || ui_locked_block_public()) return; app_state_post_cmd(PP_CMD_AFC_RESUME, NULL); }
static void on_afc_clear(lv_event_t *e)
{ (void)e; if (s_afc_is_ams || ui_locked_block_public()) return; app_state_post_cmd(PP_CMD_AFC_CLEAR, NULL); }

static lv_obj_t *afc_mini_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                              void *ud, lv_obj_t **out_lbl)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, LV_PCT(48), 36);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, PP_SURFACE_HI, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, PP_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    ui_kb_focus_add(b);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_color(l, PP_TEXT, 0);
    lv_obj_set_style_text_font(l, PP_F14, 0);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return b;
}

static void build_afc(void)
{
    s_afc = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_afc, PP_BG, 0);
    lv_obj_add_flag(s_afc, LV_OBJ_FLAG_SCROLLABLE);
    make_hdr(s_afc, tr(STR_AFC), tools_go_back);

    /* Status strip — KS “Loaded: laneN” + AFC state */
    lv_obj_t *strip = lv_obj_create(s_afc);
    lv_obj_set_size(strip, tw() - 32, 40);
    lv_obj_align(strip, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_color(strip, PP_SURFACE, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_radius(strip, 6, 0);
    lv_obj_set_style_pad_hor(strip, 12, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    s_afc_state_lbl = lv_label_create(strip);
    lv_label_set_text(s_afc_state_lbl, "");
    lv_obj_set_style_text_color(s_afc_state_lbl, PP_ORANGE, 0);
    lv_obj_set_style_text_font(s_afc_state_lbl, PP_F14, 0);
    lv_obj_align(s_afc_state_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    s_afc_loaded_lbl = lv_label_create(strip);
    lv_label_set_text(s_afc_loaded_lbl, "");
    lv_obj_set_style_text_color(s_afc_loaded_lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_afc_loaded_lbl, PP_F14, 0);
    lv_obj_align(s_afc_loaded_lbl, LV_ALIGN_LEFT_MID, 120, 0);

    lv_obj_t *gunl = make_button(strip, tr(STR_AFC_UNLOAD), on_afc_unload, NULL, NULL);
    s_afc_unload_btn = gunl;
    lv_obj_set_size(gunl, 88, 32);
    lv_obj_align(gunl, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Lane card grid */
    s_afc_lane_grid = lv_obj_create(s_afc);
    lv_obj_set_size(s_afc_lane_grid, tw() - 24, th() - 230);
    lv_obj_align(s_afc_lane_grid, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_bg_opa(s_afc_lane_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_afc_lane_grid, 0, 0);
    lv_obj_set_style_pad_all(s_afc_lane_grid, 4, 0);
    lv_obj_set_style_pad_row(s_afc_lane_grid, 10, 0);
    lv_obj_set_style_pad_column(s_afc_lane_grid, 10, 0);
    lv_obj_set_flex_flow(s_afc_lane_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_afc_lane_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_afc_lane_grid, LV_OBJ_FLAG_SCROLLABLE);

    const int cols = t_portrait() ? 2 : 4;
    const int card_w = (tw() - 24 - 8 - 10 * (cols - 1)) / cols;
    const int card_h = 148;

    for (int i = 0; i < PP_AFC_MAX_LANES; i++) {
        s_afc_lane_nums[i] = 0;
        s_afc_lane_active[i] = false;

        lv_obj_t *c = lv_obj_create(s_afc_lane_grid);
        s_afc_cards[i] = c;
        lv_obj_set_size(c, card_w, card_h);
        lv_obj_set_style_bg_color(c, PP_SURFACE, 0);
        lv_obj_set_style_border_color(c, PP_BORDER, 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_radius(c, 8, 0);
        lv_obj_set_style_pad_all(c, 8, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, on_afc_select, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_kb_focus_add(c);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);

        s_afc_swatch[i] = lv_obj_create(c);
        lv_obj_set_size(s_afc_swatch[i], 28, 28);
        lv_obj_set_style_radius(s_afc_swatch[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_afc_swatch[i], 0, 0);
        lv_obj_set_style_pad_all(s_afc_swatch[i], 0, 0);
        lv_obj_align(s_afc_swatch[i], LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_clear_flag(s_afc_swatch[i], LV_OBJ_FLAG_CLICKABLE);

        s_afc_name_lbl[i] = lv_label_create(c);
        lv_label_set_text(s_afc_name_lbl[i], "");
        lv_obj_set_style_text_color(s_afc_name_lbl[i], PP_OK, 0);
        lv_obj_set_style_text_font(s_afc_name_lbl[i], PP_F16, 0);
        lv_obj_align(s_afc_name_lbl[i], LV_ALIGN_TOP_LEFT, 36, 2);

        s_afc_map_lbl[i] = lv_label_create(c);
        lv_label_set_text(s_afc_map_lbl[i], "");
        lv_obj_set_style_text_color(s_afc_map_lbl[i], PP_ORANGE, 0);
        lv_obj_set_style_text_font(s_afc_map_lbl[i], PP_F14, 0);
        lv_obj_align(s_afc_map_lbl[i], LV_ALIGN_TOP_RIGHT, 0, 4);

        s_afc_mat_lbl[i] = lv_label_create(c);
        lv_label_set_text(s_afc_mat_lbl[i], "");
        lv_obj_set_style_text_color(s_afc_mat_lbl[i], PP_TEXT, 0);
        lv_obj_set_style_text_font(s_afc_mat_lbl[i], PP_F14, 0);
        lv_label_set_long_mode(s_afc_mat_lbl[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_afc_mat_lbl[i], card_w - 20);
        lv_obj_align(s_afc_mat_lbl[i], LV_ALIGN_TOP_LEFT, 0, 40);

        s_afc_stat_lbl[i] = lv_label_create(c);
        lv_label_set_text(s_afc_stat_lbl[i], "");
        lv_obj_set_style_text_color(s_afc_stat_lbl[i], PP_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(s_afc_stat_lbl[i], PP_F12, 0);
        lv_obj_align(s_afc_stat_lbl[i], LV_ALIGN_TOP_LEFT, 0, 62);

        lv_obj_t *row = lv_obj_create(c);
        lv_obj_set_size(row, LV_PCT(100), 40);
        lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);

        s_afc_load_btn[i] = afc_mini_btn(row, tr(STR_AFC_LOAD), on_afc_load_unload,
                                         (void *)(intptr_t)i, &s_afc_load_lbl[i]);
        s_afc_eject_btn[i] = afc_mini_btn(row, tr(STR_AFC_EJECT), on_afc_eject_lane,
                                          (void *)(intptr_t)i, NULL);
    }

    /* Lane move + system actions (footer) — KS “Lane Move” lite; hidden for Bambu AMS */
    s_afc_footer = lv_obj_create(s_afc);
    lv_obj_set_size(s_afc_footer, tw(), 48);
    lv_obj_align(s_afc_footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_afc_footer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_afc_footer, 0, 0);
    lv_obj_set_style_pad_all(s_afc_footer, 0, 0);
    lv_obj_clear_flag(s_afc_footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mm = make_button(s_afc_footer, "Move-", on_afc_move, (void *)(intptr_t)-1, NULL);
    lv_obj_t *mp = make_button(s_afc_footer, "Move+", on_afc_move, (void *)(intptr_t)1, NULL);
    lv_obj_set_size(mm, 72, 36); lv_obj_set_size(mp, 72, 36);
    lv_obj_align(mm, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_align(mp, LV_ALIGN_LEFT_MID, 96, 0);

    const int dists[] = {20, 50, 100};
    for (int i = 0; i < 3; i++) {
        char buf[12]; snprintf(buf, sizeof(buf), "%d", dists[i]);
        s_afc_dist_btns[i] = make_button(s_afc_footer, buf, on_afc_dist, (void *)(intptr_t)dists[i], NULL);
        lv_obj_set_user_data(s_afc_dist_btns[i], (void *)(intptr_t)dists[i]);
        lv_obj_set_size(s_afc_dist_btns[i], 48, 36);
        lv_obj_align(s_afc_dist_btns[i], LV_ALIGN_LEFT_MID, 180 + i * 56, 0);
    }

    lv_obj_t *prep = make_button(s_afc_footer, tr(STR_AFC_PREP), on_afc_prep, NULL, NULL);
    lv_obj_t *res = make_button(s_afc_footer, tr(STR_RESUME), on_afc_resume, NULL, NULL);
    lv_obj_t *clr = make_button(s_afc_footer, tr(STR_AFC_CLEAR), on_afc_clear, NULL, NULL);
    lv_obj_set_size(prep, 64, 36); lv_obj_set_size(res, 72, 36); lv_obj_set_size(clr, 64, 36);
    lv_obj_align(prep, LV_ALIGN_RIGHT_MID, -160, 0);
    lv_obj_align(res, LV_ALIGN_RIGHT_MID, -84, 0);
    lv_obj_align(clr, LV_ALIGN_RIGHT_MID, -16, 0);
    afc_refresh_sel_style();
}

void ui_apply_afc(void *arg)
{
    pp_afc_t *a = (pp_afc_t *)arg;
    if (!a) return;

    s_afc_is_ams = a->is_ams;
    if (s_hub_afc_btn) {
        if (a->present) lv_obj_clear_flag(s_hub_afc_btn, LV_OBJ_FLAG_HIDDEN);
        else            lv_obj_add_flag(s_hub_afc_btn, LV_OBJ_FLAG_HIDDEN);
    }
    ui_status_set_afc_chip(a);

    /* AMS is display-only — hide Klipper AFC action controls. */
    if (s_afc_unload_btn) {
        if (a->is_ams) lv_obj_add_flag(s_afc_unload_btn, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_clear_flag(s_afc_unload_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_afc_footer) {
        if (a->is_ams) lv_obj_add_flag(s_afc_footer, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_clear_flag(s_afc_footer, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_afc_state_lbl) {
        if (a->error) {
            lv_label_set_text(s_afc_state_lbl, "Error");
            lv_obj_set_style_text_color(s_afc_state_lbl, PP_ERROR, 0);
        } else {
            lv_label_set_text(s_afc_state_lbl, a->state[0] ? a->state : "—");
            lv_obj_set_style_text_color(s_afc_state_lbl, PP_ORANGE, 0);
        }
    }
    if (s_afc_loaded_lbl) {
        if (a->current[0])
            lv_label_set_text_fmt(s_afc_loaded_lbl, tr(STR_AFC_LOADED_FMT), a->current);
        else
            lv_label_set_text(s_afc_loaded_lbl, "");
    }

    s_afc_n_shown = 0;
    for (int i = 0; i < PP_AFC_MAX_LANES; i++) {
        if (!s_afc_cards[i]) continue;
        if (i >= a->n || !a->present) {
            lv_obj_add_flag(s_afc_cards[i], LV_OBJ_FLAG_HIDDEN);
            s_afc_lane_nums[i] = 0;
            s_afc_lane_active[i] = false;
            continue;
        }
        const pp_afc_lane_t *L = &a->lanes[i];
        s_afc_lane_nums[i] = L->num > 0 ? L->num : (i + 1);
        bool active = L->tool_loaded || (a->current[0] && !strcmp(a->current, L->name));
        s_afc_lane_active[i] = active;
        if (active && s_afc_sel_lane <= 0) s_afc_sel_lane = s_afc_lane_nums[i];
        s_afc_n_shown++;

        char name[20];
        snprintf(name, sizeof(name), "%d  %s", s_afc_lane_nums[i],
                 L->name[0] ? L->name : "lane");
        if (s_afc_name_lbl[i]) {
            lv_label_set_text(s_afc_name_lbl[i], name);
            lv_obj_set_style_text_color(s_afc_name_lbl[i], active ? PP_ORANGE : PP_OK, 0);
        }
        if (s_afc_map_lbl[i])
            lv_label_set_text(s_afc_map_lbl[i], L->map[0] ? L->map : "");

        if (s_afc_mat_lbl[i]) {
            if (L->material[0]) lv_label_set_text(s_afc_mat_lbl[i], L->material);
            else lv_label_set_text(s_afc_mat_lbl[i], tr(STR_AFC_EMPTY));
        }
        if (s_afc_stat_lbl[i]) {
            const char *st = L->status[0] ? L->status : (L->ready ? tr(STR_AFC_READY) : tr(STR_AFC_EMPTY));
            lv_label_set_text(s_afc_stat_lbl[i], st);
        }
        if (s_afc_load_lbl[i])
            lv_label_set_text(s_afc_load_lbl[i], active ? tr(STR_AFC_UNLOAD) : tr(STR_AFC_LOAD));

        if (s_afc_load_btn[i]) {
            if (a->is_ams) lv_obj_add_flag(s_afc_load_btn[i], LV_OBJ_FLAG_HIDDEN);
            else           lv_obj_clear_flag(s_afc_load_btn[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_afc_eject_btn[i]) {
            if (a->is_ams) lv_obj_add_flag(s_afc_eject_btn[i], LV_OBJ_FLAG_HIDDEN);
            else           lv_obj_clear_flag(s_afc_eject_btn[i], LV_OBJ_FLAG_HIDDEN);
        }

        if (s_afc_swatch[i]) {
            if (L->color[0] == '#') {
                lv_obj_set_style_bg_color(s_afc_swatch[i], afc_parse_color(L->color), 0);
                lv_obj_clear_flag(s_afc_swatch[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_set_style_bg_color(s_afc_swatch[i], PP_SURFACE_HI, 0);
                lv_obj_clear_flag(s_afc_swatch[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_clear_flag(s_afc_cards[i], LV_OBJ_FLAG_HIDDEN);
    }
    afc_refresh_sel_style();
    free(a);
}

/* Select AFC lane by on-screen number 1..n (USB keyboard). LVGL thread. */
void ui_kb_afc_select(int one_based)
{
    if (!s_afc || lv_screen_active() != s_afc) return;
    if (one_based < 1 || one_based > PP_AFC_MAX_LANES) return;
    int slot = one_based - 1;
    if (s_afc_lane_nums[slot] <= 0) return;
    s_afc_sel_lane = s_afc_lane_nums[slot];
    afc_refresh_sel_style();
    if (s_afc_cards[slot]) ui_kb_focus_set(s_afc_cards[slot]);
}

/* ---- Console (USB-A HID keyboard for gcode entry when plugged in) ---- */
static void on_console_send(lv_event_t *e)
{
    (void)e;
    if (ui_locked_block_public()) return;
    if (!s_console_ta) return;
    const char *t = lv_textarea_get_text(s_console_ta);
    if (t && t[0]) {
        app_state_post_cmd(PP_CMD_GCODE, t);
        lv_textarea_set_text(s_console_ta, "");
        app_state_post_cmd(PP_CMD_GCODE_LOG, NULL);
    }
}

static void on_console_refresh(lv_event_t *e)
{
    (void)e; app_state_post_cmd(PP_CMD_GCODE_LOG, NULL);
}

static void on_console_ta_key(lv_event_t *e)
{
    if (lv_event_get_key(e) == LV_KEY_ENTER) on_console_send(e);
}

static void style_console_ta(lv_obj_t *ta, bool editable)
{
    /* Near-black field — default LVGL textarea is white and blinding on this UI. */
    lv_obj_set_style_bg_color(ta, PP_HEADER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, PP_TEXT, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, PP_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ta, 8, LV_PART_MAIN);
    if (editable) {
        lv_obj_set_style_bg_color(ta, PP_ORANGE, LV_PART_CURSOR);
        lv_obj_set_style_text_color(ta, PP_TEXT_MUTED, LV_PART_TEXTAREA_PLACEHOLDER);
    } else {
        lv_obj_set_style_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    }
    lv_obj_set_style_bg_color(ta, PP_SURFACE_HI, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_SCROLLBAR);
}

static void build_console(void)
{
    s_console = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_console, PP_BG, 0);
    make_hdr(s_console, tr(STR_CONSOLE), tools_go_back);

    s_console_log = lv_textarea_create(s_console);
    lv_obj_set_size(s_console_log, tw() - 32, th() - 180);
    lv_obj_align(s_console_log, LV_ALIGN_TOP_MID, 0, 64);
    lv_textarea_set_text(s_console_log, "");
    lv_obj_set_style_text_font(s_console_log, PP_F12, 0);
    lv_obj_clear_flag(s_console_log, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_cursor_click_pos(s_console_log, false);
    style_console_ta(s_console_log, false);

    s_console_ta = lv_textarea_create(s_console);
    lv_obj_set_size(s_console_ta, tw() - 200, 40);
    lv_obj_align(s_console_ta, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    lv_textarea_set_one_line(s_console_ta, true);
    lv_textarea_set_placeholder_text(s_console_ta, "gcode (USB keyboard)…");
    style_console_ta(s_console_ta, true);
    lv_obj_add_event_cb(s_console_ta, on_console_ta_key, LV_EVENT_KEY, NULL);
    ui_kb_focus_add(s_console_ta);

    lv_obj_t *send = make_button(s_console, "Send", on_console_send, NULL, NULL);
    lv_obj_set_size(send, 70, 40);
    lv_obj_align(send, LV_ALIGN_BOTTOM_RIGHT, -90, -16);
    lv_obj_t *ref = make_button(s_console, LV_SYMBOL_REFRESH, on_console_refresh, NULL, NULL);
    lv_obj_set_size(ref, 50, 40);
    lv_obj_align(ref, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
}

void ui_apply_gcode_log(void *arg)
{
    pp_gcode_log_t *log = (pp_gcode_log_t *)arg;
    if (!log) return;
    if (s_console_log) {
        char *buf = malloc(PP_GCODE_LOG_MAX * PP_GCODE_LOG_LINE + 8);
        if (buf) {
            size_t off = 0; buf[0] = '\0';
            for (int i = 0; i < log->count; i++) {
                size_t len = strlen(log->lines[i]);
                if (off + len + 2 >= (size_t)(PP_GCODE_LOG_MAX * PP_GCODE_LOG_LINE + 8)) break;
                memcpy(buf + off, log->lines[i], len); off += len;
                buf[off++] = '\n'; buf[off] = '\0';
            }
            lv_textarea_set_text(s_console_log, buf);
            free(buf);
        }
    }
    free(log);
}

/* ---- Macros ---- */
static void on_macro_run(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    const char *name = (const char *)lv_event_get_user_data(e);
    if (name && name[0]) app_state_post_cmd(PP_CMD_GCODE, name);
}
static void on_macro_pin(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    const char *name = (const char *)lv_event_get_user_data(e);
    if (name && name[0]) {
        prefs_pin_macro_toggle(name);
        app_state_post_cmd(PP_CMD_LIST_MACROS, NULL);
    }
}

static char s_macro_names[PP_MACRO_MAX][PP_MACRO_NAME_LEN];

static bool macro_is_pinned(const char *name)
{
    for (int p = 0; p < prefs_pinned_macro_count(); p++)
        if (!strcmp(prefs_pinned_macro(p), name)) return true;
    return false;
}

static void add_macro_tile(const char *name, bool pinned, int tile_w)
{
    lv_obj_t *tile = lv_button_create(s_macros_grid);
    lv_obj_set_size(tile, tile_w, 64);
    lv_obj_set_style_bg_color(tile, PP_SURFACE, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, PP_SURFACE_HI, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(tile, pinned ? PP_ORANGE : PP_BORDER, 0);
    lv_obj_set_style_border_width(tile, pinned ? 2 : 1, 0);
    lv_obj_set_style_radius(tile, 8, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 6, 0);
    lv_obj_add_event_cb(tile, on_macro_run, LV_EVENT_CLICKED, (void *)name);

    lv_obj_t *lbl = lv_label_create(tile);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_color(lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(lbl, PP_F14, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl, tile_w - 44);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *pin = lv_button_create(tile);
    lv_obj_set_size(pin, 36, 36);
    lv_obj_align(pin, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(pin, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pin, 0, 0);
    lv_obj_set_style_shadow_width(pin, 0, 0);
    lv_obj_add_event_cb(pin, on_macro_pin, LV_EVENT_CLICKED, (void *)name);
    lv_obj_t *pl = lv_label_create(pin);
    lv_label_set_text(pl, pinned ? LV_SYMBOL_OK : LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(pl, pinned ? PP_ORANGE : PP_TEXT_MUTED, 0);
    lv_obj_center(pl);
}

void ui_apply_macros(void *arg)
{
    pp_macro_list_t *ml = (pp_macro_list_t *)arg;
    if (!ml) return;
    if (s_macros_grid) {
        lv_obj_clean(s_macros_grid);
        const int cols = t_portrait() ? 2 : 3;
        const int gap = 10, pad = 12;
        const int tile_w = (tw() - pad * 2 - gap * (cols - 1)) / cols;

        for (int i = 0; i < ml->count && i < PP_MACRO_MAX; i++)
            strlcpy(s_macro_names[i], ml->names[i], PP_MACRO_NAME_LEN);

        /* Pinned first (from prefs, may include names not in current list) */
        for (int i = 0; i < prefs_pinned_macro_count(); i++) {
            const char *n = prefs_pinned_macro(i);
            if (!n || !n[0] || n[0] == '_') continue;
            add_macro_tile(n, true, tile_w);
        }
        for (int i = 0; i < ml->count && i < PP_MACRO_MAX; i++) {
            if (s_macro_names[i][0] == '_' || macro_is_pinned(s_macro_names[i])) continue;
            add_macro_tile(s_macro_names[i], false, tile_w);
        }
    }
    free(ml);
}

static void build_macros(void)
{
    s_macros = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_macros, PP_BG, 0);
    make_hdr(s_macros, tr(STR_MACROS), tools_go_back);
    s_macros_grid = lv_obj_create(s_macros);
    lv_obj_set_size(s_macros_grid, LV_PCT(100), th() - 56);
    lv_obj_align(s_macros_grid, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_macros_grid, PP_BG, 0);
    lv_obj_set_style_border_width(s_macros_grid, 0, 0);
    lv_obj_set_style_pad_all(s_macros_grid, 12, 0);
    lv_obj_set_style_pad_row(s_macros_grid, 10, 0);
    lv_obj_set_style_pad_column(s_macros_grid, 10, 0);
    lv_obj_set_flex_flow(s_macros_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_macros_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_macros_grid, LV_OBJ_FLAG_SCROLLABLE);
}

/* ---- Lights (Klipper [led] PWM + [neopixel] RGB/RGBW) ---- */
static pp_led_t s_led_copy[PP_LED_MAX];
static int s_led_n;

static int led_level_pct(const pp_led_t *L)
{
    int m = L->r;
    if (L->g > m) m = L->g;
    if (L->b > m) m = L->b;
    if (L->w > m) m = L->w;
    return (m * 100 + 127) / 255;
}

static void led_post_color(int idx, uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    if (idx < 0 || idx >= s_led_n) return;
    if (!s_led_copy[idx].name[0]) return;
    int rgb = ((int)r << 16) | ((int)g << 8) | (int)b;
    app_state_post_cmd_ex(PP_CMD_SET_LED, s_led_copy[idx].name, 0, rgb, (int)w);
}

static void on_led_onoff(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int pack = (int)(intptr_t)lv_event_get_user_data(e);
    int idx = pack >> 1;
    bool on = pack & 1;
    if (idx < 0 || idx >= s_led_n) return;
    const pp_led_t *L = &s_led_copy[idx];
    if (L->kind == PP_LED_KIND_EFFECT) return;
    if (!on) { led_post_color(idx, 0, 0, 0, 0); return; }
    if (L->pwm) led_post_color(idx, 0, 0, 0, 255);
    else        led_post_color(idx, 255, 255, 255, 255);
}

static void on_led_fx_toggle(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_led_n) return;
    pp_led_t *L = &s_led_copy[idx];
    if (L->kind != PP_LED_KIND_EFFECT) return;
    bool stop = L->on;
    L->on = !stop;
    L->on_known = true;
    app_state_post_cmd_ex(PP_CMD_SET_LED, L->name, stop ? 2 : 1, 0, 0);
}

static void on_led_preset(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int pack = (int)(intptr_t)lv_event_get_user_data(e);
    int idx = pack >> 8;
    int pre = pack & 0xFF;
    if (idx < 0 || idx >= s_led_n) return;
    switch (pre) {
    case 0: led_post_color(idx, 0, 0, 0, 0); break;
    case 1: led_post_color(idx, 255, 0, 0, 0); break;
    case 2: led_post_color(idx, 0, 255, 0, 0); break;
    case 3: led_post_color(idx, 0, 0, 255, 0); break;
    case 4: led_post_color(idx, 255, 140, 40, 0); break;   /* warm */
    case 5: led_post_color(idx, 0, 0, 0, 255); break;      /* white channel (GRBW) */
    default: break;
    }
}

static void on_led_bright(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    lv_obj_t *sl = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(sl);
    if (idx < 0 || idx >= s_led_n) return;
    int pct = (int)lv_slider_get_value(sl);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    pp_led_t L = s_led_copy[idx];
    int m = L.r;
    if (L.g > m) m = L.g;
    if (L.b > m) m = L.b;
    if (L.w > m) m = L.w;
    uint8_t r, g, b, w;
    if (pct == 0) {
        r = g = b = w = 0;
    } else if (m == 0) {
        uint8_t v = (uint8_t)((pct * 255 + 50) / 100);
        if (L.pwm) { r = g = b = 0; w = v; }
        else       { r = g = b = w = v; }
    } else {
        r = (uint8_t)((L.r * pct * 255) / (m * 100));
        g = (uint8_t)((L.g * pct * 255) / (m * 100));
        b = (uint8_t)((L.b * pct * 255) / (m * 100));
        w = (uint8_t)((L.w * pct * 255) / (m * 100));
    }
    led_post_color(idx, r, g, b, w);
}

static lv_obj_t *led_chip(lv_obj_t *parent, lv_color_t col, const char *txt, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 40, 36);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, PP_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    if (txt) {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, PP_WHITE, 0);
        lv_obj_set_style_text_font(l, PP_F14, 0);
        lv_obj_center(l);
    }
    return b;
}

static void add_led_fx_btn(const pp_led_t *L, int idx)
{
    lv_obj_t *tb = make_button(s_leds_grid, L->name, on_led_fx_toggle,
                               (void *)(intptr_t)idx, NULL);
    lv_obj_set_size(tb, 176, 40);
    lv_obj_t *lab = lv_obj_get_child(tb, 0);
    if (lab) {
        lv_obj_set_width(lab, 164);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
    }
    if (L->on) {
        lv_obj_set_style_bg_opa(tb, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tb, PP_ORANGE, 0);
    }
}

static void add_led_card(const pp_led_t *L, int idx, int card_w)
{
    int card_h = L->dimmable ? 118 : 88;
    lv_obj_t *card = make_card(s_leds_grid, card_w, card_h);
    lv_obj_set_style_pad_all(card, 8, 0);

    lv_color_t swc;
    if (L->w > L->r && L->w > L->g && L->w > L->b)
        swc = lv_color_make(L->w, L->w, L->w);
    else
        swc = lv_color_make(L->r, L->g, L->b);
    lv_obj_t *sw = lv_obj_create(card);
    lv_obj_set_size(sw, 28, 28);
    lv_obj_set_style_bg_color(sw, swc, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sw, 4, 0);
    lv_obj_set_style_border_color(sw, PP_BORDER, 0);
    lv_obj_set_style_border_width(sw, 1, 0);
    lv_obj_set_style_pad_all(sw, 0, 0);
    lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 0, 4);

    lv_obj_t *nm = lv_label_create(card);
    lv_label_set_text(nm, L->name);
    lv_obj_set_style_text_color(nm, PP_TEXT, 0);
    lv_obj_set_style_text_font(nm, PP_F16, 0);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(nm, (L->pwm || !L->dimmable) ? (card_w - 200) : (card_w - 356));
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 36, 0);

    lv_obj_t *ty = lv_label_create(card);
    if (L->pixels > 1) lv_label_set_text_fmt(ty, "%s · %d", L->type, L->pixels);
    else               lv_label_set_text(ty, L->type);
    lv_obj_set_style_text_color(ty, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ty, PP_F14, 0);
    lv_obj_align(ty, LV_ALIGN_TOP_LEFT, 36, 22);

    lv_obj_t *on = make_button(card, tr(STR_LED_ON), on_led_onoff,
                               (void *)(intptr_t)((idx << 1) | 1), NULL);
    lv_obj_set_size(on, 64, 40);
    lv_obj_align(on, LV_ALIGN_TOP_RIGHT, -72, 2);
    lv_obj_t *off = make_button(card, tr(STR_LED_OFF), on_led_onoff,
                                (void *)(intptr_t)(idx << 1), NULL);
    lv_obj_set_size(off, 64, 40);
    lv_obj_align(off, LV_ALIGN_TOP_RIGHT, 0, 2);

    if (!L->dimmable) return;

    lv_obj_t *sl = lv_slider_create(card);
    lv_obj_set_width(sl, card_w - 24);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, led_level_pct(L), LV_ANIM_OFF);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(sl, PP_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_user_data(sl, (void *)(intptr_t)idx);
    lv_obj_add_event_cb(sl, on_led_bright, LV_EVENT_RELEASED, NULL);

    if (L->pwm) return;

    /* Color chips sit left of On/Off on the same row (8px gap, 40px chips). */
    lv_obj_t *cW = led_chip(card, lv_color_make(230, 230, 230), tr(STR_LED_WHITE), on_led_preset,
                            (void *)(intptr_t)((idx << 8) | 5));
    lv_obj_align(cW, LV_ALIGN_TOP_RIGHT, -144, 4);
    lv_obj_t *wl = lv_obj_get_child(cW, 0);
    if (wl) lv_obj_set_style_text_color(wl, lv_color_make(40, 40, 40), 0);
    lv_obj_t *cw = led_chip(card, lv_color_make(255, 160, 60), NULL, on_led_preset,
                            (void *)(intptr_t)((idx << 8) | 4));
    lv_obj_align(cw, LV_ALIGN_TOP_RIGHT, -192, 4);
    lv_obj_t *cb = led_chip(card, lv_color_make(40, 80, 220), NULL, on_led_preset,
                            (void *)(intptr_t)((idx << 8) | 3));
    lv_obj_align(cb, LV_ALIGN_TOP_RIGHT, -240, 4);
    lv_obj_t *cg = led_chip(card, lv_color_make(40, 180, 60), NULL, on_led_preset,
                            (void *)(intptr_t)((idx << 8) | 2));
    lv_obj_align(cg, LV_ALIGN_TOP_RIGHT, -288, 4);
    lv_obj_t *cr = led_chip(card, lv_color_make(200, 40, 40), NULL, on_led_preset,
                            (void *)(intptr_t)((idx << 8) | 1));
    lv_obj_align(cr, LV_ALIGN_TOP_RIGHT, -336, 4);
}

void ui_apply_leds(void *arg)
{
    pp_led_list_t *ll = (pp_led_list_t *)arg;
    if (!ll) return;
    int32_t sy = 0;
    if (s_leds_grid) sy = lv_obj_get_scroll_y(s_leds_grid);
    /* Keep last commanded effect state when Moonraker omits enabled/running. */
    for (int i = 0; i < ll->count && i < PP_LED_MAX; i++) {
        pp_led_t *N = &ll->items[i];
        if (N->kind != PP_LED_KIND_EFFECT || N->on_known) continue;
        for (int j = 0; j < s_led_n; j++) {
            if (s_led_copy[j].kind == PP_LED_KIND_EFFECT &&
                strcmp(s_led_copy[j].name, N->name) == 0) {
                N->on = s_led_copy[j].on;
                N->on_known = true;
                break;
            }
        }
    }
    s_led_n = 0;
    if (s_leds_grid) {
        lv_obj_clean(s_leds_grid);
        if (ll->count <= 0) {
            if (s_leds_status) {
                lv_label_set_text(s_leds_status, tr(STR_NO_LEDS));
                lv_obj_clear_flag(s_leds_status, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (s_leds_status) lv_obj_add_flag(s_leds_status, LV_OBJ_FLAG_HIDDEN);
            const int card_w = tw() - 32;
            for (int i = 0; i < ll->count && i < PP_LED_MAX; i++) {
                s_led_copy[i] = ll->items[i];
                s_led_n++;
                if (s_led_copy[i].kind != PP_LED_KIND_EFFECT)
                    add_led_card(&s_led_copy[i], i, card_w);
            }
            for (int i = 0; i < s_led_n; i++) {
                if (s_led_copy[i].kind == PP_LED_KIND_EFFECT)
                    add_led_fx_btn(&s_led_copy[i], i);
            }
            lv_obj_update_layout(s_leds_grid);
            lv_obj_scroll_to_y(s_leds_grid, sy, LV_ANIM_OFF);
        }
    }
    free(ll);
}

static void build_lights(void)
{
    s_lights = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_lights, PP_BG, 0);
    make_hdr(s_lights, tr(STR_LIGHTS), tools_go_back);
    s_leds_status = lv_label_create(s_lights);
    lv_label_set_text(s_leds_status, tr(STR_LOADING));
    lv_obj_set_style_text_color(s_leds_status, PP_TEXT_MUTED, 0);
    lv_obj_align(s_leds_status, LV_ALIGN_CENTER, 0, 0);
    s_leds_grid = lv_obj_create(s_lights);
    lv_obj_set_size(s_leds_grid, LV_PCT(100), th() - 56);
    lv_obj_align(s_leds_grid, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_leds_grid, PP_BG, 0);
    lv_obj_set_style_border_width(s_leds_grid, 0, 0);
    lv_obj_set_style_pad_all(s_leds_grid, 16, 0);
    lv_obj_set_style_pad_row(s_leds_grid, 10, 0);
    lv_obj_set_style_pad_column(s_leds_grid, 8, 0);
    lv_obj_set_flex_flow(s_leds_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_add_flag(s_leds_grid, LV_OBJ_FLAG_SCROLLABLE);
}

/* ---- Tune ---- */
static void tune_set_speed_lbl(void);
static void tune_set_flow_lbl(void);

static int bambu_pct_to_lvl(int pct)
{
    /* spd_mag / mapped %: Silent~50, Standard~100, Sport~124, Ludicrous~166. */
    if (pct <= 60) return 1;
    if (pct <= 110) return 2;
    if (pct <= 140) return 3;
    return 4;
}

static void bambu_spd_highlight(int lvl)
{
    if (lvl < 1) lvl = 1;
    if (lvl > 4) lvl = 4;
    s_bambu_spd_lvl = lvl;
    for (int i = 0; i < 4; i++) {
        if (!s_bambu_spd_btns[i]) continue;
        bool on = (i + 1) == lvl;
        lv_obj_set_style_border_color(s_bambu_spd_btns[i], on ? PP_ORANGE : PP_BORDER, 0);
        lv_obj_set_style_border_width(s_bambu_spd_btns[i], on ? 2 : 1, 0);
    }
}

static void tune_apply_backend(void)
{
    bool bambu = app_state_active_is_bambu();
    if (s_tune_fdm) {
        if (bambu) lv_obj_add_flag(s_tune_fdm, LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_clear_flag(s_tune_fdm, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_tune_bambu) {
        if (bambu) lv_obj_clear_flag(s_tune_bambu, LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_add_flag(s_tune_bambu, LV_OBJ_FLAG_HIDDEN);
    }
    if (bambu) {
        pp_status_t st;
        app_state_get(&st);
        bambu_spd_highlight(bambu_pct_to_lvl(st.speed));
    } else {
        pp_status_t st;
        app_state_get(&st);
        if (st.speed > 0) {
            s_tune_speed = st.speed;
            if (s_tune_speed_slider) {
                int sv = s_tune_speed > 200 ? 200 : s_tune_speed;
                lv_slider_set_value(s_tune_speed_slider, sv, LV_ANIM_OFF);
            }
            tune_set_speed_lbl();
        }
    }
}

static void tune_set_speed_lbl(void)
{
    if (!s_tune_speed_val) return;
    char buf[16]; snprintf(buf, sizeof(buf), "%d%%", s_tune_speed);
    lv_label_set_text(s_tune_speed_val, buf);
}
static void tune_set_flow_lbl(void)
{
    if (!s_tune_flow_val) return;
    char buf[16]; snprintf(buf, sizeof(buf), "%d%%", s_tune_flow);
    lv_label_set_text(s_tune_flow_val, buf);
}

static void on_speed_slider(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (ui_locked_block_public()) return;
    s_tune_speed = (int)lv_slider_get_value(s_tune_speed_slider);
    tune_set_speed_lbl();
    app_state_post_cmd_n(PP_CMD_SET_SPEED, 0, s_tune_speed, 0);
}
static void on_flow_slider(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
    if (ui_locked_block_public()) return;
    s_tune_flow = (int)lv_slider_get_value(s_tune_flow_slider);
    tune_set_flow_lbl();
    app_state_post_cmd_n(PP_CMD_SET_FLOW, 0, s_tune_flow, 0);
}

static void numpad_set_speed(float v)
{
    s_tune_speed = (int)(v + 0.5f);
    if (s_tune_speed < 1) s_tune_speed = 1;
    if (s_tune_speed > 999) s_tune_speed = 999;
    if (s_tune_speed_slider) {
        int sv = s_tune_speed > 200 ? 200 : s_tune_speed;
        lv_slider_set_value(s_tune_speed_slider, sv, LV_ANIM_OFF);
    }
    tune_set_speed_lbl();
    app_state_post_cmd_n(PP_CMD_SET_SPEED, 0, s_tune_speed, 0);
}
static void numpad_set_flow(float v)
{
    s_tune_flow = (int)(v + 0.5f);
    if (s_tune_flow < 1) s_tune_flow = 1;
    if (s_tune_flow > 999) s_tune_flow = 999;
    if (s_tune_flow_slider) {
        int sv = s_tune_flow > 200 ? 200 : s_tune_flow;
        lv_slider_set_value(s_tune_flow_slider, sv, LV_ANIM_OFF);
    }
    tune_set_flow_lbl();
    app_state_post_cmd_n(PP_CMD_SET_FLOW, 0, s_tune_flow, 0);
}

static void on_speed_val_click(lv_event_t *e)
{
    (void)e;
    if (ui_locked_block_public()) return;
    tools_numpad_open("Speed %", (float)s_tune_speed, 1.f, 999.f, 0, numpad_set_speed);
}
static void on_flow_val_click(lv_event_t *e)
{
    (void)e;
    if (ui_locked_block_public()) return;
    tools_numpad_open("Flow %", (float)s_tune_flow, 1.f, 999.f, 0, numpad_set_flow);
}

static void on_speed(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    s_tune_speed = v;
    if (s_tune_speed_slider) lv_slider_set_value(s_tune_speed_slider, v, LV_ANIM_OFF);
    tune_set_speed_lbl();
    app_state_post_cmd_n(PP_CMD_SET_SPEED, 0, v, 0);
}
static void on_flow(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    s_tune_flow = v;
    if (s_tune_flow_slider) lv_slider_set_value(s_tune_flow_slider, v, LV_ANIM_OFF);
    tune_set_flow_lbl();
    app_state_post_cmd_n(PP_CMD_SET_FLOW, 0, v, 0);
}

static void on_bambu_spd(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int lvl = (int)(intptr_t)lv_event_get_user_data(e);
    bambu_spd_highlight(lvl);
    app_state_post_cmd_n(PP_CMD_SET_SPEED, 0, lvl, 0);
}

static void build_tune(void)
{
    s_tune = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_tune, PP_BG, 0);
    make_hdr(s_tune, tr(STR_TUNE), tools_go_back);

    /* Moonraker / Connect: continuous % speed + flow */
    s_tune_fdm = lv_obj_create(s_tune);
    lv_obj_set_size(s_tune_fdm, tw(), th() - 56);
    lv_obj_align(s_tune_fdm, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_opa(s_tune_fdm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tune_fdm, 0, 0);
    lv_obj_set_style_pad_all(s_tune_fdm, 0, 0);
    lv_obj_clear_flag(s_tune_fdm, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sl = lv_label_create(s_tune_fdm);
    lv_label_set_text(sl, tr(STR_SPEED_FACTOR));
    lv_obj_set_style_text_color(sl, PP_TEXT_MUTED, 0);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 16, 14);

    s_tune_speed_val = lv_label_create(s_tune_fdm);
    lv_obj_set_style_text_color(s_tune_speed_val, PP_ORANGE, 0);
    lv_obj_set_style_text_font(s_tune_speed_val, PP_F20, 0);
    lv_obj_align(s_tune_speed_val, LV_ALIGN_TOP_RIGHT, -100, 10);
    lv_obj_add_flag(s_tune_speed_val, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_tune_speed_val, on_speed_val_click, LV_EVENT_CLICKED, NULL);
    tune_set_speed_lbl();
    lv_obj_t *sp_set = make_button(s_tune_fdm, "Set...", on_speed_val_click, NULL, NULL);
    lv_obj_set_size(sp_set, 72, 36);
    lv_obj_align(sp_set, LV_ALIGN_TOP_RIGHT, -16, 8);

    s_tune_speed_slider = lv_slider_create(s_tune_fdm);
    lv_obj_set_width(s_tune_speed_slider, tw() - 32);
    lv_slider_set_range(s_tune_speed_slider, 1, 200);
    lv_slider_set_value(s_tune_speed_slider, s_tune_speed, LV_ANIM_OFF);
    lv_obj_align(s_tune_speed_slider, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_bg_color(s_tune_speed_slider, PP_ORANGE, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_tune_speed_slider, on_speed_slider, LV_EVENT_RELEASED, NULL);

    const int sp[] = {50, 100, 150, 200};
    for (int i = 0; i < 4; i++) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", sp[i]);
        lv_obj_t *b = make_button(s_tune_fdm, buf, on_speed, (void *)(intptr_t)sp[i], NULL);
        lv_obj_set_size(b, 70, 40);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 16 + i * 78, 84);
    }

    lv_obj_t *fl = lv_label_create(s_tune_fdm);
    lv_label_set_text(fl, tr(STR_FLOW_FACTOR));
    lv_obj_set_style_text_color(fl, PP_TEXT_MUTED, 0);
    lv_obj_align(fl, LV_ALIGN_TOP_LEFT, 16, 144);

    s_tune_flow_val = lv_label_create(s_tune_fdm);
    lv_obj_set_style_text_color(s_tune_flow_val, PP_ORANGE, 0);
    lv_obj_set_style_text_font(s_tune_flow_val, PP_F20, 0);
    lv_obj_align(s_tune_flow_val, LV_ALIGN_TOP_RIGHT, -100, 140);
    lv_obj_add_flag(s_tune_flow_val, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_tune_flow_val, on_flow_val_click, LV_EVENT_CLICKED, NULL);
    tune_set_flow_lbl();
    lv_obj_t *fl_set = make_button(s_tune_fdm, "Set...", on_flow_val_click, NULL, NULL);
    lv_obj_set_size(fl_set, 72, 36);
    lv_obj_align(fl_set, LV_ALIGN_TOP_RIGHT, -16, 138);

    s_tune_flow_slider = lv_slider_create(s_tune_fdm);
    lv_obj_set_width(s_tune_flow_slider, tw() - 32);
    lv_slider_set_range(s_tune_flow_slider, 1, 200);
    lv_slider_set_value(s_tune_flow_slider, s_tune_flow, LV_ANIM_OFF);
    lv_obj_align(s_tune_flow_slider, LV_ALIGN_TOP_MID, 0, 174);
    lv_obj_set_style_bg_color(s_tune_flow_slider, PP_ORANGE, LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_tune_flow_slider, on_flow_slider, LV_EVENT_RELEASED, NULL);

    const int flv[] = {95, 100, 105, 110};
    for (int i = 0; i < 4; i++) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", flv[i]);
        lv_obj_t *b = make_button(s_tune_fdm, buf, on_flow, (void *)(intptr_t)flv[i], NULL);
        lv_obj_set_size(b, 70, 40);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 16 + i * 78, 214);
    }

    /* Bambu: four named presets (Silent / Standard / Sport / Ludicrous) */
    s_tune_bambu = lv_obj_create(s_tune);
    lv_obj_set_size(s_tune_bambu, tw(), th() - 56);
    lv_obj_align(s_tune_bambu, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_opa(s_tune_bambu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tune_bambu, 0, 0);
    lv_obj_set_style_pad_all(s_tune_bambu, 0, 0);
    lv_obj_clear_flag(s_tune_bambu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_tune_bambu, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *bl = lv_label_create(s_tune_bambu);
    lv_label_set_text(bl, tr(STR_SPEED_FACTOR));
    lv_obj_set_style_text_color(bl, PP_TEXT_MUTED, 0);
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 16, 12);

    static const char *const names[] = {"Silent", "Standard", "Sport", "Ludicrous"};
    const int btn_h = 48;
    const int gap = 8;
    for (int i = 0; i < 4; i++) {
        s_bambu_spd_btns[i] = make_button(s_tune_bambu, names[i], on_bambu_spd,
                                          (void *)(intptr_t)(i + 1), NULL);
        lv_obj_set_size(s_bambu_spd_btns[i], tw() - 32, btn_h);
        lv_obj_align(s_bambu_spd_btns[i], LV_ALIGN_TOP_MID, 0, 40 + i * (btn_h + gap));
    }
    bambu_spd_highlight(2);
}

/* ---- Calibration ---- */
static void go_calib(lv_event_t *e) { (void)e; lv_screen_load(s_calib); }

static void on_pid_run(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    int target = which ? 60 : 220;
    app_state_post_cmd_n(PP_CMD_PID, which, target, 0);
}
static void on_z_adj(lv_event_t *e)
{
    if (ui_locked_block_public()) return;
    int milli = (int)(intptr_t)lv_event_get_user_data(e);
    app_state_post_cmd_n(PP_CMD_Z_ADJUST, 0, milli, 0);
}
static void on_z_apply(lv_event_t *e)
{
    (void)e; if (!ui_locked_block_public()) app_state_post_cmd(PP_CMD_Z_APPLY, NULL);
}
static void numpad_set_z(float v)
{
    char g[48];
    snprintf(g, sizeof(g), "SET_GCODE_OFFSET Z=%.3f MOVE=1", (double)v);
    app_state_post_cmd(PP_CMD_GCODE, g);
}
static void on_z_numpad(lv_event_t *e)
{
    (void)e;
    if (ui_locked_block_public()) return;
    tools_numpad_open("Z offset mm", 0.f, -2.f, 2.f, 3, numpad_set_z);
}
static void on_mesh(lv_event_t *e)
{
    (void)e; if (ui_locked_block_public()) return;
    tools_confirm(tr(STR_BED_MESH), "Run BED_MESH_CALIBRATE?", do_mesh);
}
static void on_save_cfg(lv_event_t *e)
{
    (void)e; if (ui_locked_block_public()) return;
    tools_confirm(tr(STR_SAVE_CONFIG), "SAVE_CONFIG and restart Klipper?", do_save_cfg);
}

static void build_calib_children(void)
{
    s_endstops = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_endstops, PP_BG, 0);
    make_hdr(s_endstops, tr(STR_ENDSTOPS), go_calib);
    s_endstop_status = lv_label_create(s_endstops);
    lv_label_set_text(s_endstop_status, "");
    lv_obj_set_style_text_color(s_endstop_status, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_endstop_status, PP_F14, 0);
    lv_obj_align(s_endstop_status, LV_ALIGN_TOP_LEFT, 16, 64);
    lv_obj_t *ref = make_button(s_endstops, "Refresh", on_endstop_refresh, NULL, NULL);
    lv_obj_set_size(ref, 110, 40);
    lv_obj_align(ref, LV_ALIGN_TOP_RIGHT, -12, 58);
    s_endstop_grid = lv_obj_create(s_endstops);
    lv_obj_set_size(s_endstop_grid, LV_PCT(100), th() - 110);
    lv_obj_align(s_endstop_grid, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_set_style_bg_opa(s_endstop_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_endstop_grid, 0, 0);
    lv_obj_set_style_pad_all(s_endstop_grid, 8, 0);
    lv_obj_set_style_pad_row(s_endstop_grid, 8, 0);
    lv_obj_set_style_pad_column(s_endstop_grid, 8, 0);
    lv_obj_set_flex_flow(s_endstop_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_endstop_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_endstop_grid, LV_OBJ_FLAG_SCROLLABLE);

    s_pid = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_pid, PP_BG, 0);
    make_hdr(s_pid, tr(STR_AUTO_PID), go_calib);
    lv_obj_t *pe = make_button(s_pid, "Extruder 220", on_pid_run, (void *)(intptr_t)0, NULL);
    lv_obj_t *pb = make_button(s_pid, "Bed 60", on_pid_run, (void *)(intptr_t)1, NULL);
    lv_obj_set_size(pe, 180, 56); lv_obj_set_size(pb, 180, 56);
    lv_obj_align(pe, LV_ALIGN_TOP_LEFT, 16, 80);
    lv_obj_align(pb, LV_ALIGN_TOP_LEFT, 210, 80);
    lv_obj_t *ps = make_button(s_pid, tr(STR_SAVE_CONFIG), on_save_cfg, NULL, NULL);
    lv_obj_align(ps, LV_ALIGN_TOP_LEFT, 16, 160);

    s_zoff = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_zoff, PP_BG, 0);
    make_hdr(s_zoff, tr(STR_Z_OFFSET), go_calib);
    const int zm[] = {-50, -10, 10, 50}; /* milli-mm */
    const char *zl[] = { "-0.05", "-0.01", "+0.01", "+0.05" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = make_button(s_zoff, zl[i], on_z_adj, (void *)(intptr_t)zm[i], NULL);
        lv_obj_set_size(b, 90, 48);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 16 + i * 100, 80);
    }
    lv_obj_t *zset = make_button(s_zoff, "Set...", on_z_numpad, NULL, NULL);
    lv_obj_set_size(zset, 120, 48);
    lv_obj_align(zset, LV_ALIGN_TOP_LEFT, 16, 150);
    lv_obj_t *za = make_button(s_zoff, "Apply Probe", on_z_apply, NULL, NULL);
    lv_obj_align(za, LV_ALIGN_TOP_LEFT, 150, 150);
    lv_obj_t *zs = make_button(s_zoff, tr(STR_SAVE_CONFIG), on_save_cfg, NULL, NULL);
    lv_obj_align(zs, LV_ALIGN_TOP_LEFT, 16, 210);

    s_mesh = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_mesh, PP_BG, 0);
    make_hdr(s_mesh, tr(STR_BED_MESH), go_calib);
    lv_obj_t *mb = make_button(s_mesh, "Calibrate", on_mesh, NULL, NULL);
    lv_obj_set_size(mb, 160, 56);
    lv_obj_align(mb, LV_ALIGN_TOP_LEFT, 16, 80);
    lv_obj_t *ms = make_button(s_mesh, tr(STR_SAVE_CONFIG), on_save_cfg, NULL, NULL);
    lv_obj_align(ms, LV_ALIGN_TOP_LEFT, 200, 80);
}

static bool endstop_is_triggered(const char *state)
{
    if (!state) return false;
    /* Klipper reports "TRIGGERED" / "open" (case may vary). */
    for (const char *p = state; *p; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == 't') {
            const char *needle = "triggered";
            const char *a = p;
            const char *b = needle;
            while (*a && *b) {
                char ca = *a;
                if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
                if (ca != *b) break;
                a++; b++;
            }
            if (!*b) return true;
        }
    }
    return false;
}

void ui_apply_endstops(void *arg)
{
    pp_endstop_list_t *el = (pp_endstop_list_t *)arg;
    if (!el) return;
    if (s_endstop_grid) lv_obj_clean(s_endstop_grid);
    if (!s_endstop_grid) { free(el); return; }

    if (el->count <= 0) {
        if (s_endstop_status) lv_label_set_text(s_endstop_status, "(no endstops)");
        free(el);
        return;
    }
    if (s_endstop_status) {
        char buf[56];
        snprintf(buf, sizeof(buf), "%d  ·  green=TRIGGERED  red=open", el->count);
        lv_label_set_text(s_endstop_status, buf);
    }

    const int chip_w = t_portrait() ? (int)((tw() - 40) / 2) : 150;
    for (int i = 0; i < el->count; i++) {
        bool trig = endstop_is_triggered(el->state[i]);
        bool err = (strcmp(el->name[i], "error") == 0);
        lv_color_t col = err ? PP_ERROR : (trig ? PP_OK : PP_ERROR);

        lv_obj_t *chip = lv_button_create(s_endstop_grid);
        lv_obj_set_size(chip, chip_w, 56);
        lv_obj_set_style_bg_color(chip, col, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_40, 0);
        lv_obj_set_style_border_color(chip, col, 0);
        lv_obj_set_style_border_width(chip, 2, 0);
        lv_obj_set_style_radius(chip, 6, 0);
        lv_obj_set_style_shadow_width(chip, 0, 0);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chip, on_endstop_refresh, LV_EVENT_CLICKED, NULL);

        char line[48];
        snprintf(line, sizeof(line), "%s\n%s", el->name[i], el->state[i]);
        lv_obj_t *lbl = lv_label_create(chip);
        lv_label_set_text(lbl, line);
        lv_obj_set_style_text_color(lbl, PP_TEXT, 0);
        lv_obj_set_style_text_font(lbl, PP_F14, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl, chip_w - 16);
        lv_obj_center(lbl);
    }
    free(el);
}

static void build_calib(void)
{
    build_calib_children();
    s_calib = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_calib, PP_BG, 0);
    make_hdr(s_calib, tr(STR_CALIBRATION), tools_go_back);

    lv_obj_t *grid = lv_obj_create(s_calib);
    lv_obj_set_size(grid, LV_PCT(100), th() - 56);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(grid, PP_BG, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 16, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLL_ELASTIC);

    const int cols = t_portrait() ? 2 : 4;
    const int gap = 12;
    const int pad = 16;
    const int tile_w = (tw() - pad * 2 - gap * (cols - 1)) / cols;
    const int tile_h = t_portrait() ? 88 : 100;

    add_hub_tile(grid, tr(STR_ENDSTOPS), open_endstops, NULL, tile_w, tile_h, NULL);
    add_hub_tile(grid, tr(STR_AUTO_PID), open_scr, s_pid, tile_w, tile_h, NULL);
    add_hub_tile(grid, tr(STR_Z_OFFSET), open_scr, s_zoff, tile_w, tile_h, NULL);
    add_hub_tile(grid, tr(STR_BED_MESH), open_scr, s_mesh, tile_w, tile_h, NULL);
}

/* ---- Fault page ---- */
static void on_fault_restart(lv_event_t *e)
{
    (void)e; tools_confirm(tr(STR_KLIPPER_RESTART), "Send RESTART?", do_klipper_restart);
}
static void on_fault_fw(lv_event_t *e)
{
    (void)e; tools_confirm(tr(STR_FW_RESTART), "Send FIRMWARE_RESTART?", do_fw_restart);
}

static void build_fault(void)
{
    s_fault = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_fault, PP_BG, 0);
    make_hdr(s_fault, "Klipper Error", go_status);
    s_fault_msg = lv_label_create(s_fault);
    lv_label_set_text(s_fault_msg, "Printer reported an error or shutdown.");
    lv_obj_set_style_text_font(s_fault_msg, PP_F16, 0);
    lv_obj_set_width(s_fault_msg, tw() - 32);
    lv_label_set_long_mode(s_fault_msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_fault_msg, LV_ALIGN_TOP_LEFT, 16, 80);

    lv_obj_t *r = make_button(s_fault, tr(STR_KLIPPER_RESTART), on_fault_restart, NULL, NULL);
    lv_obj_t *f = make_button(s_fault, tr(STR_FW_RESTART), on_fault_fw, NULL, NULL);
    lv_obj_set_size(r, 160, 56); lv_obj_set_size(f, 160, 56);
    lv_obj_align(r, LV_ALIGN_TOP_LEFT, 16, 160);
    lv_obj_align(f, LV_ALIGN_TOP_LEFT, 200, 160);
}

void ui_tools_show_fault_if_needed(const char *state)
{
    static char last[16];
    if (!state || !app_state_active_is_moonraker() || !s_fault) return;
    if (!strcmp(state, "ERROR") && strcmp(last, "ERROR") != 0)
        lv_screen_load(s_fault);
    strlcpy(last, state, sizeof(last));
}

/* ---- public API ---- */
void ui_tools_refresh_menu(void)
{
    bool moon = app_state_active_is_moonraker();
    bool bambu = app_state_active_is_bambu();
    for (int i = 0; i < 5; i++) {
        if (!s_hub_moon_btns[i]) continue;
        /* Tune (2) and Lights (4) are available for Moonraker and Bambu. */
        bool show = moon || (bambu && (i == 2 || i == 4));
        if (show) lv_obj_clear_flag(s_hub_moon_btns[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_hub_moon_btns[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* AFC visibility driven by ui_apply_afc */
}

void ui_tools_init(void)
{
    build_move();
    build_temp();
    build_webcam();
    build_afc();
    build_console();
    build_macros();
    build_lights();
    build_tune();
    build_calib();
    build_fault();
    build_hub();
    rebuild_hub_list();
}

void ui_tools_open(void)
{
    ui_tools_set_back_to_hub();
    ui_tools_refresh_menu();
    lv_screen_load(s_hub);
}

void ui_tools_open_afc(void)
{
    pp_afc_t afc;
    app_state_get_afc(&afc);
    if (!afc.present) return;
    lv_screen_load(s_afc);
}

void ui_tools_open_move(void)   { lv_screen_load(s_move); }
void ui_tools_open_temp(void)   { lv_screen_load(s_temp); }
void ui_tools_open_tune(void)
{
    tune_apply_backend();
    lv_screen_load(s_tune);
}
void ui_tools_open_calib(void)  { lv_screen_load(s_calib); }

void ui_tools_open_webcam(void)
{
    if (s_snap_ph) lv_label_set_text(s_snap_ph, tr(STR_LOADING_WEBCAM));
    app_state_fetch_snapshot();
    lv_screen_load(s_webcam);
}

void ui_tools_open_console(void)
{
    app_state_post_cmd(PP_CMD_GCODE_LOG, NULL);
    lv_screen_load(s_console);
    ui_kb_focus_set(s_console_ta);
}

void ui_tools_open_macros(void)
{
    app_state_post_cmd(PP_CMD_LIST_MACROS, NULL);
    lv_screen_load(s_macros);
}

void ui_tools_open_lights(void)
{
    if (s_leds_status) {
        lv_label_set_text(s_leds_status, tr(STR_LOADING));
        lv_obj_clear_flag(s_leds_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_leds_grid) lv_obj_clean(s_leds_grid);
    app_state_post_cmd(PP_CMD_LIST_LEDS, NULL);
    lv_screen_load(s_lights);
}

lv_obj_t *ui_tools_hub_screen(void) { return s_hub; }

bool ui_tools_is_hub_active(void)
{
    return lv_screen_active() == s_hub;
}

bool ui_tools_is_webcam_active(void)
{
    return lv_screen_active() == s_webcam;
}

bool ui_tools_is_afc_active(void)
{
    return s_afc && lv_screen_active() == s_afc;
}
