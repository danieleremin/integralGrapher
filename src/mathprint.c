#include "mathprint.h"
#include "fmt.h"
#include <graphx.h>
#include <string.h>

/* Default graphx 8x8 monospace font. */
#define CW 8
#define CH 8

/* ============================================================
 * Helpers
 * ============================================================ */

static int is_func(NodeType t)   { return t >= NODE_FUNC_SIN && t <= NODE_FUNC_ABS; }
static int has_kids(NodeType t)  { return t != NODE_NUM && t != NODE_SYM; }

/* Children list order from ast_create_op() is right-then-left,
 * so the first list element is the RIGHT operand. */
static const ASTNode* kid0(const ASTNode *n) {
    if (!n || !has_kids(n->type) || !n->data.children) return NULL;
    return n->data.children->node;
}
static const ASTNode* kid1(const ASTNode *n) {
    if (!n || !has_kids(n->type) || !n->data.children || !n->data.children->next) return NULL;
    return n->data.children->next->node;
}
static const ASTNode* op_left (const ASTNode *n) { return kid1(n); }
static const ASTNode* op_right(const ASTNode *n) { return kid0(n); }
static const ASTNode* func_arg(const ASTNode *n) { return kid0(n); }

static int num_eq(const ASTNode *n, double v) {
    return n && n->type == NODE_NUM && n->data.num_value == v;
}

/* fmt_g renders integers without a decimal point, so it covers both cases;
 * it also avoids linking printf, whose %g on the CE prints as %f. */
static int num_to_str(const ASTNode *n, char *buf, int cap) {
    (void)cap;                     /* fmt_g writes at most 16 bytes */
    return fmt_g(buf, (float)n->data.num_value);
}

static const char* fname(NodeType t) {
    switch (t) {
        case NODE_FUNC_SIN:  return "sin";
        case NODE_FUNC_COS:  return "cos";
        case NODE_FUNC_TAN:  return "tan";
        case NODE_FUNC_EXP:  return "exp";
        case NODE_FUNC_LN:   return "ln";
        case NODE_FUNC_LOG:  return "log";
        case NODE_FUNC_ABS:  return "abs";
        default: return "?";
    }
}

/* Precedence for paren decisions. Higher = binds tighter / more atomic.
 * DIV is stacked and POW puts its exponent in superscript, so both are
 * treated as visually atomic — surrounding ops never need to parenthesise
 * a stacked fraction or a power as a whole. */
static int prec_of(const ASTNode *n) {
    if (!n) return 100;
    switch (n->type) {
        case NODE_OP_ADD:
        case NODE_OP_SUB: return 1;
        case NODE_OP_MUL: return 2;
        case NODE_OP_POW: return 3;
        case NODE_OP_DIV: return 100;
        default: return 100;
    }
}

static int parens_needed(const ASTNode *n, int parent_prec) {
    return prec_of(n) < parent_prec;
}

/* ============================================================
 * Measure
 * ============================================================ */

static void measure_raw(const ASTNode *n, MPBox *out);

static void measure(const ASTNode *n, int pp, MPBox *out) {
    measure_raw(n, out);
    if (parens_needed(n, pp)) {
        out->w += 2 * CW;   /* '(' and ')' */
    }
}

/* How far the rendered bbox extends past the baseline content on the right.
 * For x^n the bbox is base + superscript, but the baseline ends at the base —
 * so an operator placed at bbox_right looks too far away from the visible
 * baseline content. We shrink the leading space by this amount to compensate. */
static int right_pad(const ASTNode *n, int parent_prec);

static int right_pad_raw(const ASTNode *n) {
    if (!n) return 0;
    if (n->type == NODE_OP_POW) {
        MPBox E;
        measure(op_right(n), 0, &E);
        return E.w;
    }
    if (n->type == NODE_OP_ADD || n->type == NODE_OP_SUB) {
        if (n->type == NODE_OP_SUB && num_eq(op_left(n), 0.0)) {
            return right_pad(op_right(n), 3);
        }
        int rprec = (n->type == NODE_OP_SUB) ? 2 : 1;
        return right_pad(op_right(n), rprec);
    }
    if (n->type == NODE_OP_MUL) {
        return right_pad(op_right(n), 2);
    }
    return 0;
}

static int right_pad(const ASTNode *n, int parent_prec) {
    if (parens_needed(n, parent_prec)) return 0;  /* ')' is at baseline */
    return right_pad_raw(n);
}

static int leading_gap(const ASTNode *L, int lpp) {
    int rp = right_pad(L, lpp);
    int g = CW - rp;
    return g < 0 ? 0 : g;
}

static void measure_raw(const ASTNode *n, MPBox *out) {
    if (!n) { out->w = 0; out->h = CH; out->baseline = CH; return; }

    switch (n->type) {
        case NODE_NUM: {
            char buf[32];
            int len = num_to_str(n, buf, sizeof(buf));
            out->w = len * CW; out->h = CH; out->baseline = CH;
            return;
        }
        case NODE_SYM: {
            out->w = (int)strlen(n->data.sym_name) * CW;
            out->h = CH; out->baseline = CH;
            return;
        }

        case NODE_OP_ADD:
        case NODE_OP_SUB: {
            /* Render SUB(0, x) as unary "-x". */
            if (n->type == NODE_OP_SUB && num_eq(op_left(n), 0.0)) {
                MPBox R;
                measure(op_right(n), 3, &R);
                out->w = CW + R.w;
                out->h = R.h;
                out->baseline = R.baseline;
                return;
            }
            MPBox L, R;
            int rprec = (n->type == NODE_OP_SUB) ? 2 : 1;
            measure(op_left(n), 1, &L);
            measure(op_right(n), rprec, &R);
            int above = L.baseline > R.baseline ? L.baseline : R.baseline;
            int below_l = L.h - L.baseline;
            int below_r = R.h - R.baseline;
            int below = below_l > below_r ? below_l : below_r;
            out->h = above + below;
            out->baseline = above;
            int lead = leading_gap(op_left(n), 1);
            out->w = L.w + lead + CW + CW + R.w;   /* lead + op + trail */
            return;
        }

        case NODE_OP_MUL: {
            MPBox L, R;
            measure(op_left(n), 2, &L);
            measure(op_right(n), 2, &R);
            int above = L.baseline > R.baseline ? L.baseline : R.baseline;
            int below_l = L.h - L.baseline;
            int below_r = R.h - R.baseline;
            int below = below_l > below_r ? below_l : below_r;
            out->h = above + below;
            out->baseline = above;
            const ASTNode *LL = op_left(n);
            const ASTNode *RR = op_right(n);
            int jux = LL && LL->type == NODE_NUM && RR &&
                (RR->type == NODE_SYM || is_func(RR->type) ||
                 RR->type == NODE_OP_POW || RR->type == NODE_FUNC_SQRT);
            out->w = L.w + (jux ? 0 : CW) + R.w;   /* either nothing or "*" */
            return;
        }

        case NODE_OP_DIV: {
            MPBox N, D;
            measure(op_left(n), 0, &N);
            measure(op_right(n), 0, &D);
            int w = (N.w > D.w ? N.w : D.w) + 4;
            out->w = w;
            out->h = N.h + 3 + D.h;
            /* Place baseline so the bar sits ~midway through the surrounding
             * text x-height: bar at y=N.h+1, baseline 4 below the bar. */
            out->baseline = N.h + 5;
            return;
        }

        case NODE_OP_POW: {
            MPBox B, E;
            measure(op_left(n), 4, &B);   /* base: parens for ADD/SUB/MUL/POW */
            measure(op_right(n), 0, &E);
            int lift = B.h / 2;
            if (lift < 4) lift = 4;
            int base_top = (E.h > lift) ? (E.h - lift) : 0;
            out->w = B.w + E.w;
            out->h = base_top + B.h;
            out->baseline = base_top + B.baseline;
            return;
        }

        case NODE_FUNC_SQRT: {
            MPBox A;
            measure(func_arg(n), 0, &A);
            out->w = 6 + A.w + 2;
            out->h = A.h + 3;
            out->baseline = 3 + A.baseline;
            return;
        }

        case NODE_FUNC_ABS: {
            MPBox A;
            measure(func_arg(n), 0, &A);
            out->w = 4 + A.w + 4;       /* | + arg + | */
            out->h = A.h;
            out->baseline = A.baseline;
            return;
        }

        case NODE_PAREN: {
            MPBox A;
            measure(func_arg(n), 0, &A);
            out->w = 2 * CW + A.w;
            out->h = A.h;
            out->baseline = A.baseline;
            return;
        }

        default: {  /* sin/cos/tan/exp/ln/log */
            MPBox A;
            measure(func_arg(n), 0, &A);
            int name_w = (int)strlen(fname(n->type)) * CW;
            int above = (CH > A.baseline) ? CH : A.baseline;
            int below_t = 0;
            int below_a = A.h - A.baseline;
            int below = below_t > below_a ? below_t : below_a;
            out->h = above + below;
            out->baseline = above;
            out->w = name_w + CW + A.w + CW;   /* name + "(" + arg + ")" */
            return;
        }
    }
}

void mp_measure(const ASTNode *n, MPBox *out) {
    measure(n, 0, out);
}

/* ============================================================
 * Draw
 * ============================================================ */

static void draw_raw(const ASTNode *n, int x, int yt);

static void put_text_baseline(const char *s, int x, int baseline) {
    gfx_PrintStringXY(s, x, baseline - CH);
}

/* Paren. For font-height content we use the built-in glyph; for taller
 * content (fractions / superscript composites) we trace a parabolic curve
 * so it reads as a smooth '(' / ')' instead of a bracket-like bar. */
static int draw_paren(int which, int x, int y_top, int h) {
    int center, denom, maxoff, prev, r;

    if (h <= CH) {
        gfx_PrintStringXY(which == 0 ? "(" : ")", x, y_top);
        return CW;
    }

    /* Offset from the bulge column is 0 at the vertical middle and grows
     * quadratically to `maxoff` at the top and bottom — a paren profile. */
    center = (h - 1) / 2;
    denom  = center * center;
    maxoff = h / 5;
    if (maxoff < 2) maxoff = 2;
    if (maxoff > 5) maxoff = 5;

    prev = -1;
    for (r = 0; r < h; r++) {
        int d   = r - center;
        int off = denom ? (maxoff * d * d + denom / 2) / denom : 0;
        int px;
        if (off > maxoff) off = maxoff;
        /* Left paren bulges left at the middle; right paren mirrors it. */
        px = (which == 0) ? (x + 1 + off) : (x + 1 + (maxoff - off));
        /* Connect to the previous row so steep sections near the ends
         * don't leave gaps. */
        if (prev >= 0 && px != prev) {
            int lo = px < prev ? px : prev;
            int hi = px < prev ? prev : px;
            gfx_HorizLine(lo, y_top + r, hi - lo + 1);
        } else {
            gfx_SetPixel(px, y_top + r);
        }
        prev = px;
    }
    return CW;
}

static void draw(const ASTNode *n, int x, int yt, int pp) {
    if (parens_needed(n, pp)) {
        MPBox b;
        measure_raw(n, &b);
        draw_paren(0, x, yt, b.h);
        draw_raw(n, x + CW, yt);
        draw_paren(1, x + CW + b.w, yt, b.h);
    } else {
        draw_raw(n, x, yt);
    }
}

static void draw_raw(const ASTNode *n, int x, int yt) {
    if (!n) return;
    MPBox box;
    measure_raw(n, &box);
    int bl = yt + box.baseline;

    switch (n->type) {
        case NODE_NUM: {
            char buf[32];
            num_to_str(n, buf, sizeof(buf));
            gfx_PrintStringXY(buf, x, yt);
            return;
        }
        case NODE_SYM: {
            gfx_PrintStringXY(n->data.sym_name, x, yt);
            return;
        }

        case NODE_OP_ADD:
        case NODE_OP_SUB: {
            if (n->type == NODE_OP_SUB && num_eq(op_left(n), 0.0)) {
                MPBox R;
                measure(op_right(n), 3, &R);
                put_text_baseline("-", x, bl);
                int Ryt = bl - R.baseline;
                draw(op_right(n), x + CW, Ryt, 3);
                return;
            }
            const ASTNode *L = op_left(n), *R = op_right(n);
            MPBox LB, RB;
            int lpp = 1, rpp = (n->type == NODE_OP_SUB) ? 2 : 1;
            measure(L, lpp, &LB);
            measure(R, rpp, &RB);
            int Lyt = bl - LB.baseline;
            int Ryt = bl - RB.baseline;
            int lead = leading_gap(L, lpp);
            int cur = x;
            draw(L, cur, Lyt, lpp); cur += LB.w;
            cur += lead;
            put_text_baseline(n->type == NODE_OP_ADD ? "+ " : "- ", cur, bl);
            cur += 2*CW;
            draw(R, cur, Ryt, rpp);
            return;
        }

        case NODE_OP_MUL: {
            const ASTNode *L = op_left(n), *R = op_right(n);
            MPBox LB, RB;
            measure(L, 2, &LB);
            measure(R, 2, &RB);
            int Lyt = bl - LB.baseline;
            int Ryt = bl - RB.baseline;
            int jux = L && L->type == NODE_NUM && R &&
                (R->type == NODE_SYM || is_func(R->type) ||
                 R->type == NODE_OP_POW || R->type == NODE_FUNC_SQRT);
            int cur = x;
            draw(L, cur, Lyt, 2); cur += LB.w;
            if (!jux) {
                put_text_baseline("*", cur, bl);
                cur += CW;
            }
            draw(R, cur, Ryt, 2);
            return;
        }

        case NODE_OP_DIV: {
            const ASTNode *N = op_left(n), *D = op_right(n);
            MPBox NB, DB;
            measure(N, 0, &NB);
            measure(D, 0, &DB);
            int total_w = box.w;
            int Nx = x + (total_w - NB.w) / 2;
            int Dx = x + (total_w - DB.w) / 2;
            int Nyt = yt;
            int bar_y = yt + NB.h + 1;
            int Dyt = yt + NB.h + 3;
            draw(N, Nx, Nyt, 0);
            gfx_HorizLine(x, bar_y, total_w);
            draw(D, Dx, Dyt, 0);
            return;
        }

        case NODE_OP_POW: {
            const ASTNode *B = op_left(n), *E = op_right(n);
            MPBox BB, EB;
            measure(B, 4, &BB);
            measure(E, 0, &EB);
            int lift = BB.h / 2;
            if (lift < 4) lift = 4;
            int base_top = (EB.h > lift) ? (EB.h - lift) : 0;
            int exp_top  = base_top - lift;
            if (exp_top < 0) exp_top = 0;
            draw(B, x,           yt + base_top, 4);
            draw(E, x + BB.w,    yt + exp_top,  0);
            return;
        }

        case NODE_FUNC_SQRT: {
            const ASTNode *A = func_arg(n);
            MPBox AB;
            measure(A, 0, &AB);
            int arg_top = yt + 3;
            int top = yt;
            int bot = yt + AB.h + 2;
            /* Radical: short diagonal up into a long horizontal overline. */
            gfx_Line(x,     bot - 3, x + 1, bot - 1);
            gfx_Line(x + 1, bot - 1, x + 3, top);
            gfx_HorizLine(x + 3, top, AB.w + 3);
            draw(A, x + 6, arg_top, 0);
            return;
        }

        case NODE_FUNC_ABS: {
            const ASTNode *A = func_arg(n);
            MPBox AB;
            measure(A, 0, &AB);
            gfx_VertLine(x + 1,         yt, AB.h);
            draw(A, x + 4, yt, 0);
            gfx_VertLine(x + 4 + AB.w + 1, yt, AB.h);
            return;
        }

        case NODE_PAREN: {
            const ASTNode *A = func_arg(n);
            MPBox AB;
            measure(A, 0, &AB);
            draw_paren(0, x, yt, AB.h);
            draw(A, x + CW, yt, 0);
            draw_paren(1, x + CW + AB.w, yt, AB.h);
            return;
        }

        default: {  /* sin/cos/tan/exp/ln/log as name(arg) */
            const ASTNode *A = func_arg(n);
            MPBox AB;
            measure(A, 0, &AB);
            const char *nm = fname(n->type);
            int cur = x;
            put_text_baseline(nm, cur, bl);
            cur += (int)strlen(nm) * CW;
            int arg_yt = bl - AB.baseline;
            draw_paren(0, cur, arg_yt, AB.h); cur += CW;
            draw(A, cur, arg_yt, 0);          cur += AB.w;
            draw_paren(1, cur, arg_yt, AB.h);
            return;
        }
    }
}

void mp_draw(const ASTNode *n, int x, int y_top) {
    draw(n, x, y_top, 0);
}

/* Hand-drawn integral sign: vertical stem with hooks top-right and
 * bottom-left, scaled to height `h`. */
void mp_draw_integral_sign(int x, int y_top, int h) {
    if (h < 10) h = 10;
    int top = y_top;
    int bot = y_top + h - 1;
    int xmid = x + 3;
    /* top serif */
    gfx_SetPixel(xmid + 2, top);
    gfx_SetPixel(xmid + 3, top);
    gfx_SetPixel(xmid + 1, top + 1);
    gfx_SetPixel(xmid + 2, top + 1);
    /* main stem */
    gfx_VertLine(xmid, top + 2, h - 4);
    /* bottom serif */
    gfx_SetPixel(xmid - 1, bot - 1);
    gfx_SetPixel(xmid - 2, bot - 1);
    gfx_SetPixel(xmid - 2, bot);
    gfx_SetPixel(xmid - 3, bot);
}
