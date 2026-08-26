#include "eval.h"
#include <string.h>
#include <math.h>

/* ============================================================
 * Compiler: post-order AST walk -> postfix tape
 * ============================================================ */

typedef struct {
    ExprCode *ec;
    char *errsym;
    int depth, maxdepth;   /* value-stack depth tracking */
    int err;
} Compiler;

static void comp_fail(Compiler *c, const char *why) {
    if (!c->err) {
        c->err = 1;
        strncpy(c->errsym, why, 31);
        c->errsym[31] = '\0';
    }
}

static void emit(Compiler *c, uint8_t byte) {
    if (c->err) return;
    if (c->ec->ncode >= EC_MAX_CODE) { comp_fail(c, "too long"); return; }
    c->ec->code[c->ec->ncode++] = byte;
}

static void push_tracked(Compiler *c) {
    c->depth++;
    if (c->depth > c->maxdepth) c->maxdepth = c->depth;
    if (c->depth > EC_MAX_STACK) comp_fail(c, "too deep");
}

static void emit_const(Compiler *c, float v) {
    if (c->err) return;
    ExprCode *ec = c->ec;
    int idx = -1;
    for (int i = 0; i < ec->nconsts; i++) {
        if (ec->consts[i] == v) { idx = i; break; }
    }
    if (idx < 0) {
        if (ec->nconsts >= EC_MAX_CONSTS) { comp_fail(c, "too long"); return; }
        idx = ec->nconsts;
        ec->consts[ec->nconsts++] = v;
    }
    emit(c, EC_CONST);
    emit(c, (uint8_t)idx);
    push_tracked(c);
}

static int num_as_small_int(const ASTNode *n, int *out) {
    if (!n || n->type != NODE_NUM) return 0;
    double v = n->data.num_value;
    if (v != (int)v) return 0;
    int iv = (int)v;
    if (iv == 0 || iv < -12 || iv > 12) return 0;
    *out = iv;
    return 1;
}

static void compile_node(Compiler *c, const ASTNode *n) {
    if (c->err) return;
    if (!n) { comp_fail(c, "syntax"); return; }

    switch (n->type) {
        case NODE_NUM:
            emit_const(c, (float)n->data.num_value);
            return;

        case NODE_SYM: {
            const char *s = n->data.sym_name;
            if (s[0] == 'x' && s[1] == '\0') {
                emit(c, EC_X);
                push_tracked(c);
            } else if (strcmp(s, "pi") == 0) {
                emit_const(c, 3.14159265f);
            } else if (s[0] == 'e' && s[1] == '\0') {
                emit_const(c, 2.71828183f);
            } else {
                comp_fail(c, s);   /* unknown symbol, surfaced to the editor */
            }
            return;
        }

        case NODE_PAREN:   /* stripped by ast_simplify, but stay safe */
            compile_node(c, ast_get_arg(n));
            return;

        case NODE_OP_SUB: {
            const ASTNode *L = ast_get_left(n);
            /* Canonical unary minus is SUB(0, u). */
            if (L && L->type == NODE_NUM && L->data.num_value == 0.0) {
                compile_node(c, ast_get_right(n));
                emit(c, EC_NEG);
                return;
            }
            compile_node(c, L);
            compile_node(c, ast_get_right(n));
            emit(c, EC_SUB);
            c->depth--;
            return;
        }

        case NODE_OP_POW: {
            int ip;
            if (num_as_small_int(ast_get_right(n), &ip)) {
                /* ^1 and ^0 are folded away by ast_simplify already */
                compile_node(c, ast_get_left(n));
                emit(c, EC_POWI);
                emit(c, (uint8_t)(int8_t)ip);
                return;
            }
            compile_node(c, ast_get_left(n));
            compile_node(c, ast_get_right(n));
            emit(c, EC_POW);
            c->depth--;
            return;
        }

        case NODE_OP_ADD:
        case NODE_OP_MUL:
        case NODE_OP_DIV: {
            compile_node(c, ast_get_left(n));
            compile_node(c, ast_get_right(n));
            emit(c, n->type == NODE_OP_ADD ? EC_ADD :
                    n->type == NODE_OP_MUL ? EC_MUL : EC_DIV);
            c->depth--;
            return;
        }

        case NODE_FUNC_SIN:  case NODE_FUNC_COS:  case NODE_FUNC_TAN:
        case NODE_FUNC_EXP:  case NODE_FUNC_LN:   case NODE_FUNC_LOG:
        case NODE_FUNC_SQRT: case NODE_FUNC_ABS:
            compile_node(c, ast_get_arg(n));
            emit(c, (uint8_t)(EC_SIN + (n->type - NODE_FUNC_SIN)));
            return;

        default:
            comp_fail(c, "syntax");
            return;
    }
}

int ec_compile(const ASTNode *simplified, ExprCode *out, char errsym[32]) {
    Compiler c;
    out->ncode = 0;
    out->nconsts = 0;
    errsym[0] = '\0';
    c.ec = out;
    c.errsym = errsym;
    c.depth = c.maxdepth = 0;
    c.err = 0;

    compile_node(&c, simplified);
    if (!c.err && c.depth != 1) comp_fail(&c, "syntax");
    return c.err ? -1 : 0;
}

/* ============================================================
 * Interpreter
 * ============================================================ */

float ec_eval(const ExprCode *ec, float x) {
    float st[EC_MAX_STACK];
    int sp = 0;
    const uint8_t *code = ec->code;
    int n = ec->ncode;

    for (int pc = 0; pc < n; pc++) {
        switch (code[pc]) {
            case EC_CONST: st[sp++] = ec->consts[code[++pc]]; break;
            case EC_X:     st[sp++] = x; break;

            case EC_ADD: sp--; st[sp-1] += st[sp]; break;
            case EC_SUB: sp--; st[sp-1] -= st[sp]; break;
            case EC_MUL: sp--; st[sp-1] *= st[sp]; break;
            case EC_DIV:
                sp--;
                if (fabsf(st[sp]) < 1e-30f) st[sp-1] = NAN;
                else st[sp-1] /= st[sp];
                break;

            case EC_NEG: st[sp-1] = -st[sp-1]; break;

            case EC_POWI: {
                int8_t e = (int8_t)code[++pc];
                float base = st[sp-1];
                int  k = e < 0 ? -e : e;
                float r = base;
                for (int i = 1; i < k; i++) r *= base;
                if (e < 0) {
                    if (fabsf(r) < 1e-30f) r = NAN;
                    else r = 1.0f / r;
                }
                st[sp-1] = r;
                break;
            }

            case EC_POW: {
                sp--;
                float base = st[sp-1], ex = st[sp];
                if (base < 0.0f || (base == 0.0f && ex <= 0.0f)) st[sp-1] = NAN;
                else st[sp-1] = powf(base, ex);
                break;
            }

            case EC_SIN:  st[sp-1] = sinf(st[sp-1]); break;
            case EC_COS:  st[sp-1] = cosf(st[sp-1]); break;
            case EC_TAN:  st[sp-1] = tanf(st[sp-1]); break;
            case EC_EXP:  st[sp-1] = expf(st[sp-1]); break;
            case EC_LN:   st[sp-1] = (st[sp-1] <= 0.0f) ? NAN : logf(st[sp-1]); break;
            case EC_LOG:  st[sp-1] = (st[sp-1] <= 0.0f) ? NAN : log10f(st[sp-1]); break;
            case EC_SQRT: st[sp-1] = (st[sp-1] <  0.0f) ? NAN : sqrtf(st[sp-1]); break;
            case EC_ABS:  st[sp-1] = fabsf(st[sp-1]); break;
        }
    }
    return st[0];
}

/* ============================================================
 * Composite Simpson quadrature
 * ============================================================ */

float ec_integrate(const ExprCode *ec, float a, float b, int n, uint8_t *warn_out) {
    if (warn_out) *warn_out = 0;
    if (a == b) return 0.0f;
    if (n < 4) n = 4;
    n &= ~3;                         /* multiple of 4: the half-resolution
                                        check below needs an even n/2 */

    float h = (b - a) / (float)n;
    float sum = 0.0f, comp = 0.0f;   /* Kahan accumulator */
    float coarse = 0.0f;             /* same rule at half resolution */
    int half = n / 2;

    for (int i = 0; i <= n; i++) {
        float xi = a + (float)i * h;
        float v = ec_eval(ec, xi);
        if (isnan(v)) {
            /* Nudge off point singularities (e.g. ln(x) at an endpoint).
             * Either way the result is approximate here, so flag it. */
            if (warn_out) *warn_out = 1;
            v = ec_eval(ec, xi + 1e-4f * h);
            if (isnan(v)) v = ec_eval(ec, xi - 1e-4f * h);
            if (isnan(v)) v = 0.0f;
        }
        float w = (i == 0 || i == n) ? 1.0f : ((i & 1) ? 4.0f : 2.0f);

        float term = w * v - comp;
        float t = sum + term;
        comp = (t - sum) - term;
        sum = t;

        if (!(i & 1)) {              /* even samples form the coarse rule */
            int j = i / 2;
            coarse += ((j == 0 || j == half) ? 1.0f : ((j & 1) ? 4.0f : 2.0f)) * v;
        }
    }

    float fine = sum * h / 3.0f;
    coarse *= 2.0f * h / 3.0f;

    /* Halving the step should barely move a converged result. A big gap
     * means a pole or something too wiggly to resolve — the number is then
     * an artifact of where the samples happened to land, so flag it. */
    float diff = fabsf(fine - coarse);
    if (isnan(diff) || isinf(fine) || diff > 0.01f * fabsf(fine) + 1e-20f) {
        if (warn_out) *warn_out = 1;
    }

    return fine;
}
