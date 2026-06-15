#pragma once
/*
 * Prusa-Touch — theme tokens.
 *
 * SKINS (issue #6): the palette is now a RUNTIME struct (g_skin, see skin.c) instead of fixed
 * #defines. Every PP_* macro reads a field of g_skin, so all ~150 call sites in ui.c repaint from
 * the active skin with zero edits — switching skins is just swapping g_skin and rebuilding screens
 * (done on reboot, like an orientation change). The default skin reproduces the live Prusa Connect
 * dark UI exactly (--ui-color-primary #FA6831; surfaces/text/state palette measured from
 * connect.prusa3d.com). Built-in presets (Connect / Stargate / Nord) live in skin.c.
 */
#include "lvgl.h"

/* The live palette. One struct of lv_color_t, swapped wholesale when the skin changes.
 * The 19 "primary" colors are authored per preset; the 14 state badge/strip TINTS are derived
 * from them at apply time (skin_compute_tints): badge = 21% of the state color over the surface,
 * strip = 15% over the page background — the exact blend measured from Connect's dark UI. */
typedef struct {
    /* accent / primary */
    lv_color_t orange, orange_dark;
    /* surfaces */
    lv_color_t bg, header, surface, surface_hi, border;
    /* text */
    lv_color_t text, text_muted, text_inverse;
    /* state palette (printer/job states) */
    lv_color_t state_green, state_olive, state_gray, state_orange, state_blue, state_yellow, state_red;
    /* temperature accents */
    lv_color_t temp_cold, temp_hot;
    /* derived state tints (filled by skin_compute_tints) */
    lv_color_t badge_green, strip_green, badge_olive, strip_olive, badge_gray, strip_gray,
               badge_orange, strip_orange, badge_blue, strip_blue, badge_yellow, strip_yellow,
               badge_red, strip_red;
} pp_skin_t;

extern pp_skin_t g_skin;   /* defined in skin.c; valid from skin_init() (before screens build) */

/* Connect primary */
#define PP_ORANGE         (g_skin.orange)        /* accent / CTA / progress */
#define PP_ORANGE_DARK    (g_skin.orange_dark)

/* Dark surfaces */
#define PP_BG             (g_skin.bg)            /* page background           */
#define PP_HEADER         (g_skin.header)        /* top nav bar               */
#define PP_SURFACE        (g_skin.surface)       /* cards                     */
#define PP_SURFACE_HI     (g_skin.surface_hi)    /* borders/track/hover/raised*/
#define PP_BORDER         (g_skin.border)

/* Text */
#define PP_TEXT           (g_skin.text)
#define PP_TEXT_MUTED     (g_skin.text_muted)
#define PP_TEXT_INVERSE   (g_skin.text_inverse)

/* Connect --state-* palette (printer/job states) */
#define PP_STATE_GREEN    (g_skin.state_green)   /* finished        */
#define PP_STATE_OLIVE    (g_skin.state_olive)   /* ready           */
#define PP_STATE_GRAY     (g_skin.state_gray)    /* idle / offline  */
#define PP_STATE_ORANGE   (g_skin.state_orange)  /* printing        */
#define PP_STATE_BLUE     (g_skin.state_blue)    /* busy / preparing*/
#define PP_STATE_YELLOW   (g_skin.state_yellow)  /* paused/attention*/
#define PP_STATE_RED      (g_skin.state_red)     /* error / stopped */

/* Temperature accents */
#define PP_TEMP_COLD      (g_skin.temp_cold)
#define PP_TEMP_HOT       (g_skin.temp_hot)

/* Fixed, non-themed colors (functional: QR contrast, pure black/white) */
#define PP_BLACK          lv_color_hex(0x000000)
#define PP_WHITE          lv_color_hex(0xFFFFFF)

/* Functional aliases used by existing screens */
#define PP_GREEN          PP_STATE_GREEN
#define PP_GREY           PP_TEXT_MUTED
#define PP_OK             PP_STATE_GREEN
#define PP_WARN           PP_STATE_YELLOW
#define PP_ERROR          PP_STATE_RED

/* Dark-theme state tints (derived from the state colors per skin) */
#define PP_BADGE_GREEN    (g_skin.badge_green)
#define PP_STRIP_GREEN    (g_skin.strip_green)
#define PP_BADGE_OLIVE    (g_skin.badge_olive)
#define PP_STRIP_OLIVE    (g_skin.strip_olive)
#define PP_BADGE_GRAY     (g_skin.badge_gray)
#define PP_STRIP_GRAY     (g_skin.strip_gray)
#define PP_BADGE_ORANGE   (g_skin.badge_orange)
#define PP_STRIP_ORANGE   (g_skin.strip_orange)
#define PP_BADGE_BLUE     (g_skin.badge_blue)
#define PP_STRIP_BLUE     (g_skin.strip_blue)
#define PP_BADGE_YELLOW   (g_skin.badge_yellow)
#define PP_STRIP_YELLOW   (g_skin.strip_yellow)
#define PP_BADGE_RED      (g_skin.badge_red)
#define PP_STRIP_RED      (g_skin.strip_red)

typedef enum {
    PP_SC_GREEN, PP_SC_OLIVE, PP_SC_GRAY, PP_SC_ORANGE,
    PP_SC_BLUE, PP_SC_YELLOW, PP_SC_RED
} pp_state_class_t;

/* Classify a PrusaLink state string into a Connect color family. */
static inline pp_state_class_t pp_state_class(const char *s)
{
    if (!s) return PP_SC_GRAY;
    if (!__builtin_strcmp(s, "PRINTING"))  return PP_SC_ORANGE;
    if (!__builtin_strcmp(s, "ATTENTION")) return PP_SC_ORANGE;  /* measured: orange */
    if (!__builtin_strcmp(s, "PAUSED"))    return PP_SC_YELLOW;
    if (!__builtin_strcmp(s, "FINISHED"))  return PP_SC_GREEN;
    if (!__builtin_strcmp(s, "READY"))     return PP_SC_OLIVE;
    if (!__builtin_strcmp(s, "ERROR") || !__builtin_strcmp(s, "STOPPED")) return PP_SC_RED;
    if (!__builtin_strcmp(s, "BUSY")  || !__builtin_strcmp(s, "PREPARING")) return PP_SC_BLUE;
    return PP_SC_GRAY;   /* IDLE / unknown */
}

/* Bright state color (kept for any light-on-color use). */
static inline lv_color_t pp_state_color(const char *s)
{
    switch (pp_state_class(s)) {
    case PP_SC_GREEN:  return PP_STATE_GREEN;
    case PP_SC_OLIVE:  return PP_STATE_OLIVE;
    case PP_SC_ORANGE: return PP_STATE_ORANGE;
    case PP_SC_BLUE:   return PP_STATE_BLUE;
    case PP_SC_YELLOW: return PP_STATE_YELLOW;
    case PP_SC_RED:    return PP_STATE_RED;
    default:           return PP_STATE_GRAY;
    }
}

/* Muted badge tint (dark UI) — pair with white text. */
static inline lv_color_t pp_state_badge(const char *s)
{
    switch (pp_state_class(s)) {
    case PP_SC_GREEN:  return PP_BADGE_GREEN;
    case PP_SC_OLIVE:  return PP_BADGE_OLIVE;
    case PP_SC_ORANGE: return PP_BADGE_ORANGE;
    case PP_SC_BLUE:   return PP_BADGE_BLUE;
    case PP_SC_YELLOW: return PP_BADGE_YELLOW;
    case PP_SC_RED:    return PP_BADGE_RED;
    default:           return PP_BADGE_GRAY;
    }
}

/* Darker name-strip tint behind the card header. */
static inline lv_color_t pp_state_strip(const char *s)
{
    switch (pp_state_class(s)) {
    case PP_SC_GREEN:  return PP_STRIP_GREEN;
    case PP_SC_OLIVE:  return PP_STRIP_OLIVE;
    case PP_SC_ORANGE: return PP_STRIP_ORANGE;
    case PP_SC_BLUE:   return PP_STRIP_BLUE;
    case PP_SC_YELLOW: return PP_STRIP_YELLOW;
    case PP_SC_RED:    return PP_STRIP_RED;
    default:           return PP_STRIP_GRAY;
    }
}
