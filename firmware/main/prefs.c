/* Prusa-Touch — user preferences, persisted in NVS. See prefs.h. */
#include "prefs.h"
#include "nvs.h"

#define NS "ppprefs"

static pp_sort_t s_sort = PP_SORT_STATUS;
static bool      s_hide_offline = false;
static pp_logo_t s_logo = PP_LOGO_STACKED;

static void save_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, key, v);
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
    nvs_close(h);
}

pp_sort_t prefs_sort(void) { return s_sort; }
void prefs_set_sort(pp_sort_t s) { if (s < PP_SORT_COUNT) { s_sort = s; save_u8("sort", (uint8_t)s); } }

bool prefs_hide_offline(void) { return s_hide_offline; }
void prefs_set_hide_offline(bool v) { s_hide_offline = v; save_u8("hideoff", v ? 1 : 0); }

pp_logo_t prefs_logo(void) { return s_logo; }
void prefs_set_logo(pp_logo_t l) { s_logo = l; save_u8("logo", (uint8_t)l); }
