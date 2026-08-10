/* Prusa-Touch — UI skins (issue #6). See skin.h. */
#include "skin.h"
#include "pandaprusa_theme.h"
#include "inter_fonts.h"   /* Inter (Medium) LVGL fonts — the alternate typeface */
#include "i18n.h"          /* i18n_lang — non-English needs the full-Latin Inter fonts */
#include <string.h>
#ifndef PP_HOST_SIM
#include "nvs.h"        /* ESP-IDF only; the desktop sim renders skins without persistence */
#endif

#define NS "ppskin"

/* Active font set for the current skin (PP_F12..PP_F40 read these). Default to Montserrat so they
 * are valid even before skin_apply_index() runs. */
const lv_font_t *g_f12 = &lv_font_montserrat_12, *g_f14 = &lv_font_montserrat_14,
                *g_f16 = &lv_font_montserrat_16, *g_f20 = &lv_font_montserrat_20,
                *g_f28 = &lv_font_montserrat_28, *g_f40 = &lv_font_montserrat_40;

static void set_fonts(uint8_t fam)   /* 0 = Montserrat (LVGL built-in), 1 = Inter */
{
    /* The LVGL built-in Montserrat fonts only carry ASCII; the Inter fonts carry the full
     * Latin range. For any non-English UI, force Inter so accented glyphs render (not boxes). */
    if (fam == 1 || i18n_lang() != LANG_EN) {
        g_f12 = &inter_12; g_f14 = &inter_14; g_f16 = &inter_16;
        g_f20 = &inter_20; g_f28 = &inter_28; g_f40 = &inter_40;
    } else {
        g_f12 = &lv_font_montserrat_12; g_f14 = &lv_font_montserrat_14; g_f16 = &lv_font_montserrat_16;
        g_f20 = &lv_font_montserrat_20; g_f28 = &lv_font_montserrat_28; g_f40 = &lv_font_montserrat_40;
    }
}

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
    .temp_cold=LV_COLOR_MAKE(0x00,0x72,0xFF), .temp_hot=LV_COLOR_MAKE(0xFF,0x00,0x00), \
    .font_family=0, .brand="KLIPPER | TOUCH", .byline="Klipper first" }

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
    .temp_cold=LV_COLOR_MAKE(0x2E,0x96,0xFF), .temp_hot=LV_COLOR_MAKE(0xFF,0x4E,0x3A), \
    .font_family=0, .brand="KLIPPER | TOUCH", .byline="Klipper first" }

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
    .temp_cold=LV_COLOR_MAKE(0x81,0xA1,0xC1), .temp_hot=LV_COLOR_MAKE(0xBF,0x61,0x6A), \
    .font_family=1, .brand="KLIPPER | TOUCH", .byline="Klipper first" }   /* Nord showcases the Inter font */

static const pp_skin_t PRESETS[]    = { SKIN_CONNECT, SKIN_STARGATE, SKIN_NORD };
static const char     *PRESET_NAME[] = { "Default", "Stargate", "Nord" };
#define SKIN_N ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

/* The live palette. Defaults to Connect so it's valid even before skin_init() runs. */
pp_skin_t g_skin = SKIN_CONNECT;
static pp_skin_t s_custom = SKIN_CONNECT;   /* user's custom palette (== Connect until set) */
static int s_current = 0;
#define CUSTOM_IDX SKIN_N                    /* the Custom slot sits right after the presets */

/* Canonical token order for the packed 57-byte (19 x RGB) custom palette — must match the web
 * editor's POST body and the GET response. */
const char *const SKIN_TOKENS[19] = {
    "orange","orange_dark","bg","header","surface","surface_hi","border",
    "text","text_muted","text_inverse",
    "state_green","state_olive","state_gray","state_orange","state_blue","state_yellow","state_red",
    "temp_cold","temp_hot"
};

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

/* Pack a skin's 19 primaries to R,G,B bytes (SKIN_TOKENS order); unpack does the reverse. */
static void pack_palette(const pp_skin_t *s, uint8_t o[57])
{
    int i = 0;
    #define PK(f) o[i++] = (s->f).red; o[i++] = (s->f).green; o[i++] = (s->f).blue;
    PK(orange) PK(orange_dark) PK(bg) PK(header) PK(surface) PK(surface_hi) PK(border)
    PK(text) PK(text_muted) PK(text_inverse)
    PK(state_green) PK(state_olive) PK(state_gray) PK(state_orange) PK(state_blue) PK(state_yellow) PK(state_red)
    PK(temp_cold) PK(temp_hot)
    #undef PK
}
static void unpack_palette(pp_skin_t *s, const uint8_t o[57])
{
    int i = 0;
    #define UP(f) (s->f).red = o[i]; (s->f).green = o[i+1]; (s->f).blue = o[i+2]; i += 3;
    UP(orange) UP(orange_dark) UP(bg) UP(header) UP(surface) UP(surface_hi) UP(border)
    UP(text) UP(text_muted) UP(text_inverse)
    UP(state_green) UP(state_olive) UP(state_gray) UP(state_orange) UP(state_blue) UP(state_yellow) UP(state_red)
    UP(temp_cold) UP(temp_hot)
    #undef UP
}

void skin_apply_index(int idx)   /* set g_skin to a preset/custom (no NVS) — boot, sim, preview */
{
    if (idx < 0 || idx > CUSTOM_IDX) idx = 0;
    s_current = idx;
    g_skin = (idx == CUSTOM_IDX) ? s_custom : PRESETS[idx];
    skin_compute_tints(&g_skin);
    set_fonts(g_skin.font_family);
}

void skin_init(void)
{
    int idx = 0;
#ifndef PP_HOST_SIM
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "idx", &v) == ESP_OK && v <= CUSTOM_IDX) idx = v;
        uint8_t blob[106]; size_t sz = sizeof(blob);   /* custom palette (+ font/brand/byline) if any */
        if (nvs_get_blob(h, "custom", blob, &sz) == ESP_OK && sz >= 57) {
            unpack_palette(&s_custom, blob);
            if (sz >= 58)  s_custom.font_family = blob[57] ? 1 : 0;   /* newer blobs carry typography */
            if (sz >= 82)  strlcpy(s_custom.brand,  (char *)blob + 58, sizeof(s_custom.brand));
            if (sz >= 106) strlcpy(s_custom.byline, (char *)blob + 82, sizeof(s_custom.byline));
        }
        nvs_close(h);
    }
#endif
    skin_apply_index(idx);
}

int         skin_count(void)        { return SKIN_N + 1; }   /* presets + Custom */
const char *skin_name(int idx)      { return idx == CUSTOM_IDX ? "Custom"
                                            : (idx >= 0 && idx < SKIN_N ? PRESET_NAME[idx] : ""); }
int         skin_current(void)      { return s_current; }
int         skin_custom_index(void) { return CUSTOM_IDX; }
uint8_t     skin_font(void)         { return g_skin.font_family; }
const char *skin_brand(void)        { return g_skin.brand; }
const char *skin_byline(void)       { return g_skin.byline; }

/* Per-index typography/wordmark — so the web editor can list every built-in preset's palette. */
uint8_t     skin_font_of(int idx)   { return idx == CUSTOM_IDX ? s_custom.font_family
                                            : (idx >= 0 && idx < SKIN_N ? PRESETS[idx].font_family : g_skin.font_family); }
const char *skin_brand_of(int idx)  { return idx == CUSTOM_IDX ? s_custom.brand
                                            : (idx >= 0 && idx < SKIN_N ? PRESETS[idx].brand : g_skin.brand); }
const char *skin_byline_of(int idx) { return idx == CUSTOM_IDX ? s_custom.byline
                                            : (idx >= 0 && idx < SKIN_N ? PRESETS[idx].byline : g_skin.byline); }

void skin_palette_rgb(int idx, uint8_t out[57])
{
    if (idx == CUSTOM_IDX)             pack_palette(&s_custom, out);
    else if (idx >= 0 && idx < SKIN_N) pack_palette(&PRESETS[idx], out);
    else                               pack_palette(&g_skin, out);
}

void skin_persist_index(int idx)    /* net task only (NVS write) — caller reboots to apply */
{
    if (idx < 0 || idx > CUSTOM_IDX) return;
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

void skin_set_custom(const uint8_t rgb[57], uint8_t font, const char *brand, const char *byline)
{
    unpack_palette(&s_custom, rgb);
    s_custom.font_family = font ? 1 : 0;
    strlcpy(s_custom.brand,  (brand && brand[0]) ? brand : "PRUSA | TOUCH", sizeof(s_custom.brand));
    strlcpy(s_custom.byline, byline ? byline : "", sizeof(s_custom.byline));
    skin_compute_tints(&s_custom);
    s_current = CUSTOM_IDX;
    g_skin = s_custom;
    set_fonts(s_custom.font_family);
#ifndef PP_HOST_SIM
    uint8_t blob[106];
    memcpy(blob, rgb, 57);
    blob[57] = s_custom.font_family;
    memset(blob + 58, 0, 48);
    strlcpy((char *)blob + 58, s_custom.brand,  24);
    strlcpy((char *)blob + 82, s_custom.byline, 24);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "custom", blob, sizeof(blob));
        nvs_commit(h);
        nvs_close(h);
    }
#endif
}
