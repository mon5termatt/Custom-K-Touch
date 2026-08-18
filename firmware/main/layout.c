/* Custom layouts (issue #6 Phase 3). See layout.h. */
#include "layout.h"
#include <string.h>
#ifndef PP_HOST_SIM
#include "nvs.h"
#endif

#define NS "pplayout"

/* NVS blob before the rows field was added (cols, n, tiles[]). */
typedef struct { uint8_t cols, n; pp_tile_t tiles[PP_LAYOUT_MAX]; } pp_layout_v0_t;

const char *const PP_TILE_KEYS[LT_COUNT] = {
    "", "name", "model", "state", "nozzle", "bed", "speed", "z", "progress", "eta", "thumb", "header", "job",
    "pause", "stop", "files", "tools", "move", "temp", "webcam", "macros", "console", "tune", "calib", "afc",
    "layer_progress", "lights"
};
const char *const PP_TILE_LABELS[LT_COUNT] = {
    "", "PRINTER", "MODEL", "STATE", "NOZZLE", "BED", "SPEED", "Z AXIS", "PROGRESS", "ETA", "", "", "FILE",
    "", "", "", "", "", "", "", "", "", "", "", "", "LAYER", ""
};

/* Landscape default (cols=6): hero, telemetry 4-across, job card, action buttons on the bottom row. */
static const pp_layout_t DEFAULT_LAYOUT_LANDSCAPE = {
    .cols = 6, .rows = 6, .n = 15, .tiles = {
        { LT_THUMB,    0, 0, 1, 2, LS_ACCENT, 0 },
        { LT_HEADER,   1, 0, 5, 1, LS_CARD,   0 },
        { LT_MODEL,    1, 1, 5, 1, LS_BARE,   0 },
        { LT_NOZZLE,   0, 2, 1, 1, LS_CARD,   0 }, { LT_BED,   1, 2, 1, 1, LS_CARD, 0 },
        { LT_SPEED,    2, 2, 1, 1, LS_CARD,   0 }, { LT_ZAXIS, 3, 2, 1, 1, LS_CARD, 0 },
        { LT_JOB,      0, 3, 6, 1, LS_BARE,   1 },
        { LT_PROGRESS, 0, 4, 3, 1, LS_BARE,   1 }, { LT_LAYER_PROGRESS, 3, 4, 1, 1, LS_BARE, 1 },
        { LT_ETA, 4, 4, 2, 1, LS_BARE, 1 },
        { LT_PAUSE,    0, 5, 1, 1, LS_CARD,   0 }, { LT_STOP,  1, 5, 1, 1, LS_CARD, 0 },
        { LT_FILES,    2, 5, 1, 1, LS_CARD,   0 }, { LT_TOOLS, 3, 5, 1, 1, LS_CARD, 0 },
    }
};

/* Portrait default: telemetry 2×2, same job card + action row. */
static const pp_layout_t DEFAULT_LAYOUT_PORTRAIT = {
    .cols = 6, .rows = 7, .n = 15, .tiles = {
        { LT_THUMB,    0, 0, 1, 2, LS_ACCENT, 0 },
        { LT_HEADER,   1, 0, 5, 1, LS_CARD,   0 },
        { LT_MODEL,    1, 1, 5, 1, LS_BARE,   0 },
        { LT_NOZZLE,   0, 2, 3, 1, LS_CARD,   0 }, { LT_BED,   3, 2, 3, 1, LS_CARD, 0 },
        { LT_SPEED,    0, 3, 3, 1, LS_CARD,   0 }, { LT_ZAXIS, 3, 3, 3, 1, LS_CARD, 0 },
        { LT_JOB,      0, 4, 6, 1, LS_BARE,   1 },
        { LT_PROGRESS, 0, 5, 3, 1, LS_BARE,   1 }, { LT_LAYER_PROGRESS, 3, 5, 1, 1, LS_BARE, 1 },
        { LT_ETA, 4, 5, 2, 1, LS_BARE, 1 },
        { LT_PAUSE,    0, 6, 1, 1, LS_CARD,   0 }, { LT_STOP,  1, 6, 1, 1, LS_CARD, 0 },
        { LT_FILES,    2, 6, 1, 1, LS_CARD,   0 }, { LT_TOOLS, 3, 6, 1, 1, LS_CARD, 0 },
    }
};

static pp_layout_t s_layout = DEFAULT_LAYOUT_LANDSCAPE;
static bool        s_has_custom;

int layout_rows_used(const pp_layout_t *l)
{
    if (!l) return 1;
    if (l->rows >= 1 && l->rows <= PP_LAYOUT_ROWS_MAX) return l->rows;
    int rows = 1;
    for (int i = 0; i < l->n; i++) {
        int rr = l->tiles[i].r + l->tiles[i].h;
        if (rr > rows) rows = rr;
    }
    return rows > 0 ? rows : 1;
}

/* Sanity-check a spec: cols/rows 1..16/32, tiles 1×1..6×6, n<=MAX, every tile in-bounds. */
bool layout_valid(const pp_layout_t *l)
{
    if (!l || l->cols < 1 || l->cols > 16 || l->n > PP_LAYOUT_MAX) return false;
    if (l->rows && (l->rows < 1 || l->rows > PP_LAYOUT_ROWS_MAX)) return false;
    int row_lim = layout_rows_used(l);
    for (int i = 0; i < l->n; i++) {
        const pp_tile_t *t = &l->tiles[i];
        if (t->type == 0 || t->type >= LT_COUNT) return false;
        if (t->style >= LS_COUNT) return false;
        if (t->group >= PP_LAYOUT_GROUPS) return false;
        if (t->w < 1 || t->h < 1 || t->w > 6 || t->h > 6) return false;
        if (t->c + t->w > l->cols || t->r + t->h > row_lim) return false;
    }
    return true;
}

void layout_init(void)
{
#ifndef PP_HOST_SIM
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        pp_layout_t tmp = {0};
        size_t sz = sizeof(tmp);
        esp_err_t er = nvs_get_blob(h, "spec", &tmp, &sz);
        if (er == ESP_OK && sz == sizeof(tmp) && layout_valid(&tmp)) {
            s_layout = tmp;
            s_has_custom = true;
        } else if (er == ESP_OK && sz == sizeof(pp_layout_v0_t)) {
            pp_layout_v0_t v0;
            if (nvs_get_blob(h, "spec", &v0, &sz) == ESP_OK) {
                tmp.cols = v0.cols;
                tmp.rows = 0;
                tmp.n = v0.n;
                memcpy(tmp.tiles, v0.tiles, sizeof(v0.tiles));
                if (layout_valid(&tmp)) {
                    s_layout = tmp;
                    s_has_custom = true;
                }
            }
        }
        nvs_close(h);
    }
#endif
}

const pp_layout_t *layout_get(void)
{
    return s_has_custom ? &s_layout : &DEFAULT_LAYOUT_LANDSCAPE;
}

const pp_layout_t *layout_get_for_screen(bool portrait)
{
    if (s_has_custom) return &s_layout;
    return portrait ? &DEFAULT_LAYOUT_PORTRAIT : &DEFAULT_LAYOUT_LANDSCAPE;
}

bool layout_has_custom(void) { return s_has_custom; }

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

void layout_reset(void)
{
    s_layout = DEFAULT_LAYOUT_LANDSCAPE;
    s_has_custom = false;
#ifndef PP_HOST_SIM
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "spec");
        nvs_commit(h);
        nvs_close(h);
    }
#endif
}
