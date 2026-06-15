#pragma once
/* Custom layouts (issue #6 Phase 3): a chunk-grid of data tiles bound to the ACTIVE printer.
 * The browser "Layout" designer (Phase 4) does the drag-and-drop and emits this compact spec;
 * the device just interprets it — computes pixel rects from chunk coords and binds each tile to
 * a field of the live pp_status_t. Tiles are responsive primitives: each fills whatever box the
 * grid gives it. Stored as an NVS blob; one built-in default. */
#include <stdbool.h>
#include <stdint.h>

/* Tile types = the bindable data fields (+ structural). Keep in sync with PP_TILE_KEYS. */
typedef enum {
    LT_EMPTY = 0, LT_NAME, LT_MODEL, LT_STATE, LT_NOZZLE, LT_BED, LT_SPEED, LT_ZAXIS,
    LT_PROGRESS, LT_ETA, LT_THUMB, LT_COUNT
} pp_tile_type_t;

typedef struct { uint8_t type, c, r, w, h; } pp_tile_t;   /* type + chunk col/row + span (chunks) */

#define PP_LAYOUT_MAX 20
typedef struct { uint8_t cols, n; pp_tile_t tiles[PP_LAYOUT_MAX]; } pp_layout_t;

/* token names (designer/API) + human labels (the small caption on each tile), indexed by type */
extern const char *const PP_TILE_KEYS[LT_COUNT];
extern const char *const PP_TILE_LABELS[LT_COUNT];

void               layout_init(void);          /* load from NVS or fall back to the default */
const pp_layout_t *layout_get(void);           /* the active spec (never NULL)               */
bool               layout_has_custom(void);    /* a user layout is stored (vs the default)   */
void               layout_set(const pp_layout_t *l);   /* validate, store to NVS, set active (httpd task) */
