#include "graph.h"
#include "help.h"
#include "antideriv.h"
#include "mathprint.h"
#include "fmt.h"

#include <graphx.h>
#include <keypadc.h>
#include <ti/getcsc.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Palette: 0x00/0xFF are xlibc black/white; 1..7 installed by us. */
#define C_FG    0x00
#define C_BG    0xFF
#define C_GRID  1
#define C_F     2
#define C_ACC   3
#define C_SHP   4
#define C_SHN   5
#define C_TRC   6
#define C_STAT  7

#define CHAR_W  8       /* graphx default font cell width */

#define PAN_STEP 6      /* pixels per frame while an arrow is held */

void grf_init_palette(void) {
    gfx_palette[C_GRID] = gfx_RGBTo1555(214, 214, 214);
    gfx_palette[C_F]    = gfx_RGBTo1555(0, 64, 224);
    gfx_palette[C_ACC]  = gfx_RGBTo1555(160, 32, 240);
    gfx_palette[C_SHP]  = gfx_RGBTo1555(170, 210, 255);
    gfx_palette[C_SHN]  = gfx_RGBTo1555(255, 190, 170);
    gfx_palette[C_TRC]  = gfx_RGBTo1555(224, 0, 0);
    gfx_palette[C_STAT] = gfx_RGBTo1555(236, 236, 236);
}

/* ============================================================
 * Viewport and caches
 * ============================================================ */

static void vp_update(Viewport *vp) {
    vp->pxw = (vp->xmax - vp->xmin) / (float)GRAPH_W;
    vp->pxh = (vp->ymax - vp->ymin) / (float)GRAPH_H;
}

/* Always multiply, never accumulate x += pxw — float drift adds up. */
static float col_x(const GraphState *g, int col) {
    return g->vp.xmin + (float)col * g->vp.pxw;
}

static int16_t project_y(const GraphState *g, float wy) {
    if (isnan(wy)) return Y_SENT;
    float s = (g->vp.ymax - wy) / g->vp.pxh;
    /* Park far-off-screen samples on the rails: keeps the drawing loops in
     * safe integer range and gives them a cheap "is it way off" test. */
    if (s < (float)Y_RAIL_LO) s = (float)Y_RAIL_LO;
    else if (s > (float)Y_RAIL_HI) s = (float)Y_RAIL_HI;
    return (int16_t)s;
}

void grf_reset_anchor(GraphState *g) {
    g->xref = g->a;
    g->Fref = 0.0f;
}

static void recompute_f(GraphState *g) {
    for (int i = 0; i < GRAPH_W; i++) {
        g->fcol[i] = ec_eval(&g->fcode, col_x(g, i));
    }
}

/* One left-to-right trapezoid sweep, anchored at (xref, Fref). */
static void recompute_F(GraphState *g) {
    float x0 = g->vp.xmin;
    float F0 = g->Fref;
    g->Fwarn = 0;

    if (x0 != g->xref) {
        float fn = 2.0f * fabsf(x0 - g->xref) / g->vp.pxw;
        int n = (fn >= 254.0f) ? 256 : (int)fn + 2;
        if (n < 16) n = 16;
        uint8_t w = 0;
        F0 += ec_integrate(&g->fcode, g->xref, x0, n, &w);
        if (w) g->Fwarn = 1;
    }
    g->xref = x0;
    g->Fref = F0;

    float half = 0.5f * g->vp.pxw;
    float prev = g->fcol[0];
    if (isnan(prev)) { prev = 0.0f; g->Fwarn = 1; }
    float acc = F0;
    g->Fcol[0] = F0;
    for (int i = 1; i < GRAPH_W; i++) {
        float cur = g->fcol[i];
        if (isnan(cur)) { cur = 0.0f; g->Fwarn = 1; }
        acc += (prev + cur) * half;
        g->Fcol[i] = acc;
        prev = cur;
    }
}

static void project_all(GraphState *g) {
    for (int i = 0; i < GRAPH_W; i++) {
        g->fy[i] = project_y(g, g->fcol[i]);
        g->Fy[i] = project_y(g, g->Fcol[i]);
    }
}

void grf_recompute_all(GraphState *g) {
    vp_update(&g->vp);
    recompute_f(g);
    recompute_F(g);
    project_all(g);
}

/* ============================================================
 * Pan / zoom
 * ============================================================ */

/* Shift the view k columns right (k>0) or left (k<0). Cached columns are
 * memmoved; only newly exposed ones are evaluated, and the accumulation is
 * extended incrementally — valid because the anchor doesn't move on pan. */
static void pan_cols(GraphState *g, int k) {
    Viewport *vp = &g->vp;
    float dx = (float)k * vp->pxw;
    vp->xmin += dx;
    vp->xmax += dx;

    if (k >= GRAPH_W || k <= -GRAPH_W) {
        recompute_f(g);
        recompute_F(g);
        project_all(g);
        return;
    }

    float half = 0.5f * vp->pxw;
    if (k > 0) {
        int keep = GRAPH_W - k;
        memmove(g->fcol, g->fcol + k, (size_t)keep * sizeof(float));
        memmove(g->Fcol, g->Fcol + k, (size_t)keep * sizeof(float));
        memmove(g->fy,   g->fy + k,   (size_t)keep * sizeof(int16_t));
        memmove(g->Fy,   g->Fy + k,   (size_t)keep * sizeof(int16_t));
        for (int i = keep; i < GRAPH_W; i++) {
            g->fcol[i] = ec_eval(&g->fcode, col_x(g, i));
            float prev = g->fcol[i - 1], cur = g->fcol[i];
            if (isnan(prev)) prev = 0.0f;
            if (isnan(cur)) { cur = 0.0f; g->Fwarn = 1; }
            g->Fcol[i] = g->Fcol[i - 1] + (prev + cur) * half;
            g->fy[i] = project_y(g, g->fcol[i]);
            g->Fy[i] = project_y(g, g->Fcol[i]);
        }
    } else {
        int n = -k;
        int keep = GRAPH_W - n;
        memmove(g->fcol + n, g->fcol, (size_t)keep * sizeof(float));
        memmove(g->Fcol + n, g->Fcol, (size_t)keep * sizeof(float));
        memmove(g->fy + n,   g->fy,   (size_t)keep * sizeof(int16_t));
        memmove(g->Fy + n,   g->Fy,   (size_t)keep * sizeof(int16_t));
        for (int i = n - 1; i >= 0; i--) {
            g->fcol[i] = ec_eval(&g->fcode, col_x(g, i));
            float next = g->fcol[i + 1], cur = g->fcol[i];
            if (isnan(next)) next = 0.0f;
            if (isnan(cur)) { cur = 0.0f; g->Fwarn = 1; }
            /* backward: F[i] = F[i+1] - trapezoid(i, i+1) */
            g->Fcol[i] = g->Fcol[i + 1] - (cur + next) * half;
            g->fy[i] = project_y(g, g->fcol[i]);
            g->Fy[i] = project_y(g, g->Fcol[i]);
        }
    }
    g->xref = vp->xmin;
    g->Fref = g->Fcol[0];
}

static void grf_pan(GraphState *g, int dxp, int dyp) {
    if (dxp) pan_cols(g, dxp);
    if (dyp) {
        float dy = (float)dyp * g->vp.pxh;
        g->vp.ymin += dy;
        g->vp.ymax += dy;
        project_all(g);          /* zero evals for vertical motion */
    }
}

/* Returns an error message if the zoom is refused, else NULL. */
static const char* grf_zoom(GraphState *g, float factor, float cx, float cy) {
    Viewport *vp = &g->vp;
    float w = (vp->xmax - vp->xmin) * factor;
    float h = (vp->ymax - vp->ymin) * factor;

    if (factor < 1.0f) {
        /* ~7 significant digits: columns collapse when the window gets this
         * narrow relative to its center — refuse with a clean message. */
        if (w < fabsf(cx) * 3e-5f + 1e-30f ||
            h < fabsf(cy) * 3e-5f + 1e-30f) {
            return "Zoom limit: out of float precision";
        }
    } else if (w > 1e30f || h > 1e30f) {
        return "Zoom limit";
    }

    vp->xmin = cx - (cx - vp->xmin) * factor;
    vp->xmax = cx + (vp->xmax - cx) * factor;
    vp->ymin = cy - (cy - vp->ymin) * factor;
    vp->ymax = cy + (vp->ymax - cy) * factor;
    grf_recompute_all(g);
    return NULL;
}

void grf_set_standard_window(GraphState *g) {
    g->vp.xmin = -10.0f;
    g->vp.xmax = 10.0f;
    g->vp.ymin = -6.5625f;     /* square pixels: 10 * 210/320 */
    g->vp.ymax = 6.5625f;
    grf_reset_anchor(g);
    grf_recompute_all(g);
}

void grf_auto_window(GraphState *g) {
    float lo = g->a < g->b ? g->a : g->b;
    float hi = g->a < g->b ? g->b : g->a;
    if (hi - lo < 1e-9f) { lo -= 5.0f; hi += 5.0f; }
    float pad = (hi - lo) * 0.15f;
    g->vp.xmin = lo - pad;
    g->vp.xmax = hi + pad;
    g->vp.ymin = -1.0f;        /* provisional; pxh unused until projection */
    g->vp.ymax = 1.0f;
    vp_update(&g->vp);

    grf_reset_anchor(g);
    recompute_f(g);
    recompute_F(g);

    float ymin = 0.0f, ymax = 0.0f;   /* keep the x-axis in view */
    for (int i = 0; i < GRAPH_W; i++) {
        float v = g->fcol[i];
        if (!g->b_is_x && !isnan(v)) {   /* b = x: only F is drawn */
            if (v < ymin) ymin = v;
            if (v > ymax) ymax = v;
        }
        if (g->show_F) {
            v = g->Fcol[i];
            if (!isnan(v)) {
                if (v < ymin) ymin = v;
                if (v > ymax) ymax = v;
            }
        }
    }
    if (ymax - ymin < 1e-9f) { ymin -= 1.0f; ymax += 1.0f; }
    /* Cap blowups (poles) so the interesting region stays visible. */
    float span = ymax - ymin;
    if (span > 1e6f) { ymin = -10.0f; ymax = 10.0f; span = 20.0f; }
    float m = span * 0.1f;
    g->vp.ymin = ymin - m;
    g->vp.ymax = ymax + m;
    vp_update(&g->vp);
    project_all(g);
}

/* ============================================================
 * Rendering
 * ============================================================ */

/* Smallest 1/2/5 * 10^k >= raw. Walk outward from exactly 1.0 so common
 * steps (0.5, 2, 2.5, ...) come out clean, not 2.5000002. */
static float nice_step(float raw) {
    float p = 1.0f;
    if (raw > 1.0f) {
        while (p < raw && p < 1e30f) p *= 10.0f;
    } else {
        while (p * 0.1f >= raw && p > 1e-30f) p *= 0.1f;
    }
    /* p/10 < raw <= p */
    if (p * 0.2f >= raw) return p * 0.2f;
    if (p * 0.5f >= raw) return p * 0.5f;
    return p;
}

static int zero_row(const GraphState *g) {
    float s = g->vp.ymax / g->vp.pxh;
    if (s < -320.0f) s = -320.0f;
    else if (s > (float)(GRAPH_H + 320)) s = (float)(GRAPH_H + 320);
    return (int)s;
}

/* Samples sit one pixel apart, so the segment joining two of them spans a
 * single column: filling that column between the two rows draws the same
 * pixels Bresenham would, without walking a clipped line per segment. That
 * matters on steep curves (tan near a pole), where the untruncated segments
 * are hundreds of pixels tall and dominate the frame. */
/* Everything here is integer: this loop runs 638 times per frame, and a
 * single software-float compare costs more than the rest of an iteration
 * put together. project_y() already parked out-of-range samples on the
 * rails, which is what the pole test reads. */
static void draw_curve(const int16_t *ys, uint8_t color, int c0, int c1) {
    gfx_SetColor(color);
    int i0 = c0 > 0 ? c0 - 1 : 0;
    int i1 = (c1 < GRAPH_W ? c1 : GRAPH_W) - 1;
    for (int i = i0; i < i1; i++) {
        int y1 = ys[i], y2 = ys[i + 1];
        if (y1 == Y_SENT || y2 == Y_SENT) continue;

        int lo, hi;
        if (y1 < y2) { lo = y1; hi = y2; } else { lo = y2; hi = y1; }
        if (hi < 0 || lo > GRAPH_H - 1) continue;   /* off-screen segment */
        /* Pole: one end pinned far above the window, the other far below
         * (tan across an asymptote) — leave a gap instead of a spike. */
        if (lo <= Y_RAIL_LO && hi >= Y_RAIL_HI) continue;

        if (lo < 0) lo = 0;
        if (hi > GRAPH_H - 1) hi = GRAPH_H - 1;
        gfx_VertLine_NoClip(i, lo, hi - lo + 1);
    }
}

/* Draw plot content for columns [c0,c1). Assumes clip = plot rect. */
static void draw_plot(const GraphState *g, int c0, int c1, int labels) {
    const Viewport *vp = &g->vp;

    gfx_SetColor(C_BG);
    gfx_FillRectangle_NoClip(c0, 0, c1 - c0, GRAPH_H);

    float xstep = nice_step(40.0f * vp->pxw);
    float ystep = nice_step(40.0f * vp->pxh);

    /* grid */
    gfx_SetColor(C_GRID);
    float gx0 = ceilf(vp->xmin / xstep) * xstep;
    for (float gx = gx0; gx < vp->xmax; gx += xstep) {
        int c = (int)((gx - vp->xmin) / vp->pxw + 0.5f);
        if (c >= c0 && c < c1) gfx_VertLine_NoClip(c, 0, GRAPH_H);
    }
    float gy0 = ceilf(vp->ymin / ystep) * ystep;
    for (float gy = gy0; gy < vp->ymax; gy += ystep) {
        int r = (int)((vp->ymax - gy) / vp->pxh);
        if (r >= 0 && r < GRAPH_H) gfx_HorizLine_NoClip(c0, r, c1 - c0);
    }

    /* Shading between a and b. Turn the bounds into a column range once,
     * so the per-column work stays integer (see draw_curve). */
    int sy0 = zero_row(g);
    if (!g->b_is_x) {   /* b = x: no interval to shade */
        float lo = g->a < g->b ? g->a : g->b;
        float hi = g->a < g->b ? g->b : g->a;
        int ca = (int)ceilf((lo - vp->xmin) / vp->pxw);
        int cb = (int)((hi - vp->xmin) / vp->pxw);
        if (ca < c0) ca = c0;
        if (cb > c1 - 1) cb = c1 - 1;
        uint8_t cur_col = 0xFF;
        for (int c = ca; c <= cb; c++) {
            int fyc = g->fy[c];
            if (fyc == Y_SENT) continue;
            int ytop = sy0 < fyc ? sy0 : fyc;
            int ybot = sy0 < fyc ? fyc : sy0;
            if (ytop < 0) ytop = 0;
            if (ybot > GRAPH_H - 1) ybot = GRAPH_H - 1;
            if (ytop > ybot) continue;
            /* sign of f, without a float compare: below the zero row = negative */
            uint8_t want = (fyc <= sy0) ? C_SHP : C_SHN;
            if (want != cur_col) { gfx_SetColor(want); cur_col = want; }
            gfx_VertLine_NoClip(c, ytop, ybot - ytop + 1);
        }
    }

    /* axes over the shading */
    gfx_SetColor(C_FG);
    if (sy0 >= 0 && sy0 < GRAPH_H) gfx_HorizLine_NoClip(c0, sy0, c1 - c0);
    if (vp->xmin <= 0.0f && vp->xmax >= 0.0f) {
        int ac = (int)((0.0f - vp->xmin) / vp->pxw + 0.5f);
        if (ac >= c0 && ac < c1) gfx_VertLine_NoClip(ac, 0, GRAPH_H);
    }

    /* curves */
    if (g->show_F) draw_curve(g->Fy, C_ACC, c0, c1);
    if (!g->b_is_x) draw_curve(g->fy, C_F, c0, c1);

    /* axis tick labels (full redraws only) */
    if (labels) {
        char buf[16];
        gfx_SetTextFGColor(C_FG);
        gfx_SetTextBGColor(C_BG);
        gfx_SetTextTransparentColor(C_BG);

        int ly = sy0 + 3;
        if (ly < 2) ly = 2;
        if (ly > GRAPH_H - 10) ly = GRAPH_H - 10;
        /* Far from the origin the numbers get long enough to run into each
         * other, so drop any label that would touch the previous one. */
        int last_right = -1000;
        for (float gx = gx0; gx < vp->xmax; gx += xstep) {
            float v = (fabsf(gx) < 1e-4f * xstep) ? 0.0f : gx;
            int c = (int)((gx - vp->xmin) / vp->pxw + 0.5f);
            int len = fmt_g(buf, v);
            if (c + 3 < last_right + CHAR_W) continue;
            gfx_PrintStringXY(buf, c + 3, ly);
            last_right = c + 3 + len * CHAR_W;
        }

        int ac = (int)((0.0f - vp->xmin) / vp->pxw + 0.5f);
        int lx = ac + 4;
        if (lx < 2) lx = 2;
        if (lx > GRAPH_W - 20) lx = GRAPH_W - 20;
        for (float gy = gy0; gy < vp->ymax; gy += ystep) {
            float v = (fabsf(gy) < 1e-4f * ystep) ? 0.0f : gy;
            if (v == 0.0f) continue;   /* origin already labeled by x pass */
            int r = (int)((vp->ymax - gy) / vp->pxh);
            if (r < 2 || r > GRAPH_H - 10) continue;
            fmt_g(buf, v);
            gfx_PrintStringXY(buf, lx, r + 2);
        }
    }
}

static void draw_trace(const GraphState *g) {
    int c = g->trace_col;
    /* the crosshair rides the curve that is actually drawn */
    const int16_t *ys = g->b_is_x ? g->Fy : g->fy;
    gfx_SetColor(C_TRC);
    if (ys[c] != Y_SENT && ys[c] >= 0 && ys[c] < GRAPH_H) {
        gfx_HorizLine(c - 3, ys[c], 7);
        gfx_VertLine(c, ys[c] - 3, 7);
    } else {
        gfx_VertLine(c, 0, GRAPH_H);   /* off-screen: show the column */
    }
    if (g->show_F && !g->b_is_x && g->Fy[c] != Y_SENT &&
        g->Fy[c] >= 1 && g->Fy[c] < GRAPH_H - 1) {
        gfx_FillRectangle(c - 1, g->Fy[c] - 1, 3, 3);
    }
}

static void draw_status(const GraphState *g, const char *msg) {
    char line[64];
    int p;

    gfx_SetColor(C_STAT);
    gfx_FillRectangle_NoClip(0, STATUS_Y, GRAPH_W, 240 - STATUS_Y);
    gfx_SetColor(C_FG);
    gfx_HorizLine_NoClip(0, STATUS_Y, GRAPH_W);

    gfx_SetTextFGColor(C_FG);
    gfx_SetTextBGColor(C_STAT);
    gfx_SetTextTransparentColor(C_STAT);

    mp_draw_integral_sign(3, STATUS_Y + 3, 20);
    p = 0;
    line[p++] = '[';
    p += fmt_g(line + p, g->a);
    line[p++] = ',';
    if (g->b_is_x) {
        memcpy(line + p, "x] = F(x)", 10); p += 9;
    } else {
        p += fmt_g(line + p, g->b);
        memcpy(line + p, "] = ", 4); p += 4;
        p += fmt_g(line + p, g->integral_ab);
        if (g->integral_warn) { memcpy(line + p, " !", 3); p += 2; }
    }
    line[p] = '\0';
    gfx_PrintStringXY(line, 13, STATUS_Y + 5);

    if (g->b_is_x) {
        gfx_SetTextFGColor(C_ACC);
        gfx_PrintStringXY("b=x", GRAPH_W - 44, STATUS_Y + 5);
    } else {
        gfx_SetTextFGColor(g->show_F ? C_ACC : C_FG);
        gfx_PrintStringXY(g->show_F ? "F:on" : "F:off", GRAPH_W - 44, STATUS_Y + 5);
    }
    gfx_SetTextFGColor(C_FG);

    if (msg) {
        gfx_PrintStringXY(msg, 4, STATUS_Y + 18);
    } else if (g->tracing) {
        int c = g->trace_col;
        p = 0;
        memcpy(line + p, "x=", 2); p += 2;
        p += fmt_g(line + p, col_x(g, c));
        memcpy(line + p, " f=", 3); p += 3;
        p += fmt_g(line + p, g->fcol[c]);
        memcpy(line + p, " F=", 3); p += 3;
        p += fmt_g(line + p, g->Fcol[c]);
        line[p] = '\0';
        gfx_PrintStringXY(line, 4, STATUS_Y + 18);
    } else {
        gfx_PrintStringXY("pan:arrows zoom:+/- help:alpha sym:math", 4, STATUS_Y + 18);
    }
}

/* Redrawing the status bar costs about as much as the whole grid; while the
 * user is only panning, none of its text changes. Because rendering
 * alternates buffers, a change has to be painted into both — hence the
 * count rather than a flag. */
static int status_pending;

static void status_changed(void) { status_pending = 2; }

static void render_full(GraphState *g, const char *msg) {
    gfx_SetClipRegion(0, 0, GRAPH_W, GRAPH_H);
    draw_plot(g, 0, GRAPH_W, 1);
    if (g->tracing) draw_trace(g);
    gfx_SetClipRegion(0, 0, 320, 240);
    if (status_pending > 0) {
        status_pending--;
        draw_status(g, msg);
    }
    gfx_SwapDraw();
}

/* ============================================================
 * Interaction loop (keypadc)
 * ============================================================ */

static uint8_t prevk[8];


static int key_edge(kb_lkey_t k) {
    return kb_IsDown(k) && !(prevk[k >> 8] & (uint8_t)k);
}

static void keys_save(void) {
    for (int i = 1; i < 8; i++) prevk[i] = (uint8_t)kb_Data[i];
}

int grf_run(GraphState *g) {
    const char *msg = NULL;
    clock_t last_zoom = 0;

    memset(prevk, 0xFF, sizeof prevk);   /* swallow keys still held on entry */
    status_changed();
    render_full(g, NULL);

    for (;;) {
        kb_Scan();
        int dirty = 0;

        if (key_edge(kb_KeyMode) || key_edge(kb_KeyClear)) {
            kb_Reset();
            return GRF_QUIT;
        }
        if (key_edge(kb_KeyYequ))   { kb_Reset(); return GRF_REEDIT_F; }
        if (key_edge(kb_KeyWindow)) { kb_Reset(); return GRF_REEDIT_AB; }

        if (key_edge(kb_KeyAlpha)) {
            help_show();          /* owns the keypad while it is up */
            kb_Scan();
            keys_save();
            status_changed();
            render_full(g, msg);
            continue;
        }
        if (key_edge(kb_KeyMath)) {
            antideriv_show(g);    /* likewise */
            kb_Scan();
            keys_save();
            status_changed();
            render_full(g, msg);
            continue;
        }

        if (key_edge(kb_KeyZoom)) {
            grf_set_standard_window(g);
            if (msg) status_changed();
            msg = NULL;
            dirty = 1;
        }
        if (key_edge(kb_KeyTrace)) {
            g->tracing = !g->tracing;
            if (g->tracing) {
                /* Re-anchor exactly from a so the readout is trustworthy
                 * after long pan/zoom sessions. */
                grf_reset_anchor(g);
                recompute_F(g);
                project_all(g);
            }
            msg = NULL;
            status_changed();
            dirty = 1;
        }
        if (key_edge(kb_KeyGraph) && !g->b_is_x) {   /* b = x: F is all there is */
            g->show_F = !g->show_F;
            status_changed();
            dirty = 1;
        }

        /* Zoom: a tap steps once; holding repeats on a timer (each step is
         * a full recompute, so pace it rather than firing every frame). */
        int zin = kb_IsDown(kb_KeyAdd) != 0, zout = kb_IsDown(kb_KeySub) != 0;
        if (zin || zout) {
            clock_t now = clock();
            if (key_edge(kb_KeyAdd) || key_edge(kb_KeySub) ||
                now - last_zoom > CLOCKS_PER_SEC / 5) {
                float cx = g->tracing ? col_x(g, g->trace_col)
                                      : (g->vp.xmin + g->vp.xmax) * 0.5f;
                float cy = (g->vp.ymin + g->vp.ymax) * 0.5f;
                const char *prev = msg;
                msg = grf_zoom(g, zin ? 0.8f : 1.25f, cx, cy);
                if (msg != prev) status_changed();
                last_zoom = now;
                dirty = 1;
            }
        }

        if (g->tracing) {
            int step = kb_IsDown(kb_Key2nd) ? 8 : 2;
            if (kb_IsDown(kb_KeyLeft)) {
                g->trace_col -= step;
                if (g->trace_col < 0) {
                    pan_cols(g, g->trace_col);
                    g->trace_col = 0;
                }
                dirty = 1;
            }
            if (kb_IsDown(kb_KeyRight)) {
                g->trace_col += step;
                if (g->trace_col > GRAPH_W - 1) {
                    pan_cols(g, g->trace_col - (GRAPH_W - 1));
                    g->trace_col = GRAPH_W - 1;
                }
                dirty = 1;
            }
            if (kb_IsDown(kb_KeyUp))   { grf_pan(g, 0, PAN_STEP);  dirty = 1; }
            if (kb_IsDown(kb_KeyDown)) { grf_pan(g, 0, -PAN_STEP); dirty = 1; }
            if (key_edge(kb_KeyEnter)) {
                pan_cols(g, g->trace_col - GRAPH_W / 2);
                g->trace_col = GRAPH_W / 2;
                dirty = 1;
            }
            /* the readout follows the cursor, so it is never stale-safe */
            if (dirty) status_changed();
        } else {
            int dxp = 0, dyp = 0;
            if (kb_IsDown(kb_KeyLeft))  dxp -= PAN_STEP;
            if (kb_IsDown(kb_KeyRight)) dxp += PAN_STEP;
            if (kb_IsDown(kb_KeyUp))    dyp += PAN_STEP;
            if (kb_IsDown(kb_KeyDown))  dyp -= PAN_STEP;
            if (dxp || dyp) {
                grf_pan(g, dxp, dyp);
                if (msg) { msg = NULL; status_changed(); }
                dirty = 1;
            }
        }

        keys_save();
        if (dirty) render_full(g, msg);
    }
}
