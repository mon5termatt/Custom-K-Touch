#pragma once
/*
 * Prusa-Touch — theme tokens, matched to the LIVE Prusa Connect dark UI
 * (extracted from connect.prusa3d.com CSS custom properties).
 *
 * Note on orange: Connect's UI primary is --ui-color-primary = #FA6831, which is
 * what users see in Connect — so we match it for visual fidelity. (The print Brand
 * Manual's "Prusa Orange" is #FD5000; the web UI uses #FA6831. We follow Connect.)
 */
#include "lvgl.h"

/* Connect primary */
#define PP_ORANGE         lv_color_hex(0xFA6831)   /* --ui-color-primary 250,104,49 */
#define PP_ORANGE_DARK    lv_color_hex(0x9C401E)   /* --color-primary-faded         */

/* Connect dark surfaces */
#define PP_BG             lv_color_hex(0x1C1E21)   /* --background-body  (page)     */
#define PP_HEADER         lv_color_hex(0x111316)   /* top nav bar (Connect = black) */
#define PP_SURFACE        lv_color_hex(0x2A2A2A)   /* --background-primary (cards)  */
#define PP_SURFACE_HI     lv_color_hex(0x4E4E4E)   /* borders/track/hover/raised    */
#define PP_BORDER         lv_color_hex(0x4E4E4E)   /* --neutral-500                 */

/* Text */
#define PP_TEXT           lv_color_hex(0xFFFFFF)   /* values / primary text         */
#define PP_TEXT_MUTED     lv_color_hex(0xA7A7A7)   /* --color-navigation labels     */
#define PP_TEXT_INVERSE   lv_color_hex(0x212529)   /* on light/colored backgrounds  */

/* Connect --state-* palette (printer/job states; dark text sits on these) */
#define PP_STATE_GREEN    lv_color_hex(0xA1EA70)   /* finished                      */
#define PP_STATE_OLIVE    lv_color_hex(0x92C78C)   /* ready                         */
#define PP_STATE_GRAY     lv_color_hex(0xADADAD)   /* idle / offline                */
#define PP_STATE_ORANGE   lv_color_hex(0xF59C66)   /* printing                      */
#define PP_STATE_BLUE     lv_color_hex(0x7DA7D9)   /* busy / preparing              */
#define PP_STATE_YELLOW   lv_color_hex(0xFDDC71)   /* paused / attention            */
#define PP_STATE_RED      lv_color_hex(0xF8795F)   /* error / stopped               */

/* Temperature accents (--temp-cold/--temp-hot) */
#define PP_TEMP_COLD      lv_color_hex(0x0072FF)
#define PP_TEMP_HOT       lv_color_hex(0xFF0000)

/* Functional aliases used by existing screens */
#define PP_GREEN          PP_STATE_GREEN
#define PP_GREY           PP_TEXT_MUTED
#define PP_BLACK          lv_color_hex(0x000000)
#define PP_WHITE          lv_color_hex(0xFFFFFF)
#define PP_OK             PP_STATE_GREEN
#define PP_WARN           PP_STATE_YELLOW
#define PP_ERROR          PP_STATE_RED

/* ---- Dark-theme state tints (Connect's DARK UI) ----------------------------
 * On dark cards Connect renders state as a MUTED tint of the state color with
 * WHITE text — NOT the bright pastel + dark text (that's Connect's LIGHT theme).
 * Measured live from connect.prusa3d.com: the state badge ≈ 21% of the state
 * color blended over the card (#2A2A2A); the name strip behind it ≈ 15% blended
 * over the page (#1C1E21). Precomputed per state below (matches measured RGB to
 * within rounding, e.g. FINISHED badge rgb(67,82,57), strip rgb(48,61,45)).
 */
#define PP_BADGE_GREEN    lv_color_hex(0x435239)
#define PP_STRIP_GREEN    lv_color_hex(0x303D2D)
#define PP_BADGE_OLIVE    lv_color_hex(0x404B3F)
#define PP_STRIP_OLIVE    lv_color_hex(0x2E3731)
#define PP_BADGE_GRAY     lv_color_hex(0x454545)
#define PP_STRIP_GRAY     lv_color_hex(0x323336)
#define PP_BADGE_ORANGE   lv_color_hex(0x554237)
#define PP_STRIP_ORANGE   lv_color_hex(0x3D312B)
#define PP_BADGE_BLUE     lv_color_hex(0x3C444F)
#define PP_STRIP_BLUE     lv_color_hex(0x2B333D)
#define PP_BADGE_YELLOW   lv_color_hex(0x564F39)
#define PP_STRIP_YELLOW   lv_color_hex(0x3E3B2D)
#define PP_BADGE_RED      lv_color_hex(0x553B35)
#define PP_STRIP_RED      lv_color_hex(0x3D2C2A)

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
