/* Host-simulator stubs: satisfy every non-LVGL symbol ui.c references with a no-op or a
 * mock value, plus a small in-memory printer store, so the real ui.c screens can be built
 * and rendered on the desktop. Layout-only — no network/NVS/hardware. */
#include "pandaprusa.h"
#include "ui.h"
#include "app_state.h"
#include "printer_store.h"
#include "wifi.h"
#include "prefs.h"
#include "pandatouch_display.h"
#include <string.h>
#include <stddef.h>

/* glibc < 2.38 (Ubuntu 22.04 = 2.35) has no strlcpy/strlcat; newlib on device does. Provide them. */
size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t sl = strlen(src);
    if (size) { size_t n = (sl >= size) ? size - 1 : sl; memcpy(dst, src, n); dst[n] = '\0'; }
    return sl;
}
size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dl = strnlen(dst, size), sl = strlen(src);
    if (dl == size) return size + sl;
    size_t n = (sl < size - dl) ? sl : size - dl - 1;
    memcpy(dst + dl, src, n); dst[dl + n] = '\0';
    return dl + sl;
}

/* ---------- mock printer store ---------- */
static pp_printer_t s_mock[] = {
    { .name="Apollo",          .host="cloud:apollo",  .port=80, .uuid="apollo",  .local_host="192.168.0.92" },
    { .name="Artemis",         .host="cloud:artemis", .port=80, .uuid="artemis", .local_host="192.168.0.49" },
    { .name="Mini - Fermi",    .host="cloud:fermi",   .port=80, .uuid="fermi",   .local_host="192.168.0.149" },
    { .name="Mini - Pulsar",   .host="cloud:pulsar",  .port=80, .uuid="pulsar",  .local_host="192.168.0.216" },
    { .name="Tau Ceti - Office",.host="cloud:tauceti",.port=80, .uuid="tauceti", .local_host="192.168.61.82" },
};
static const int s_mock_n = (int)(sizeof(s_mock)/sizeof(s_mock[0]));
static int s_active = 2;   /* Fermi — the ATTENTION printer, so the detail screen shows the banner */

int  printer_store_count(void) { return s_mock_n; }
int  printer_store_active(void) { return s_active; }
bool printer_store_get(int idx, pp_printer_t *out) {
    if (idx < 0 || idx >= s_mock_n || !out) return false;
    *out = s_mock[idx]; return true;
}
int  printer_store_add(const pp_printer_t *p) { (void)p; return 0; }
bool printer_store_update(int idx, const pp_printer_t *p) { (void)idx; (void)p; return true; }
void printer_store_remove(int idx) { (void)idx; }

/* sim hook: choose which printer the detail screen shows */
void sim_set_active(int idx) { if (idx >= 0 && idx < s_mock_n) s_active = idx; }

/* ---------- app_state (no backend in sim) ---------- */
void app_state_post_cmd(pp_cmd_kind_t k, const char *p) { (void)k; (void)p; }
void app_state_post_cmd_n(pp_cmd_kind_t k, int i, int a, int b) { (void)k; (void)i; (void)a; (void)b; }
void app_state_post_cmd_ex(pp_cmd_kind_t k, const char *p, int i, int a, int b) { (void)k; (void)p; (void)i; (void)a; (void)b; }
void app_state_dialog_action(int id, const char *btn) { (void)id; (void)btn; }
void app_state_select_printer(int i) { (void)i; }
void app_state_refresh_dashboard(void) {}
void app_state_farm_refresh(void) {}
void app_state_fetch_snapshot(void) {}
void app_state_printers_changed(void) {}
void app_state_wifi_scan(void) {}
void app_state_wifi_connect(const char *s, const char *p) { (void)s; (void)p; }
void app_state_fetch_thumb(const char *r) { (void)r; }
void app_state_fetch_thumb_dash(const char *r, int i) { (void)r; (void)i; }
void app_state_set_pref(pp_pref_kind_t pref, int value) { (void)pref; (void)value; }
void app_state_store_add(const pp_printer_t *p) { (void)p; }
void app_state_store_update(int idx, const pp_printer_t *p) { (void)idx; (void)p; }
void app_state_store_remove(int idx) { (void)idx; }

/* ---------- wifi ---------- */
bool wifi_is_connected(void) { return true; }
bool wifi_is_ap_active(void) { return false; }
const char *wifi_ap_ssid(void) { return "PrusaTouch-SIM"; }
const char *wifi_ip_str(void)  { return "192.168.1.123"; }   /* mock display value */

/* ---------- prefs ---------- */
/* Orientation is driven by the display dimensions the sim creates (see sim_main), so the UI's
 * own rotation hook stays a no-op here — always report LANDSCAPE. ui_portrait() keys off res. */
pp_sort_t   prefs_sort(void)        { return PP_SORT_STATUS; }
bool        prefs_hide_offline(void){ return false; }
pp_logo_t   prefs_logo(void)        { return PP_LOGO_STACKED; }
pp_orient_t prefs_orient(void)      { return PP_ORIENT_LANDSCAPE; }
bool        prefs_auto_update(void) { return false; }
uint8_t     prefs_dim_min(void)     { return 0; }
/* Security opt-ins: sim hooks let sim_main force the lock overlay for a layout render. */
static uint8_t s_sim_lockmin = 0;
static char    s_sim_pin[12] = "";
uint8_t     prefs_lock_min(void)    { return s_sim_lockmin; }
const char *prefs_scrpin(void)      { return s_sim_pin; }
const char *prefs_web_pass(void)    { return ""; }
void        sim_set_lock(const char *pin, uint8_t minutes) {
    if (pin) { size_t i=0; for (; pin[i] && i<sizeof(s_sim_pin)-1; i++) s_sim_pin[i]=pin[i]; s_sim_pin[i]='\0'; }
    s_sim_lockmin = minutes;
}

/* ---------- display marshal: run inline (single-threaded host) ---------- */
lv_result_t pt_display_schedule_ui(pt_ui_fn_t fn, void *arg) {
    if (fn) fn(arg);
    return LV_RESULT_OK;
}
esp_lcd_panel_handle_t pt_get_panel(void) { return 0; }
void pt_lvgl_lock(void) {}      /* single-threaded host: no LVGL mutex needed */
void pt_lvgl_unlock(void) {}
