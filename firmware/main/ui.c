/* Prusa-Touch — LVGL UI implementation (Prusa-themed). */
#include "ui.h"
#include "usb_hid_kb.h"
#include "app_state.h"
#include "printer_store.h"
#include "pandaprusa_theme.h"
#include "wifi.h"
#include "prefs.h"
#include "i18n.h"
#include "skin.h"
#include "layout.h"
#include "pandatouch_display.h"   /* pt_display_schedule_ui — for the test nav API */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl.h"
#include "misc/cache/lv_image_cache.h"   /* lv_image_cache_drop (not in lvgl.h) */
#include "draw/lv_image_decoder.h"       /* lv_image_decoder_get_info           */
#include "esp_attr.h"
#include "esp_heap_caps.h"   /* PSRAM allocation for the fleet snapshot */

static const char *TAG = "ui";

/* ---- display geometry (orientation-aware) ----
 * Screens are built once at boot AFTER ui_apply_orient() sets the rotation, so these report the
 * active logical resolution: 800x480 landscape, 480x800 portrait. Builders use these (and
 * ui_portrait()) to lay out single-column in portrait instead of hardcoding 800/480. */
static inline int32_t scr_w(void) { return lv_display_get_horizontal_resolution(lv_display_get_default()); }
static inline int32_t scr_h(void) { return lv_display_get_vertical_resolution(lv_display_get_default()); }
static inline bool    ui_portrait(void) { return scr_w() < 600; }   /* 480 portrait vs 800 landscape */

/* ---- screens ---- */
static lv_obj_t *s_scr_boot;       /* splash / loading        */
static lv_obj_t *s_boot_bar;
static lv_obj_t *s_boot_status;

static lv_obj_t *s_scr_dash;       /* fleet dashboard (home)  */
static lv_obj_t *s_dash_grid;      /* scrollable card grid    */
static int       s_dash_count;
static pp_status_t *s_dash_items;  /* persistent fleet snapshot (PSRAM) for instant card-open */
typedef struct {
    uint8_t       *buf;
    lv_image_dsc_t dsc;
    char           url[160];
} pp_card_thumb_t;
static EXT_RAM_BSS_ATTR pp_card_thumb_t s_card_thumbs[PP_MAX_PRINTERS];
static lv_obj_t      *s_scr_status;     /* per-printer detail      */
static lv_obj_t *s_scr_files;
static lv_obj_t *s_scr_printers;
static lv_obj_t *s_scr_addpick;    /* "Add a printer" -> QR/IP escort to the web UI (issue #5) */
static lv_obj_t *s_scr_about;
static lv_obj_t *s_scr_prefs;      /* Preferences (sort/filter/logo) */
static lv_obj_t *s_scr_farm;       /* Prusa Farm (org stats + orders) */
static lv_obj_t *s_farm_stat;      /* farm printer-summary label */
static lv_obj_t *s_farm_list;      /* farm orders container */

/* header title (shows active printer name) */
static lv_obj_t *s_title_lbl;

/* Wordmark bylines across all headers — toggled by the logo preference. */
static lv_obj_t *s_bylines[12];
static int       s_byline_count;

/* Preferences widgets */
static lv_obj_t *s_pref_sort_dd;
static lv_obj_t *s_pref_logo_dd;
static lv_obj_t *s_pref_hideoff_sw;
static lv_obj_t *s_pref_autoupd_sw;
static lv_obj_t *s_pref_orient_dd;
static lv_obj_t *s_pref_theme_dd;

static lv_obj_t *s_pr_list;           /* the "Settings" tab list (device settings + web escort) */

/* wifi setup */
static lv_obj_t *s_scr_wifi;
static lv_obj_t *s_wifi_list;
static lv_obj_t *s_wifi_sel_lbl;
static lv_obj_t *s_wifi_ap_lbl;    /* hotspot-fallback hint */
static lv_obj_t *s_wifi_ta_pass;
static lv_obj_t *s_wifi_kb;
static char      s_wifi_ssids[PP_WIFI_MAX_SCAN][33];
static int       s_wifi_scan_count;
static char      s_wifi_selected[33];

/* ---- status / printer-detail widgets ---- */
static lv_obj_t *s_conn_dot;
static lv_obj_t *s_detail_img;     /* hero: model render on orange tile     */
static lv_obj_t *s_badge;          /* hero: state badge chip                */
static lv_obj_t *s_herotop;        /* hero: name+badge strip (state-tinted) */
static lv_obj_t *s_state_lbl;      /* hero: state text (badge label)        */
static lv_obj_t *s_model_lbl;      /* hero: model sub-line                  */
static lv_obj_t *s_nozzle_lbl;
static lv_obj_t *s_bed_lbl;
static lv_obj_t *s_speed_lbl;
static lv_obj_t *s_z_lbl;
static lv_obj_t *s_job_lbl;
static lv_obj_t *s_bar;
static lv_obj_t *s_pct_lbl;
static lv_obj_t *s_eta_lbl;
static lv_obj_t *s_btn_pause_lbl;
static lv_obj_t *s_btn_control;
/* ---- attention dialog banner (detail screen) ---- */
static lv_obj_t *s_jobcard;        /* hidden while an attention dialog is shown */
static lv_obj_t *s_attn_card;
static lv_obj_t *s_attn_title;
static lv_obj_t *s_attn_text;
static lv_obj_t *s_attn_btns[3];
static lv_obj_t *s_attn_btn_lbls[3];
static int       s_attn_dialog_id;            /* current dialog id (for the action) */
static char      s_attn_btn_text[3][24];      /* current button labels (for the action) */

/* ---- file screen ---- */
static lv_obj_t *s_file_list;
static lv_obj_t *s_files_banner;          /* "Files on <printer>" context banner */
static char      s_active_printer[24];    /* mirror of active printer name/model  */
static char      s_active_model[28];
static pp_file_t s_files[PP_MAX_FILES];
static int       s_file_count;
static bool      s_files_usb_mode = false;

/* ---- file-detail (gcode preview) screen ---- */
static lv_obj_t      *s_scr_filedetail;
static lv_obj_t      *s_fd_name;        /* header: file name        */
static lv_obj_t      *s_thumb_img;      /* lv_image (PNG preview)   */
static lv_obj_t      *s_thumb_ph;       /* placeholder label        */
static lv_obj_t      *s_thumb_load;     /* indeterminate load bar   */
static lv_image_dsc_t s_thumb_dsc;      /* descriptor over s_thumb_buf */
static uint8_t       *s_thumb_buf;      /* owned PNG bytes on display  */
static char           s_sel_path[160];  /* file selected for printing  */


/* AFC (Moonraker BoxTurtle) — Control lane card + Status chip */
static lv_obj_t      *s_afc_chip;       /* Status: "AFC: laneN"                   */

static void fmt_eta(int secs, char *out, size_t n)
{
    if (secs < 0) { snprintf(out, n, "--"); return; }
    int h = secs / 3600, m = (secs % 3600) / 60;
    if (h > 0) snprintf(out, n, "%dh %02dm", h, m);
    else       snprintf(out, n, "%dm", m);
}

/* forward declarations */
static void on_control_clicked(lv_event_t *e);
static void on_estop_clicked(lv_event_t *e);
static void on_afc_chip_clicked(lv_event_t *e);
static void on_printers_clicked(lv_event_t *e);
static void refresh_printers_list(void);
/* Screen lock: returns true (and pops the PIN prompt) if the screen is locked, so an action
 * callback can bail. Browsing callbacks don't call it. */
static bool ui_locked_block(void);
bool ui_locked_block_public(void) { return ui_locked_block(); }
static void on_wifi_open(lv_event_t *e);
static void on_about_open(lv_event_t *e);
static void on_farm_open(lv_event_t *e);
static void on_prefs_open(lv_event_t *e);
static void thumb_clear(void);
static void thumb_show_loading(bool on);
static void nav_dash(lv_event_t *e);
static lv_obj_t *make_header(lv_obj_t *parent, const char *text);
static lv_obj_t *make_header_ex(lv_obj_t *parent, const char *text, bool brand);
static lv_obj_t *make_barbtn(lv_obj_t *bar, const char *text, lv_event_cb_t cb,
                             void *user_data, lv_coord_t w);
static void make_wordmark(lv_obj_t *parent);

static void thumb_load_anim_cb(void *bar, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)bar, (int32_t)v, LV_ANIM_OFF);
}

/* Show/hide the indeterminate preview load bar + keep the placeholder label in sync. */
static void thumb_show_loading(bool on)
{
    if (!s_thumb_load || !s_thumb_ph) return;
    lv_anim_delete(s_thumb_load, thumb_load_anim_cb);
    if (on) {
        lv_obj_clear_flag(s_thumb_ph, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_thumb_load, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_thumb_ph, tr(STR_LOADING_PREVIEW));
        lv_bar_set_value(s_thumb_load, 15, LV_ANIM_OFF);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_thumb_load);
        lv_anim_set_values(&a, 15, 85);
        lv_anim_set_duration(&a, 750);
        lv_anim_set_playback_duration(&a, 750);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a, thumb_load_anim_cb);
        lv_anim_start(&a);
    } else {
        lv_obj_add_flag(s_thumb_load, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_thumb_load, 0, LV_ANIM_OFF);
    }
}

/* Detach the preview image, drop its cached bitmap, free the PNG bytes, and show
 * the placeholder. Safe to call repeatedly (LVGL thread only). */
static void thumb_clear(void)
{
    lv_image_set_src(s_thumb_img, NULL);   /* stop referencing the buffer */
    lv_image_cache_drop(&s_thumb_dsc);     /* free any decoded bitmap      */
    if (s_thumb_buf) { free(s_thumb_buf); s_thumb_buf = NULL; }
    lv_memzero(&s_thumb_dsc, sizeof(s_thumb_dsc));
    lv_obj_add_flag(s_thumb_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_thumb_ph, LV_OBJ_FLAG_HIDDEN);
    thumb_show_loading(false);
}

/* Clear the dashboard per-card thumbnail cache. */
static void card_thumbs_clear(void)
{
    for (int i = 0; i < PP_MAX_PRINTERS; i++) {
        if (s_card_thumbs[i].buf) {
            lv_image_cache_drop(&s_card_thumbs[i].dsc);
            free(s_card_thumbs[i].buf);
            s_card_thumbs[i].buf = NULL;
        }
        s_card_thumbs[i].url[0] = '\0';
        lv_memzero(&s_card_thumbs[i].dsc, sizeof(lv_image_dsc_t));
    }
}

/* ---------- event handlers (LVGL thread) ---------- */
static void on_pause_clicked(lv_event_t *e)
{
    if (ui_locked_block()) return;
    /* The label text tells us which action applies. */
    const char *txt = lv_label_get_text(s_btn_pause_lbl);
    if (txt && strcmp(txt, tr(STR_RESUME)) == 0) {
        app_state_post_cmd(PP_CMD_RESUME, NULL);
    } else {
        app_state_post_cmd(PP_CMD_PAUSE, NULL);
    }
}

static void on_stop_clicked(lv_event_t *e)
{
    if (ui_locked_block()) return;
    app_state_post_cmd(PP_CMD_STOP, NULL);
}

static void on_files_clicked(lv_event_t *e)
{
    app_state_post_cmd(PP_CMD_LIST, NULL);
    lv_screen_load(s_scr_files);
}

static void on_back_clicked(lv_event_t *e)
{
    lv_screen_load(s_scr_status);
}

static void on_control_back(lv_event_t *e)
{
    (void)e;
    ui_request_screen("status");
}

static void on_control_clicked(lv_event_t *e)
{
    (void)e;
    ui_tools_open();
}

static void on_estop_clicked(lv_event_t *e)
{
    (void)e;
    ui_tools_estop();
}

/* Attention-banner button: answer the active printer's Connect dialog with the tapped label. */
static void on_attn_btn_clicked(lv_event_t *e)
{
    if (ui_locked_block()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 2 || !s_attn_dialog_id) return;
    app_state_dialog_action(s_attn_dialog_id, s_attn_btn_text[idx]);
}

/* Tapping a file opens its detail/preview screen (does NOT start a print). */
static void on_file_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_file_count) return;

    strlcpy(s_sel_path, s_files[idx].path, sizeof(s_sel_path));
    lv_label_set_text(s_fd_name, s_files[idx].display[0] ? s_files[idx].display
                                                         : s_files[idx].path);
    thumb_clear();
    if (s_files[idx].thumb[0]) {
        thumb_show_loading(true);
        app_state_fetch_thumb(s_files[idx].thumb);   /* -> ui_apply_thumb */
    } else {
        lv_label_set_text(s_thumb_ph, tr(STR_NO_PREVIEW));
        thumb_show_loading(false);
    }
    lv_screen_load(s_scr_filedetail);
}

/* ---------- small UI builders ---------- */
static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, PP_SURFACE, 0);
    lv_obj_set_style_border_color(c, PP_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 6, 0);   /* match Connect's 6px card radius */
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                             void *user_data, lv_obj_t **out_label)
{
    /* Connect-style ghost button: transparent fill, thin #4E4E4E outline, 4px radius,
     * white text; fills with surface-hi when pressed (matches Connect's Pause/Stop/Print). */
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 150, 56);
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

/* Telemetry icons (orange), rasterized from Connect's inline hero SVGs. */
extern const lv_image_dsc_t pt_ic_nozzle;
extern const lv_image_dsc_t pt_ic_bed;
extern const lv_image_dsc_t pt_ic_speed;

/* One Connect-style telemetry pill: muted label on top, then an orange icon + the
 * big white value. icon may be NULL. Returns the value label for the applier. */
static lv_obj_t *detail_cell(lv_obj_t *parent, int x, int y, int w, const char *label,
                             const lv_image_dsc_t *icon)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_set_size(cell, w, 56);
    lv_obj_align(cell, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(cell, PP_SURFACE, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_radius(cell, 6, 0);
    lv_obj_set_style_pad_all(cell, 8, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(cell);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(l, PP_F12, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);

    int vx = 0;
    if (icon) {
        lv_obj_t *ic = lv_image_create(cell);
        lv_image_set_src(ic, icon);
        lv_obj_align(ic, LV_ALIGN_BOTTOM_LEFT, 0, 2);
        vx = 34;   /* value sits to the right of the 28px icon */
    }
    lv_obj_t *v = lv_label_create(cell);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_color(v, PP_TEXT, 0);
    lv_obj_set_style_text_font(v, PP_F20, 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, vx, 0);
    return v;
}

static void on_afc_chip_clicked(lv_event_t *e)
{
    (void)e;
    ui_tools_open_afc();
}

static void build_status_screen(void)
{
    s_scr_status = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_status, PP_BG, 0);
    lv_obj_clear_flag(s_scr_status, LV_OBJ_FLAG_SCROLLABLE);

    /* Black top bar: wordmark + e-stop + settings + home (fleet) + connection dot */
    lv_obj_t *bar = make_header(s_scr_status, NULL);   /* identical wordmark placement */

    lv_obj_t *home = make_barbtn(bar, LV_SYMBOL_HOME, nav_dash, NULL, 44);
    lv_obj_align(home, LV_ALIGN_RIGHT_MID, -4, 0);

    s_conn_dot = lv_obj_create(bar);
    lv_obj_set_size(s_conn_dot, 18, 18);
    lv_obj_set_style_radius(s_conn_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_conn_dot, 0, 0);
    lv_obj_set_style_bg_color(s_conn_dot, PP_ERROR, 0);
    lv_obj_align(s_conn_dot, LV_ALIGN_RIGHT_MID, -52, 0);

    lv_obj_t *gear = make_barbtn(bar, LV_SYMBOL_SETTINGS, on_printers_clicked, NULL, 44);
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -78, 0);

    /* Centered red E-STOP — wide hit target; confirm dialog still required. */
    lv_obj_t *estop = lv_button_create(bar);
    lv_obj_set_size(estop, 160, 44);
    lv_obj_align(estop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(estop, PP_ERROR, 0);
    lv_obj_set_style_bg_opa(estop, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(estop, lv_color_darken(PP_ERROR, 30), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(estop, 0, 0);
    lv_obj_set_style_radius(estop, 6, 0);
    lv_obj_set_style_shadow_width(estop, 0, 0);
    lv_obj_add_event_cb(estop, on_estop_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *estop_lbl = lv_label_create(estop);
    lv_label_set_text(estop_lbl, tr(STR_ESTOP));
    lv_obj_set_style_text_color(estop_lbl, PP_WHITE, 0);
    lv_obj_set_style_text_font(estop_lbl, PP_F16, 0);
    lv_obj_center(estop_lbl);

    /* Portrait (480x800) lays the detail screen out single-column: hero across the top,
     * 2x2 telemetry, full-width job/attention card, 2x2 action buttons. Landscape keeps the
     * wide single-row layout. CW = full-width card (16px side margins). */
    const bool P = ui_portrait();
    const int  CW = scr_w() - 32;

    /* ---- hero: orange model tile + state badge + model line ---- */
    lv_obj_t *tile = lv_obj_create(s_scr_status);
    lv_obj_set_size(tile, 84, 84);
    lv_obj_align(tile, LV_ALIGN_TOP_LEFT, 16, 64);
    lv_obj_set_style_bg_color(tile, PP_ORANGE, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 8, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    s_detail_img = lv_image_create(tile);
    lv_obj_center(s_detail_img);

    /* name + state badge on one row (Connect hero) */
    lv_obj_t *herotop = lv_obj_create(s_scr_status);
    lv_obj_set_size(herotop, P ? scr_w() - 128 : 660, 38);
    lv_obj_align(herotop, LV_ALIGN_TOP_LEFT, 112, 66);
    /* State-tinted strip behind name+badge — mirrors the dashboard card header (recolored
     * per-state in ui_apply_status). */
    lv_obj_set_style_bg_opa(herotop, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(herotop, PP_STRIP_GRAY, 0);
    lv_obj_set_style_radius(herotop, 6, 0);
    lv_obj_set_style_border_width(herotop, 0, 0);
    lv_obj_set_style_pad_all(herotop, 0, 0);
    lv_obj_set_style_pad_hor(herotop, 10, 0);
    lv_obj_set_style_pad_column(herotop, 12, 0);
    s_herotop = herotop;
    lv_obj_clear_flag(herotop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(herotop, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(herotop, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_title_lbl = lv_label_create(herotop);            /* printer name (was in the bar) */
    lv_label_set_text(s_title_lbl, tr(STR_NAV_PRINTER));
    lv_obj_set_style_text_color(s_title_lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_title_lbl, PP_F28, 0);

    s_badge = lv_obj_create(herotop);
    lv_obj_set_size(s_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(s_badge, 12, 0);
    lv_obj_set_style_pad_ver(s_badge, 4, 0);
    lv_obj_set_style_radius(s_badge, 4, 0);
    lv_obj_set_style_border_width(s_badge, 0, 0);
    lv_obj_set_style_bg_color(s_badge, PP_BADGE_GRAY, 0);
    lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE);
    s_state_lbl = lv_label_create(s_badge);
    lv_label_set_text(s_state_lbl, "...");
    lv_obj_set_style_text_color(s_state_lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_state_lbl, PP_F16, 0);
    lv_obj_center(s_state_lbl);

    s_model_lbl = lv_label_create(s_scr_status);
    lv_label_set_text(s_model_lbl, "");
    lv_obj_set_style_text_color(s_model_lbl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_model_lbl, PP_F14, 0);
    lv_label_set_long_mode(s_model_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_model_lbl, P ? scr_w() - 128 : 540);
    lv_obj_align(s_model_lbl, LV_ALIGN_TOP_LEFT, 112, 114);

    s_afc_chip = lv_label_create(s_scr_status);
    lv_label_set_text(s_afc_chip, "");
    lv_obj_set_style_text_color(s_afc_chip, PP_ORANGE, 0);
    lv_obj_set_style_text_font(s_afc_chip, PP_F14, 0);
    lv_obj_align(s_afc_chip, LV_ALIGN_TOP_LEFT, 112, 134);
    lv_obj_add_flag(s_afc_chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_afc_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_afc_chip, on_afc_chip_clicked, LV_EVENT_CLICKED, NULL);

    /* ---- telemetry cells ---- landscape: 4 across; portrait: 2x2 grid ---- */
    if (P) {
        int cw2 = (CW - 12) / 2, xa = 16, xb = 16 + cw2 + 12, r0 = 160, r1 = 226;
        s_nozzle_lbl = detail_cell(s_scr_status, xa, r0, cw2, tr(STR_NOZZLE), &pt_ic_nozzle);
        s_bed_lbl    = detail_cell(s_scr_status, xb, r0, cw2, tr(STR_BED),    &pt_ic_bed);
        s_speed_lbl  = detail_cell(s_scr_status, xa, r1, cw2, tr(STR_SPEED),  &pt_ic_speed);
        s_z_lbl      = detail_cell(s_scr_status, xb, r1, cw2, tr(STR_Z_AXIS), NULL);
    } else {
        s_nozzle_lbl = detail_cell(s_scr_status, 16,  160, 180, tr(STR_NOZZLE),  &pt_ic_nozzle);
        s_bed_lbl    = detail_cell(s_scr_status, 208, 160, 180, tr(STR_BED), &pt_ic_bed);
        s_speed_lbl  = detail_cell(s_scr_status, 400, 160, 180, tr(STR_SPEED),   &pt_ic_speed);
        s_z_lbl      = detail_cell(s_scr_status, 592, 160, 192, tr(STR_Z_AXIS),  NULL);
    }

    /* ---- job / progress card ---- */
    lv_obj_t *jobcard = lv_obj_create(s_scr_status);
    s_jobcard = jobcard;
    const int CARDY = P ? 300 : 228;   /* below the 2x2 telemetry in portrait */
    lv_obj_set_size(jobcard, P ? CW : 768, 88);
    lv_obj_align(jobcard, LV_ALIGN_TOP_LEFT, 16, CARDY);
    lv_obj_set_style_bg_color(jobcard, PP_SURFACE, 0);
    lv_obj_set_style_border_width(jobcard, 0, 0);
    lv_obj_set_style_radius(jobcard, 6, 0);
    lv_obj_set_style_pad_all(jobcard, 12, 0);
    lv_obj_clear_flag(jobcard, LV_OBJ_FLAG_SCROLLABLE);

    s_job_lbl = lv_label_create(jobcard);
    lv_label_set_long_mode(s_job_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_job_lbl, P ? CW - 100 : 560);
    lv_label_set_text(s_job_lbl, "");
    lv_obj_set_style_text_color(s_job_lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_job_lbl, PP_F16, 0);
    lv_obj_align(s_job_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pct_lbl = lv_label_create(jobcard);
    lv_label_set_text(s_pct_lbl, "");
    lv_obj_set_style_text_color(s_pct_lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_pct_lbl, PP_F20, 0);
    lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_bar = lv_bar_create(jobcard);
    lv_obj_set_size(s_bar, P ? CW - 48 : 600, 12);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, PP_SURFACE_HI, 0);
    lv_obj_set_style_bg_color(s_bar, PP_ORANGE, LV_PART_INDICATOR);

    s_eta_lbl = lv_label_create(jobcard);
    lv_label_set_text(s_eta_lbl, "");
    lv_obj_set_style_text_color(s_eta_lbl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_eta_lbl, PP_F16, 0);
    lv_obj_align(s_eta_lbl, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* ---- attention dialog banner (overlays the job card when the printer needs attention) ---- */
    s_attn_card = lv_obj_create(s_scr_status);
    lv_obj_set_size(s_attn_card, P ? CW : 768, 130);
    lv_obj_align(s_attn_card, LV_ALIGN_TOP_LEFT, 16, CARDY);
    lv_obj_set_style_bg_color(s_attn_card, PP_STATE_YELLOW, 0);
    lv_obj_set_style_bg_opa(s_attn_card, LV_OPA_20, 0);
    lv_obj_set_style_border_color(s_attn_card, PP_STATE_YELLOW, 0);
    lv_obj_set_style_border_width(s_attn_card, 1, 0);
    lv_obj_set_style_radius(s_attn_card, 6, 0);
    lv_obj_set_style_pad_all(s_attn_card, 10, 0);
    lv_obj_clear_flag(s_attn_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_attn_card, LV_OBJ_FLAG_HIDDEN);

    s_attn_title = lv_label_create(s_attn_card);
    lv_label_set_text(s_attn_title, "");
    lv_obj_set_style_text_color(s_attn_title, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_attn_title, PP_F16, 0);
    lv_obj_align(s_attn_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_attn_text = lv_label_create(s_attn_card);
    lv_label_set_long_mode(s_attn_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_attn_text, (P ? CW : 768) - 28);
    lv_label_set_text(s_attn_text, "");
    lv_obj_set_style_text_color(s_attn_text, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_attn_text, PP_F14, 0);
    lv_obj_align(s_attn_text, LV_ALIGN_TOP_LEFT, 0, 24);

    for (int i = 0; i < 3; i++) {
        s_attn_btns[i] = make_button(s_attn_card, "", on_attn_btn_clicked, (void *)(intptr_t)i, &s_attn_btn_lbls[i]);
        lv_obj_set_size(s_attn_btns[i], 150, 34);
        lv_obj_align(s_attn_btns[i], LV_ALIGN_BOTTOM_LEFT, i * 160, 0);
        lv_obj_add_flag(s_attn_btns[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* ---- action buttons ---- landscape: 4 in a row near the bottom;
     * portrait: 2x2 grid under the job/attention card ---- */
    lv_obj_t *pause_btn = make_button(s_scr_status, tr(STR_PAUSE), on_pause_clicked, NULL, &s_btn_pause_lbl);
    lv_obj_t *stop_btn  = make_button(s_scr_status, "Stop", on_stop_clicked, NULL, NULL);
    lv_obj_t *files_btn = make_button(s_scr_status, tr(STR_FILES), on_files_clicked, NULL, NULL);
    s_btn_control = make_button(s_scr_status, tr(STR_TOOLS), on_control_clicked, NULL, NULL);
    if (P) {
        /* Portrait: sit the 2x2 buttons directly under the job/attention card (which ends ~y430)
         * instead of pinning them to the bottom — bottom-pinning left a ~160px dead band mid-screen. */
        lv_obj_align(pause_btn,     LV_ALIGN_TOP_LEFT,  16, 452);
        lv_obj_align(stop_btn,      LV_ALIGN_TOP_RIGHT, -16, 452);
        lv_obj_align(files_btn,     LV_ALIGN_TOP_LEFT,  16, 524);
        lv_obj_align(s_btn_control, LV_ALIGN_TOP_RIGHT, -16, 524);
    } else {
        lv_obj_align(pause_btn,     LV_ALIGN_BOTTOM_LEFT, 16,  -16);
        lv_obj_align(stop_btn,      LV_ALIGN_BOTTOM_LEFT, 180, -16);
        lv_obj_align(files_btn,     LV_ALIGN_BOTTOM_LEFT, 344, -16);
        lv_obj_align(s_btn_control, LV_ALIGN_BOTTOM_LEFT, 508, -16);
    }
    lv_obj_add_flag(s_btn_control, LV_OBJ_FLAG_HIDDEN);
}
static void on_storage_toggle(lv_event_t *e)
{
    s_files_usb_mode = !s_files_usb_mode;
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (s_files_usb_mode) {
        lv_label_set_text_fmt(lbl, LV_SYMBOL_USB " %s", tr(STR_USB));
        lv_label_set_text(s_files_banner, tr(STR_LOCAL_FILES_USB));
        app_state_post_cmd(PP_CMD_LIST_USB, NULL);
    } else {
        lv_label_set_text_fmt(lbl, LV_SYMBOL_IMAGE " %s", tr(STR_PRINTER));
        lv_label_set_text(s_files_banner, tr(STR_FILES_ON_THIS));
        app_state_post_cmd(PP_CMD_LIST, NULL);
    }
}

static void build_files_screen(void)
{
    s_scr_files = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_files, PP_BG, 0);

    lv_obj_t *bar = make_header(s_scr_files, tr(STR_FILES));
    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_back_clicked, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_t *toggle = make_barbtn(bar, LV_SYMBOL_IMAGE " Printer", on_storage_toggle, NULL, 120);
    lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -116, 0);

    /* Printer-context banner — makes it explicit that files live on the active printer. */
    lv_obj_t *banner = lv_obj_create(s_scr_files);
    lv_obj_set_size(banner, LV_PCT(100), 34);
    lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_radius(banner, 0, 0);
    lv_obj_set_style_border_width(banner, 0, 0);
    lv_obj_set_style_bg_color(banner, PP_SURFACE, 0);
    lv_obj_set_style_pad_hor(banner, 16, 0);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
    s_files_banner = lv_label_create(banner);
    lv_label_set_text(s_files_banner, tr(STR_FILES_ON_THIS));
    lv_label_set_long_mode(s_files_banner, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_files_banner, 760);
    lv_obj_set_style_text_color(s_files_banner, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_files_banner, PP_F14, 0);
    lv_obj_align(s_files_banner, LV_ALIGN_LEFT_MID, 0, 0);

    /* Scrollable column of Connect-style file rows. */
    s_file_list = lv_obj_create(s_scr_files);
    lv_obj_set_size(s_file_list, LV_PCT(100), scr_h() - 56 - 34);   /* header + banner */
    lv_obj_align(s_file_list, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_color(s_file_list, PP_BG, 0);
    lv_obj_set_style_border_width(s_file_list, 0, 0);
    lv_obj_set_style_pad_all(s_file_list, 8, 0);
    lv_obj_set_style_pad_row(s_file_list, 8, 0);
    lv_obj_set_flex_flow(s_file_list, LV_FLEX_FLOW_COLUMN);
}

/* ---------- file detail (preview + print) ---------- */
static void on_fd_back(lv_event_t *e)
{
    (void)e;
    lv_screen_load(s_scr_files);
}

static void on_fd_print(lv_event_t *e)
{
    (void)e;
    if (ui_locked_block()) return;
    if (s_sel_path[0]) {
        if (s_files_usb_mode) {
            app_state_post_cmd(PP_CMD_UPLOAD, s_sel_path);
        } else {
            app_state_post_cmd(PP_CMD_PRINT, s_sel_path);
        }
    }
    lv_screen_load(s_scr_status);
}

static void build_filedetail_screen(void)
{
    s_scr_filedetail = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_filedetail, PP_BG, 0);
    lv_obj_clear_flag(s_scr_filedetail, LV_OBJ_FLAG_SCROLLABLE);

    /* Title-only header (no KLIPPER|TOUCH wordmark — it collided with long filenames). */
    lv_obj_t *bar = make_header_ex(s_scr_filedetail, NULL, false);
    s_fd_name = lv_label_create(bar);
    lv_label_set_text(s_fd_name, tr(STR_FILE));
    lv_label_set_long_mode(s_fd_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_fd_name, scr_w() - 16 - 108 - 8);   /* left pad + Back + gap */
    lv_obj_set_style_text_color(s_fd_name, PP_WHITE, 0);
    lv_obj_set_style_text_font(s_fd_name, PP_F20, 0);
    lv_obj_align(s_fd_name, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_fd_back, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    /* preview card holds thumbnail, loading bar, or a placeholder label */
    lv_obj_t *card = make_card(s_scr_filedetail, 360, 300);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 70);

    s_thumb_ph = lv_label_create(card);
    lv_label_set_text(s_thumb_ph, tr(STR_NO_PREVIEW));
    lv_obj_set_style_text_color(s_thumb_ph, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_thumb_ph, PP_F16, 0);
    lv_obj_align(s_thumb_ph, LV_ALIGN_CENTER, 0, -12);

    s_thumb_load = lv_bar_create(card);
    lv_obj_set_size(s_thumb_load, 180, 8);
    lv_obj_align(s_thumb_load, LV_ALIGN_CENTER, 0, 18);
    lv_bar_set_range(s_thumb_load, 0, 100);
    lv_bar_set_value(s_thumb_load, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_thumb_load, PP_SURFACE_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_thumb_load, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_thumb_load, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_thumb_load, PP_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_thumb_load, 4, LV_PART_INDICATOR);
    lv_obj_add_flag(s_thumb_load, LV_OBJ_FLAG_HIDDEN);

    s_thumb_img = lv_image_create(card);
    lv_obj_set_size(s_thumb_img, 340, 280);   /* fixed viewport; image centered + scaled to fit */
    lv_obj_center(s_thumb_img);
    lv_obj_add_flag(s_thumb_img, LV_OBJ_FLAG_HIDDEN);

    /* PRINT action */
    lv_obj_t *print_btn = make_button(s_scr_filedetail, "Print", on_fd_print, NULL, NULL);
    lv_obj_set_size(print_btn, 220, 64);
    lv_obj_set_style_bg_color(print_btn, PP_ORANGE, 0);        /* orange = primary CTA */
    lv_obj_set_style_bg_opa(print_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(print_btn, 0, 0);
    lv_obj_set_style_bg_color(print_btn, PP_ORANGE_DARK, LV_STATE_PRESSED);
    lv_obj_align(print_btn, LV_ALIGN_BOTTOM_MID, 0, -24);
}

/* ---------- printer picker + add form ---------- */
static void on_printers_clicked(lv_event_t *e)
{
    refresh_printers_list();
    lv_screen_load(s_scr_printers);
}

static void addpick_refresh(void);       /* fwd: refresh the escort QR/URL from the current IP */
static void on_add_open(lv_event_t *e)   /* "+ Add printer" -> the web-UI escort screen */
{
    (void)e;
    addpick_refresh();                   /* IP may have changed since boot — repoint the QR */
    lv_screen_load(s_scr_addpick);
}

static void on_pick_cancel(lv_event_t *e) { (void)e; lv_screen_load(s_scr_printers); }

/* Scheduled by the net task after a printer-store write so the Settings list reflects it. */
void ui_apply_printers(void *unused)
{
    (void)unused;
    if (s_pr_list) refresh_printers_list();
}


/* Force a text font on an object and ALL its descendants. lv_list buttons nest their label a level
 * down and the LVGL theme pins it to the ASCII-only default font, so a top-level/per-item set won't
 * reach it — recurse to the actual labels (non-text widgets just ignore text_font). */
static void set_text_font_deep(lv_obj_t *o, const lv_font_t *f)
{
    lv_obj_set_style_text_font(o, f, 0);
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) set_text_font_deep(lv_obj_get_child(o, i), f);
}

/* ---------- on-device firmware update check ---------- */
static lv_obj_t *s_upd_mbox;   /* transient "checking…" / "updating…" modal (LVGL thread only) */

/* lv_msgbox parts (incl. footer-button labels nested a level down) take the LVGL default ASCII-only
 * font; deep-set the language font so accented dialog text renders. Returns o for chaining. */
static lv_obj_t *uf(lv_obj_t *o) { if (o) set_text_font_deep(o, PP_F16); return o; }

static void upd_close_cb(lv_event_t *e) { lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e)); }

static void upd_apply_cb(lv_event_t *e)   /* "Update now" -> flash the checked release */
{
    lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
    s_upd_mbox = lv_msgbox_create(NULL);
    uf(lv_msgbox_add_title(s_upd_mbox, tr(STR_UPDATE_NOW)));
    uf(lv_msgbox_add_text(s_upd_mbox, tr(STR_UPDATING)));   /* no buttons: applies then reboots */
    app_state_apply_update();
}

/* Result of app_state_check_update(), scheduled on the LVGL thread. Owns arg. */
void ui_apply_update_check(void *arg)
{
    pp_upd_check_t *u = (pp_upd_check_t *)arg;
    if (s_upd_mbox) { lv_msgbox_close(s_upd_mbox); s_upd_mbox = NULL; }   /* drop the "checking…" modal */
    lv_obj_t *m = lv_msgbox_create(NULL);
    char buf[160];
    if (!u || !u->ok) {
        uf(lv_msgbox_add_title(m, tr(STR_UPDATE_FAILED)));
        uf(lv_msgbox_add_text(m, tr(STR_UPDFAIL_MSG)));
        lv_obj_add_event_cb(uf(lv_msgbox_add_footer_button(m, tr(STR_OK))), upd_close_cb, LV_EVENT_CLICKED, m);
    } else if (u->available) {
        uf(lv_msgbox_add_title(m, tr(STR_UPDATE_AVAILABLE)));
        snprintf(buf, sizeof(buf), tr(STR_NEWVER_FMT), u->latest, u->current);
        uf(lv_msgbox_add_text(m, buf));
        lv_obj_t *go = uf(lv_msgbox_add_footer_button(m, tr(STR_UPDATE_NOW)));
        lv_obj_set_style_bg_color(go, PP_ORANGE, 0);
        lv_obj_add_event_cb(go, upd_apply_cb, LV_EVENT_CLICKED, m);
        lv_obj_add_event_cb(uf(lv_msgbox_add_footer_button(m, tr(STR_LATER))), upd_close_cb, LV_EVENT_CLICKED, m);
    } else {
        uf(lv_msgbox_add_title(m, tr(STR_UP_TO_DATE)));
        snprintf(buf, sizeof(buf), tr(STR_UPTODATE_FMT), u->current);
        uf(lv_msgbox_add_text(m, buf));
        lv_obj_add_event_cb(uf(lv_msgbox_add_footer_button(m, tr(STR_OK))), upd_close_cb, LV_EVENT_CLICKED, m);
    }
    free(u);
}

/* OTA apply failed (success reboots). Owns arg (a strdup'd message). */
void ui_apply_update_fail(void *arg)
{
    char *msg = (char *)arg;
    if (s_upd_mbox) { lv_msgbox_close(s_upd_mbox); s_upd_mbox = NULL; }
    lv_obj_t *m = lv_msgbox_create(NULL);
    uf(lv_msgbox_add_title(m, tr(STR_UPDATE_FAILED)));
    uf(lv_msgbox_add_text(m, (msg && msg[0]) ? msg : tr(STR_UPDFAIL_MSG)));
    lv_obj_add_event_cb(uf(lv_msgbox_add_footer_button(m, tr(STR_OK))), upd_close_cb, LV_EVENT_CLICKED, m);
    free(msg);
}

static void on_check_update_clicked(lv_event_t *e)
{
    (void)e;
    if (s_upd_mbox) return;   /* a check/apply is already in flight */
    s_upd_mbox = lv_msgbox_create(NULL);
    uf(lv_msgbox_add_title(s_upd_mbox, tr(STR_CHECK_UPDATES)));
    uf(lv_msgbox_add_text(s_upd_mbox, tr(STR_CHECKING)));
    app_state_check_update();
}

static void refresh_printers_list(void)
{
    lv_obj_clean(s_pr_list);
    int n = printer_store_count();
    /* --- Device settings (top) --- */
    lv_obj_t *hd = lv_list_add_text(s_pr_list, tr(STR_DEVICE));
    lv_obj_set_style_text_color(hd, PP_TEXT_MUTED, 0);

    lv_obj_t *pf = lv_list_add_button(s_pr_list, LV_SYMBOL_SETTINGS, tr(STR_PREFERENCES));
    lv_obj_set_style_bg_color(pf, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(pf, PP_TEXT, 0);
    lv_obj_add_event_cb(pf, on_prefs_open, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wf = lv_list_add_button(s_pr_list, LV_SYMBOL_WIFI, tr(STR_WIFI_SETUP));
    lv_obj_set_style_bg_color(wf, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(wf, PP_TEXT, 0);
    lv_obj_add_event_cb(wf, on_wifi_open, LV_EVENT_CLICKED, NULL);

    /* Installed firmware version + manual update check (the auto-updater is silent). */
    char fwline[64];
    snprintf(fwline, sizeof(fwline), tr(STR_FW_FMT), PP_FW_VERSION);
    lv_obj_t *fwi = lv_list_add_text(s_pr_list, fwline);
    lv_obj_set_style_text_color(fwi, PP_TEXT_MUTED, 0);
    lv_obj_t *cu = lv_list_add_button(s_pr_list, LV_SYMBOL_REFRESH, tr(STR_CHECK_UPDATES));
    lv_obj_set_style_bg_color(cu, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(cu, PP_TEXT, 0);
    lv_obj_add_event_cb(cu, on_check_update_clicked, LV_EVENT_CLICKED, NULL);

    /* --- Printers: add/manage from the web, not the touchscreen (issue #5). Entering API keys,
     * the Klipper host:port, or the Prusa Connect sign-in is impractical on a touch keyboard, so we
     * point users at the device's web page (QR + IP) which handles every printer type properly. --- */
    lv_obj_t *hp = lv_list_add_text(s_pr_list, tr(STR_PRINTERS_HDR));
    lv_obj_set_style_text_color(hp, PP_TEXT_MUTED, 0);

    lv_obj_t *mg = lv_list_add_button(s_pr_list, LV_SYMBOL_PLUS, tr(STR_ADD_MANAGE_PRINTERS));
    lv_obj_set_style_bg_color(mg, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(mg, PP_ORANGE, 0);
    lv_obj_add_event_cb(mg, on_add_open, LV_EVENT_CLICKED, NULL);

    const char *ip = wifi_ip_str();
    char info[88];
    snprintf(info, sizeof(info), tr(STR_CONFIGURED_FMT),
             n, (ip && ip[0]) ? ip : "192.168.4.1");
    lv_obj_t *ipi = lv_list_add_text(s_pr_list, info);
    lv_obj_set_style_text_color(ipi, PP_TEXT_MUTED, 0);

    /* --- More --- */
    lv_obj_t *hm = lv_list_add_text(s_pr_list, tr(STR_MORE));
    lv_obj_set_style_text_color(hm, PP_TEXT_MUTED, 0);
    lv_obj_t *fm = lv_list_add_button(s_pr_list, LV_SYMBOL_LIST, "Prusa Farm");
    lv_obj_set_style_bg_color(fm, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(fm, PP_TEXT, 0);
    lv_obj_add_event_cb(fm, on_farm_open, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ab = lv_list_add_button(s_pr_list, LV_SYMBOL_LIST, tr(STR_ABOUT));
    lv_obj_set_style_bg_color(ab, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(ab, PP_TEXT, 0);
    lv_obj_add_event_cb(ab, on_about_open, LV_EVENT_CLICKED, NULL);

    /* The list labels nest inside the item buttons and the theme pins them to the ASCII-only default
     * font; recurse so accented text renders instead of boxes. */
    set_text_font_deep(s_pr_list, PP_F16);
}

/* Black top bar carrying the persistent [ KLIPPER | TOUCH ] wordmark (left) plus an
 * optional page title — used on every screen for a consistent header.
 * Pass brand=false for title-only bars (file detail) where the wordmark collides. */
static lv_obj_t *make_header_ex(lv_obj_t *parent, const char *text, bool brand)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 56);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, PP_HEADER, 0);     /* Connect: black top bar */
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    if (brand) make_wordmark(bar);

    if (text && text[0]) {
        lv_obj_t *t = lv_label_create(bar);
        lv_label_set_text(t, text);
        lv_obj_set_style_text_color(t, PP_TEXT, 0);
        lv_obj_set_style_text_font(t, PP_F20, 0);
        /* A centered title collides with the left wordmark at 480px wide — in portrait, left-align
         * it clear of the wordmark; in landscape there's room to center it. */
        if (!brand)                lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);
        else if (ui_portrait())    lv_obj_align(t, LV_ALIGN_LEFT_MID, 184, 0);
        else                       lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
    }
    return bar;
}

static lv_obj_t *make_header(lv_obj_t *parent, const char *text)
{
    return make_header_ex(parent, text, true);
}

/* A borderless white icon/text button for placement on the black top bar
 * (Connect's top-bar controls have no button background). */
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

static void build_printers_screen(void)
{
    s_scr_printers = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_printers, PP_BG, 0);
    lv_obj_t *bar = make_header(s_scr_printers, tr(STR_SETTINGS));
    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", nav_dash, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    s_pr_list = lv_list_create(s_scr_printers);
    lv_obj_set_size(s_pr_list, LV_PCT(100), scr_h() - 56);   /* header only — no bottom nav */
    lv_obj_align(s_pr_list, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_pr_list, PP_BG, 0);
    lv_obj_set_style_text_font(s_pr_list, PP_F16, 0);   /* list items inherit: the LVGL default is ASCII-only Montserrat */
    lv_obj_set_style_border_width(s_pr_list, 0, 0);
}

/* "Add a printer" — escort to the web UI (issue #5). Adding or configuring a printer needs a long
 * PrusaLink API key, a Klipper host:port, or the Prusa Connect browser sign-in — none of which a
 * touchscreen keyboard handles well, so a printer "added" on-device often landed in the fleet but
 * never authenticated. The on-device add/edit form is gone; instead we hand the user a QR + URL to
 * the device's own web page, which handles every printer type properly. */
static lv_obj_t *s_addpick_qr;
static lv_obj_t *s_addpick_url;

static void addpick_refresh(void)   /* point the QR + URL at the device's current web address */
{
    const char *ip = wifi_ip_str();
    char url[40];
    snprintf(url, sizeof(url), "http://%s/", (ip && ip[0]) ? ip : "192.168.4.1");
    if (s_addpick_qr)  lv_qrcode_update(s_addpick_qr, url, strlen(url));
    if (s_addpick_url) lv_label_set_text(s_addpick_url, url);
}

static void build_addpick_screen(void)
{
    s_scr_addpick = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_addpick, PP_BG, 0);
    lv_obj_clear_flag(s_scr_addpick, LV_OBJ_FLAG_SCROLLABLE);
    make_header(s_scr_addpick, "Add a printer");

    const bool P = ui_portrait();
    lv_obj_t *col = lv_obj_create(s_scr_addpick);
    lv_obj_set_size(col, scr_w(), scr_h() - 52);
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 16, 0);
    lv_obj_set_style_pad_row(col, P ? 14 : 8, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_label_create(col);
    lv_label_set_text(head, tr(STR_ADD_MANAGE));
    lv_obj_set_style_text_color(head, PP_TEXT, 0);
    lv_obj_set_style_text_font(head, PP_F16, 0);
    lv_label_set_long_mode(head, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(head, scr_w() - 48);
    lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_CENTER, 0);

    s_addpick_qr = lv_qrcode_create(col);
    lv_qrcode_set_size(s_addpick_qr, P ? 220 : 150);
    lv_qrcode_set_dark_color(s_addpick_qr, PP_BLACK);
    lv_qrcode_set_light_color(s_addpick_qr, PP_WHITE);
    lv_obj_set_style_border_color(s_addpick_qr, PP_WHITE, 0);
    lv_obj_set_style_border_width(s_addpick_qr, 6, 0);   /* QR quiet-zone */

    s_addpick_url = lv_label_create(col);
    /* The URL is the primary CTA — use high-contrast text (not the accent, which is low-contrast
     * on some skins' backgrounds); the size already makes it prominent. */
    lv_obj_set_style_text_color(s_addpick_url, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_addpick_url, PP_F20, 0);

    lv_obj_t *why = lv_label_create(col);
    lv_label_set_text(why, tr(STR_SCAN_ADDRESS));
    lv_obj_set_style_text_color(why, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(why, PP_F14, 0);
    lv_label_set_long_mode(why, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(why, scr_w() - 48);
    lv_obj_set_style_text_align(why, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *back = make_button(col, LV_SYMBOL_LEFT " Back", on_pick_cancel, NULL, NULL);
    lv_obj_set_width(back, P ? scr_w() - 140 : 220);
    lv_obj_set_style_margin_top(back, 4, 0);

    addpick_refresh();   /* QR + URL from the current IP (build runs before Wi-Fi is up) */
}

/* ---------- WiFi setup ---------- */
static void on_wifi_scan_clicked(lv_event_t *e)
{
    lv_obj_clean(s_wifi_list);
    lv_list_add_text(s_wifi_list, "Scanning...");
    app_state_wifi_scan();
}

static void on_wifi_ssid_pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_wifi_scan_count) {
        strlcpy(s_wifi_selected, s_wifi_ssids[idx], sizeof(s_wifi_selected));
        lv_label_set_text_fmt(s_wifi_sel_lbl, tr(STR_NETWORK_FMT), s_wifi_selected);
    }
}

static void on_wifi_pass_focus(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_wifi_kb, lv_event_get_target(e));
        lv_obj_remove_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_wifi_connect_clicked(lv_event_t *e)
{
    if (s_wifi_selected[0]) {
        app_state_wifi_connect(s_wifi_selected, lv_textarea_get_text(s_wifi_ta_pass));
    }
    lv_screen_load(s_scr_status);
}

/* Update the Wi-Fi status line. Three states, in priority order: connected (show
 * the device IP + web-UI URL so the user can reach it from a computer), hotspot up,
 * or the setup tip. Cheap + idempotent — also called from ui_apply_status each poll
 * so the IP appears within a cycle of connecting while the screen is open. */
static void wifi_status_label_refresh(void)
{
    if (!s_wifi_ap_lbl) return;
    if (wifi_is_connected() && wifi_ip_str()[0]) {
        lv_obj_set_style_text_color(s_wifi_ap_lbl, PP_OK, 0);
        lv_label_set_text_fmt(s_wifi_ap_lbl,
            LV_SYMBOL_OK " Connected. From a computer on the same network, open "
            "http://%s/ to manage printers and update firmware.", wifi_ip_str());
    } else if (wifi_is_ap_active()) {
        lv_obj_set_style_text_color(s_wifi_ap_lbl, PP_TEXT_MUTED, 0);
        lv_label_set_text_fmt(s_wifi_ap_lbl,
            LV_SYMBOL_WARNING " No network. Hotspot \"%s\" is open - join it from a "
            "phone and open http://192.168.4.1 to set up Wi-Fi.", wifi_ap_ssid());
    } else {
        lv_obj_set_style_text_color(s_wifi_ap_lbl, PP_TEXT_MUTED, 0);
        lv_label_set_text(s_wifi_ap_lbl,
            "Tip: if no known network is found, the device opens a \"KlipperTouch-...\" "
            "hotspot at http://192.168.4.1 for setup.");
    }
}

/* Reset the Wi-Fi screen widgets + hotspot hint and kick off a scan. Shared by the
 * menu entry and the test-nav API so the screen is always correctly populated. */
static void wifi_screen_prepare(void)
{
    s_wifi_selected[0] = '\0';
    lv_label_set_text(s_wifi_sel_lbl, tr(STR_NETWORK_SCAN));
    lv_textarea_set_text(s_wifi_ta_pass, "");
    lv_obj_clean(s_wifi_list);

    wifi_status_label_refresh();   /* connected IP / hotspot / setup tip */
    app_state_wifi_scan();         /* auto-scan */
    lv_list_add_text(s_wifi_list, "Scanning...");
}

static void on_wifi_open(lv_event_t *e)
{
    wifi_screen_prepare();
    lv_screen_load(s_scr_wifi);
}

void ui_apply_wifi_list(void *arg)
{
    pp_wifi_list_t *wl = (pp_wifi_list_t *)arg;
    lv_obj_clean(s_wifi_list);
    s_wifi_scan_count = wl->count;
    for (int i = 0; i < wl->count && i < PP_WIFI_MAX_SCAN; i++) {
        strlcpy(s_wifi_ssids[i], wl->ssids[i], sizeof(s_wifi_ssids[i]));
        lv_obj_t *b = lv_list_add_button(s_wifi_list, LV_SYMBOL_WIFI, wl->ssids[i]);
        lv_obj_set_style_bg_color(b, PP_SURFACE, 0);
        lv_obj_set_style_text_color(b, PP_TEXT, 0);
        lv_obj_add_event_cb(b, on_wifi_ssid_pick, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    if (wl->count == 0) {
        lv_list_add_text(s_wifi_list, "No networks found — tap Scan");
    }
    free(wl);
}

static void build_wifi_screen(void)
{
    s_scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_wifi, PP_BG, 0);
    lv_obj_t *bar = make_header(s_scr_wifi, "Wi-Fi");

    lv_obj_t *scan = make_barbtn(bar, LV_SYMBOL_REFRESH " Scan", on_wifi_scan_clicked, NULL, 120);
    lv_obj_align(scan, LV_ALIGN_RIGHT_MID, -8, 0);

    /* Landscape: scan list (left) + selection form (right column at x=396). Portrait stacks
     * them: the compact form at top, the scan list filling the width below it. */
    const bool P = ui_portrait();
    const int  fx = P ? 16 : 396;          /* form column x */
    const int  fw = P ? scr_w() - 32 : 380;/* form field width */

    s_wifi_sel_lbl = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_wifi_sel_lbl, tr(STR_NETWORK_SCAN));
    lv_obj_set_style_text_color(s_wifi_sel_lbl, PP_TEXT, 0);
    lv_obj_align(s_wifi_sel_lbl, LV_ALIGN_TOP_LEFT, fx, 72);

    s_wifi_ta_pass = lv_textarea_create(s_scr_wifi);
    lv_textarea_set_one_line(s_wifi_ta_pass, true);
    lv_textarea_set_password_mode(s_wifi_ta_pass, true);
    lv_textarea_set_placeholder_text(s_wifi_ta_pass, "password");
    lv_obj_set_width(s_wifi_ta_pass, fw);
    lv_obj_align(s_wifi_ta_pass, LV_ALIGN_TOP_LEFT, fx, 104);
    lv_obj_add_event_cb(s_wifi_ta_pass, on_wifi_pass_focus, LV_EVENT_ALL, NULL);

    lv_obj_t *conn = lv_button_create(s_scr_wifi);
    lv_obj_set_size(conn, 160, 56);
    lv_obj_align(conn, LV_ALIGN_TOP_LEFT, fx, 156);
    lv_obj_set_style_bg_color(conn, PP_ORANGE, 0);
    lv_obj_add_event_cb(conn, on_wifi_connect_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(conn);
    lv_label_set_text(cl, tr(STR_CONNECT));
    lv_obj_set_style_text_color(cl, PP_WHITE, 0);
    lv_obj_center(cl);

    s_wifi_ap_lbl = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_wifi_ap_lbl, "");
    lv_label_set_long_mode(s_wifi_ap_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi_ap_lbl, fw);
    lv_obj_set_style_text_color(s_wifi_ap_lbl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_wifi_ap_lbl, PP_F14, 0);
    lv_obj_align(s_wifi_ap_lbl, LV_ALIGN_TOP_LEFT, fx, 232);

    /* Scan list: left half in landscape, full width below the form in portrait. */
    s_wifi_list = lv_list_create(s_scr_wifi);
    lv_obj_set_style_text_font(s_wifi_list, PP_F16, 0);   /* items inherit non-ASCII-capable font */
    if (P) {
        lv_obj_set_size(s_wifi_list, scr_w(), scr_h() - 300);
        lv_obj_align(s_wifi_list, LV_ALIGN_TOP_LEFT, 0, 290);
    } else {
        lv_obj_set_size(s_wifi_list, 380, 480 - 56);
        lv_obj_align(s_wifi_list, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    /* A dark surface (not the screen bg) so the list reads as a defined, framed region that fills
     * the available space — otherwise the empty/"Scanning..." state looks like a dead black band. */
    lv_obj_set_style_bg_color(s_wifi_list, PP_SURFACE, 0);
    lv_obj_set_style_radius(s_wifi_list, 6, 0);
    lv_obj_set_style_border_width(s_wifi_list, 0, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 6, 0);

    s_wifi_kb = lv_keyboard_create(s_scr_wifi);
    lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- fleet home (wordmark tap) + settings ---------- */
static void nav_dash(lv_event_t *e)     { (void)e; lv_screen_load(s_scr_dash); }
static void nav_settings(lv_event_t *e)
{
    (void)e;
    refresh_printers_list();
    lv_screen_load(s_scr_printers);
}

/* ---------- fleet dashboard ---------- */
static void on_card_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_dash_count) {   /* index may be stale after a remove */
        app_state_select_printer(idx);
        /* Render the clicked printer's cached status immediately so the detail screen
         * doesn't show the previously-selected printer for the seconds it takes the next
         * cloud poll to land. ui_apply_status runs on this (LVGL) thread and frees copy. */
        if (s_dash_items) {
            pp_status_t *copy = malloc(sizeof(*copy));
            if (copy) { *copy = s_dash_items[idx]; ui_apply_status(copy); }
        }
    }
    lv_screen_load(s_scr_status);   /* open this printer's detail */
}

/* Printer-model renders (rasterized from Prusa Connect's SVG icons → LVGL images). */
extern const lv_image_dsc_t pt_core_one;
extern const lv_image_dsc_t pt_core_one_l;
extern const lv_image_dsc_t pt_mini;
extern const lv_image_dsc_t pt_mk4s;
extern const lv_image_dsc_t pt_xl;
extern const lv_image_dsc_t pt_fluidd;   /* Klipper / Moonraker printers */

/* Pick the model image for a friendly model string (NULL → show placeholder). */
static const lv_image_dsc_t *model_image(const char *model)
{
    if (!model || !model[0]) return NULL;
    if (strstr(model, "CORE One L")) return &pt_core_one_l;   /* before "CORE One" */
    if (strstr(model, "CORE One"))   return &pt_core_one;
    if (strstr(model, "MINI"))       return &pt_mini;
    if (strstr(model, "MK4S"))       return &pt_mk4s;
    if (strstr(model, "XL"))         return &pt_xl;
    if (strstr(model, "Klipper"))    return &pt_fluidd;       /* Moonraker backend */
    return NULL;
}

/* One Connect-style telemetry cell: muted uppercase label over a bold white value. */
static lv_obj_t *card_cell(lv_obj_t *parent, int x, int y, const char *label, const char *value)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(l, PP_F12, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_color(v, PP_TEXT, 0);
    lv_obj_set_style_text_font(v, PP_F16, 0);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, x, y + 17);
    return v;   /* the value label, for in-place dashboard updates */
}

/* Format the 4 telemetry values exactly as the card shows them (shared by build + in-place update). */
static void fmt_telemetry(const pp_status_t *s, char *nz, char *hb, char *sp, char *zx)
{
    if (s->online) {
        if ((int)s->target_nozzle >= 1) sprintf(nz, "%d/%d\xC2\xB0""C", (int)s->temp_nozzle, (int)s->target_nozzle);
        else sprintf(nz, "%d\xC2\xB0""C", (int)s->temp_nozzle);
        if ((int)s->target_bed >= 1) sprintf(hb, "%d/%d\xC2\xB0""C", (int)s->temp_bed, (int)s->target_bed);
        else sprintf(hb, "%d\xC2\xB0""C", (int)s->temp_bed);
        sprintf(sp, "%d%%", s->speed);
        sprintf(zx, "%.2fmm", s->axis_z);
    } else { strcpy(nz, "--"); strcpy(hb, "--"); strcpy(sp, "--"); strcpy(zx, "--"); }
}

/* Per-card widget handles captured at build time so a poll that changes only values can update
 * them in place (gist #11) instead of destroying + rebuilding every card (flicker + CPU). */
typedef struct {
    lv_obj_t *strip, *badge, *badge_lbl, *name_lbl, *model_lbl;
    lv_obj_t *v_noz, *v_speed, *v_bed, *v_z, *prog_bar, *prog_lbl;
} dash_refs_t;
static dash_refs_t s_dref[PP_MAX_PRINTERS];
static int      s_dref_n;        /* number of cards currently laid out */
static uint32_t s_dash_sig;      /* structural signature of the current layout */
static bool     s_dash_have;     /* a valid prior layout exists */

/* ---- Snapshot-cached cards ----
 * Scrolling the fleet was render-bound (~10 FPS): every frame software-rendered each card's
 * rounded-corner masks, ~10 labels of text, and (PNG-decoded!) thumbnail. Instead, each card's
 * live widget tree now lives on a hidden host screen and is rendered ONCE per data change into
 * a PSRAM bitmap; the visible grid holds plain lv_image widgets showing those bitmaps, so a
 * scroll frame is just a few opaque blits. Cards are pixel-identical (corners bake against the
 * grid background). If a bitmap can't be allocated, that slot falls back to a live card in the
 * grid, exactly the old behavior. */
#define DASH_CARD_W 380
#define DASH_CARD_H 170
static lv_obj_t      *s_card_host;                    /* hidden screen hosting the live cards  */
static lv_obj_t      *s_card_wrap[PP_MAX_PRINTERS];   /* per-slot wrapper on the host          */
static lv_obj_t      *s_card_img [PP_MAX_PRINTERS];   /* per-slot image widget in the grid     */
static lv_draw_buf_t *s_card_snap[PP_MAX_PRINTERS];   /* per-slot RGB565 bitmap (PSRAM, reused) */

/* Re-render slot's live card into its bitmap and refresh the grid image showing it. */
static void dash_snapshot_slot(int slot)
{
    if (slot < 0 || slot >= PP_MAX_PRINTERS) return;
    if (!s_card_wrap[slot] || !s_card_snap[slot] || !s_card_img[slot]) return;   /* live-card fallback slot */
    lv_obj_update_layout(s_card_wrap[slot]);
    if (lv_snapshot_take_to_draw_buf(s_card_wrap[slot], LV_COLOR_FORMAT_RGB565, s_card_snap[slot]) == LV_RESULT_OK) {
        lv_image_cache_drop(s_card_snap[slot]);       /* content changed under the same pointer */
        lv_image_set_src(s_card_img[slot], s_card_snap[slot]);
        lv_obj_invalidate(s_card_img[slot]);
    }
}

/* A publish landing mid-gesture would re-snapshot cards and invalidate images, stealing frames
 * from the scroll — park it and apply the newest one when the scroll (incl. momentum) settles. */
static bool       s_dash_scrolling;
static pp_dash_t *s_dash_pending;    /* newest publish deferred during a scroll (we own/free it) */

static void on_dash_scroll(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL_BEGIN) {
        s_dash_scrolling = true;
    } else if (code == LV_EVENT_SCROLL_END && s_dash_scrolling) {
        s_dash_scrolling = false;
        if (s_dash_pending) {           /* apply the publish that arrived mid-scroll */
            pp_dash_t *d = s_dash_pending;
            s_dash_pending = NULL;
            ui_apply_dashboard(d);      /* takes ownership and frees it */
        }
    }
}

/* Set a label only if the text actually differs; reports whether anything changed so the
 * caller can skip the (whole-card) re-snapshot when a publish was a visual no-op. */
static bool lbl_set_if_changed(lv_obj_t *lbl, const char *txt)
{
    if (!lbl || strcmp(lv_label_get_text(lbl), txt) == 0) return false;
    lv_label_set_text(lbl, txt);
    return true;
}

static bool update_dash_card(const dash_refs_t *r, const pp_status_t *s)
{
    bool ch = false;
    bool online = s->online;
    const char *st = online ? (s->state[0] ? s->state : "READY") : "OFFLINE";
    const char *disp = tr_state(st);   /* localized label; tint still keys off raw s->state */
    /* The state text uniquely determines the strip/badge tints ("OFFLINE" covers the online
     * flag), so colors only need refreshing when the badge text changes. */
    if (r->badge_lbl && strcmp(lv_label_get_text(r->badge_lbl), disp) != 0) {
        lv_label_set_text(r->badge_lbl, disp);
        if (r->strip) lv_obj_set_style_bg_color(r->strip, online ? pp_state_strip(s->state) : PP_STRIP_GRAY, 0);
        if (r->badge) lv_obj_set_style_bg_color(r->badge, online ? pp_state_badge(s->state) : PP_BADGE_GRAY, 0);
        ch = true;
    }
    ch |= lbl_set_if_changed(r->name_lbl, s->printer_name[0] ? s->printer_name : "Printer");
    ch |= lbl_set_if_changed(r->model_lbl, s->model[0] ? s->model : (online ? "Printer" : ""));
    char nz[24], hb[24], sp[16], zx[16];
    fmt_telemetry(s, nz, hb, sp, zx);
    ch |= lbl_set_if_changed(r->v_noz, nz);
    ch |= lbl_set_if_changed(r->v_speed, sp);
    ch |= lbl_set_if_changed(r->v_bed, hb);
    ch |= lbl_set_if_changed(r->v_z, zx);
    if (s->has_job && r->prog_bar) {
        int pct = (int)(s->progress + 0.5f);
        if (lv_bar_get_value(r->prog_bar) != pct) {
            lv_bar_set_value(r->prog_bar, pct, LV_ANIM_OFF);
            ch = true;
        }
        if (r->prog_lbl) {
            char pb[8];
            snprintf(pb, sizeof(pb), "%d%%", pct);
            ch |= lbl_set_if_changed(r->prog_lbl, pb);
        }
    }
    return ch;
}

/* Structural fingerprint: which printers, in what order, online/job/firmware/thumbnail state.
 * Excludes the churning values (temps/progress/...) so those go through the in-place path. */
static uint32_t dash_sig(const pp_dash_t *d, const int *order, int n, bool hide_off)
{
    uint32_t h = 2166136261u;
#define MIX(x) do { h ^= (uint32_t)(x); h *= 16777619u; } while (0)
    MIX(d->conn_expired ? 1 : 0); MIX(hide_off ? 1 : 0);
    int shown = 0;
    for (int k = 0; k < n; k++) {
        int idx = order[k];
        if (hide_off && !d->items[idx].online) continue;
        const pp_status_t *s = &d->items[idx];
        MIX(idx); MIX(s->online ? 1 : 0); MIX(s->has_job ? 1 : 0); MIX(s->firmware[0] ? 1 : 0);
        MIX(s_card_thumbs[idx].buf ? 1 : 0);          /* thumb arrival forces a rebuild to show it */
        for (const char *p = s->job_thumb; *p; p++) MIX(*p);
        shown++;
    }
    MIX(shown);
    return h;
#undef MIX
}

/* Prusa Connect dark-card anatomy: state-tinted header strip (name + badge),
 * then a 3-column labeled telemetry grid; progress bar slot when printing. */
static void make_printer_card(lv_obj_t *parent, const pp_status_t *s, int idx, dash_refs_t *r)
{
    if (r) memset(r, 0, sizeof(*r));
    const bool online = s->online;
    const char *st = online ? (s->state[0] ? s->state : "READY") : "OFFLINE";

    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 380, 170);
    lv_obj_set_style_bg_color(c, PP_SURFACE, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 6, 0);
    lv_obj_set_style_clip_corner(c, true, 0);    /* round the header strip too */
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, on_card_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    ui_kb_focus_add(c);
    if (!online) lv_obj_set_style_opa(c, LV_OPA_70, 0);   /* dim offline cards */

    /* ---- header strip: name (white) + flush state badge (muted tint, white text) ---- */
    lv_obj_t *head = lv_obj_create(c);
    lv_obj_set_size(head, 380, 34);
    lv_obj_align(head, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_style_bg_color(head, online ? pp_state_strip(s->state) : PP_STRIP_GRAY, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    if (r) r->strip = head;

    lv_obj_t *badge = lv_obj_create(head);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_pad_hor(badge, 12, 0);
    lv_obj_set_style_pad_ver(badge, 0, 0);
    lv_obj_set_style_radius(badge, 0, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_bg_color(badge, online ? pp_state_badge(s->state) : PP_BADGE_GRAY, 0);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    if (r) r->badge = badge;
    lv_obj_t *bl = lv_label_create(badge);
    lv_label_set_text(bl, tr_state(st));
    lv_obj_set_style_text_color(bl, PP_TEXT, 0);
    lv_obj_set_style_text_font(bl, PP_F16, 0);
    lv_obj_center(bl);
    if (r) r->badge_lbl = bl;

    lv_obj_t *nm = lv_label_create(head);
    lv_label_set_text(nm, s->printer_name[0] ? s->printer_name : "Printer");
    lv_obj_set_style_text_color(nm, PP_TEXT, 0);
    lv_obj_set_style_text_font(nm, PP_F20, 0);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nm, 226);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 12, 0);
    if (r) r->name_lbl = nm;

    /* ---- identity row: thumbnail slot + model + firmware ---- */
    lv_obj_t *thumb = lv_obj_create(c);
    lv_obj_set_size(thumb, 48, 48);
    lv_obj_align(thumb, LV_ALIGN_TOP_LEFT, 12, 36);
    lv_obj_set_style_bg_color(thumb, PP_SURFACE_HI, 0);
    lv_obj_set_style_border_width(thumb, 0, 0);
    lv_obj_set_style_radius(thumb, 4, 0);
    lv_obj_set_style_pad_all(thumb, 0, 0);
    lv_obj_clear_flag(thumb, LV_OBJ_FLAG_SCROLLABLE);

    const lv_image_dsc_t *img = model_image(s->model);
    bool has_job_thumb = (s->has_job && s->job_thumb[0]);

    if (has_job_thumb) {
        if (strcmp(s->job_thumb, s_card_thumbs[idx].url) != 0) {
            /* New URL! Clear old one for this slot and fetch. */
            if (s_card_thumbs[idx].buf) {
                lv_image_cache_drop(&s_card_thumbs[idx].dsc);
                free(s_card_thumbs[idx].buf);
                s_card_thumbs[idx].buf = NULL;
            }
            strlcpy(s_card_thumbs[idx].url, s->job_thumb, sizeof(s_card_thumbs[idx].url));
            lv_memzero(&s_card_thumbs[idx].dsc, sizeof(lv_image_dsc_t));
            app_state_fetch_thumb_dash(s->job_thumb, idx);
        }

        if (s_card_thumbs[idx].buf) {
            lv_obj_t *jt = lv_image_create(thumb);
            lv_image_set_src(jt, &s_card_thumbs[idx].dsc);
            lv_image_header_t hdr;
            if (lv_image_decoder_get_info(&s_card_thumbs[idx].dsc, &hdr) == LV_RESULT_OK
                && hdr.w > 0 && hdr.h > 0) {
                uint32_t scale = (48u * 256u) / (hdr.w > hdr.h ? hdr.w : hdr.h);
                if (scale > 256) scale = 256;
                lv_image_set_scale(jt, scale);
            }
            lv_obj_center(jt);
        } else if (img) {
            lv_obj_t *mi = lv_image_create(thumb);
            lv_image_set_src(mi, img);
            lv_obj_center(mi);
        }
    } else {
        /* No job; clear slot cache if it was occupied. */
        if (s_card_thumbs[idx].url[0]) {
             if (s_card_thumbs[idx].buf) {
                lv_image_cache_drop(&s_card_thumbs[idx].dsc);
                free(s_card_thumbs[idx].buf);
                s_card_thumbs[idx].buf = NULL;
            }
            s_card_thumbs[idx].url[0] = '\0';
        }

        if (img) {
            lv_obj_t *mi = lv_image_create(thumb);
            lv_image_set_src(mi, img);
            lv_obj_center(mi);
        } else {
            lv_obj_t *ti = lv_label_create(thumb);
            lv_label_set_text(ti, LV_SYMBOL_IMAGE);
            lv_obj_set_style_text_color(ti, PP_TEXT_MUTED, 0);
            lv_obj_center(ti);
        }
    }

    lv_obj_t *md = lv_label_create(c);
    lv_label_set_text(md, s->model[0] ? s->model : (online ? "Printer" : ""));
    lv_obj_set_style_text_color(md, PP_TEXT, 0);
    lv_obj_set_style_text_font(md, PP_F14, 0);
    lv_label_set_long_mode(md, LV_LABEL_LONG_DOT);
    lv_obj_set_width(md, 300);
    lv_obj_align(md, LV_ALIGN_TOP_LEFT, 66, 40);
    if (r) r->model_lbl = md;

    if (s->firmware[0]) {
        lv_obj_t *fwl = lv_label_create(c);
        lv_label_set_text_fmt(fwl, tr(STR_FW_FMT), s->firmware);
        lv_obj_set_style_text_color(fwl, PP_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(fwl, PP_F12, 0);
        lv_label_set_long_mode(fwl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(fwl, 300);
        lv_obj_align(fwl, LV_ALIGN_TOP_LEFT, 66, 59);
    }

    /* ---- 3-column labeled telemetry grid ---- */
    char nz[24], hb[24], sp[16], zx[16];
    fmt_telemetry(s, nz, hb, sp, zx);
    const int X1 = 14, X2 = 140, X3 = 266, R1 = 86, R2 = 124;
    lv_obj_t *vn = card_cell(c, X1, R1, tr(STR_NOZZLE), nz);
    lv_obj_t *vs = card_cell(c, X2, R1, tr(STR_SPEED),  sp);   /* Connect column order: NOZZLE / SPEED / BED */
    lv_obj_t *vb = card_cell(c, X3, R1, tr(STR_BED),    hb);
    lv_obj_t *vz = card_cell(c, X1, R2, tr(STR_Z_AXIS), zx);
    if (r) { r->v_noz = vn; r->v_speed = vs; r->v_bed = vb; r->v_z = vz; }

    /* progress (when printing) fills the 2nd/3rd column of row 2 */
    if (s->has_job) {
        int pct = (int)(s->progress + 0.5f);
        lv_obj_t *pl = lv_label_create(c);
        lv_label_set_text(pl, tr(STR_PROGRESS));
        lv_obj_set_style_text_color(pl, PP_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(pl, PP_F12, 0);
        lv_obj_align(pl, LV_ALIGN_TOP_LEFT, X2, R2);

        lv_obj_t *bar = lv_bar_create(c);
        lv_obj_set_size(bar, 140, 10);
        lv_obj_align(bar, LV_ALIGN_TOP_LEFT, X2, R2 + 20);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, PP_SURFACE_HI, 0);
        lv_obj_set_style_bg_color(bar, PP_ORANGE, LV_PART_INDICATOR);

        lv_obj_t *pv = lv_label_create(c);
        lv_label_set_text_fmt(pv, "%d%%", pct);
        lv_obj_set_style_text_color(pv, PP_TEXT, 0);
        lv_obj_set_style_text_font(pv, PP_F16, 0);
        lv_obj_align(pv, LV_ALIGN_TOP_LEFT, X3, R2 + 14);
        if (r) { r->prog_bar = bar; r->prog_lbl = pv; }
    }
}

/* Build one dashboard slot: the live card on the hidden host plus a snapshot image in the
 * grid. Falls back to the old behavior (live card directly in the grid) if the bitmap can't
 * be allocated. The wrapper's plain PP_BG background is what the card's rounded corners bake
 * against, matching the grid background exactly. */
static void dash_build_slot(int slot, const pp_status_t *s, int idx)
{
    if (!s_card_snap[slot])
        s_card_snap[slot] = lv_draw_buf_create(DASH_CARD_W, DASH_CARD_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!s_card_host || !s_card_snap[slot]) {
        make_printer_card(s_dash_grid, s, idx, &s_dref[slot]);   /* fallback: live card */
        return;
    }
    lv_obj_t *wrap = lv_obj_create(s_card_host);
    lv_obj_set_size(wrap, DASH_CARD_W, DASH_CARD_H);
    lv_obj_set_style_bg_color(wrap, PP_BG, 0);
    lv_obj_set_style_radius(wrap, 0, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    make_printer_card(wrap, s, idx, &s_dref[slot]);
    s_card_wrap[slot] = wrap;

    lv_obj_t *img = lv_image_create(s_dash_grid);
    lv_obj_set_size(img, DASH_CARD_W, DASH_CARD_H);
    lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(img, on_card_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
    s_card_img[slot] = img;
    dash_snapshot_slot(slot);
}

/* Wordmark: white-outlined box with [ PRUSA | TOUCH ] over a small "by NomadsGalaxy"
 * byline, stacked so it fits the standard header height. */
static void make_wordmark(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);   /* shrinks when byline hidden */
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(box, PP_WHITE, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 2, 0);
    lv_obj_set_style_pad_hor(box, 10, 0);
    lv_obj_set_style_pad_ver(box, 2, 0);
    lv_obj_set_style_pad_row(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(box, LV_ALIGN_LEFT_MID, 8, 0);
    /* Tapping the wordmark always returns to the fleet dashboard (home). */
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, nav_dash, LV_EVENT_CLICKED, NULL);

    /* top line: the skin's brand, split on "|" into the two-tone "left | right". */
    char bl[16] = {0}, br[16] = {0};
    {
        const char *brand = g_skin.brand[0] ? g_skin.brand : "KLIPPER | TOUCH";
        const char *bar = strchr(brand, '|');
        if (bar) {
            int n = (int)(bar - brand);
            while (n > 0 && brand[n - 1] == ' ') n--;
            if (n > 15) n = 15;
            memcpy(bl, brand, (size_t)n);
            const char *r = bar + 1;
            while (*r == ' ') r++;
            strncpy(br, r, 15);
        } else {
            strncpy(bl, brand, 15);
        }
    }
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);   /* clicks reach the box -> home */
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *p = lv_label_create(row);
    lv_label_set_text(p, bl);
    lv_obj_set_style_text_color(p, PP_WHITE, 0);
    lv_obj_set_style_text_font(p, PP_F16, 0);

    if (br[0]) {   /* divider + second part only when the brand has a "|" */
        lv_obj_t *divr = lv_obj_create(row);
        lv_obj_set_size(divr, 2, 18);
        lv_obj_set_style_bg_color(divr, PP_WHITE, 0);
        lv_obj_set_style_border_width(divr, 0, 0);
        lv_obj_set_style_radius(divr, 0, 0);
        lv_obj_clear_flag(divr, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *t = lv_label_create(row);
        lv_label_set_text(t, br);
        lv_obj_set_style_text_color(t, PP_WHITE, 0);
        lv_obj_set_style_text_font(t, PP_F16, 0);
    }

    /* bottom line: by NomadsGalaxy */
    lv_obj_t *by = lv_label_create(box);
    lv_label_set_text(by, g_skin.byline);
    lv_obj_set_style_text_color(by, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(by, PP_F12, 0);
    lv_obj_add_flag(by, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Track for the logo preference; hide the byline in single-line mode. */
    if (s_byline_count < (int)(sizeof(s_bylines) / sizeof(s_bylines[0])))
        s_bylines[s_byline_count++] = by;
    if (prefs_logo() == PP_LOGO_SINGLE) lv_obj_add_flag(by, LV_OBJ_FLAG_HIDDEN);
}

/* Show/hide all wordmark bylines to match the current logo preference. Scheduled on
 * the LVGL thread by app_state (after the NVS write on the net task). arg unused. */
void ui_apply_logo(void *unused)
{
    (void)unused;
    bool single = (prefs_logo() == PP_LOGO_SINGLE);
    for (int i = 0; i < s_byline_count; i++) {
        if (!s_bylines[i] || !lv_obj_is_valid(s_bylines[i])) continue;
        if (single) lv_obj_add_flag(s_bylines[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_remove_flag(s_bylines[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_apply_orient(void *unused)
{
    (void)unused;
    lv_display_rotation_t r;
    switch (prefs_orient()) {
        case PP_ORIENT_LANDSCAPE_FLIPPED: r = LV_DISPLAY_ROTATION_180; break;
        case PP_ORIENT_PORTRAIT:          r = LV_DISPLAY_ROTATION_90;  break;
        case PP_ORIENT_PORTRAIT_FLIPPED:  r = LV_DISPLAY_ROTATION_270; break;
        default:                          r = LV_DISPLAY_ROTATION_0;   break;
    }
    lv_display_set_rotation(lv_display_get_default(), r);
}

/* ---------- screen lock (opt-in) ----------
 * After N idle minutes the screen "locks": browsing stays open, but action callbacks call
 * ui_locked_block() which pops a PIN prompt and bails. The overlay lives on the top layer so
 * it floats over whichever screen is active, in either orientation. */
static bool        s_locked;
static lv_obj_t   *s_lock_modal;
static lv_obj_t   *s_lock_ta;
static lv_obj_t   *s_lock_msg;
static lv_obj_t   *s_lock_ind;
static lv_timer_t *s_lock_timer;

static void lock_release(void)
{
    s_locked = false;
    if (s_lock_ind)   lv_obj_add_flag(s_lock_ind, LV_OBJ_FLAG_HIDDEN);
    if (s_lock_modal) lv_obj_add_flag(s_lock_modal, LV_OBJ_FLAG_HIDDEN);
}

static void lock_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_locked) return;
    uint8_t m = prefs_lock_min();
    if (m == 0 || !prefs_scrpin()[0]) return;
    if (lv_display_get_inactive_time(NULL) > (uint32_t)m * 60000) {
        s_locked = true;
        if (s_lock_ind) lv_obj_remove_flag(s_lock_ind, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_apply_lock_cfg(void *unused)
{
    (void)unused;
    bool want = (prefs_lock_min() > 0 && prefs_scrpin()[0]);
    if (want && !s_lock_timer)        s_lock_timer = lv_timer_create(lock_timer_cb, 5000, NULL);
    else if (!want && s_lock_timer) { lv_timer_delete(s_lock_timer); s_lock_timer = NULL; lock_release(); }
}

static void on_pin_ok(lv_event_t *e)
{
    (void)e;
    if (strcmp(lv_textarea_get_text(s_lock_ta), prefs_scrpin()) == 0) lock_release();
    else { lv_label_set_text(s_lock_msg, tr(STR_WRONG_PIN)); lv_textarea_set_text(s_lock_ta, ""); }
}
static void on_pin_cancel(lv_event_t *e)
{
    (void)e;
    if (s_lock_modal) lv_obj_add_flag(s_lock_modal, LV_OBJ_FLAG_HIDDEN);   /* stays locked; just dismiss */
}

static void lock_show_prompt(void)
{
    if (!s_lock_modal) return;
    lv_textarea_set_text(s_lock_ta, "");
    lv_label_set_text(s_lock_msg, tr(STR_ENTER_PIN));
    lv_obj_remove_flag(s_lock_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_lock_modal);
}

static bool ui_locked_block(void)
{
    if (!s_locked) return false;
    lock_show_prompt();
    return true;
}

/* Tapping the LOCKED badge brings up the PIN prompt without having to poke an action first. */
static void on_lock_badge(lv_event_t *e) { (void)e; lock_show_prompt(); }

/* Public: lock the screen immediately (e.g. a future "Lock now" affordance / sim preview).
 * Browsing stays available; the prompt appears on an action or a badge tap. */
void ui_lock_now(void)
{
    if (prefs_scrpin()[0]) {
        s_locked = true;
        if (s_lock_ind) lv_obj_remove_flag(s_lock_ind, LV_OBJ_FLAG_HIDDEN);
    }
}
void ui_show_lock_prompt(void) { lock_show_prompt(); }

static void build_lock_overlay(void)
{
    lv_obj_t *top = lv_layer_top();

    /* small "LOCKED" badge, top-right; hidden until the screen locks */
    s_lock_ind = lv_label_create(top);
    lv_label_set_text_fmt(s_lock_ind, LV_SYMBOL_BELL " %s", tr(STR_LOCKED));
    lv_obj_set_style_text_color(s_lock_ind, PP_ORANGE, 0);
    lv_obj_set_style_text_font(s_lock_ind, PP_F14, 0);
    lv_obj_set_style_bg_color(s_lock_ind, PP_HEADER, 0);
    lv_obj_set_style_bg_opa(s_lock_ind, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_lock_ind, 6, 0);
    lv_obj_set_style_radius(s_lock_ind, 4, 0);
    lv_obj_align(s_lock_ind, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_add_flag(s_lock_ind, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lock_ind, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lock_ind, on_lock_badge, LV_EVENT_CLICKED, NULL);

    /* PIN-entry modal: full-screen backdrop + message + password field + number keypad */
    s_lock_modal = lv_obj_create(top);
    lv_obj_set_size(s_lock_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_lock_modal, PP_BG, 0);
    lv_obj_set_style_bg_opa(s_lock_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_lock_modal, 0, 0);
    lv_obj_set_style_radius(s_lock_modal, 0, 0);
    lv_obj_clear_flag(s_lock_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_lock_modal, LV_OBJ_FLAG_HIDDEN);

    s_lock_msg = lv_label_create(s_lock_modal);
    lv_label_set_text(s_lock_msg, tr(STR_ENTER_PIN));
    lv_obj_set_style_text_color(s_lock_msg, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_lock_msg, PP_F20, 0);

    s_lock_ta = lv_textarea_create(s_lock_modal);
    lv_textarea_set_one_line(s_lock_ta, true);
    lv_textarea_set_password_mode(s_lock_ta, true);
    lv_textarea_set_placeholder_text(s_lock_ta, "PIN");
    lv_obj_set_width(s_lock_ta, ui_portrait() ? 320 : 280);

    lv_obj_t *cancel = make_barbtn(s_lock_modal, LV_SYMBOL_CLOSE " Cancel", on_pin_cancel, NULL, 120);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -8, 8);

    lv_obj_t *kb = lv_keyboard_create(s_lock_modal);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(kb, s_lock_ta);
    lv_obj_add_event_cb(kb, on_pin_ok, LV_EVENT_READY, NULL);   /* the keypad's check key = unlock */

    /* Group the prompt + PIN field directly above the keypad (centred in the space above it)
     * rather than pinning them to the very top — the default left a big dead band between them. */
    int kbh = ui_portrait() ? 360 : 260;
    lv_obj_set_height(kb, kbh);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    int gtop = (scr_h() - kbh - 90) / 2;
    if (gtop < 56) gtop = 56;
    lv_obj_align(s_lock_msg, LV_ALIGN_TOP_MID, 0, gtop);
    lv_obj_align(s_lock_ta,  LV_ALIGN_TOP_MID, 0, gtop + 44);
}









static void build_dashboard_screen(void)
{
    s_scr_dash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_dash, PP_BG, 0);
    lv_obj_t *bar = make_header(s_scr_dash, NULL);   /* wordmark = home */
    lv_obj_t *gear = make_barbtn(bar, LV_SYMBOL_SETTINGS, nav_settings, NULL, 48);
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -8, 0);

    s_dash_grid = lv_obj_create(s_scr_dash);
    lv_obj_set_size(s_dash_grid, LV_PCT(100), scr_h() - 56);   /* fill under header (no bottom nav) */
    lv_obj_align(s_dash_grid, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_dash_grid, PP_BG, 0);
    lv_obj_set_style_border_width(s_dash_grid, 0, 0);
    lv_obj_set_style_pad_all(s_dash_grid, 8, 0);
    lv_obj_set_style_pad_bottom(s_dash_grid, 16, 0);
    lv_obj_set_flex_flow(s_dash_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_dash_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    /* Defer publishes while the grid is scrolling (applied on scroll-end). */
    lv_obj_add_event_cb(s_dash_grid, on_dash_scroll, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_dash_grid, on_dash_scroll, LV_EVENT_SCROLL_END, NULL);

    /* Hidden screen (never loaded) hosting the live card widget trees the grid's bitmap
     * cards are snapshotted from. */
    s_card_host = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_card_host, PP_BG, 0);
}

/* Lower rank sorts earlier when grouping by status. */
static int dash_state_rank(const pp_status_t *s)
{
    if (!s->online) return 5;
    switch (pp_state_class(s->state)) {
    case PP_SC_ORANGE: return 0;   /* printing / attention */
    case PP_SC_YELLOW: return 1;   /* paused */
    case PP_SC_RED:    return 2;   /* error / stopped */
    case PP_SC_GREEN:  return 3;   /* finished */
    case PP_SC_OLIVE:  return 4;   /* ready */
    default:           return 4;   /* idle / busy */
    }
}

static const pp_status_t *s_dash_sort_items;   /* set before qsort (single-threaded UI) */
static int dash_order_cmp(const void *pa, const void *pb)
{
    const pp_status_t *a = &s_dash_sort_items[*(const int *)pa];
    const pp_status_t *b = &s_dash_sort_items[*(const int *)pb];
    switch (prefs_sort()) {
    case PP_SORT_NAME:
        return strcmp(a->printer_name, b->printer_name);
    case PP_SORT_MODEL: {
        int m = strcmp(a->model, b->model);
        return m ? m : strcmp(a->printer_name, b->printer_name);
    }
    case PP_SORT_PROGRESS: {
        int aj = a->online && a->has_job, bj = b->online && b->has_job;
        if (aj && bj) { float d = b->progress - a->progress; return (d > 0) - (d < 0); }
        if (aj != bj) return bj - aj;                    /* printing first */
        int r = dash_state_rank(a) - dash_state_rank(b);
        return r ? r : strcmp(a->printer_name, b->printer_name);
    }
    case PP_SORT_STATUS:
    default: {
        int r = dash_state_rank(a) - dash_state_rank(b);
        return r ? r : strcmp(a->printer_name, b->printer_name);
    }
    }
}

void ui_apply_dashboard(void *arg)
{
    pp_dash_t *d = (pp_dash_t *)arg;
    /* Mid-gesture: park the newest publish, applied from the scroll-end handler. The
     * lv_obj_is_scrolling() check keeps a lost SCROLL_END (e.g. a screen switch mid-throw)
     * from parking publishes forever. */
    if (s_dash_scrolling && s_dash_grid && lv_obj_is_scrolling(s_dash_grid)) {
        free(s_dash_pending);
        s_dash_pending = d;
        return;
    }
    s_dash_count = d->count;

    int n = d->count; if (n > PP_MAX_PRINTERS) n = PP_MAX_PRINTERS;
    if (!s_dash_items)   /* one-time PSRAM alloc — keeps ~30KB off the scarce internal heap (mbedTLS needs it) */
        s_dash_items = heap_caps_malloc(PP_MAX_PRINTERS * sizeof(pp_status_t), MALLOC_CAP_SPIRAM);
    if (s_dash_items) for (int i = 0; i < n; i++) s_dash_items[i] = d->items[i];   /* snapshot for instant card-open */
    int order[PP_MAX_PRINTERS];
    for (int i = 0; i < n; i++) order[i] = i;
    s_dash_sort_items = d->items;
    if (n > 1) qsort(order, n, sizeof(int), dash_order_cmp);

    bool hide_off = prefs_hide_offline();
    uint32_t sig = dash_sig(d, order, n, hide_off);

    /* Fast path (gist #11): structure unchanged -> update the existing cards' values in place.
     * No lv_obj_clean / rebuild means no flicker, preserved scroll, and far less CPU. */
    if (s_dash_have && sig == s_dash_sig) {
        int slot = 0;
        for (int k = 0; k < n && slot < s_dref_n; k++) {
            int idx = order[k];
            if (hide_off && !d->items[idx].online) continue;
            if (update_dash_card(&s_dref[slot], &d->items[idx]))
                dash_snapshot_slot(slot);   /* re-render the bitmap only when something visible changed */
            slot++;
        }
        free(d);
        return;
    }

    /* Slow path: the structure changed (count/order/online/job/firmware/thumbnail) -> rebuild.
     * Bitmap buffers (s_card_snap) are slot-sized and survive rebuilds; only the widget trees
     * (grid images + host cards) are recreated. */
    int32_t scroll_y = lv_obj_get_scroll_y(s_dash_grid);   /* keep scroll position across rebuild */
    lv_obj_clean(s_dash_grid);
    if (s_card_host) lv_obj_clean(s_card_host);
    memset(s_card_wrap, 0, sizeof(s_card_wrap));
    memset(s_card_img,  0, sizeof(s_card_img));
    s_dref_n = 0;

    /* Connect sign-in lapsed: prepend a full-width re-connect banner (flex ROW_WRAP gives it
     * its own row above the cards). No credential entry on-device — the user re-authenticates
     * from the web Account tab; local PrusaLink fallback keeps configured printers reachable. */
    if (d->conn_expired) {
        lv_obj_t *bn = lv_obj_create(s_dash_grid);
        lv_obj_set_width(bn, LV_PCT(100));
        lv_obj_set_height(bn, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(bn, PP_ORANGE, 0);
        lv_obj_set_style_bg_opa(bn, LV_OPA_20, 0);
        lv_obj_set_style_border_color(bn, PP_ORANGE, 0);
        lv_obj_set_style_border_width(bn, 1, 0);
        lv_obj_set_style_radius(bn, 6, 0);
        lv_obj_set_style_pad_all(bn, 10, 0);
        lv_obj_clear_flag(bn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *bl = lv_label_create(bn);
        lv_label_set_long_mode(bl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(bl, LV_PCT(100));
        lv_obj_set_style_text_color(bl, PP_TEXT, 0);
        if (wifi_is_connected() && wifi_ip_str()[0])
            lv_label_set_text_fmt(bl, LV_SYMBOL_WARNING "  Prusa Connect sign-in expired. "
                "Reconnect from http://%s/ \xE2\x86\x92 Account. Local printers stay reachable.",
                wifi_ip_str());
        else
            lv_label_set_text(bl, LV_SYMBOL_WARNING "  Prusa Connect sign-in expired. "
                "Reconnect from the web Account tab. Local printers stay reachable.");
    }

    int shown = 0;
    for (int k = 0; k < n; k++) {
        int idx = order[k];                              /* original store index */
        if (hide_off && !d->items[idx].online) continue;
        if (shown < PP_MAX_PRINTERS) dash_build_slot(shown, &d->items[idx], idx);
        else make_printer_card(s_dash_grid, &d->items[idx], idx, NULL);
        shown++;
    }
    if (shown == 0) {
        lv_obj_t *l = lv_label_create(s_dash_grid);
        lv_label_set_text(l, d->count == 0 ? tr(STR_NO_PRINTERS) : tr(STR_NO_MATCH));
        lv_obj_set_style_text_color(l, PP_TEXT_MUTED, 0);
    }
    s_dref_n = shown;
    s_dash_sig = sig;
    s_dash_have = true;

    /* Restore scroll position. */
    lv_obj_update_layout(s_dash_grid);   /* ensure children positions are calculated */
    lv_obj_scroll_to_y(s_dash_grid, scroll_y, LV_ANIM_OFF);

    free(d);
}

/* ---------- About / attribution (satisfies SWAtt v1 UI requirement) ---------- */
static void on_about_back(lv_event_t *e) { lv_screen_load(s_scr_printers); }

static void on_about_open(lv_event_t *e) { lv_screen_load(s_scr_about); }

void ui_status_set_afc_chip(const pp_afc_t *a)
{
    if (!s_afc_chip || !a) return;
    if (!a->present || a->n <= 0) {
        lv_obj_add_flag(s_afc_chip, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (a->current[0]) {
        char buf[48];
        const char *mat = "";
        for (int i = 0; i < a->n; i++) {
            if (!strcmp(a->lanes[i].name, a->current)) { mat = a->lanes[i].material; break; }
        }
        if (mat && mat[0]) snprintf(buf, sizeof(buf), "AFC: %s (%s)", a->current, mat);
        else snprintf(buf, sizeof(buf), tr(STR_AFC_LANE_FMT), a->current);
        lv_label_set_text(s_afc_chip, buf);
        lv_obj_clear_flag(s_afc_chip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text_fmt(s_afc_chip, tr(STR_AFC_LANE_FMT), a->state[0] ? a->state : "-");
        lv_obj_clear_flag(s_afc_chip, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- Custom layout (issue #6): the web designer emits a pp_layout_t. The device no longer
 * shows it as a navigable screen — it renders the spec OFF-SCREEN into a snapshot for the web
 * "Generate preview" button (editing AND previewing both live in the web UI). ---------- */

/* Render layout spec L, bound to status st, into `parent` sized w x h. One-shot: each tile is built
 * AND bound inline (no live-update array, no header bar). Used only by the off-screen preview. */
static void layout_render_into(lv_obj_t *parent, const pp_layout_t *L, const pp_status_t *st, int w, int h)
{
    if (!parent || !L || !st) return;
    char buf[40];
    int cols = L->cols ? L->cols : 8, rows = 1;
    for (int i = 0; i < L->n; i++) { int rr = L->tiles[i].r + L->tiles[i].h; if (rr > rows) rows = rr; }
    const int pad = 6;
    int cw = w / cols, ch = h / rows;     /* no header off-screen: the full canvas is tile area */
    /* Cap the row height so cells stay as compact as the stock ones (a 1-row cell ~= the 56px
     * detail_cell) instead of stretching tall to fill the panel; the layout then top-aligns. */
    if (ch > 68) ch = 68;

    /* Pass 1: one shared surface card behind each group's bounding box (combined cards), drawn first
     * so the grouped tiles' content sits on top of it. */
    bool gdrawn[PP_LAYOUT_GROUPS] = {0};
    for (int i = 0; i < L->n; i++) {
        if (L->tiles[i].type == 0 || L->tiles[i].type >= LT_COUNT) continue;
        int g = L->tiles[i].group;
        if (g == 0 || g >= PP_LAYOUT_GROUPS || gdrawn[g]) continue;
        gdrawn[g] = true;
        int minc = 255, minr = 255, maxc = 0, maxr = 0;
        for (int j = 0; j < L->n; j++) {
            const pp_tile_t *u = &L->tiles[j];
            if (u->group != g || u->type == 0 || u->type >= LT_COUNT) continue;
            if (u->c < minc) minc = u->c;
            if (u->r < minr) minr = u->r;
            if (u->c + u->w > maxc) maxc = u->c + u->w;
            if (u->r + u->h > maxr) maxr = u->r + u->h;
        }
        lv_obj_t *gc = lv_obj_create(parent);
        lv_obj_set_pos(gc, minc * cw + pad, minr * ch + pad);
        lv_obj_set_size(gc, (maxc - minc) * cw - 2 * pad, (maxr - minr) * ch - 2 * pad);
        lv_obj_set_style_bg_color(gc, PP_SURFACE, 0);
        lv_obj_set_style_border_width(gc, 0, 0);
        lv_obj_set_style_radius(gc, 6, 0);
        lv_obj_clear_flag(gc, LV_OBJ_FLAG_SCROLLABLE);
    }

    for (int i = 0; i < L->n; i++) {
        const pp_tile_t *t = &L->tiles[i];
        if (t->type == 0 || t->type >= LT_COUNT) continue;
        int tw = t->w * cw - 2 * pad, th = t->h * ch - 2 * pad;
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_pos(card, t->c * cw + pad, t->r * ch + pad);
        lv_obj_set_size(card, tw, th);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        /* HEADER: printer name + state badge fused in the state-tinted strip (the hero header). */
        if (t->type == LT_HEADER) {
            lv_obj_set_style_bg_color(card, st->online ? pp_state_strip(st->state) : PP_STRIP_GRAY, 0);
            lv_obj_set_style_pad_hor(card, 10, 0);
            lv_obj_set_style_pad_ver(card, 4, 0);
            lv_obj_set_style_pad_column(card, 10, 0);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t *nm = lv_label_create(card);
            lv_label_set_text(nm, st->printer_name[0] ? st->printer_name : "Printer");
            lv_obj_set_style_text_color(nm, PP_TEXT, 0);
            lv_obj_set_style_text_font(nm, PP_F20, 0);
            lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
            lv_obj_set_flex_grow(nm, 1);
            lv_obj_t *bdg = lv_obj_create(card);
            lv_obj_set_size(bdg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_hor(bdg, 10, 0);
            lv_obj_set_style_pad_ver(bdg, 3, 0);
            lv_obj_set_style_radius(bdg, 4, 0);
            lv_obj_set_style_border_width(bdg, 0, 0);
            lv_obj_set_style_bg_color(bdg, st->online ? pp_state_badge(st->state) : PP_BADGE_GRAY, 0);
            lv_obj_clear_flag(bdg, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *sl = lv_label_create(bdg);
            lv_label_set_text(sl, st->state[0] ? st->state : "...");
            lv_obj_set_style_text_color(sl, PP_TEXT, 0);
            lv_obj_set_style_text_font(sl, PP_F16, 0);
            continue;
        }

        /* tile chrome per style: CARD = surface; BARE = transparent (floating label); ACCENT = orange.
         * A grouped tile is always transparent — its group's shared card (pass 1) is the background. */
        bool bare = (t->style == LS_BARE), accent = (t->style == LS_ACCENT);
        bool grouped = (t->group >= 1 && t->group < PP_LAYOUT_GROUPS);
        if (grouped) {
            lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_all(card, bare ? 4 : 8, 0);
        } else if (bare) {
            lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_all(card, 4, 0);
        } else if (accent) {
            lv_obj_set_style_bg_color(card, PP_ORANGE, 0);
            lv_obj_set_style_radius(card, 8, 0);
            lv_obj_set_style_pad_all(card, 8, 0);
        } else {
            lv_obj_set_style_bg_color(card, PP_SURFACE, 0);
            lv_obj_set_style_pad_all(card, 8, 0);
        }
        lv_color_t vcol = (accent && !grouped) ? PP_TEXT_INVERSE : PP_TEXT;   /* grouped drops the orange bg */

        /* caption only on CARD style (BARE/ACCENT drop it); NAME never captions. */
        bool has_cap = !bare && !accent && PP_TILE_LABELS[t->type][0] != '\0' && t->type != LT_NAME;
        if (has_cap) {
            lv_obj_t *cap = lv_label_create(card);
            lv_label_set_text(cap, PP_TILE_LABELS[t->type]);
            lv_obj_set_style_text_color(cap, PP_TEXT_MUTED, 0);
            lv_obj_set_style_text_font(cap, PP_F12, 0);
            lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        lv_obj_t *val = NULL, *barw = NULL, *img = NULL;
        if (t->type == LT_PROGRESS) {
            barw = lv_bar_create(card);
            lv_obj_set_size(barw, tw - 16, 12);
            lv_obj_align(barw, LV_ALIGN_BOTTOM_LEFT, 0, -2);
            lv_bar_set_range(barw, 0, 100);
            lv_bar_set_value(barw, 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(barw, PP_SURFACE_HI, LV_PART_MAIN);
            lv_obj_set_style_bg_color(barw, PP_ORANGE, LV_PART_INDICATOR);
            val = lv_label_create(card);
            lv_obj_set_style_text_color(val, vcol, 0);
            lv_obj_set_style_text_font(val, PP_F16, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, 0, 0);
        } else if (t->type == LT_THUMB) {
            if (!accent && !grouped) lv_obj_set_style_bg_color(card, PP_SURFACE_HI, 0);   /* ACCENT/group keep their bg */
            lv_obj_set_style_pad_all(card, 0, 0);
            img = lv_image_create(card);
            /* Scale the 48px model asset to fill ~80% of the slot's smaller side. */
            int side = (tw < th) ? tw : th;
            uint32_t sc = (uint32_t)(side * 4 / 5) * 256u / 48u;
            if (sc < 256) sc = 256;
            if (sc > 1024) sc = 1024;
            lv_image_set_scale(img, sc);
            lv_obj_center(img);
        } else if (t->type == LT_STATE) {
            val = lv_label_create(card);
            lv_obj_set_style_text_color(val, PP_TEXT, 0);
            lv_obj_set_style_text_font(val, PP_F16, 0);
            lv_obj_set_style_bg_opa(val, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(val, PP_BADGE_GRAY, 0);
            lv_obj_set_style_pad_hor(val, 12, 0);
            lv_obj_set_style_pad_ver(val, 3, 0);
            lv_obj_set_style_radius(val, 4, 0);
            lv_obj_center(val);
        } else if (t->type == LT_NAME) {
            val = lv_label_create(card);
            lv_obj_set_style_text_color(val, vcol, 0);
            lv_obj_set_style_text_font(val, PP_F20, 0);
            lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
            lv_obj_set_width(val, tw - 4);
            lv_obj_align(val, LV_ALIGN_LEFT_MID, 0, 0);
        } else {
            /* MODEL / JOB (secondary, muted-ish) or NOZZLE/BED/SPEED/Z/ETA (telemetry detail_cell). */
            const lv_image_dsc_t *ic = (t->type == LT_NOZZLE) ? &pt_ic_nozzle
                                     : (t->type == LT_BED)    ? &pt_ic_bed
                                     : (t->type == LT_SPEED)  ? &pt_ic_speed : NULL;
            int vx = 0;
            if (ic) {
                lv_obj_t *im = lv_image_create(card);
                lv_image_set_src(im, ic);
                lv_obj_align(im, LV_ALIGN_BOTTOM_LEFT, 0, 2);
                vx = 34;
            }
            bool secondary = (t->type == LT_MODEL || t->type == LT_JOB);
            val = lv_label_create(card);
            lv_obj_set_style_text_color(val, (t->type == LT_MODEL) ? PP_TEXT_MUTED : vcol, 0);
            lv_obj_set_style_text_font(val, secondary ? PP_F16 : PP_F20, 0);
            lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
            lv_obj_set_width(val, tw - vx);
            lv_obj_align(val, bare ? LV_ALIGN_LEFT_MID : LV_ALIGN_BOTTOM_LEFT, vx, 0);
        }
        /* bind the sample status inline (folded-in layout_bind) */
        switch (t->type) {
        case LT_NAME:  if (val) lv_label_set_text(val, st->printer_name[0] ? st->printer_name : "Printer"); break;
        case LT_MODEL: if (val) lv_label_set_text(val, st->model[0] ? st->model : "Printer"); break;
        case LT_JOB:   if (val) lv_label_set_text(val, st->job_name[0] ? st->job_name : "--"); break;
        case LT_STATE: if (val) { lv_label_set_text(val, st->state[0] ? st->state : "...");
                                  lv_obj_set_style_bg_color(val, st->online ? pp_state_badge(st->state) : PP_BADGE_GRAY, 0); } break;
        case LT_NOZZLE:
            if (val) {
                if (st->target_nozzle > 0) snprintf(buf, sizeof(buf), "%d/%d\xC2\xB0""C", (int)st->temp_nozzle, (int)st->target_nozzle);
                else                       snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", (int)st->temp_nozzle);
                lv_label_set_text(val, buf);
            }
            break;
        case LT_BED:
            if (val) {
                if (st->target_bed > 0) snprintf(buf, sizeof(buf), "%d/%d\xC2\xB0""C", (int)st->temp_bed, (int)st->target_bed);
                else                    snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", (int)st->temp_bed);
                lv_label_set_text(val, buf);
            }
            break;
        case LT_SPEED: if (val) { snprintf(buf, sizeof(buf), "%d%%", st->speed); lv_label_set_text(val, buf); } break;
        case LT_ZAXIS: if (val) { snprintf(buf, sizeof(buf), "%.2fmm", st->axis_z); lv_label_set_text(val, buf); } break;
        case LT_PROGRESS: {
            int pct = (int)st->progress;
            if (barw) lv_bar_set_value(barw, pct, LV_ANIM_OFF);
            if (val) { snprintf(buf, sizeof(buf), "%d%%", pct); lv_label_set_text(val, buf); }
        } break;
        case LT_ETA:
            if (val) {
                if (st->time_remaining > 0) { int m = st->time_remaining / 60; snprintf(buf, sizeof(buf), "%dh %02dm", m / 60, m % 60); }
                else strlcpy(buf, "--", sizeof(buf));
                lv_label_set_text(val, buf);
            }
            break;
        case LT_THUMB:
            if (img) { const lv_image_dsc_t *m = model_image(st->model); if (m) lv_image_set_src(img, m); }
            break;
        default: break;
        }
    }
}

#ifndef PP_HOST_SIM
/* Shared ownership for the preview job: the httpd handler and the (possibly-late) LVGL applier each
 * hold one ref; whichever drops the last ref frees everything. This makes the timeout path leak-free
 * AND use-after-free-free regardless of which side finishes first. */
void pp_preview_job_release(pp_preview_job_t *j)
{
    bool last;
    taskENTER_CRITICAL(&j->mux);
    last = (--j->refs <= 0);
    taskEXIT_CRITICAL(&j->mux);
    if (last) {
        free(j->rgb);
        if (j->sem) vSemaphoreDelete(j->sem);
        free(j);
    }
}

/* Off-screen preview applier — runs on the LVGL task via pt_display_schedule_ui. Renders the spec
 * with sample data onto a throwaway (never-loaded) screen on the LIVE display at the panel's NATIVE
 * resolution, snapshots it to a packed RGB565 PSRAM buffer the httpd task streams as BMP, then
 * signals the handler. Native-resolution = exactly what the device shows; rendering wider than the
 * panel corrupts the draw pipeline, so we always match the panel. ALWAYS gives the semaphore. */
void ui_layout_preview_render(void *arg)
{
    pp_preview_job_t *j = (pp_preview_job_t *)arg;
    j->ok = false; j->rgb = NULL;

    pp_status_t st = {0};      /* representative sample so a preview always looks populated */
    st.online = true;
    strlcpy(st.printer_name, "Apollo", sizeof(st.printer_name));
    strlcpy(st.model, "Prusa CORE One", sizeof(st.model));
    strlcpy(st.state, "PRINTING", sizeof(st.state));
    strlcpy(st.job_name, "benchy.gcode", sizeof(st.job_name));
    st.temp_nozzle = 215; st.target_nozzle = 215;
    st.temp_bed = 60;  st.target_bed = 60;
    st.speed = 100;    st.axis_z = 12.4f;
    st.progress = 64;  st.time_remaining = 64 * 60;

    lv_display_t *disp = lv_display_get_default();
    int w = (int)lv_display_get_horizontal_resolution(disp);
    int h = (int)lv_display_get_vertical_resolution(disp);
    j->w = w; j->h = h;        /* report the actual size back to the handler for the BMP header */

    lv_obj_t *host = lv_obj_create(NULL);   /* a screen on the live display; never lv_screen_load'ed */
    lv_obj_set_size(host, w, h);
    lv_obj_set_style_bg_color(host, PP_BG, 0);
    lv_obj_set_style_radius(host, 0, 0);
    lv_obj_set_style_border_width(host, 0, 0);
    lv_obj_set_style_pad_all(host, 0, 0);
    lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);

    layout_render_into(host, &j->spec, &st, w, h);
    lv_obj_update_layout(host);

    lv_draw_buf_t *snap = lv_snapshot_take(host, LV_COLOR_FORMAT_RGB565);
    if (snap) {
        size_t rowbytes = (size_t)w * 2;
        uint8_t *raw = heap_caps_malloc(rowbytes * (size_t)h, MALLOC_CAP_SPIRAM);
        if (raw) {
            const uint8_t *src = (const uint8_t *)snap->data;
            uint32_t stride = snap->header.stride;   /* source bytes per row (may be padded) */
            for (int y = 0; y < h; y++)
                memcpy(raw + (size_t)y * rowbytes, src + (size_t)y * stride, rowbytes);
            j->rgb = raw; j->ok = true;
        }
        lv_draw_buf_destroy(snap);
    }
    lv_obj_delete(host);
    xSemaphoreGive(j->sem);     /* wake the handler (if it's still waiting) */
    pp_preview_job_release(j);  /* drop the applier's ref; frees if the handler already gave up */
}
#endif /* !PP_HOST_SIM */

static void build_about_screen(void)
{
    s_scr_about = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_about, PP_BG, 0);
    lv_obj_clear_flag(s_scr_about, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bar = make_header(s_scr_about, "About / License");

    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_about_back, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    /* Landscape: text left, QR right. Portrait: text on top, QR centered below it. */
    const bool P = ui_portrait();

    /* ---- product + license text ---- */
    lv_obj_t *title = lv_label_create(s_scr_about);
    lv_label_set_text(title, "Klipper Touch");
    lv_obj_set_style_text_color(title, PP_ORANGE, 0);
    lv_obj_set_style_text_font(title, PP_F28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 74);

    lv_obj_t *by = lv_label_create(s_scr_about);
    lv_label_set_text(by, "Klipper / Moonraker first");
    lv_obj_set_style_text_color(by, PP_TEXT, 0);
    lv_obj_set_style_text_font(by, PP_F16, 0);
    lv_obj_align(by, LV_ALIGN_TOP_LEFT, 16, 108);

    lv_obj_t *body = lv_label_create(s_scr_about);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, P ? scr_w() - 32 : 470);
    lv_label_set_text(body,
        "Firmware " PP_FW_VERSION "\n"
        "Open-firmware touchscreen for Klipper (Moonraker),\n"
        "with optional PrusaLink, Prusa Connect, and Bambu.\n\n"
        "License: OCL v1.1 + SWAtt v1\n"
        "Built on PandaTouch_IDF (BigTreeTech, MIT).\n\n"
        "Fork of Prusa Touch / community firmware. Not affiliated\n"
        "with Prusa Research. \"Prusa\" and \"Prusa Connect\" are\n"
        "trademarks of Prusa Research.");
    lv_obj_set_style_text_color(body, PP_TEXT_MUTED, 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 16, 140);

    /* ---- right column: GitHub QR + URL ---- */
    static const char *repo = "https://github.com/mon5termatt/Custom-K-Touch";
    lv_obj_t *qr = lv_qrcode_create(s_scr_about);
    lv_qrcode_set_size(qr, 170);
    lv_qrcode_set_dark_color(qr, PP_BLACK);
    lv_qrcode_set_light_color(qr, PP_WHITE);
    lv_qrcode_update(qr, repo, strlen(repo));
    lv_obj_set_style_border_color(qr, PP_WHITE, 0);
    lv_obj_set_style_border_width(qr, 6, 0);      /* white quiet-zone border */
    if (P) lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 340);
    else   lv_obj_align(qr, LV_ALIGN_TOP_RIGHT, -70, 110);

    lv_obj_t *qcap = lv_label_create(s_scr_about);
    lv_label_set_text(qcap, tr(STR_SCAN_GITHUB));
    lv_obj_set_style_text_color(qcap, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(qcap, PP_F14, 0);
    if (P) lv_obj_align(qcap, LV_ALIGN_TOP_MID, 0, 524);
    else   lv_obj_align(qcap, LV_ALIGN_TOP_RIGHT, -40, 292);

    lv_obj_t *url = lv_label_create(s_scr_about);
    lv_label_set_text(url, "github.com/mon5termatt/\nCustom-K-Touch");
    lv_obj_set_style_text_color(url, PP_TEXT, 0);
    lv_obj_set_style_text_font(url, PP_F14, 0);
    lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);
    if (P) lv_obj_align(url, LV_ALIGN_TOP_MID, 0, 548);
    else   lv_obj_align(url, LV_ALIGN_TOP_RIGHT, -55, 314);
}

/* ---------- Prusa Farm (org-wide printer + order status) ---------- */
static void on_farm_back(lv_event_t *e) { (void)e; lv_screen_load(s_scr_printers); }

static void build_farm_screen(void)
{
    s_scr_farm = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_farm, PP_BG, 0);
    lv_obj_clear_flag(s_scr_farm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bar = make_header(s_scr_farm, "Prusa Farm");
    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_farm_back, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    s_farm_stat = lv_label_create(s_scr_farm);
    lv_label_set_text(s_farm_stat, tr(STR_LOADING_FARM));
    lv_obj_set_style_text_color(s_farm_stat, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_farm_stat, PP_F16, 0);
    lv_label_set_long_mode(s_farm_stat, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_farm_stat, scr_w() - 32);
    lv_obj_align(s_farm_stat, LV_ALIGN_TOP_LEFT, 16, 70);

    s_farm_list = lv_obj_create(s_scr_farm);
    lv_obj_set_size(s_farm_list, scr_w(), scr_h() - 132);
    lv_obj_align(s_farm_list, LV_ALIGN_TOP_LEFT, 0, 124);
    lv_obj_set_style_bg_color(s_farm_list, PP_BG, 0);
    lv_obj_set_style_border_width(s_farm_list, 0, 0);
    lv_obj_set_style_pad_all(s_farm_list, 16, 0);
    lv_obj_set_flex_flow(s_farm_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_farm_list, 8, 0);
}

void ui_apply_farm(void *arg)
{
    pp_farm_t *f = (pp_farm_t *)arg;
    if (!lv_obj_is_valid(s_farm_stat)) { free(f); return; }
    if (!f->valid && f->order_count == 0) {
        lv_label_set_text(s_farm_stat, tr(STR_FARM_UNAVAIL));
    } else {
        lv_label_set_text_fmt(s_farm_stat,
                              "Printers:  %d active   %d online   %d total%s   |   Orders: %d",
                              f->p_active, f->p_online, f->p_total,
                              f->p_error ? "   (errors!)" : "", f->order_count);
    }
    if (lv_obj_is_valid(s_farm_list)) {
        lv_obj_clean(s_farm_list);
        for (int i = 0; i < f->order_count; i++) {
            lv_obj_t *card = lv_obj_create(s_farm_list);
            lv_obj_set_width(card, LV_PCT(100));
            lv_obj_set_height(card, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(card, PP_SURFACE, 0);
            lv_obj_set_style_border_width(card, 0, 0);
            lv_obj_set_style_radius(card, 6, 0);
            lv_obj_set_style_pad_all(card, 10, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *l = lv_label_create(card);
            lv_obj_set_style_text_color(l, PP_TEXT, 0);
            lv_label_set_text_fmt(l, tr(STR_FARM_ORDER_FMT),
                                  f->orders[i].name[0] ? f->orders[i].name : "(order)",
                                  f->orders[i].done, f->orders[i].total,
                                  f->orders[i].attn ? "   needs attention" : "");
        }
    }
    free(f);
}

static void on_farm_open(lv_event_t *e)
{
    (void)e;
    if (lv_obj_is_valid(s_farm_stat)) lv_label_set_text(s_farm_stat, tr(STR_LOADING_FARM));
    if (lv_obj_is_valid(s_farm_list)) lv_obj_clean(s_farm_list);
    app_state_farm_refresh();
    lv_screen_load(s_scr_farm);
}

/* ---------- Preferences (sort / filter / logo) ---------- */
static void on_prefs_back(lv_event_t *e) { (void)e; lv_screen_load(s_scr_printers); }

/* All pref writes go through the net task (PSRAM-stack LVGL task can't touch flash). */
static void on_pref_sort_changed(lv_event_t *e)
{
    app_state_set_pref(PP_PREF_SORT, (int)lv_dropdown_get_selected(lv_event_get_target(e)));
}
static void on_pref_hideoff_changed(lv_event_t *e)
{
    app_state_set_pref(PP_PREF_HIDE_OFFLINE,
                       lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void on_pref_logo_changed(lv_event_t *e)
{
    app_state_set_pref(PP_PREF_LOGO, (int)lv_dropdown_get_selected(lv_event_get_target(e)));
}
static void on_pref_autoupd_changed(lv_event_t *e)
{
    app_state_set_pref(PP_PREF_AUTOUPDATE,
                       lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
/* Switching between a landscape class (0,1) and a portrait class (2,3) changes the screen
 * resolution, which needs a reboot to re-lay-out. Warn + confirm first; a same-class flip
 * (0<->1 / 2<->3) applies live with no reboot. */
static int s_pending_orient = -1;

static bool orient_is_portrait(int o) { return o == PP_ORIENT_PORTRAIT || o == PP_ORIENT_PORTRAIT_FLIPPED; }

static void orient_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (s_pending_orient >= 0) app_state_set_pref(PP_PREF_ORIENT, s_pending_orient);   /* reboots */
    lv_msgbox_close(mbox);
}
static void orient_cancel_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_dropdown_set_selected(s_pref_orient_dd, (uint16_t)prefs_orient());   /* revert the picker */
    lv_msgbox_close(mbox);
}

static void on_pref_orient_changed(lv_event_t *e)
{
    int sel = (int)lv_dropdown_get_selected(lv_event_get_target(e));
    if (orient_is_portrait(sel) == orient_is_portrait((int)prefs_orient())) {
        app_state_set_pref(PP_PREF_ORIENT, sel);   /* same class: live rotate, no reboot */
        return;
    }
    s_pending_orient = sel;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Restart required");
    lv_msgbox_add_text(mbox, orient_is_portrait(sel)
        ? "Switching to portrait restarts the device to re-lay-out the screens. Continue?"
        : "Switching to landscape restarts the device to re-lay-out the screens. Continue?");
    lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "Restart");
    lv_obj_set_style_bg_color(ok, PP_ORANGE, 0);
    lv_obj_add_event_cb(ok, orient_confirm_cb, LV_EVENT_CLICKED, mbox);
    lv_obj_t *cancel = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(cancel, orient_cancel_cb, LV_EVENT_CLICKED, mbox);
}

/* Theme/skin: colors bake into widgets at build time, so applying one reboots to rebuild every
 * screen (and brings the boot screen up themed). Confirm first, like an orientation class change. */
static int s_pending_skin = -1;

static void skin_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    if (s_pending_skin >= 0) app_state_set_pref(PP_PREF_SKIN, s_pending_skin);   /* reboots */
    lv_msgbox_close(mbox);
}
static void skin_cancel_cb(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_dropdown_set_selected(s_pref_theme_dd, (uint16_t)skin_current());   /* revert the picker */
    lv_msgbox_close(mbox);
}
static void on_pref_theme_changed(lv_event_t *e)
{
    int sel = (int)lv_dropdown_get_selected(lv_event_get_target(e));
    if (sel == skin_current()) return;
    s_pending_skin = sel;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Apply theme");
    char msg[112];
    snprintf(msg, sizeof(msg), "Apply the \"%s\" theme? The device restarts to repaint every screen.", skin_name(sel));
    lv_msgbox_add_text(mbox, msg);
    lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "Restart");
    lv_obj_set_style_bg_color(ok, PP_ORANGE, 0);
    lv_obj_add_event_cb(ok, skin_confirm_cb, LV_EVENT_CLICKED, mbox);
    lv_obj_t *cancel = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_add_event_cb(cancel, skin_cancel_cb, LV_EVENT_CLICKED, mbox);
}

static lv_obj_t *pref_label(lv_obj_t *parent, const char *text, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(l, PP_F14, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 24, y);
    return l;
}

/* Dark-theme a dropdown (closed button + open list) to match the Connect UI. */
static void dropdown_dark(lv_obj_t *dd)
{
    lv_obj_set_style_bg_color(dd, PP_SURFACE, 0);
    lv_obj_set_style_text_color(dd, PP_TEXT, 0);
    lv_obj_set_style_border_color(dd, PP_BORDER, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, 4, 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_bg_color(list, PP_SURFACE, 0);
        lv_obj_set_style_text_color(list, PP_TEXT, 0);
        lv_obj_set_style_border_color(list, PP_BORDER, 0);
        lv_obj_set_style_bg_color(list, PP_ORANGE, LV_PART_SELECTED | LV_STATE_CHECKED);
    }
}

static void build_prefs_screen(void)
{
    s_scr_prefs = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_prefs, PP_BG, 0);
    lv_obj_clear_flag(s_scr_prefs, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bar = make_header(s_scr_prefs, "Preferences");
    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_prefs_back, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    /* Landscape uses a 2nd column (x=420) for orientation; portrait stacks it under the rest. */
    const bool P = ui_portrait();

    /* Sort fleet by */
    pref_label(s_scr_prefs, "Sort fleet by", 84);
    s_pref_sort_dd = lv_dropdown_create(s_scr_prefs);
    lv_dropdown_set_options(s_pref_sort_dd, "Status\nName\nModel\nCompletion %");
    lv_obj_set_width(s_pref_sort_dd, 320);
    lv_obj_align(s_pref_sort_dd, LV_ALIGN_TOP_LEFT, 24, 112);
    dropdown_dark(s_pref_sort_dd);
    lv_obj_add_event_cb(s_pref_sort_dd, on_pref_sort_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* Hide offline */
    pref_label(s_scr_prefs, "Hide offline printers", 188);
    s_pref_hideoff_sw = lv_switch_create(s_scr_prefs);
    lv_obj_align(s_pref_hideoff_sw, LV_ALIGN_TOP_LEFT, 24, 214);
    lv_obj_set_style_bg_color(s_pref_hideoff_sw, PP_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_pref_hideoff_sw, on_pref_hideoff_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* Header logo */
    pref_label(s_scr_prefs, "Header logo", 290);
    s_pref_logo_dd = lv_dropdown_create(s_scr_prefs);
    lv_dropdown_set_options(s_pref_logo_dd, "KLIPPER|TOUCH + byline\nKLIPPER|TOUCH (single line)");
    lv_obj_set_width(s_pref_logo_dd, 320);
    lv_obj_align(s_pref_logo_dd, LV_ALIGN_TOP_LEFT, 24, 318);
    dropdown_dark(s_pref_logo_dd);
    lv_obj_add_event_cb(s_pref_logo_dd, on_pref_logo_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* Automatic updates (opt-in; off by default). Landscape: top of the RIGHT column so the
     * two columns balance 3-and-2 instead of stranding one lonely control on the right. */
    lv_obj_t *aul = lv_label_create(s_scr_prefs);
    lv_label_set_text(aul, tr(STR_AUTO_FW_UPDATES));
    lv_obj_set_style_text_color(aul, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(aul, PP_F14, 0);
    lv_obj_align(aul, LV_ALIGN_TOP_LEFT, P ? 24 : 420, P ? 392 : 84);
    s_pref_autoupd_sw = lv_switch_create(s_scr_prefs);
    lv_obj_align(s_pref_autoupd_sw, LV_ALIGN_TOP_LEFT, P ? 24 : 420, P ? 418 : 110);
    lv_obj_set_style_bg_color(s_pref_autoupd_sw, PP_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_pref_autoupd_sw, on_pref_autoupd_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* Screen orientation — landscape: right column under auto-updates; portrait: stacked. */
    lv_obj_t *ol = lv_label_create(s_scr_prefs);
    lv_label_set_text(ol, tr(STR_SCREEN_ORIENTATION));
    lv_obj_set_style_text_color(ol, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ol, PP_F14, 0);
    lv_obj_align(ol, LV_ALIGN_TOP_LEFT, P ? 24 : 420, P ? 484 : 196);
    s_pref_orient_dd = lv_dropdown_create(s_scr_prefs);
    lv_dropdown_set_options(s_pref_orient_dd, "Landscape\nLandscape (flipped)\nPortrait\nPortrait (flipped)");
    lv_obj_set_width(s_pref_orient_dd, 320);
    lv_obj_align(s_pref_orient_dd, LV_ALIGN_TOP_LEFT, P ? 24 : 420, P ? 512 : 224);
    dropdown_dark(s_pref_orient_dd);
    lv_obj_add_event_cb(s_pref_orient_dd, on_pref_orient_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* Theme / skin — landscape: right column under orientation; portrait: stacked. */
    lv_obj_t *tl = lv_label_create(s_scr_prefs);
    lv_label_set_text(tl, tr(STR_THEME));
    lv_obj_set_style_text_color(tl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(tl, PP_F14, 0);
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, P ? 24 : 420, P ? 596 : 308);
    char skopts[128]; skopts[0] = '\0';
    for (int i = 0; i < skin_count(); i++) {
        if (i) strlcat(skopts, "\n", sizeof(skopts));
        strlcat(skopts, skin_name(i), sizeof(skopts));
    }
    s_pref_theme_dd = lv_dropdown_create(s_scr_prefs);
    lv_dropdown_set_options(s_pref_theme_dd, skopts);
    lv_dropdown_set_selected(s_pref_theme_dd, (uint16_t)skin_current());
    lv_obj_set_width(s_pref_theme_dd, 320);
    lv_obj_align(s_pref_theme_dd, LV_ALIGN_TOP_LEFT, P ? 24 : 420, P ? 624 : 336);
    dropdown_dark(s_pref_theme_dd);
    lv_obj_add_event_cb(s_pref_theme_dd, on_pref_theme_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

static void on_prefs_open(lv_event_t *e)
{
    (void)e;
    lv_dropdown_set_selected(s_pref_sort_dd, (uint16_t)prefs_sort());
    lv_dropdown_set_selected(s_pref_logo_dd, (uint16_t)prefs_logo());
    if (prefs_hide_offline()) lv_obj_add_state(s_pref_hideoff_sw, LV_STATE_CHECKED);
    else                      lv_obj_remove_state(s_pref_hideoff_sw, LV_STATE_CHECKED);
    if (prefs_auto_update()) lv_obj_add_state(s_pref_autoupd_sw, LV_STATE_CHECKED);
    else                     lv_obj_remove_state(s_pref_autoupd_sw, LV_STATE_CHECKED);
    lv_dropdown_set_selected(s_pref_orient_dd, (uint16_t)prefs_orient());
    lv_screen_load(s_scr_prefs);
}

static void build_boot_screen(void)
{
    s_scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_boot, PP_BG, 0);   /* themed splash */
    lv_obj_clear_flag(s_scr_boot, LV_OBJ_FLAG_SCROLLABLE);

    /* brand (the skin's wordmark, "|" shown literally here) */
    lv_obj_t *l1 = lv_label_create(s_scr_boot);
    lv_label_set_text(l1, g_skin.brand[0] ? g_skin.brand : "KLIPPER | TOUCH");
    lv_obj_set_style_text_font(l1, PP_F40, 0);
    lv_obj_set_style_text_color(l1, PP_TEXT, 0);
    lv_obj_align(l1, LV_ALIGN_CENTER, 0, -40);

    /* byline */
    lv_obj_t *l2 = lv_label_create(s_scr_boot);
    lv_label_set_text(l2, g_skin.byline);
    lv_obj_set_style_text_font(l2, PP_F16, 0);
    lv_obj_set_style_text_color(l2, PP_ORANGE, 0);
    lv_obj_align(l2, LV_ALIGN_CENTER, 0, 10);

    /* Loading bar */
    s_boot_bar = lv_bar_create(s_scr_boot);
    lv_obj_set_size(s_boot_bar, 400, 12);
    lv_obj_align(s_boot_bar, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_bg_color(s_boot_bar, PP_SURFACE_HI, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_boot_bar, PP_ORANGE, LV_PART_INDICATOR);
    lv_bar_set_value(s_boot_bar, 0, LV_ANIM_OFF);

    s_boot_status = lv_label_create(s_scr_boot);
    lv_label_set_text(s_boot_status, tr(STR_STARTING));
    lv_obj_set_style_text_font(s_boot_status, PP_F14, 0);
    lv_obj_set_style_text_color(s_boot_status, PP_TEXT_MUTED, 0);
    lv_obj_align(s_boot_status, LV_ALIGN_CENTER, 0, 110);
}

void ui_boot_update(int progress, const char *status)
{
    PT_LVGL_SCOPE_LOCK() {
        if (s_boot_bar) lv_bar_set_value(s_boot_bar, progress, LV_ANIM_OFF);
        if (s_boot_status && status) lv_label_set_text(s_boot_status, status);
    }
}

/* Auto-refresh the Control screen's webcam preview while it's on screen. The snapshot
 * decode (TJPGD) is cheap and the JPEG buffer lives in PSRAM, so this is light. */
static void webcam_refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (ui_tools_is_webcam_active()) app_state_fetch_snapshot();
}

void ui_init(void)
{
    card_thumbs_clear();
    /* The LVGL default theme pins every widget (lists, msgboxes, buttons…) to the ASCII-only
     * Montserrat font, which overrides per-widget styles — so non-English text shows boxes. Point
     * the theme's fonts at the language-appropriate set (Inter for non-English) BEFORE any screen is
     * built, so the whole UI renders accents. */
    if (i18n_lang() != LANG_EN) {
        /* Re-init the default theme with an Inter base font so every widget (lists, msgboxes, …)
         * renders accented text. Colors are placeholders — the skin restyles widgets per-part after
         * this — and dark=true matches the UI. English keeps the stock theme (Montserrat). */
        lv_display_t *disp = lv_display_get_default();
        lv_theme_t *th = lv_theme_default_init(disp, lv_color_hex(0xFA6831), lv_color_hex(0x4E4E4E),
                                               true, PP_F16);
        if (th) lv_display_set_theme(disp, th);
    }
    /* Apply the saved orientation BEFORE building screens so resolution-aware sizing
     * (scr_w()/scr_h(), LV_PCT containers) lays out for portrait (480x800) when selected. */
    ui_apply_orient(NULL);
    build_boot_screen();
    lv_screen_load(s_scr_boot);

    build_dashboard_screen();
    build_status_screen();
    build_files_screen();
    build_filedetail_screen();
    build_printers_screen();
    build_addpick_screen();
    ui_tools_init();
    build_wifi_screen();
    build_about_screen();
    build_prefs_screen();
    build_farm_screen();
    ui_apply_orient(NULL);   /* apply the saved screen orientation */
    /* No bottom nav — fleet is home; Settings via header gear; Files from printer detail. */

    build_lock_overlay();    /* PIN-entry overlay on the top layer (hidden until locked) */
    ui_apply_lock_cfg(NULL); /* arm the idle-lock timer if the opt-in is configured */

    lv_timer_create(webcam_refresh_timer_cb, 7000, NULL);   /* live webcam on Webcam screen */
}

/* ---------- test/automation nav API ----------
 * ui_request_screen() is callable from any thread (e.g. the web server); it
 * marshals the actual lv_screen_load onto the LVGL thread via the BSP scheduler. */
static void ui_apply_nav(void *arg)
{
    char *name = (char *)arg;
    if (name) {
        if      (!strcmp(name, "dash")   || !strcmp(name, "fleet"))   lv_screen_load(s_scr_dash);
        else if (!strcmp(name, "status") || !strcmp(name, "printer")) lv_screen_load(s_scr_status);
        else if (!strcmp(name, "control") || !strcmp(name, "tools")) ui_tools_open();
        else if (!strcmp(name, "files"))  { app_state_post_cmd(s_files_usb_mode ? PP_CMD_LIST_USB : PP_CMD_LIST, NULL); lv_screen_load(s_scr_files); }
        else if (!strcmp(name, "printers") || !strcmp(name, "settings")) { refresh_printers_list(); lv_screen_load(s_scr_printers); }
        else if (!strcmp(name, "addpick"))                            lv_screen_load(s_scr_addpick);
        else if (!strcmp(name, "about"))                              lv_screen_load(s_scr_about);
        else if (!strcmp(name, "prefs"))                              on_prefs_open(NULL);
        else if (!strcmp(name, "farm"))                               on_farm_open(NULL);
        else if (!strcmp(name, "wifi"))   { wifi_screen_prepare(); lv_screen_load(s_scr_wifi); }
    }
    free(name);
}

void ui_request_screen(const char *name)
{
    if (!name || !name[0]) return;
    char *copy = malloc(24);
    if (!copy) return;
    strlcpy(copy, name, 24);
    if (pt_display_schedule_ui(ui_apply_nav, copy) != LV_RESULT_OK) free(copy);
}

const char *ui_current_screen(void)
{
    lv_obj_t *s = lv_screen_active();
    if (s == s_scr_dash)       return "dash";
    if (s == s_scr_status)     return "status";
    if (ui_tools_is_hub_active()) return "control";
    if (s == s_scr_files)      return "files";
    if (s == s_scr_filedetail) return "filedetail";
    if (s == s_scr_printers)   return "printers";
    if (s == s_scr_about)      return "about";
    if (s == s_scr_prefs)      return "prefs";
    if (s == s_scr_farm)       return "farm";
    if (s == s_scr_wifi)       return "wifi";
    return "unknown";
}

/* ---------- scheduled appliers (own + free arg) ---------- */
void ui_apply_status(void *arg)
{
    pp_status_t *s = (pp_status_t *)arg;
    char buf[64];

    if (s->printer_name[0]) {
        lv_label_set_text(s_title_lbl, s->printer_name);
        strlcpy(s_active_printer, s->printer_name, sizeof(s_active_printer));
    }
    strlcpy(s_active_model, s->model, sizeof(s_active_model));
    lv_obj_set_style_bg_color(s_conn_dot, s->online ? PP_OK : PP_ERROR, 0);
    wifi_status_label_refresh();   /* keep the Wi-Fi screen's IP line current */

    /* hero: model render on the orange tile, scaled to fill */
    const lv_image_dsc_t *mimg = model_image(s->model);
    if (mimg) {
        lv_image_set_src(s_detail_img, mimg);
        lv_image_set_scale(s_detail_img, 384);     /* 48px asset -> ~72px on tile */
        lv_obj_remove_flag(s_detail_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_detail_img, LV_OBJ_FLAG_HIDDEN);
    }

    /* hero: state-tinted strip + state badge (muted tint + white text) + model sub-line */
    lv_obj_set_style_bg_color(s_herotop, s->online ? pp_state_strip(s->state) : PP_STRIP_GRAY, 0);
    lv_obj_set_style_bg_color(s_badge, s->online ? pp_state_badge(s->state) : PP_BADGE_GRAY, 0);
    lv_label_set_text(s_state_lbl, tr_state(s->online ? (s->state[0] ? s->state : "READY") : "OFFLINE"));
    lv_label_set_text(s_model_lbl, s->model[0] ? s->model : "");

    /* telemetry cells */
    if (s->online) {
        if ((int)s->target_nozzle >= 1) snprintf(buf, sizeof(buf), "%d/%d\xC2\xB0""C", (int)s->temp_nozzle, (int)s->target_nozzle);
        else snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", (int)s->temp_nozzle);
        lv_label_set_text(s_nozzle_lbl, buf);
        if ((int)s->target_bed >= 1) snprintf(buf, sizeof(buf), "%d/%d\xC2\xB0""C", (int)s->temp_bed, (int)s->target_bed);
        else snprintf(buf, sizeof(buf), "%d\xC2\xB0""C", (int)s->temp_bed);
        lv_label_set_text(s_bed_lbl, buf);
        snprintf(buf, sizeof(buf), "%d%%", s->speed);
        lv_label_set_text(s_speed_lbl, buf);
        snprintf(buf, sizeof(buf), "%.2fmm", s->axis_z);
        lv_label_set_text(s_z_lbl, buf);
    } else {
        lv_label_set_text(s_nozzle_lbl, "--");
        lv_label_set_text(s_bed_lbl, "--");
        lv_label_set_text(s_speed_lbl, "--");
        lv_label_set_text(s_z_lbl, "--");
    }

    if (s->has_job) {
        lv_label_set_text(s_job_lbl, s->job_name[0] ? s->job_name : tr(STR_PRINTING_PAREN));
        lv_bar_set_value(s_bar, (int)(s->progress + 0.5f), LV_ANIM_ON);
        snprintf(buf, sizeof(buf), "%d%%", (int)(s->progress + 0.5f));
        lv_label_set_text(s_pct_lbl, buf);
        char eta[24];
        fmt_eta(s->time_remaining, eta, sizeof(eta));
        snprintf(buf, sizeof(buf), tr(STR_ETA_FMT), eta);
        lv_label_set_text(s_eta_lbl, buf);
    } else {
        lv_label_set_text(s_job_lbl, s->online ? tr(STR_NO_ACTIVE_PRINT) : "");
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(s_pct_lbl, "");
        lv_label_set_text(s_eta_lbl, "");
    }

    /* Pause button reflects the paused/printing state. */
    bool paused = (strcmp(s->state, "PAUSED") == 0);
    lv_label_set_text(s_btn_pause_lbl, paused ? tr(STR_RESUME) : tr(STR_PAUSE));

    /* CONTROL button visibility based on capability probe. */
    if (s->has_control) lv_obj_remove_flag(s_btn_control, LV_OBJ_FLAG_HIDDEN);
    else                lv_obj_add_flag(s_btn_control, LV_OBJ_FLAG_HIDDEN);

    ui_tools_refresh_menu();
    if (s->online) ui_tools_show_fault_if_needed(s->state);

    /* Attention dialog banner: when the printer has an active Connect dialog, surface its
     * title/text + action buttons over the (empty) job card and wire each button to DIALOG_ACTION. */
    if (s_attn_card) {
        if (s->dialog_id) {
            s_attn_dialog_id = s->dialog_id;
            lv_label_set_text(s_attn_title, s->dialog_title[0] ? s->dialog_title : tr(STR_ATTENTION));
            lv_label_set_text(s_attn_text, s->dialog_text);
            for (int i = 0; i < 3; i++) {
                if (i < s->dialog_btn_count && s->dialog_btns[i][0]) {
                    strlcpy(s_attn_btn_text[i], s->dialog_btns[i], sizeof(s_attn_btn_text[i]));
                    lv_label_set_text(s_attn_btn_lbls[i], s->dialog_btns[i]);
                    lv_obj_remove_flag(s_attn_btns[i], LV_OBJ_FLAG_HIDDEN);
                } else {
                    s_attn_btn_text[i][0] = '\0';
                    lv_obj_add_flag(s_attn_btns[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            lv_obj_remove_flag(s_attn_card, LV_OBJ_FLAG_HIDDEN);
            if (s_jobcard) lv_obj_add_flag(s_jobcard, LV_OBJ_FLAG_HIDDEN);
        } else {
            s_attn_dialog_id = 0;
            lv_obj_add_flag(s_attn_card, LV_OBJ_FLAG_HIDDEN);
            if (s_jobcard) lv_obj_remove_flag(s_jobcard, LV_OBJ_FLAG_HIDDEN);
        }
    }

    free(s);
}

void ui_apply_files(void *arg)
{
    pp_file_list_t *list = (pp_file_list_t *)arg;

    lv_obj_clean(s_file_list);
    s_file_count = 0;

    /* Refresh the printer-context banner (these files belong to the active printer). */
    if (s_active_printer[0] && s_active_model[0]) {
        lv_label_set_text_fmt(s_files_banner, tr(STR_FILES_ON_2_FMT),
                              s_active_printer, s_active_model);
    } else if (s_active_printer[0]) {
        lv_label_set_text_fmt(s_files_banner, tr(STR_FILES_ON_FMT), s_active_printer);
    }

    for (int i = 0; i < list->count && i < PP_MAX_FILES; i++) {
        if (!list->items[i].is_print) continue;   /* printable files only */
        s_files[s_file_count] = list->items[i];

        /* Connect-style row: file icon + bold name + muted meta (date · material). */
        lv_obj_t *row = lv_obj_create(s_file_list);
        lv_obj_set_size(row, LV_PCT(100), 58);
        lv_obj_set_style_bg_color(row, PP_SURFACE, 0);
        lv_obj_set_style_bg_color(row, PP_SURFACE_HI, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_file_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)s_file_count);

        lv_obj_t *ic = lv_label_create(row);
        lv_label_set_text(ic, LV_SYMBOL_FILE);
        lv_obj_set_style_text_color(ic, PP_ORANGE, 0);
        lv_obj_set_style_text_font(ic, PP_F20, 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text(nm, list->items[i].display);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_width(nm, 700);
        lv_obj_set_style_text_color(nm, PP_TEXT, 0);
        lv_obj_set_style_text_font(nm, PP_F16, 0);
        lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 36, 0);

        if (list->items[i].meta[0]) {
            lv_obj_t *mt = lv_label_create(row);
            lv_label_set_text(mt, list->items[i].meta);
            lv_obj_set_style_text_color(mt, PP_TEXT_MUTED, 0);
            lv_obj_set_style_text_font(mt, PP_F12, 0);
            lv_obj_align(mt, LV_ALIGN_BOTTOM_LEFT, 36, 0);
        }
        s_file_count++;
    }
    if (s_file_count == 0) {
        lv_obj_t *empty = lv_label_create(s_file_list);
        lv_label_set_text(empty, tr(STR_NO_PRINTABLE));
        lv_obj_set_style_text_color(empty, PP_TEXT_MUTED, 0);
    }
    free(list);
}

/* Display a fetched gcode thumbnail (PNG bytes). Takes ownership of the wrapper
 * and the PNG buffer; frees the wrapper, retains the buffer for the descriptor. */
void ui_apply_thumb(void *arg)
{
    pp_image_t *im = (pp_image_t *)arg;
    if (!im) return;

    /* Release whatever was on screen before. */
    thumb_clear();

    if (!im->data || im->len <= 0) {
        free(im->data);
        free(im);
        thumb_show_loading(false);
        lv_obj_clear_flag(s_thumb_ph, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_thumb_ph, tr(STR_PREVIEW_UNAVAIL));
        return;
    }

    /* Adopt the PNG bytes; build a descriptor LVGL's lodepng decoder can read. */
    s_thumb_buf = im->data;
    int len = im->len;
    free(im);                  /* wrapper done; buffer now owned by s_thumb_buf */

    s_thumb_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_thumb_dsc.header.cf    = LV_COLOR_FORMAT_RAW;   /* encoded (PNG) source */
    s_thumb_dsc.header.w     = 0;                     /* filled by the decoder */
    s_thumb_dsc.header.h     = 0;
    s_thumb_dsc.data         = s_thumb_buf;
    s_thumb_dsc.data_size    = (uint32_t)len;

    /* Uniform downscale-to-fit (never upscale) within the 340x280 viewport. */
    lv_image_header_t hdr;
    uint32_t scale = LV_SCALE_NONE;   /* 256 = 1x */
    if (lv_image_decoder_get_info(&s_thumb_dsc, &hdr) == LV_RESULT_OK
        && hdr.w > 0 && hdr.h > 0) {
        uint32_t sx = (340u * LV_SCALE_NONE) / hdr.w;
        uint32_t sy = (280u * LV_SCALE_NONE) / hdr.h;
        scale = sx < sy ? sx : sy;
        if (scale > LV_SCALE_NONE) scale = LV_SCALE_NONE;
    }

    thumb_show_loading(false);
    lv_obj_clear_flag(s_thumb_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_thumb_ph, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(s_thumb_img, &s_thumb_dsc);
    lv_image_set_scale(s_thumb_img, scale);
}

void ui_apply_thumb_dash(void *arg)
{
    pp_thumb_dash_t *td = (pp_thumb_dash_t *)arg;
    if (!td) return;

    int i = td->index;
    if (i >= 0 && i < PP_MAX_PRINTERS && td->image && td->image->data) {
        /* Store in cache. */
        if (s_card_thumbs[i].buf) {
            lv_image_cache_drop(&s_card_thumbs[i].dsc);
            free(s_card_thumbs[i].buf);
        }
        s_card_thumbs[i].buf = td->image->data;

        s_card_thumbs[i].dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_card_thumbs[i].dsc.header.cf    = LV_COLOR_FORMAT_RAW;
        s_card_thumbs[i].dsc.header.w     = 0;
        s_card_thumbs[i].dsc.header.h     = 0;
        s_card_thumbs[i].dsc.data         = s_card_thumbs[i].buf;
        s_card_thumbs[i].dsc.data_size    = (uint32_t)td->image->len;

        free(td->image); /* wrapper done; buffer now owned by cache */

        /* Re-trigger dashboard refresh to show the new thumbnail. */
        app_state_refresh_dashboard();
    } else {
        if (td->image) { free(td->image->data); free(td->image); }
    }
    free(td);
}
