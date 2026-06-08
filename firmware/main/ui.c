/* Prusa-Touch — LVGL UI implementation (Prusa-themed). */
#include "ui.h"
#include "app_state.h"
#include "printer_store.h"
#include "pandaprusa_theme.h"
#include "wifi.h"
#include "pandatouch_display.h"   /* pt_display_schedule_ui — for the test nav API */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl.h"
#include "misc/cache/lv_image_cache.h"   /* lv_image_cache_drop (not in lvgl.h) */
#include "draw/lv_image_decoder.h"       /* lv_image_decoder_get_info           */

/* ---- screens ---- */
static lv_obj_t *s_scr_dash;       /* fleet dashboard (home)  */
static lv_obj_t *s_dash_grid;      /* scrollable card grid    */
static int       s_dash_count;
typedef struct {
    uint8_t       *buf;
    lv_image_dsc_t dsc;
    char           url[160];
} pp_card_thumb_t;
static pp_card_thumb_t s_card_thumbs[PP_MAX_PRINTERS];
static lv_obj_t      *s_scr_status;     /* per-printer detail      */
static lv_obj_t *s_scr_control;    /* preheat/jog/home        */
static lv_obj_t *s_scr_files;
static lv_obj_t *s_scr_printers;
static lv_obj_t *s_scr_addform;
static lv_obj_t *s_scr_about;

/* header title (shows active printer name) */
static lv_obj_t *s_title_lbl;

/* printer picker + add form */
static lv_obj_t *s_pr_list;
static lv_obj_t *s_ta_name;
static lv_obj_t *s_ta_host;
static lv_obj_t *s_ta_key;
static lv_obj_t *s_kb;
static int       s_edit_idx = -1;     /* -1 = add new; >=0 = editing that printer */
static lv_obj_t *s_btn_remove;
static lv_obj_t *s_btn_setactive;

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

/* ---- file screen ---- */
static lv_obj_t *s_file_list;
static lv_obj_t *s_files_banner;          /* "Files on <printer>" context banner */
static char      s_active_printer[24];    /* mirror of active printer name/model  */
static char      s_active_model[28];
static pp_file_t s_files[PP_MAX_FILES];
static int       s_file_count;

/* ---- file-detail (gcode preview) screen ---- */
static lv_obj_t      *s_scr_filedetail;
static lv_obj_t      *s_fd_name;        /* header: file name        */
static lv_obj_t      *s_thumb_img;      /* lv_image (PNG preview)   */
static lv_obj_t      *s_thumb_ph;       /* placeholder label        */
static lv_image_dsc_t s_thumb_dsc;      /* descriptor over s_thumb_buf */
static uint8_t       *s_thumb_buf;      /* owned PNG bytes on display  */
static char           s_sel_path[160];  /* file selected for printing  */

static void fmt_eta(int secs, char *out, size_t n)
{
    if (secs < 0) { snprintf(out, n, "--"); return; }
    int h = secs / 3600, m = (secs % 3600) / 60;
    if (h > 0) snprintf(out, n, "%dh %02dm", h, m);
    else       snprintf(out, n, "%dm", m);
}

/* forward declarations */
static void on_printers_clicked(lv_event_t *e);
static void refresh_printers_list(void);
static void on_wifi_open(lv_event_t *e);
static void on_about_open(lv_event_t *e);
static void thumb_clear(void);
static lv_obj_t *make_header(lv_obj_t *parent, const char *text);
static lv_obj_t *make_barbtn(lv_obj_t *bar, const char *text, lv_event_cb_t cb,
                             void *user_data, lv_coord_t w);
static void make_wordmark(lv_obj_t *parent);

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
    /* The label text tells us which action applies. */
    const char *txt = lv_label_get_text(s_btn_pause_lbl);
    if (txt && strcmp(txt, "RESUME") == 0) {
        app_state_post_cmd(PP_CMD_RESUME, NULL);
    } else {
        app_state_post_cmd(PP_CMD_PAUSE, NULL);
    }
}

static void on_stop_clicked(lv_event_t *e)
{
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
    lv_screen_load(s_scr_status);
}

static void on_control_clicked(lv_event_t *e)
{
    (void)e;
    lv_screen_load(s_scr_control);
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
        lv_label_set_text(s_thumb_ph, "Loading preview...");
        app_state_fetch_thumb(s_files[idx].thumb);   /* -> ui_apply_thumb */
    } else {
        lv_label_set_text(s_thumb_ph, "No preview");
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
    lv_obj_set_style_radius(c, 10, 0);
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
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, PP_TEXT, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
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
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
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
    lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, vx, 0);
    return v;
}

static void build_status_screen(void)
{
    s_scr_status = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_status, PP_BG, 0);
    lv_obj_clear_flag(s_scr_status, LV_OBJ_FLAG_SCROLLABLE);

    /* Black top bar: [ PRUSA | TOUCH ] wordmark + connection dot + picker */
    lv_obj_t *bar = make_header(s_scr_status, NULL);   /* identical wordmark placement */

    lv_obj_t *pick = make_barbtn(bar, LV_SYMBOL_LIST, on_printers_clicked, NULL, 48);
    lv_obj_align(pick, LV_ALIGN_RIGHT_MID, -44, 0);

    s_conn_dot = lv_obj_create(bar);
    lv_obj_set_size(s_conn_dot, 18, 18);
    lv_obj_set_style_radius(s_conn_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_conn_dot, 0, 0);
    lv_obj_set_style_bg_color(s_conn_dot, PP_ERROR, 0);
    lv_obj_align(s_conn_dot, LV_ALIGN_RIGHT_MID, -12, 0);

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
    lv_obj_set_size(herotop, 660, 38);
    lv_obj_align(herotop, LV_ALIGN_TOP_LEFT, 112, 66);
    lv_obj_set_style_bg_opa(herotop, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(herotop, 0, 0);
    lv_obj_set_style_pad_all(herotop, 0, 0);
    lv_obj_set_style_pad_column(herotop, 12, 0);
    lv_obj_clear_flag(herotop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(herotop, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(herotop, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_title_lbl = lv_label_create(herotop);            /* printer name (was in the bar) */
    lv_label_set_text(s_title_lbl, "Printer");
    lv_obj_set_style_text_color(s_title_lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_title_lbl, &lv_font_montserrat_28, 0);

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
    lv_obj_set_style_text_font(s_state_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(s_state_lbl);

    s_model_lbl = lv_label_create(s_scr_status);
    lv_label_set_text(s_model_lbl, "");
    lv_obj_set_style_text_color(s_model_lbl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_model_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(s_model_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_model_lbl, 540);
    lv_obj_align(s_model_lbl, LV_ALIGN_TOP_LEFT, 112, 114);

    /* ---- telemetry cells (Connect hero row) ---- */
    s_nozzle_lbl = detail_cell(s_scr_status, 16,  160, 180, "NOZZLE",  &pt_ic_nozzle);
    s_bed_lbl    = detail_cell(s_scr_status, 208, 160, 180, "HEATBED", &pt_ic_bed);
    s_speed_lbl  = detail_cell(s_scr_status, 400, 160, 180, "SPEED",   &pt_ic_speed);
    s_z_lbl      = detail_cell(s_scr_status, 592, 160, 192, "Z AXIS",  NULL);

    /* ---- job / progress card ---- */
    lv_obj_t *jobcard = lv_obj_create(s_scr_status);
    lv_obj_set_size(jobcard, 768, 88);
    lv_obj_align(jobcard, LV_ALIGN_TOP_LEFT, 16, 228);
    lv_obj_set_style_bg_color(jobcard, PP_SURFACE, 0);
    lv_obj_set_style_border_width(jobcard, 0, 0);
    lv_obj_set_style_radius(jobcard, 6, 0);
    lv_obj_set_style_pad_all(jobcard, 12, 0);
    lv_obj_clear_flag(jobcard, LV_OBJ_FLAG_SCROLLABLE);

    s_job_lbl = lv_label_create(jobcard);
    lv_label_set_long_mode(s_job_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_job_lbl, 560);
    lv_label_set_text(s_job_lbl, "");
    lv_obj_set_style_text_color(s_job_lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_job_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_job_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pct_lbl = lv_label_create(jobcard);
    lv_label_set_text(s_pct_lbl, "");
    lv_obj_set_style_text_color(s_pct_lbl, PP_TEXT, 0);
    lv_obj_set_style_text_font(s_pct_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_bar = lv_bar_create(jobcard);
    lv_obj_set_size(s_bar, 600, 12);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, PP_SURFACE_HI, 0);
    lv_obj_set_style_bg_color(s_bar, PP_ORANGE, LV_PART_INDICATOR);

    s_eta_lbl = lv_label_create(jobcard);
    lv_label_set_text(s_eta_lbl, "");
    lv_obj_set_style_text_color(s_eta_lbl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_eta_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_eta_lbl, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* ---- action buttons (above the 60px bottom nav) ---- */
    lv_obj_t *pause_btn = make_button(s_scr_status, "PAUSE", on_pause_clicked, NULL, &s_btn_pause_lbl);
    lv_obj_align(pause_btn, LV_ALIGN_BOTTOM_LEFT, 16, -72);
    lv_obj_t *stop_btn = make_button(s_scr_status, "STOP", on_stop_clicked, NULL, NULL);
    lv_obj_align(stop_btn, LV_ALIGN_BOTTOM_LEFT, 180, -72);
    lv_obj_t *files_btn = make_button(s_scr_status, "FILES", on_files_clicked, NULL, NULL);
    lv_obj_align(files_btn, LV_ALIGN_BOTTOM_LEFT, 344, -72);
    s_btn_control = make_button(s_scr_status, "CONTROL", on_control_clicked, NULL, NULL);
    lv_obj_align(s_btn_control, LV_ALIGN_BOTTOM_LEFT, 508, -72);
    lv_obj_add_flag(s_btn_control, LV_OBJ_FLAG_HIDDEN);
}

static void build_files_screen(void)
{
    s_scr_files = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_files, PP_BG, 0);

    lv_obj_t *bar = make_header(s_scr_files, "Files");
    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_back_clicked, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

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
    lv_label_set_text(s_files_banner, "Files on this printer");
    lv_label_set_long_mode(s_files_banner, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_files_banner, 760);
    lv_obj_set_style_text_color(s_files_banner, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_files_banner, &lv_font_montserrat_14, 0);
    lv_obj_align(s_files_banner, LV_ALIGN_LEFT_MID, 0, 0);

    /* Scrollable column of Connect-style file rows. */
    s_file_list = lv_obj_create(s_scr_files);
    lv_obj_set_size(s_file_list, LV_PCT(100), 480 - 56 - 34 - 60);   /* header+banner+nav */
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
    if (s_sel_path[0]) app_state_post_cmd(PP_CMD_PRINT, s_sel_path);
    lv_screen_load(s_scr_status);
}

static void build_filedetail_screen(void)
{
    s_scr_filedetail = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_filedetail, PP_BG, 0);
    lv_obj_clear_flag(s_scr_filedetail, LV_OBJ_FLAG_SCROLLABLE);

    /* black header with file name + Back */
    lv_obj_t *bar = make_header(s_scr_filedetail, "");
    s_fd_name = lv_label_create(bar);
    lv_label_set_text(s_fd_name, "File");
    lv_label_set_long_mode(s_fd_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_fd_name, 560);
    lv_obj_set_style_text_color(s_fd_name, PP_WHITE, 0);
    lv_obj_set_style_text_font(s_fd_name, &lv_font_montserrat_20, 0);
    lv_obj_align(s_fd_name, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_fd_back, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    /* preview card holds either the thumbnail or a placeholder label */
    lv_obj_t *card = make_card(s_scr_filedetail, 360, 300);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 70);

    s_thumb_ph = lv_label_create(card);
    lv_label_set_text(s_thumb_ph, "No preview");
    lv_obj_set_style_text_color(s_thumb_ph, PP_TEXT_MUTED, 0);
    lv_obj_center(s_thumb_ph);

    s_thumb_img = lv_image_create(card);
    lv_obj_set_size(s_thumb_img, 340, 280);   /* fixed viewport; image centered + scaled to fit */
    lv_obj_center(s_thumb_img);
    lv_obj_add_flag(s_thumb_img, LV_OBJ_FLAG_HIDDEN);

    /* PRINT action */
    lv_obj_t *print_btn = make_button(s_scr_filedetail, "PRINT", on_fd_print, NULL, NULL);
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

static void on_add_open(lv_event_t *e)   /* add mode */
{
    s_edit_idx = -1;
    lv_textarea_set_text(s_ta_name, "");
    lv_textarea_set_text(s_ta_host, "");
    lv_textarea_set_text(s_ta_key, "");
    lv_obj_add_flag(s_btn_remove, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_setactive, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(s_scr_addform);
}

static void on_edit_open(lv_event_t *e)  /* edit mode: prefill from store */
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    pp_printer_t p;
    if (!printer_store_get(idx, &p)) return;
    s_edit_idx = idx;
    lv_textarea_set_text(s_ta_name, p.name);
    lv_textarea_set_text(s_ta_host, p.host);
    lv_textarea_set_text(s_ta_key, p.api_key);
    lv_obj_remove_flag(s_btn_remove, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_btn_setactive, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(s_scr_addform);
}

static void on_add_save(lv_event_t *e)
{
    pp_printer_t p = {0};
    strlcpy(p.name, lv_textarea_get_text(s_ta_name), sizeof(p.name));
    strlcpy(p.host, lv_textarea_get_text(s_ta_host), sizeof(p.host));
    strlcpy(p.api_key, lv_textarea_get_text(s_ta_key), sizeof(p.api_key));
    p.port = 80;
    if (p.name[0] == '\0') strlcpy(p.name, p.host, sizeof(p.name));
    if (p.host[0]) {
        if (s_edit_idx < 0) {
            int idx = printer_store_add(&p);
            app_state_printers_changed();
            if (idx >= 0) { app_state_select_printer(idx); lv_screen_load(s_scr_status); return; }
        } else {
            printer_store_update(s_edit_idx, &p);
            app_state_printers_changed();
        }
    }
    refresh_printers_list();
    lv_screen_load(s_scr_printers);
}

static void on_remove(lv_event_t *e)
{
    if (s_edit_idx >= 0) {
        printer_store_remove(s_edit_idx);
        app_state_printers_changed();
    }
    refresh_printers_list();
    lv_screen_load(s_scr_printers);
}

static void on_setactive(lv_event_t *e)
{
    if (s_edit_idx >= 0) app_state_select_printer(s_edit_idx);
    lv_screen_load(s_scr_status);
}

static void on_add_cancel(lv_event_t *e)
{
    lv_screen_load(s_scr_printers);
}

static void ta_focus_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(s_kb, NULL);
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_printers_list(void)
{
    lv_obj_clean(s_pr_list);
    int n = printer_store_count();
    int active = printer_store_active();
    for (int i = 0; i < n; i++) {
        pp_printer_t p;
        if (!printer_store_get(i, &p)) continue;
        char buf[80];
        snprintf(buf, sizeof(buf), "%s%s  (%s)", (i == active) ? "* " : "",
                 p.name, p.host);
        lv_obj_t *btn = lv_list_add_button(s_pr_list, LV_SYMBOL_EDIT, buf);
        lv_obj_set_style_bg_color(btn, PP_SURFACE, 0);
        lv_obj_set_style_text_color(btn, PP_TEXT, 0);
        lv_obj_add_event_cb(btn, on_edit_open, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    lv_obj_t *add = lv_list_add_button(s_pr_list, LV_SYMBOL_PLUS, "Add printer");
    lv_obj_set_style_bg_color(add, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(add, PP_ORANGE, 0);
    lv_obj_add_event_cb(add, on_add_open, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wf = lv_list_add_button(s_pr_list, LV_SYMBOL_WIFI, "Wi-Fi setup");
    lv_obj_set_style_bg_color(wf, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(wf, PP_TEXT, 0);
    lv_obj_add_event_cb(wf, on_wifi_open, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ab = lv_list_add_button(s_pr_list, LV_SYMBOL_LIST, "About / License");
    lv_obj_set_style_bg_color(ab, PP_SURFACE_HI, 0);
    lv_obj_set_style_text_color(ab, PP_TEXT, 0);
    lv_obj_add_event_cb(ab, on_about_open, LV_EVENT_CLICKED, NULL);
}

/* Black top bar carrying the persistent [ PRUSA | TOUCH ] wordmark (left) plus an
 * optional page title to its right — used on every screen for a consistent header. */
static lv_obj_t *make_header(lv_obj_t *parent, const char *text)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 56);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, PP_HEADER, 0);     /* Connect: black top bar */
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    make_wordmark(bar);                                /* persistent brand, left */

    if (text && text[0]) {
        lv_obj_t *t = lv_label_create(bar);
        lv_label_set_text(t, text);
        lv_obj_set_style_text_color(t, PP_TEXT, 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);        /* page title centered on screen */
    }
    return bar;
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
    make_header(s_scr_printers, "Printers");

    s_pr_list = lv_list_create(s_scr_printers);
    lv_obj_set_size(s_pr_list, LV_PCT(100), 480 - 56 - 60);   /* header + nav */
    lv_obj_align(s_pr_list, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_pr_list, PP_BG, 0);
    lv_obj_set_style_border_width(s_pr_list, 0, 0);
}

static lv_obj_t *make_field(lv_obj_t *parent, const char *label, lv_coord_t y,
                            bool password)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, PP_TEXT_MUTED, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 16, y);
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password);
    lv_obj_set_width(ta, 520);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 130, y - 8);
    lv_obj_add_event_cb(ta, ta_focus_event, LV_EVENT_ALL, NULL);
    return ta;
}

static void build_addform_screen(void)
{
    s_scr_addform = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_addform, PP_BG, 0);
    make_header(s_scr_addform, "Add printer");

    s_ta_name = make_field(s_scr_addform, "Name", 72, false);
    s_ta_host = make_field(s_scr_addform, "IP / host", 116, false);
    s_ta_key  = make_field(s_scr_addform, "API key", 160, true);

    lv_obj_t *save = lv_button_create(s_scr_addform);
    lv_obj_set_size(save, 140, 50);
    lv_obj_align(save, LV_ALIGN_TOP_LEFT, 130, 204);
    lv_obj_set_style_bg_color(save, PP_ORANGE, 0);
    lv_obj_add_event_cb(save, on_add_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(save);
    lv_label_set_text(sl, "Save");
    lv_obj_set_style_text_color(sl, PP_WHITE, 0);
    lv_obj_center(sl);

    lv_obj_t *cancel = lv_button_create(s_scr_addform);
    lv_obj_set_size(cancel, 140, 50);
    lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, 280, 204);
    lv_obj_set_style_bg_color(cancel, PP_SURFACE_HI, 0);
    lv_obj_add_event_cb(cancel, on_add_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, PP_TEXT, 0);
    lv_obj_center(cl);

    /* Edit-mode actions (hidden in add mode) */
    s_btn_setactive = lv_button_create(s_scr_addform);
    lv_obj_set_size(s_btn_setactive, 140, 50);
    lv_obj_align(s_btn_setactive, LV_ALIGN_TOP_LEFT, 130, 262);
    lv_obj_set_style_bg_color(s_btn_setactive, PP_SURFACE_HI, 0);
    lv_obj_set_style_border_color(s_btn_setactive, PP_ORANGE, 0);
    lv_obj_set_style_border_width(s_btn_setactive, 2, 0);
    lv_obj_add_event_cb(s_btn_setactive, on_setactive, LV_EVENT_CLICKED, NULL);
    lv_obj_t *al = lv_label_create(s_btn_setactive);
    lv_label_set_text(al, "Set active");
    lv_obj_set_style_text_color(al, PP_TEXT, 0);
    lv_obj_center(al);

    s_btn_remove = lv_button_create(s_scr_addform);
    lv_obj_set_size(s_btn_remove, 140, 50);
    lv_obj_align(s_btn_remove, LV_ALIGN_TOP_LEFT, 280, 262);
    lv_obj_set_style_bg_color(s_btn_remove, PP_ERROR, 0);
    lv_obj_add_event_cb(s_btn_remove, on_remove, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(s_btn_remove);
    lv_label_set_text(rl, "Remove");
    lv_obj_set_style_text_color(rl, PP_WHITE, 0);
    lv_obj_center(rl);

    /* On-screen keyboard, hidden until a field is focused. */
    s_kb = lv_keyboard_create(s_scr_addform);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
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
        lv_label_set_text_fmt(s_wifi_sel_lbl, "Network: %s", s_wifi_selected);
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

/* Reset the Wi-Fi screen widgets + hotspot hint and kick off a scan. Shared by the
 * menu entry and the test-nav API so the screen is always correctly populated. */
static void wifi_screen_prepare(void)
{
    s_wifi_selected[0] = '\0';
    lv_label_set_text(s_wifi_sel_lbl, "Network: (tap Scan)");
    lv_textarea_set_text(s_wifi_ta_pass, "");
    lv_obj_clean(s_wifi_list);

    /* Surface the provisioning hotspot when it's up (no network reachable). */
    if (wifi_is_ap_active()) {
        lv_label_set_text_fmt(s_wifi_ap_lbl,
            LV_SYMBOL_WARNING " No network. Hotspot \"%s\" is open - join it from a "
            "phone and open http://192.168.4.1 to set up Wi-Fi.", wifi_ap_ssid());
    } else {
        lv_label_set_text(s_wifi_ap_lbl,
            "Tip: if no known network is found, the device opens a \"PrusaTouch-...\" "
            "hotspot at http://192.168.4.1 for setup.");
    }
    app_state_wifi_scan();   /* auto-scan */
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

    s_wifi_list = lv_list_create(s_scr_wifi);
    lv_obj_set_size(s_wifi_list, 380, 480 - 56);
    lv_obj_align(s_wifi_list, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_wifi_list, PP_BG, 0);
    lv_obj_set_style_border_width(s_wifi_list, 0, 0);

    s_wifi_sel_lbl = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_wifi_sel_lbl, "Network: (tap Scan)");
    lv_obj_set_style_text_color(s_wifi_sel_lbl, PP_TEXT, 0);
    lv_obj_align(s_wifi_sel_lbl, LV_ALIGN_TOP_LEFT, 396, 72);

    s_wifi_ta_pass = lv_textarea_create(s_scr_wifi);
    lv_textarea_set_one_line(s_wifi_ta_pass, true);
    lv_textarea_set_password_mode(s_wifi_ta_pass, true);
    lv_textarea_set_placeholder_text(s_wifi_ta_pass, "password");
    lv_obj_set_width(s_wifi_ta_pass, 380);
    lv_obj_align(s_wifi_ta_pass, LV_ALIGN_TOP_LEFT, 396, 104);
    lv_obj_add_event_cb(s_wifi_ta_pass, on_wifi_pass_focus, LV_EVENT_ALL, NULL);

    lv_obj_t *conn = lv_button_create(s_scr_wifi);
    lv_obj_set_size(conn, 160, 56);
    lv_obj_align(conn, LV_ALIGN_TOP_LEFT, 396, 156);
    lv_obj_set_style_bg_color(conn, PP_ORANGE, 0);
    lv_obj_add_event_cb(conn, on_wifi_connect_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(conn);
    lv_label_set_text(cl, "Connect");
    lv_obj_set_style_text_color(cl, PP_WHITE, 0);
    lv_obj_center(cl);

    s_wifi_ap_lbl = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_wifi_ap_lbl, "");
    lv_label_set_long_mode(s_wifi_ap_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi_ap_lbl, 388);
    lv_obj_set_style_text_color(s_wifi_ap_lbl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_wifi_ap_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wifi_ap_lbl, LV_ALIGN_TOP_LEFT, 396, 232);

    s_wifi_kb = lv_keyboard_create(s_scr_wifi);
    lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- bottom navigation (persistent, Panda-Touch style) ---------- */
static void nav_dash(lv_event_t *e)     { lv_screen_load(s_scr_dash); }
static void nav_detail(lv_event_t *e)   { lv_screen_load(s_scr_status); }
static void nav_files(lv_event_t *e)    { app_state_post_cmd(PP_CMD_LIST, NULL); lv_screen_load(s_scr_files); }
static void nav_settings(lv_event_t *e) { refresh_printers_list(); lv_screen_load(s_scr_printers); }

static void make_nav(lv_obj_t *scr, int active)
{
    static const char *labels[4] = { "Fleet", "Printer", "Files", "Settings" };
    const lv_event_cb_t cbs[4] = { nav_dash, nav_detail, nav_files, nav_settings };
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LV_PCT(100), 60);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, PP_SURFACE, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_button_create(bar);
        lv_obj_set_size(b, 188, 50);
        lv_obj_set_style_bg_color(b, i == active ? PP_ORANGE : PP_SURFACE_HI, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_add_event_cb(b, cbs[i], LV_EVENT_CLICKED, NULL);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, labels[i]);
        lv_obj_set_style_text_color(l, PP_TEXT, 0);
        lv_obj_center(l);
    }
}

/* ---------- fleet dashboard ---------- */
static void on_card_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_dash_count) {   /* index may be stale after a remove */
        app_state_select_printer(idx);
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
static void card_cell(lv_obj_t *parent, int x, int y, const char *label, const char *value)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, value);
    lv_obj_set_style_text_color(v, PP_TEXT, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_16, 0);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, x, y + 17);
}

/* Prusa Connect dark-card anatomy: state-tinted header strip (name + badge),
 * then a 3-column labeled telemetry grid; progress bar slot when printing. */
static void make_printer_card(lv_obj_t *parent, const pp_status_t *s, int idx)
{
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

    lv_obj_t *badge = lv_obj_create(head);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_pad_hor(badge, 12, 0);
    lv_obj_set_style_pad_ver(badge, 0, 0);
    lv_obj_set_style_radius(badge, 0, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_bg_color(badge, online ? pp_state_badge(s->state) : PP_BADGE_GRAY, 0);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bl = lv_label_create(badge);
    lv_label_set_text(bl, st);
    lv_obj_set_style_text_color(bl, PP_TEXT, 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_center(bl);

    lv_obj_t *nm = lv_label_create(head);
    lv_label_set_text(nm, s->printer_name[0] ? s->printer_name : "Printer");
    lv_obj_set_style_text_color(nm, PP_TEXT, 0);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_20, 0);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nm, 226);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 12, 0);

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
    lv_label_set_text(md, s->model[0] ? s->model : (online ? "Prusa printer" : ""));
    lv_obj_set_style_text_color(md, PP_TEXT, 0);
    lv_obj_set_style_text_font(md, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(md, LV_LABEL_LONG_DOT);
    lv_obj_set_width(md, 300);
    lv_obj_align(md, LV_ALIGN_TOP_LEFT, 66, 40);

    if (s->firmware[0]) {
        lv_obj_t *fwl = lv_label_create(c);
        lv_label_set_text_fmt(fwl, "Firmware: %s", s->firmware);
        lv_obj_set_style_text_color(fwl, PP_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(fwl, &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(fwl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(fwl, 300);
        lv_obj_align(fwl, LV_ALIGN_TOP_LEFT, 66, 59);
    }

    /* ---- 3-column labeled telemetry grid ---- */
    char nz[24], hb[24], sp[16], zx[16];
    if (online) {
        if ((int)s->target_nozzle >= 1) snprintf(nz, sizeof(nz), "%d/%d\xC2\xB0""C", (int)s->temp_nozzle, (int)s->target_nozzle);
        else snprintf(nz, sizeof(nz), "%d\xC2\xB0""C", (int)s->temp_nozzle);
        if ((int)s->target_bed >= 1) snprintf(hb, sizeof(hb), "%d/%d\xC2\xB0""C", (int)s->temp_bed, (int)s->target_bed);
        else snprintf(hb, sizeof(hb), "%d\xC2\xB0""C", (int)s->temp_bed);
        snprintf(sp, sizeof(sp), "%d%%", s->speed);
        snprintf(zx, sizeof(zx), "%.2fmm", s->axis_z);
    } else {
        strcpy(nz, "--"); strcpy(hb, "--"); strcpy(sp, "--"); strcpy(zx, "--");
    }
    const int X1 = 14, X2 = 140, X3 = 266, R1 = 86, R2 = 124;
    card_cell(c, X1, R1, "NOZZLE",  nz);
    card_cell(c, X2, R1, "HEATBED", hb);
    card_cell(c, X3, R1, "SPEED",   sp);
    card_cell(c, X1, R2, "Z AXIS",  zx);

    /* progress (when printing) fills the 2nd/3rd column of row 2 */
    if (s->has_job) {
        int pct = (int)(s->progress + 0.5f);
        lv_obj_t *pl = lv_label_create(c);
        lv_label_set_text(pl, "PROGRESS");
        lv_obj_set_style_text_color(pl, PP_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(pl, &lv_font_montserrat_12, 0);
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
        lv_obj_set_style_text_font(pv, &lv_font_montserrat_16, 0);
        lv_obj_align(pv, LV_ALIGN_TOP_LEFT, X3, R2 + 14);
    }
}

/* Wordmark: white-outlined box with [ PRUSA | TOUCH ] over a small "by NomadsGalaxy"
 * byline, stacked so it fits the standard header height. */
static void make_wordmark(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, 46);
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

    /* top line: PRUSA | TOUCH */
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
    lv_label_set_text(p, "PRUSA");
    lv_obj_set_style_text_color(p, PP_WHITE, 0);
    lv_obj_set_style_text_font(p, &lv_font_montserrat_16, 0);

    lv_obj_t *divr = lv_obj_create(row);
    lv_obj_set_size(divr, 2, 18);
    lv_obj_set_style_bg_color(divr, PP_WHITE, 0);
    lv_obj_set_style_border_width(divr, 0, 0);
    lv_obj_set_style_radius(divr, 0, 0);
    lv_obj_clear_flag(divr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, "TOUCH");
    lv_obj_set_style_text_color(t, PP_WHITE, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);

    /* bottom line: by NomadsGalaxy */
    lv_obj_t *by = lv_label_create(box);
    lv_label_set_text(by, "by NomadsGalaxy");
    lv_obj_set_style_text_color(by, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(by, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(by, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void build_dashboard_screen(void)
{
    s_scr_dash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_dash, PP_BG, 0);
    make_header(s_scr_dash, NULL);   /* same builder as every screen → wordmark never shifts */

    s_dash_grid = lv_obj_create(s_scr_dash);
    lv_obj_set_size(s_dash_grid, LV_PCT(100), 480 - 56 - 60);   /* header + nav */
    lv_obj_align(s_dash_grid, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_dash_grid, PP_BG, 0);
    lv_obj_set_style_border_width(s_dash_grid, 0, 0);
    lv_obj_set_style_pad_all(s_dash_grid, 8, 0);
    lv_obj_set_flex_flow(s_dash_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_dash_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

void ui_apply_dashboard(void *arg)
{
    pp_dash_t *d = (pp_dash_t *)arg;
    lv_obj_clean(s_dash_grid);
    s_dash_count = d->count;
    for (int i = 0; i < d->count && i < PP_MAX_PRINTERS; i++) {
        make_printer_card(s_dash_grid, &d->items[i], i);
    }
    if (d->count == 0) {
        lv_obj_t *l = lv_label_create(s_dash_grid);
        lv_label_set_text(l, "No printers yet — add one in Settings.");
        lv_obj_set_style_text_color(l, PP_TEXT_MUTED, 0);
    }
    free(d);
}

/* ---------- About / attribution (satisfies SWAtt v1 UI requirement) ---------- */
static void on_about_back(lv_event_t *e) { lv_screen_load(s_scr_printers); }

static void on_about_open(lv_event_t *e) { lv_screen_load(s_scr_about); }

static void on_preheat_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    app_state_post_cmd(PP_CMD_PREHEAT, (const char *)(intptr_t)idx);
}

static void on_jog_clicked(lv_event_t *e)
{
    const char *gcode = (const char *)lv_event_get_user_data(e);
    app_state_post_cmd(PP_CMD_GCODE, gcode);
}

static void build_control_screen(void)
{
    s_scr_control = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_control, PP_BG, 0);

    lv_obj_t *bar = make_header(s_scr_control, "Control");
    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_control_back, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    /* Temperatures Card */
    lv_obj_t *temp_card = make_card(s_scr_control, 380, 180);
    lv_obj_align(temp_card, LV_ALIGN_TOP_LEFT, 16, 72);
    lv_obj_t *tl = lv_label_create(temp_card);
    lv_label_set_text(tl, "PREHEAT");
    lv_obj_set_style_text_color(tl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);

    const char *mats[] = { "PLA", "PETG", "ASA", "Cooldown" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = make_button(temp_card, mats[i], on_preheat_clicked, (void *)(intptr_t)i, NULL);
        lv_obj_set_size(b, 160, 50);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, (i % 2) * 180, 30 + (i / 2) * 60);
    }

    /* Jog Card */
    lv_obj_t *jog_card = make_card(s_scr_control, 380, 240);
    lv_obj_align(jog_card, LV_ALIGN_TOP_RIGHT, -16, 72);
    lv_obj_set_style_pad_all(jog_card, 0, 0);   /* predictable absolute coords */
    lv_obj_t *jl = lv_label_create(jog_card);
    lv_label_set_text(jl, "MOVE");
    lv_obj_set_style_text_color(jl, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(jl, &lv_font_montserrat_14, 0);
    lv_obj_align(jl, LV_ALIGN_TOP_LEFT, 12, 8);

    /* X / Y / home jog pad — clean 3x3 cross with gaps so outlines never touch */
    lv_obj_t *byp  = make_button(jog_card, LV_SYMBOL_UP   " Y+", on_jog_clicked, "G1 Y10 F3000",  NULL);
    lv_obj_t *bxm  = make_button(jog_card, LV_SYMBOL_LEFT " X-", on_jog_clicked, "G1 X-10 F3000", NULL);
    lv_obj_t *home = make_button(jog_card, LV_SYMBOL_HOME,       on_jog_clicked, "G28",           NULL);
    lv_obj_t *bxp  = make_button(jog_card, LV_SYMBOL_RIGHT " X+",on_jog_clicked, "G1 X10 F3000",  NULL);
    lv_obj_t *bym  = make_button(jog_card, LV_SYMBOL_DOWN " Y-", on_jog_clicked, "G1 Y-10 F3000", NULL);
    lv_obj_set_size(byp, 76, 52); lv_obj_set_size(bxm, 76, 52); lv_obj_set_size(home, 76, 52);
    lv_obj_set_size(bxp, 76, 52); lv_obj_set_size(bym, 76, 52);
    lv_obj_align(byp,  LV_ALIGN_TOP_LEFT, 152, 40);
    lv_obj_align(bxm,  LV_ALIGN_TOP_LEFT, 66,  96);
    lv_obj_align(home, LV_ALIGN_TOP_LEFT, 152, 96);
    lv_obj_align(bxp,  LV_ALIGN_TOP_LEFT, 238, 96);
    lv_obj_align(bym,  LV_ALIGN_TOP_LEFT, 152, 152);

    /* Z controls */
    lv_obj_t *bzp = make_button(s_scr_control, "Z+ 10", on_jog_clicked, "G1 Z10 F600", NULL);
    lv_obj_t *bzm = make_button(s_scr_control, "Z- 10", on_jog_clicked, "G1 Z-10 F600", NULL);
    lv_obj_set_size(bzp, 120, 50); lv_obj_set_size(bzm, 120, 50);
    lv_obj_align(bzp, LV_ALIGN_TOP_LEFT, 16, 260);
    lv_obj_align(bzm, LV_ALIGN_TOP_LEFT, 150, 260);
}

static void build_about_screen(void)
{
    s_scr_about = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_about, PP_BG, 0);
    lv_obj_clear_flag(s_scr_about, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bar = make_header(s_scr_about, "About / License");

    lv_obj_t *back = make_barbtn(bar, LV_SYMBOL_LEFT " Back", on_about_back, NULL, 100);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -8, 0);

    /* ---- left column: product + license text ---- */
    lv_obj_t *title = lv_label_create(s_scr_about);
    lv_label_set_text(title, "Prusa Touch");
    lv_obj_set_style_text_color(title, PP_ORANGE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 74);

    lv_obj_t *by = lv_label_create(s_scr_about);
    lv_label_set_text(by, "by NomadsGalaxy");
    lv_obj_set_style_text_color(by, PP_TEXT, 0);
    lv_obj_set_style_text_font(by, &lv_font_montserrat_16, 0);
    lv_obj_align(by, LV_ALIGN_TOP_LEFT, 16, 108);

    lv_obj_t *body = lv_label_create(s_scr_about);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 470);
    lv_label_set_text(body,
        "Firmware " PP_FW_VERSION "\n"
        "Open-firmware touchscreen for Prusa printers (PrusaLink).\n\n"
        "License: OCL v1.1 + SWAtt v1\n"
        "Built on PandaTouch_IDF (BigTreeTech, MIT).\n\n"
        "Independent community project - not affiliated with\n"
        "or endorsed by Prusa Research. \"Prusa\" and \"Prusa\n"
        "Connect\" are trademarks of Prusa Research.");
    lv_obj_set_style_text_color(body, PP_TEXT_MUTED, 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 16, 140);

    /* ---- right column: GitHub QR + URL ---- */
    static const char *repo = "https://github.com/nomadsgalaxy/Prusa-Connect-Touch";
    lv_obj_t *qr = lv_qrcode_create(s_scr_about);
    lv_qrcode_set_size(qr, 170);
    lv_qrcode_set_dark_color(qr, PP_BLACK);
    lv_qrcode_set_light_color(qr, PP_WHITE);
    lv_qrcode_update(qr, repo, strlen(repo));
    lv_obj_set_style_border_color(qr, PP_WHITE, 0);
    lv_obj_set_style_border_width(qr, 6, 0);      /* white quiet-zone border */
    lv_obj_align(qr, LV_ALIGN_TOP_RIGHT, -70, 110);

    lv_obj_t *qcap = lv_label_create(s_scr_about);
    lv_label_set_text(qcap, "Scan for the project on GitHub");
    lv_obj_set_style_text_color(qcap, PP_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(qcap, &lv_font_montserrat_14, 0);
    lv_obj_align(qcap, LV_ALIGN_TOP_RIGHT, -40, 292);

    lv_obj_t *url = lv_label_create(s_scr_about);
    lv_label_set_text(url, "github.com/nomadsgalaxy/\nPrusa-Connect-Touch");
    lv_obj_set_style_text_color(url, PP_TEXT, 0);
    lv_obj_set_style_text_font(url, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(url, LV_ALIGN_TOP_RIGHT, -55, 314);
}

void ui_init(void)
{
    card_thumbs_clear();
    build_dashboard_screen();
    build_status_screen();
    build_files_screen();
    build_filedetail_screen();
    build_printers_screen();
    build_addform_screen();
    build_control_screen();
    build_wifi_screen();
    build_about_screen();
    /* persistent bottom nav on the primary screens */
    make_nav(s_scr_dash, 0);
    make_nav(s_scr_status, 1);
    make_nav(s_scr_files, 2);
    make_nav(s_scr_printers, 3);
    lv_screen_load(s_scr_dash);   /* fleet dashboard is home */
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
        else if (!strcmp(name, "control"))                            lv_screen_load(s_scr_control);
        else if (!strcmp(name, "files"))  { app_state_post_cmd(PP_CMD_LIST, NULL); lv_screen_load(s_scr_files); }
        else if (!strcmp(name, "printers") || !strcmp(name, "settings")) { refresh_printers_list(); lv_screen_load(s_scr_printers); }
        else if (!strcmp(name, "about"))                              lv_screen_load(s_scr_about);
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
    if (s == s_scr_control)    return "control";
    if (s == s_scr_files)      return "files";
    if (s == s_scr_filedetail) return "filedetail";
    if (s == s_scr_printers)   return "printers";
    if (s == s_scr_addform)    return "addform";
    if (s == s_scr_about)      return "about";
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

    /* hero: model render on the orange tile, scaled to fill */
    const lv_image_dsc_t *mimg = model_image(s->model);
    if (mimg) {
        lv_image_set_src(s_detail_img, mimg);
        lv_image_set_scale(s_detail_img, 384);     /* 48px asset -> ~72px on tile */
        lv_obj_remove_flag(s_detail_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_detail_img, LV_OBJ_FLAG_HIDDEN);
    }

    /* hero: state badge (muted tint + white text) + model sub-line */
    lv_obj_set_style_bg_color(s_badge, s->online ? pp_state_badge(s->state) : PP_BADGE_GRAY, 0);
    lv_label_set_text(s_state_lbl, s->online ? (s->state[0] ? s->state : "READY") : "OFFLINE");
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
        lv_label_set_text(s_job_lbl, s->job_name[0] ? s->job_name : "(printing)");
        lv_bar_set_value(s_bar, (int)(s->progress + 0.5f), LV_ANIM_ON);
        snprintf(buf, sizeof(buf), "%d%%", (int)(s->progress + 0.5f));
        lv_label_set_text(s_pct_lbl, buf);
        char eta[24];
        fmt_eta(s->time_remaining, eta, sizeof(eta));
        snprintf(buf, sizeof(buf), "ETA %s", eta);
        lv_label_set_text(s_eta_lbl, buf);
    } else {
        lv_label_set_text(s_job_lbl, s->online ? "No active print" : "");
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(s_pct_lbl, "");
        lv_label_set_text(s_eta_lbl, "");
    }

    /* Pause button reflects the paused/printing state. */
    bool paused = (strcmp(s->state, "PAUSED") == 0);
    lv_label_set_text(s_btn_pause_lbl, paused ? "RESUME" : "PAUSE");

    /* CONTROL button visibility based on capability probe. */
    if (s->has_control) lv_obj_remove_flag(s_btn_control, LV_OBJ_FLAG_HIDDEN);
    else                lv_obj_add_flag(s_btn_control, LV_OBJ_FLAG_HIDDEN);

    free(s);
}

void ui_apply_files(void *arg)
{
    pp_file_list_t *list = (pp_file_list_t *)arg;

    lv_obj_clean(s_file_list);
    s_file_count = 0;

    /* Refresh the printer-context banner (these files belong to the active printer). */
    if (s_active_printer[0] && s_active_model[0]) {
        lv_label_set_text_fmt(s_files_banner, "Files on  %s   -   %s",
                              s_active_printer, s_active_model);
    } else if (s_active_printer[0]) {
        lv_label_set_text_fmt(s_files_banner, "Files on  %s", s_active_printer);
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
        lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text(nm, list->items[i].display);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_width(nm, 700);
        lv_obj_set_style_text_color(nm, PP_TEXT, 0);
        lv_obj_set_style_text_font(nm, &lv_font_montserrat_16, 0);
        lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 36, 0);

        if (list->items[i].meta[0]) {
            lv_obj_t *mt = lv_label_create(row);
            lv_label_set_text(mt, list->items[i].meta);
            lv_obj_set_style_text_color(mt, PP_TEXT_MUTED, 0);
            lv_obj_set_style_text_font(mt, &lv_font_montserrat_12, 0);
            lv_obj_align(mt, LV_ALIGN_BOTTOM_LEFT, 36, 0);
        }
        s_file_count++;
    }
    if (s_file_count == 0) {
        lv_obj_t *empty = lv_label_create(s_file_list);
        lv_label_set_text(empty, "No printable files on this printer");
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
        lv_label_set_text(s_thumb_ph, "Preview unavailable");
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
