/* Prusa-Touch — user preferences, persisted in NVS. See prefs.h. */
#include "prefs.h"
#include "nvs.h"
#include <string.h>

#define NS "ppprefs"

static pp_sort_t s_sort = PP_SORT_STATUS;
static bool      s_hide_offline = false;
static pp_logo_t s_logo = PP_LOGO_STACKED;
static bool      s_auto_update = false;   /* opt-in: off by default */
static pp_orient_t s_orient = PP_ORIENT_LANDSCAPE;
static uint8_t   s_lang = 0;              /* UI language index (0 = English) */
/* Security opt-ins (all off/empty by default). */
static uint8_t   s_lock_min = 0;          /* auto-lock the screen after N idle minutes (0 = off) */
static char      s_scrpin[12] = "";       /* PIN to unlock on-device actions while locked        */
static char      s_webpw[40]  = "";       /* password gating the web interface (Basic auth)      */
static uint8_t   s_reboot_hour = 0xFF;    /* daily maintenance reboot at this local hour (0xFF=off) */
static int8_t    s_tz_offset = 0;         /* device UTC offset (hours) for the reboot hour          */

static void save_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void save_str(const char *key, const char *v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        if (v && v[0]) nvs_set_str(h, key, v);
        else           nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }
}

void prefs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v;
    if (nvs_get_u8(h, "sort", &v) == ESP_OK && v < PP_SORT_COUNT) s_sort = (pp_sort_t)v;
    if (nvs_get_u8(h, "hideoff", &v) == ESP_OK) s_hide_offline = (v != 0);
    if (nvs_get_u8(h, "logo", &v) == ESP_OK && v <= PP_LOGO_SINGLE) s_logo = (pp_logo_t)v;
    if (nvs_get_u8(h, "autoupd", &v) == ESP_OK) s_auto_update = (v != 0);
    if (nvs_get_u8(h, "orient", &v) == ESP_OK && v <= PP_ORIENT_PORTRAIT_FLIPPED) s_orient = (pp_orient_t)v;
    if (nvs_get_u8(h, "lang", &v) == ESP_OK) s_lang = v;
    if (nvs_get_u8(h, "lockmin", &v) == ESP_OK) s_lock_min = v;
    if (nvs_get_u8(h, "rbthr", &v) == ESP_OK) s_reboot_hour = v;
    if (nvs_get_u8(h, "tzoff", &v) == ESP_OK) s_tz_offset = (int8_t)v;
    size_t sz = sizeof(s_scrpin); nvs_get_str(h, "scrpin", s_scrpin, &sz);
    sz = sizeof(s_webpw); nvs_get_str(h, "webpw", s_webpw, &sz);
    nvs_close(h);
}

pp_sort_t prefs_sort(void) { return s_sort; }
void prefs_set_sort(pp_sort_t s) { if (s < PP_SORT_COUNT) { s_sort = s; save_u8("sort", (uint8_t)s); } }

bool prefs_hide_offline(void) { return s_hide_offline; }
void prefs_set_hide_offline(bool v) { s_hide_offline = v; save_u8("hideoff", v ? 1 : 0); }

pp_logo_t prefs_logo(void) { return s_logo; }
void prefs_set_logo(pp_logo_t l) { s_logo = l; save_u8("logo", (uint8_t)l); }

bool prefs_auto_update(void) { return s_auto_update; }
void prefs_set_auto_update(bool v) { s_auto_update = v; save_u8("autoupd", v ? 1 : 0); }

pp_orient_t prefs_orient(void) { return s_orient; }
void prefs_set_orient(pp_orient_t o) { if (o <= PP_ORIENT_PORTRAIT_FLIPPED) { s_orient = o; save_u8("orient", (uint8_t)o); } }

uint8_t prefs_lang(void) { return s_lang; }
void prefs_set_lang(uint8_t l) { s_lang = l; save_u8("lang", l); }

/* --- security opt-ins --- */
uint8_t prefs_lock_min(void) { return s_lock_min; }
void prefs_set_lock_min(uint8_t m) { s_lock_min = m; save_u8("lockmin", m); }

const char *prefs_scrpin(void) { return s_scrpin; }
void prefs_set_scrpin(const char *p) { strlcpy(s_scrpin, p ? p : "", sizeof(s_scrpin)); save_str("scrpin", s_scrpin); }

const char *prefs_web_pass(void) { return s_webpw; }
void prefs_set_web_pass(const char *p) { strlcpy(s_webpw, p ? p : "", sizeof(s_webpw)); save_str("webpw", s_webpw); }

uint8_t prefs_reboot_hour(void) { return s_reboot_hour; }
void prefs_set_reboot_hour(uint8_t h) { s_reboot_hour = (h <= 23) ? h : 0xFF; save_u8("rbthr", s_reboot_hour); }

int8_t prefs_tz_offset(void) { return s_tz_offset; }
void prefs_set_tz_offset(int8_t hrs) { if (hrs < -12) hrs = -12; if (hrs > 14) hrs = 14; s_tz_offset = hrs; save_u8("tzoff", (uint8_t)hrs); }
