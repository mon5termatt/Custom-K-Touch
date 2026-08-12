#pragma once
/* Custom layouts (issue #6 Phase 3): a chunk-grid of data tiles bound to the ACTIVE printer.
 * The browser "Layout" designer (Phase 4) does the drag-and-drop and emits this compact spec;
 * the live status screen content area interprets it (top header bar stays fixed chrome).
 * Tiles are responsive primitives: each fills whatever box the grid gives it (1×1..6×6).
 * Stored as an NVS blob; orientation-aware built-in defaults when no custom layout is saved. */
#include <stdbool.h>
#include <stdint.h>

/* Tile types = data fields, structural chrome, and action/tool buttons. Keep in sync with PP_TILE_KEYS.
 * HEADER = printer name + state badge fused in the state-tinted strip (the hero header);
 * JOB    = the current print's file name.
 * LT_PAUSE..LT_AFC = ghost buttons wired to status actions or Tools screens. */
typedef enum {
    LT_EMPTY = 0, LT_NAME, LT_MODEL, LT_STATE, LT_NOZZLE, LT_BED, LT_SPEED, LT_ZAXIS,
    LT_PROGRESS, LT_ETA, LT_THUMB, LT_HEADER, LT_JOB,
    LT_PAUSE, LT_STOP, LT_FILES, LT_TOOLS,
    LT_MOVE, LT_TEMP, LT_WEBCAM, LT_MACROS, LT_CONSOLE, LT_TUNE, LT_CALIB, LT_AFC,
    LT_LAYER_PROGRESS,
    LT_COUNT
} pp_tile_type_t;

/* How a tile is chromed (so the editor can reproduce the status screen 1:1):
 * CARD = surface background + muted caption (the default); BARE = transparent, no caption (a
 * floating label, e.g. the model line); ACCENT = orange background (the hero thumbnail). */
typedef enum { LS_CARD = 0, LS_BARE, LS_ACCENT, LS_COUNT } pp_tile_style_t;

/* group: 0 = standalone tile. Tiles sharing a nonzero group (1..PP_LAYOUT_GROUPS-1) render on ONE
 * shared surface card spanning their bounding box (like the stock job card bundling name+%+bar+ETA). */
#define PP_LAYOUT_GROUPS 16
typedef struct { uint8_t type, c, r, w, h, style, group; } pp_tile_t;

#define PP_LAYOUT_MAX 20
#define PP_LAYOUT_ROWS_MAX 32
typedef struct { uint8_t cols, rows, n; pp_tile_t tiles[PP_LAYOUT_MAX]; } pp_layout_t;

/* rows==0: infer from tile positions (legacy NVS + designer auto-height). */
int layout_rows_used(const pp_layout_t *l);

/* token names (designer/API) + human labels (the small caption on each tile), indexed by type */
extern const char *const PP_TILE_KEYS[LT_COUNT];
extern const char *const PP_TILE_LABELS[LT_COUNT];

void               layout_init(void);          /* load from NVS or fall back to the default */
const pp_layout_t *layout_get(void);           /* custom, else landscape default (web editor) */
const pp_layout_t *layout_get_for_screen(bool portrait); /* custom, else orientation stock */
bool               layout_has_custom(void);    /* a user layout is stored (vs the default)   */
bool               layout_valid(const pp_layout_t *l); /* cols/n/tiles in range + known types */
void               layout_set(const pp_layout_t *l);   /* validate, store to NVS, set active (httpd task) */
void               layout_reset(void);         /* clear custom layout; revert to stock defaults */
