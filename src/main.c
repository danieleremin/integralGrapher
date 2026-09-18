#include <graphx.h>
#include <string.h>
#include <math.h>

#include "expr.h"
#include "eval.h"
#include "input.h"
#include "graph.h"

static GraphState G;
static char err_buf[36];

/* parse -> simplify -> compile; returns 0 on success */
static int compile_text(const char *text, ExprCode *out, char errsym[32]) {
    ASTNode *t = parser_init_from_string(text);
    if (!t) {
        strcpy(errsym, "syntax");
        return -1;
    }
    ASTNode *s = ast_simplify(t);   /* consumes t */
    int rc = ec_compile(s, out, errsym);
    ast_free_tree(s);
    return rc;
}

/* compile_text reports either a reason ("syntax", "too long", "too deep") or
 * the name of a symbol it doesn't know; turn both into something readable. */
static const char* explain(const char *sym) {
    if (strcmp(sym, "syntax") == 0)   return "Incomplete - check the expression";
    if (strcmp(sym, "too long") == 0) return "Expression is too long";
    if (strcmp(sym, "too deep") == 0) return "Expression is too complicated";
    strcpy(err_buf, "Don't know what ");
    strncat(err_buf, sym, sizeof err_buf - 20);
    strcat(err_buf, " is");
    return err_buf;
}

static const char* validate_f(const char *text) {
    ExprCode tmp;
    char sym[32];
    if (compile_text(text, &tmp, sym)) return explain(sym);
    return NULL;
}

static const char* validate_bound(const char *text, const char *x_err) {
    ExprCode tmp;
    char sym[32];
    ASTNode *t = parser_init_from_string(text);
    if (t) {
        int has_x = ast_contains_var(t, 'x');
        ast_free_tree(t);
        if (has_x) return x_err;
    }
    if (compile_text(text, &tmp, sym)) return explain(sym);
    float v = ec_eval(&tmp, 0.0f);
    if (isnan(v)) return "That value is undefined";
    if (fabsf(v) > 1e30f) return "Value too large";
    return NULL;
}

static const char* validate_a(const char *text) {
    return validate_bound(text, "x is not allowed here");
}

/* b may be exactly "x" (graph F(x) alone) or a constant. */
static const char* validate_b(const char *text) {
    if (strcmp(text, "x") == 0) return NULL;
    return validate_bound(text, "Use x by itself, or a number");
}

static float eval_const(const char *text) {
    ExprCode tmp;
    char sym[32];
    if (compile_text(text, &tmp, sym)) return 0.0f;
    return ec_eval(&tmp, 0.0f);
}

static int read_function(void) {
    InputCfg cfg = { "f(x) = ", 1, validate_f };
    char tmp[sizeof G.ftext];
    char sym[32];
    strcpy(tmp, G.ftext);
    int r = input_line(&cfg, tmp, sizeof tmp);
    if (r == INPUT_OK) {
        strcpy(G.ftext, tmp);
        compile_text(G.ftext, &G.fcode, sym);   /* validated: cannot fail */
    }
    return r;
}

/* a then b. CLEAR on an empty line steps back: b -> a -> caller, which
 * gets INPUT_CANCEL. Whatever was accepted along the way stays in G. */
static int read_bounds(void) {
    InputCfg cfg = { NULL, 0, NULL };
    char tmp[sizeof G.atext];
    int step = 0, r = INPUT_OK;

    while (step < 2) {
        if (step == 0) {
            cfg.title = "a = "; cfg.allow_x = 0; cfg.validate = validate_a;
            strcpy(tmp, G.atext);
        } else {
            cfg.title = "b = "; cfg.allow_x = 1; cfg.validate = validate_b;
            strcpy(tmp, G.btext);
        }
        r = input_line(&cfg, tmp, sizeof tmp);
        if (r == INPUT_QUIT) return r;
        if (r == INPUT_CANCEL) {
            if (step == 0) break;
            step = 0;
            continue;
        }
        if (step == 0) {
            strcpy(G.atext, tmp);
            G.a = eval_const(G.atext);
        } else {
            strcpy(G.btext, tmp);
            G.b_is_x = strcmp(G.btext, "x") == 0;
            if (G.b_is_x) G.show_F = 1;
            else G.b = eval_const(G.btext);
        }
        step++;
    }
    /* With b = x there is no fixed interval; parking b on a keeps the
     * window-fitting code (which only looks at [a,b]) happy. */
    if (G.b_is_x) G.b = G.a;
    return r;
}

static void refresh_integral(void) {
    if (G.b_is_x) {
        G.integral_ab = 0.0f;
        G.integral_warn = 0;
    } else {
        G.integral_ab = ec_integrate(&G.fcode, G.a, G.b, 512, &G.integral_warn);
    }
    grf_reset_anchor(&G);
    /* ------------------------------------------------------------------
     * SYMBOLIC HOOK: a symbolic antiderivative display would plug in here.
     * cePort's calculate_integral() / ast_derivative()
     * (reverse_engineering/cePort/src/integrate.c:785-1426) return an AST
     * that mathprint.c renders directly — parse G.ftext, run the rules, and
     * show "F(x) = ..." in the status area or a popup when a rule matches.
     * This build is numeric-only by design.
     * ------------------------------------------------------------------ */
}

int main(void) {
    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_SetTextConfig(gfx_text_clip);
    grf_init_palette();

    memset(&G, 0, sizeof G);      /* all three prompts start blank */
    G.show_F = 1;
    G.trace_col = GRAPH_W / 2;

    /* First run: every value must be typed. Backing out of a returns to
     * f; backing out of f quits. */
    for (;;) {
        if (read_function() != INPUT_OK) goto done;
        int rb = read_bounds();
        if (rb == INPUT_QUIT) goto done;
        if (rb == INPUT_OK) break;
    }
    refresh_integral();
    grf_auto_window(&G);

    for (;;) {
        int r = grf_run(&G);
        if (r == GRF_QUIT) break;

        if (r == GRF_REEDIT_F) {
            int rr = read_function();
            if (rr == INPUT_QUIT) break;
            if (rr == INPUT_OK) {
                refresh_integral();
                grf_recompute_all(&G);
            }
        } else if (r == GRF_REEDIT_AB) {
            int rr = read_bounds();
            if (rr == INPUT_QUIT) break;
            refresh_integral();
            /* Keep a window the user set up, unless the new region is
             * entirely off-screen — then they'd see no shading at all. */
            float lo = G.a < G.b ? G.a : G.b;
            float hi = G.a < G.b ? G.b : G.a;
            if (hi < G.vp.xmin || lo > G.vp.xmax) grf_auto_window(&G);
            else grf_recompute_all(&G);
        }
    }

done:
    gfx_End();
    return 0;
}
