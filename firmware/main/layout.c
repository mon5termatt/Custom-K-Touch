/* Custom layouts (issue #6 Phase 3). See layout.h. */
#include "layout.h"
#include <string.h>
#ifndef PP_HOST_SIM
#include "nvs.h"
#endif

#define NS "pplayout"

const char *const PP_TILE_KEYS[LT_COUNT] = {
    "", "name", "model", "state", "nozzle", "bed", "speed", "z", "progress", "eta", "thumb"
};
const char *const PP_TILE_LABELS[LT_COUNT] = {
    "", "PRINTER", "MODEL", "STATE", "NOZZLE", "BED", "SPEED", "Z AXIS", "PROGRESS", "ETA", ""
};

/* Built-in default: mirrors the on-device status screen — a hero (model thumbnail at left, printer
 * name + state badge, model line), a 2x2 telemetry block (nozzle/bed/speed/z), then the job card
 * (progress bar + ETA). 4 columns so the cells stay roomy in portrait, matching the status screen's
 * portrait layout; it reflows to landscape too. Opening the editor starts from the real layout. */
static const pp_layout_t DEFAULT_LAYOUT = {
    .cols = 4, .n = 10, .tiles = {
        { LT_THUMB,    0, 0, 2, 2 },
        { LT_NAME,     2, 0, 2, 1 }, { LT_STATE, 2, 1, 2, 1 },
        { LT_MODEL,    0, 2, 4, 1 },
        { LT_NOZZLE,   0, 3, 2, 1 }, { LT_BED,   2, 3, 2, 1 },
        { LT_SPEED,    0, 4, 2, 1 }, { LT_ZAXIS, 2, 4, 2, 1 },
        { LT_PROGRESS, 0, 5, 3, 1 }, { LT_ETA,   3, 5, 1, 1 },
    }
};

static pp_layout_t s_layout = DEFAULT_LAYOUT;
static bool        s_has_custom;

/* Sanity-check a spec: cols 1..16, n<=MAX, every tile in-bounds with a known type. */
static bool layout_valid(const pp_layout_t *l)
{
    if (!l || l->cols < 1 || l->cols > 16 || l->n > PP_LAYOUT_MAX) return false;
    for (int i = 0; i < l->n; i++) {
        const pp_tile_t *t = &l->tiles[i];
        if (t->type == 0 || t->type >= LT_COUNT) return false;
        if (t->w < 1 || t->h < 1) return false;
        if (t->c + t->w > l->cols || t->r + t->h > 32) return false;
    }
    return true;
}

void layout_init(void)
{
#ifndef PP_HOST_SIM
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        pp_layout_t tmp; size_t sz = sizeof(tmp);
        if (nvs_get_blob(h, "spec", &tmp, &sz) == ESP_OK && sz == sizeof(tmp) && layout_valid(&tmp)) {
            s_layout = tmp;
            s_has_custom = true;
        }
        nvs_close(h);
    }
#endif
}

const pp_layout_t *layout_get(void)    { return &s_layout; }
bool               layout_has_custom(void) { return s_has_custom; }

void layout_set(const pp_layout_t *l)
{
    if (!layout_valid(l)) return;
    s_layout = *l;
    s_has_custom = true;
#ifndef PP_HOST_SIM
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "spec", &s_layout, sizeof(s_layout));
        nvs_commit(h);
        nvs_close(h);
    }
#endif
}
