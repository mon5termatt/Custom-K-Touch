/* Prusa-Touch desktop simulator (headless).
 *   pt_sim <screen> <W> <H> <out.bmp>
 * Builds the real ui.c screens at the given logical resolution (800x480 landscape or
 * 480x800 portrait), injects mock fleet data, navigates to <screen>, renders one settled
 * frame, and writes it to a 24-bit BMP. Portrait vs landscape is chosen purely by W/H, so
 * this previews the layout work without the device. */
#include "lvgl.h"
#include "ui.h"
#include "pandaprusa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void sim_set_active(int idx);   /* from sim_stubs.c */

static int      s_w, s_h;
static uint16_t *s_fb;          /* captured framebuffer (RGB565) */
static uint32_t  s_ms = 0;

static uint32_t tick_cb(void) { return s_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map) {
    /* full-refresh single buffer: one flush covers the whole screen */
    int32_t w = area->x2 - area->x1 + 1;
    const uint16_t *src = (const uint16_t *)px_map;
    for (int32_t y = area->y1; y <= area->y2; y++)
        memcpy(&s_fb[y * s_w + area->x1], &src[(y - area->y1) * w], (size_t)w * 2);
    lv_display_flush_ready(d);
}

/* ---- mock fleet ---- */
static void mk(pp_status_t *s, const char *name, const char *model, const char *state,
               bool online, int tn, int ttn, int tb, int ttb, int spd, float z,
               bool job, float prog, int eta, const char *jobname) {
    memset(s, 0, sizeof(*s));
    s->online = online; s->is_cloud = true; s->has_control = online;
    strncpy(s->printer_name, name, sizeof(s->printer_name)-1);
    strncpy(s->model, model, sizeof(s->model)-1);
    strncpy(s->state, state, sizeof(s->state)-1);
    strncpy(s->firmware, "6.4.0", sizeof(s->firmware)-1);
    s->temp_nozzle=tn; s->target_nozzle=ttn; s->temp_bed=tb; s->target_bed=ttb;
    s->speed=spd; s->axis_z=z; s->time_remaining = job ? eta : -1;
    s->has_job=job; s->progress=prog;
    if (job && jobname) strncpy(s->job_name, jobname, sizeof(s->job_name)-1);
}

static void build_dash(pp_dash_t *d) {
    memset(d, 0, sizeof(*d));
    d->count = 5; d->conn_expired = false;
    mk(&d->items[0], "Apollo",            "Prusa CORE One",   "PRINTING", true, 215,215, 60,60, 100, 12.4f, true, 64, 3120, "bracket_v3.gcode");
    mk(&d->items[1], "Artemis",           "Prusa CORE One L", "FINISHED", true, 28,0,    26,0,  100, 168.8f, false, 0, 0, NULL);
    mk(&d->items[2], "Mini - Fermi",      "Original Prusa MINI","ATTENTION", true, 22,0,  23,0, 100, 69.1f, false, 0, 0, NULL);
    mk(&d->items[3], "Mini - Pulsar",     "Original Prusa MINI","PRINTING", true, 215,215, 60,60, 100, 4.2f, true, 23, 5400, "calibration_cube.gcode");
    mk(&d->items[4], "Tau Ceti - Office", "Original Prusa MK4S","READY",    true, 25,0,   25,0, 100, 2.0f, false, 0, 0, NULL);
}

/* The detail screen reads the *active* printer's status (Fermi, in ATTENTION). */
static void build_status(pp_status_t *s) {
    mk(s, "Mini - Fermi", "Original Prusa MINI", "ATTENTION", true, 22,0, 23,0, 100, 69.14f, false, 0, 0, NULL);
    s->dialog_id = 1350426482;
    strncpy(s->dialog_title, "Warning", sizeof(s->dialog_title)-1);
    strncpy(s->dialog_text, "Heating disabled due to 30 minutes of inactivity.", sizeof(s->dialog_text)-1);
    strncpy(s->dialog_btns[0], "Continue", sizeof(s->dialog_btns[0])-1);
    s->dialog_btn_count = 1;
}

static void write_bmp(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    uint32_t imgsize = (uint32_t)s_w * s_h * 3, total = 54 + imgsize;
    uint8_t hdr[54] = {'B','M'};
    memcpy(&hdr[2], &total, 4); hdr[10]=54; hdr[14]=40;
    int32_t iw=s_w, ih=s_h; memcpy(&hdr[18],&iw,4); memcpy(&hdr[22],&ih,4);
    hdr[26]=1; hdr[28]=24;
    fwrite(hdr, 1, 54, f);
    uint8_t *row = malloc((size_t)s_w*3);
    for (int y=s_h-1; y>=0; y--) {            /* BMP is bottom-up */
        for (int x=0; x<s_w; x++) {
            uint16_t c = s_fb[y*s_w+x];
            row[x*3+0]=(uint8_t)((c&0x1F)<<3);
            row[x*3+1]=(uint8_t)(((c>>5)&0x3F)<<2);
            row[x*3+2]=(uint8_t)(((c>>11)&0x1F)<<3);
        }
        fwrite(row, 1, (size_t)s_w*3, f);
    }
    free(row); fclose(f);
}

int main(int argc, char **argv) {
    const char *screen = argc>1 ? argv[1] : "dash";
    s_w = argc>2 ? atoi(argv[2]) : 800;
    s_h = argc>3 ? atoi(argv[3]) : 480;
    const char *out = argc>4 ? argv[4] : "out.bmp";

    lv_init();
    lv_tick_set_cb(tick_cb);

    s_fb = calloc((size_t)s_w*s_h, sizeof(uint16_t));
    static lv_color_t *drawbuf;
    drawbuf = malloc((size_t)s_w*s_h*sizeof(lv_color_t));
    lv_display_t *disp = lv_display_create(s_w, s_h);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, drawbuf, NULL, (uint32_t)s_w*s_h*sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);

    ui_init();    /* builds every screen at the active resolution */

    /* inject mock data */
    pp_dash_t *d = malloc(sizeof(*d)); build_dash(d); ui_apply_dashboard(d);   /* applier frees */
    pp_status_t *st = malloc(sizeof(*st)); build_status(st); ui_apply_status(st);

    /* "lock" preview: render the PIN-unlock overlay over the dashboard */
    if (!strcmp(screen, "lock")) {
        extern void sim_set_lock(const char *pin, uint8_t minutes);
        extern void ui_lock_now(void); extern void ui_show_lock_prompt(void);
        sim_set_lock("1234", 1);
        ui_request_screen("dash");
        ui_lock_now();
        ui_show_lock_prompt();
    } else {
        ui_request_screen(screen);
    }

    /* settle: advance time so bars/anim finish, run the LVGL pipeline */
    for (int i=0;i<50;i++){ s_ms += 30; lv_timer_handler(); }
    lv_refr_now(disp);

    write_bmp(out);
    printf("rendered '%s' %dx%d -> %s\n", screen, s_w, s_h, out);
    return 0;
}
