#ifndef GRAPH_MODULE_H
#define GRAPH_MODULE_H

#include <stdint.h>
#include "eval.h"

#define GRAPH_W  320
#define GRAPH_H  210               /* rows 0..209; status bar below */
#define STATUS_Y GRAPH_H
#define Y_SENT   INT16_MIN         /* fy/Fy value meaning NaN / unusable */
/* Samples outside the window are parked on these rails, so "far off-screen"
 * is an integer test in the drawing loops rather than a float one. */
#define Y_RAIL_LO (-320)
#define Y_RAIL_HI (GRAPH_H + 320)

typedef struct {
    float xmin, xmax, ymin, ymax;
    float pxw, pxh;                /* world units per pixel, cached */
} Viewport;

/* Why grf_run returned. */
enum { GRF_QUIT = 0, GRF_REEDIT_F = 1, GRF_REEDIT_AB = 2 };

typedef struct {
    /* raw user text — source of truth for re-editing */
    char ftext[65], atext[33], btext[33];
    ExprCode fcode;                /* compiled simplified f */
    float a, b;
    float integral_ab;             /* headline Simpson value for the bar */
    uint8_t integral_warn;         /* 1 if NaN samples were skipped */

    Viewport vp;
    /* Per-column caches: world values and their projected screen rows.
     * Any y-window change needs zero evals — just re-project. */
    float   fcol[GRAPH_W];         /* f(x_i), x_i = xmin + i*pxw */
    float   Fcol[GRAPH_W];         /* F(x_i) = integral from a */
    int16_t fy[GRAPH_W], Fy[GRAPH_W];

    /* Accumulation anchor: F(xref) == Fref. Keeps re-anchoring after a
     * zoom/pan cheap — only the gap to the new left edge is integrated,
     * never the whole way back to a. */
    float xref, Fref;
    uint8_t Fwarn;                 /* NaN columns folded to 0 in the sweep */

    uint8_t show_F;                /* accumulation-curve toggle */
    uint8_t tracing;
    int trace_col;                 /* 0..GRAPH_W-1 */
} GraphState;

void grf_init_palette(void);       /* call once after gfx_Begin */
void grf_reset_anchor(GraphState *g);            /* xref = a, Fref = 0 */
void grf_set_standard_window(GraphState *g);     /* -10..10, square px */
void grf_auto_window(GraphState *g);             /* fit [a,b] and y range */
void grf_recompute_all(GraphState *g);           /* f, F sweep, project */
int  grf_run(GraphState *g);                     /* keypadc loop; GRF_* */

#endif
