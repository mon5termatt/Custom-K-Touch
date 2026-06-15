#pragma once
/* Prusa-Touch — UI skins (issue #6). A skin is the whole color palette (g_skin in
 * pandaprusa_theme.h). Built-in presets are selected by index and persisted in NVS; the change
 * takes effect on reboot (screens bake colors at build time, like an orientation change).
 *
 * Phase 1 = built-in color presets. Custom user palettes (web color editor + import/export) and
 * runtime font/wordmark theming are later phases; the g_skin foundation already supports them. */
#include <stdbool.h>
#include <stdint.h>

/* Load the saved skin index from NVS and apply it to g_skin (call ONCE at boot, before ui_init
 * builds the screens). Safe to call before Wi-Fi/net are up. */
void skin_init(void);

/* Set g_skin to a preset WITHOUT touching NVS (used at boot, by the sim, and for live preview). */
void skin_apply_index(int idx);

int         skin_count(void);            /* number of built-in presets */
const char *skin_name(int idx);          /* display name, or "" if out of range */
int         skin_current(void);          /* the active preset index */

/* Persist a new skin index to NVS. Call ONLY from the net task (NVS write). Does NOT repaint —
 * the caller reboots so the new skin is applied when screens are rebuilt at boot. */
void skin_persist_index(int idx);

/* ---- Custom skin (Phase 1b: the ThemeForge web editor) ----
 * A user palette of the 19 primary colors, packed R,G,B per token (57 bytes) in SKIN_TOKENS order,
 * POSTed from the web editor. Stored as an NVS blob; selected as the slot at skin_custom_index()
 * (after the built-in presets). The device derives the 14 state tints itself (skin_compute_tints). */
extern const char *const SKIN_TOKENS[19];        /* canonical token order for the 57-byte palette */
int  skin_custom_index(void);                    /* the "Custom" slot index (== skin_count()-1)    */
void skin_set_custom(const uint8_t rgb[57]);     /* install + persist a custom palette (httpd task) */
void skin_palette_rgb(int idx, uint8_t out[57]); /* pack a preset's (or custom's) 19 primaries->RGB */
