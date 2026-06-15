/* Prusa-Touch — UI skins (issue #6). See skin.h. */
#include "skin.h"
#include "pandaprusa_theme.h"
#ifndef PP_HOST_SIM
#include "nvs.h"        /* ESP-IDF only; the desktop sim renders skins without persistence */
#endif

#define NS "ppskin"

/* Each preset authors only the 19 "primary" colors; the 14 state badge/strip tints are derived
 * (skin_compute_tints). LV_COLOR_MAKE is a compile-time lv_color_t aggregate, so these are const. */

#define SKIN_CONNECT { \
    .orange=LV_COLOR_MAKE(0xFA,0x68,0x31), .orange_dark=LV_COLOR_MAKE(0x9C,0x40,0x1E), \
    .bg=LV_COLOR_MAKE(0x1C,0x1E,0x21), .header=LV_COLOR_MAKE(0x11,0x13,0x16), \
    .surface=LV_COLOR_MAKE(0x2A,0x2A,0x2A), .surface_hi=LV_COLOR_MAKE(0x4E,0x4E,0x4E), \
    .border=LV_COLOR_MAKE(0x4E,0x4E,0x4E), \
    .text=LV_COLOR_MAKE(0xFF,0xFF,0xFF), .text_muted=LV_COLOR_MAKE(0xA7,0xA7,0xA7), \
    .text_inverse=LV_COLOR_MAKE(0x21,0x25,0x29), \
    .state_green=LV_COLOR_MAKE(0xA1,0xEA,0x70), .state_olive=LV_COLOR_MAKE(0x92,0xC7,0x8C), \
    .state_gray=LV_COLOR_MAKE(0xAD,0xAD,0xAD), .state_orange=LV_COLOR_MAKE(0xF5,0x9C,0x66), \
    .state_blue=LV_COLOR_MAKE(0x7D,0xA7,0xD9), .state_yellow=LV_COLOR_MAKE(0xFD,0xDC,0x71), \
    .state_red=LV_COLOR_MAKE(0xF8,0x79,0x5F), \
    .temp_cold=LV_COLOR_MAKE(0x00,0x72,0xFF), .temp_hot=LV_COLOR_MAKE(0xFF,0x00,0x00) }

/* "Stargate" — deep-space blue-black + DHD amber, event-horizon blue. Dark, so white text reads. */
#define SKIN_STARGATE { \
    .orange=LV_COLOR_MAKE(0xC7,0x7B,0x2A), .orange_dark=LV_COLOR_MAKE(0x7A,0x4A,0x18), \
    .bg=LV_COLOR_MAKE(0x0B,0x0F,0x1A), .header=LV_COLOR_MAKE(0x05,0x07,0x0D), \
    .surface=LV_COLOR_MAKE(0x13,0x1C,0x2E), .surface_hi=LV_COLOR_MAKE(0x2B,0x3B,0x57), \
    .border=LV_COLOR_MAKE(0x2B,0x3B,0x57), \
    .text=LV_COLOR_MAKE(0xE9,0xE2,0xD0), .text_muted=LV_COLOR_MAKE(0x85,0x95,0xAD), \
    .text_inverse=LV_COLOR_MAKE(0x0B,0x0F,0x1A), \
    .state_green=LV_COLOR_MAKE(0x84,0xC9,0x8A), .state_olive=LV_COLOR_MAKE(0x9B,0xBF,0x8C), \
    .state_gray=LV_COLOR_MAKE(0x85,0x95,0xAD), .state_orange=LV_COLOR_MAKE(0xC7,0x7B,0x2A), \
    .state_blue=LV_COLOR_MAKE(0x4E,0x9F,0xD6), .state_yellow=LV_COLOR_MAKE(0xD9,0xB8,0x4A), \
    .state_red=LV_COLOR_MAKE(0xD9,0x60,0x5A), \
    .temp_cold=LV_COLOR_MAKE(0x2E,0x96,0xFF), .temp_hot=LV_COLOR_MAKE(0xFF,0x4E,0x3A) }

/* "Nord" — the popular polar-night dark palette with a frost-blue accent. */
#define SKIN_NORD { \
    .orange=LV_COLOR_MAKE(0x5E,0x81,0xAC), .orange_dark=LV_COLOR_MAKE(0x3B,0x54,0x78), \
    .bg=LV_COLOR_MAKE(0x2E,0x34,0x40), .header=LV_COLOR_MAKE(0x24,0x29,0x33), \
    .surface=LV_COLOR_MAKE(0x3B,0x42,0x52), .surface_hi=LV_COLOR_MAKE(0x4C,0x56,0x6A), \
    .border=LV_COLOR_MAKE(0x4C,0x56,0x6A), \
    .text=LV_COLOR_MAKE(0xEC,0xEF,0xF4), .text_muted=LV_COLOR_MAKE(0x9A,0xA4,0xB8), \
    .text_inverse=LV_COLOR_MAKE(0x2E,0x34,0x40), \
    .state_green=LV_COLOR_MAKE(0xA3,0xBE,0x8C), .state_olive=LV_COLOR_MAKE(0x8F,0xBC,0xBB), \
    .state_gray=LV_COLOR_MAKE(0x9A,0xA4,0xB8), .state_orange=LV_COLOR_MAKE(0xD0,0x87,0x70), \
    .state_blue=LV_COLOR_MAKE(0x81,0xA1,0xC1), .state_yellow=LV_COLOR_MAKE(0xEB,0xCB,0x8B), \
    .state_red=LV_COLOR_MAKE(0xBF,0x61,0x6A), \
    .temp_cold=LV_COLOR_MAKE(0x81,0xA1,0xC1), .temp_hot=LV_COLOR_MAKE(0xBF,0x61,0x6A) }

static const pp_skin_t PRESETS[]    = { SKIN_CONNECT, SKIN_STARGATE, SKIN_NORD };
static const char     *PRESET_NAME[] = { "Connect (default)", "Stargate", "Nord" };
#define SKIN_N ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

/* The live palette. Defaults to Connect so it's valid even before skin_init() runs. */
pp_skin_t g_skin = SKIN_CONNECT;
static int s_current = 0;

/* Derive each state's badge (21% of the state color over the surface) and name-strip (15% over
 * the page background) — the exact blend measured from Connect's dark UI, now applied to any skin. */
static void skin_compute_tints(pp_skin_t *s)
{
    s->badge_green  = lv_color_mix(s->state_green,  s->surface, 54);  s->strip_green  = lv_color_mix(s->state_green,  s->bg, 38);
    s->badge_olive  = lv_color_mix(s->state_olive,  s->surface, 54);  s->strip_olive  = lv_color_mix(s->state_olive,  s->bg, 38);
    s->badge_gray   = lv_color_mix(s->state_gray,   s->surface, 54);  s->strip_gray   = lv_color_mix(s->state_gray,   s->bg, 38);
    s->badge_orange = lv_color_mix(s->state_orange, s->surface, 54);  s->strip_orange = lv_color_mix(s->state_orange, s->bg, 38);
    s->badge_blue   = lv_color_mix(s->state_blue,   s->surface, 54);  s->strip_blue   = lv_color_mix(s->state_blue,   s->bg, 38);
    s->badge_yellow = lv_color_mix(s->state_yellow, s->surface, 54);  s->strip_yellow = lv_color_mix(s->state_yellow, s->bg, 38);
    s->badge_red    = lv_color_mix(s->state_red,    s->surface, 54);  s->strip_red    = lv_color_mix(s->state_red,    s->bg, 38);
}

void skin_apply_index(int idx)   /* set g_skin to a preset (no NVS) — boot, sim, future live preview */
{
    if (idx < 0 || idx >= SKIN_N) idx = 0;
    s_current = idx;
    g_skin = PRESETS[idx];
    skin_compute_tints(&g_skin);
}

void skin_init(void)
{
    int idx = 0;
#ifndef PP_HOST_SIM
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "idx", &v) == ESP_OK && v < SKIN_N) idx = v;
        nvs_close(h);
    }
#endif
    skin_apply_index(idx);
}

int         skin_count(void)        { return SKIN_N; }
const char *skin_name(int idx)      { return (idx >= 0 && idx < SKIN_N) ? PRESET_NAME[idx] : ""; }
int         skin_current(void)      { return s_current; }

void skin_persist_index(int idx)    /* net task only (NVS write) — caller reboots to apply */
{
    if (idx < 0 || idx >= SKIN_N) return;
    s_current = idx;
#ifndef PP_HOST_SIM
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "idx", (uint8_t)idx);
        nvs_commit(h);
        nvs_close(h);
    }
#endif
}
