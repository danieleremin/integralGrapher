#include "symbolic.h"

#include <stdlib.h>
#include <string.h>

/* Ported from integralCalc/src/integrate.c (ast_equal, ast_derivative and
 * the SYMBOLIC INTEGRATION section). Changes here: const-correct accessors
 * from expr.h, calculate_integral renamed to ast_integrate, the rule-7
 * linear branch reuses antideriv_func_of instead of building a throwaway
 * tree, and the NULL-return paths were checked for leaks - the overlay can
 * call this any number of times per session and the CE heap is small.
 * No stdio: this build links without printf. */

static int ast_is_var(const ASTNode* n, char var) {
    return n && n->type == NODE_SYM
        && n->data.sym_name[0] == var
        && n->data.sym_name[1] == '\0';
}

static ASTNode* ast_make_var(char var) {
    char s[2] = {var, '\0'};
    return ast_create_sym(s);
}

// ======================
// EQUALITY & DERIVATIVES
// ======================

int ast_equal(const ASTNode* a, const ASTNode* b) {
    if (!a && !b) return 1;
    if (!a || !b)  return 0;
    if (a->type != b->type) return 0;

    if (a->type == NODE_NUM) return a->data.num_value == b->data.num_value;
    if (a->type == NODE_SYM) return strcmp(a->data.sym_name, b->data.sym_name) == 0;

    // Commutative ops: ADD and MUL — try both child orderings.
    if (a->type == NODE_OP_ADD || a->type == NODE_OP_MUL) {
        ChildNode *ca = a->data.children;
        ChildNode *cb = b->data.children;
        if (!ca || !ca->next || !cb || !cb->next) return 0;
        ASTNode *a1 = ca->node, *a2 = ca->next->node;
        ASTNode *b1 = cb->node, *b2 = cb->next->node;
        if (ast_equal(a1, b1) && ast_equal(a2, b2)) return 1;
        if (ast_equal(a1, b2) && ast_equal(a2, b1)) return 1;
        return 0;
    }

    // Non-commutative ops: in-order list compare.
    ChildNode *ca = a->data.children;
    ChildNode *cb = b->data.children;
    while (ca && cb) {
        if (!ast_equal(ca->node, cb->node)) return 0;
        ca = ca->next;
        cb = cb->next;
    }
    return ca == NULL && cb == NULL;
}

ASTNode* ast_derivative(const ASTNode* n, char v) {
    if (!n) return NULL;

    switch (n->type) {
        case NODE_NUM:
            return ast_create_num(0.0);

        case NODE_SYM:
            if (n->data.sym_name[0] == v && n->data.sym_name[1] == '\0') {
                return ast_create_num(1.0);
            }
            return ast_create_num(0.0);

        case NODE_PAREN:
            return ast_derivative(ast_get_arg(n), v);

        case NODE_OP_ADD:
            return ast_create_op(NODE_OP_ADD,
                ast_derivative(ast_get_left(n), v),
                ast_derivative(ast_get_right(n), v));

        case NODE_OP_SUB:
            return ast_create_op(NODE_OP_SUB,
                ast_derivative(ast_get_left(n), v),
                ast_derivative(ast_get_right(n), v));

        case NODE_OP_MUL: {
            // (LR)' = L'R + LR'
            ASTNode *L = ast_get_left(n);
            ASTNode *R = ast_get_right(n);
            return ast_create_op(NODE_OP_ADD,
                ast_create_op(NODE_OP_MUL, ast_derivative(L, v), ast_clone(R)),
                ast_create_op(NODE_OP_MUL, ast_clone(L), ast_derivative(R, v)));
        }

        case NODE_OP_DIV: {
            // (L/R)' = (L'R - LR') / R^2
            ASTNode *L = ast_get_left(n);
            ASTNode *R = ast_get_right(n);
            return ast_create_op(NODE_OP_DIV,
                ast_create_op(NODE_OP_SUB,
                    ast_create_op(NODE_OP_MUL, ast_derivative(L, v), ast_clone(R)),
                    ast_create_op(NODE_OP_MUL, ast_clone(L), ast_derivative(R, v))),
                ast_create_op(NODE_OP_POW, ast_clone(R), ast_create_num(2.0)));
        }

        case NODE_OP_POW: {
            ASTNode *base = ast_get_left(n);
            ASTNode *expn = ast_get_right(n);
            // base^c with c constant in v: c * base^(c-1) * base'
            if (!ast_contains_var(expn, v)) {
                ASTNode *new_exp = ast_create_op(NODE_OP_SUB,
                    ast_clone(expn), ast_create_num(1.0));
                return ast_create_op(NODE_OP_MUL,
                    ast_create_op(NODE_OP_MUL,
                        ast_clone(expn),
                        ast_create_op(NODE_OP_POW, ast_clone(base), new_exp)),
                    ast_derivative(base, v));
            }
            // c^g with c constant in v: c^g * ln(c) * g'
            if (!ast_contains_var(base, v)) {
                return ast_create_op(NODE_OP_MUL,
                    ast_create_op(NODE_OP_MUL,
                        ast_clone(n),
                        ast_create_func(NODE_FUNC_LN, ast_clone(base))),
                    ast_derivative(expn, v));
            }
            return NULL;   // f(x)^g(x) — skipping
        }

        case NODE_FUNC_SIN: {
            ASTNode *arg = ast_get_arg(n);
            return ast_create_op(NODE_OP_MUL,
                ast_create_func(NODE_FUNC_COS, ast_clone(arg)),
                ast_derivative(arg, v));
        }
        case NODE_FUNC_COS: {
            ASTNode *arg = ast_get_arg(n);
            return ast_create_op(NODE_OP_MUL,
                ast_create_op(NODE_OP_SUB, ast_create_num(0.0),
                    ast_create_func(NODE_FUNC_SIN, ast_clone(arg))),
                ast_derivative(arg, v));
        }
        case NODE_FUNC_TAN: {
            ASTNode *arg = ast_get_arg(n);
            // f' / cos(f)^2
            return ast_create_op(NODE_OP_DIV,
                ast_derivative(arg, v),
                ast_create_op(NODE_OP_POW,
                    ast_create_func(NODE_FUNC_COS, ast_clone(arg)),
                    ast_create_num(2.0)));
        }
        case NODE_FUNC_EXP: {
            ASTNode *arg = ast_get_arg(n);
            return ast_create_op(NODE_OP_MUL,
                ast_create_func(NODE_FUNC_EXP, ast_clone(arg)),
                ast_derivative(arg, v));
        }
        case NODE_FUNC_LN: {
            ASTNode *arg = ast_get_arg(n);
            return ast_create_op(NODE_OP_DIV,
                ast_derivative(arg, v),
                ast_clone(arg));
        }
        case NODE_FUNC_SQRT: {
            ASTNode *arg = ast_get_arg(n);
            return ast_create_op(NODE_OP_DIV,
                ast_derivative(arg, v),
                ast_create_op(NODE_OP_MUL,
                    ast_create_num(2.0),
                    ast_create_func(NODE_FUNC_SQRT, ast_clone(arg))));
        }
        case NODE_FUNC_LOG: {
            // log10(f)' = f' / (f * ln(10))
            ASTNode *arg = ast_get_arg(n);
            return ast_create_op(NODE_OP_DIV,
                ast_derivative(arg, v),
                ast_create_op(NODE_OP_MUL,
                    ast_clone(arg),
                    ast_create_func(NODE_FUNC_LN, ast_create_num(10.0))));
        }
        case NODE_FUNC_ABS:
        default:
            return NULL;
    }
}

// ======================
// SYMBOLIC INTEGRATION
// ======================

// Try to express `expr` as a*v + b where a, b are independent of v.
// Returns 1 if matched and writes cloned subtrees into *a_out, *b_out.
// Returns 0 otherwise; *a_out and *b_out are left untouched.
static int extract_linear(const ASTNode* expr, char v, ASTNode** a_out, ASTNode** b_out) {
    if (!expr) return 0;
    // Case: pure constant w.r.t. v  →  0*v + expr   (not linear in v; reject)
    if (!ast_contains_var(expr, v)) return 0;
    // Case: bare variable  →  1*v + 0
    if (ast_is_var(expr, v)) {
        *a_out = ast_create_num(1.0);
        *b_out = ast_create_num(0.0);
        return 1;
    }
    // Case: c * v  or  v * c
    if (expr->type == NODE_OP_MUL) {
        const ASTNode* L = ast_get_left(expr);
        const ASTNode* R = ast_get_right(expr);
        if (!ast_contains_var(L, v) && ast_is_var(R, v)) {
            *a_out = ast_clone(L);
            *b_out = ast_create_num(0.0);
            return 1;
        }
        if (!ast_contains_var(R, v) && ast_is_var(L, v)) {
            *a_out = ast_clone(R);
            *b_out = ast_create_num(0.0);
            return 1;
        }
    }
    // Case: ADD/SUB — one side linear in v, other side independent
    if (expr->type == NODE_OP_ADD || expr->type == NODE_OP_SUB) {
        const ASTNode* L = ast_get_left(expr);
        const ASTNode* R = ast_get_right(expr);
        // L is linear, R is independent
        if (ast_contains_var(L, v) && !ast_contains_var(R, v)) {
            ASTNode *a, *b;
            if (extract_linear(L, v, &a, &b)) {
                *a_out = a;
                if (expr->type == NODE_OP_ADD) {
                    *b_out = ast_simplify(ast_create_op(NODE_OP_ADD, b, ast_clone(R)));
                } else {
                    *b_out = ast_simplify(ast_create_op(NODE_OP_SUB, b, ast_clone(R)));
                }
                return 1;
            }
        }
        // L is independent, R is linear
        if (!ast_contains_var(L, v) && ast_contains_var(R, v)) {
            ASTNode *a, *b;
            if (extract_linear(R, v, &a, &b)) {
                if (expr->type == NODE_OP_ADD) {
                    *a_out = a;
                    *b_out = ast_simplify(ast_create_op(NODE_OP_ADD, ast_clone(L), b));
                } else {
                    // L - (a*v + b) = -a*v + (L - b)
                    *a_out = ast_simplify(ast_create_op(NODE_OP_SUB, ast_create_num(0.0), a));
                    *b_out = ast_simplify(ast_create_op(NODE_OP_SUB, ast_clone(L), b));
                }
                return 1;
            }
        }
    }
    return 0;
}

// Antiderivative of an elementary unary function applied directly to v.
// Returns NULL if no rule applies.
static ASTNode* integrate_func_of_var(NodeType ftype, char v) {
    switch (ftype) {
        case NODE_FUNC_SIN:
            // -cos(v)
            return ast_create_op(NODE_OP_SUB, ast_create_num(0.0),
                ast_create_func(NODE_FUNC_COS, ast_make_var(v)));
        case NODE_FUNC_COS:
            return ast_create_func(NODE_FUNC_SIN, ast_make_var(v));
        case NODE_FUNC_TAN:
            // -ln|cos(v)|
            return ast_create_op(NODE_OP_SUB, ast_create_num(0.0),
                ast_create_func(NODE_FUNC_LN,
                    ast_create_func(NODE_FUNC_ABS,
                        ast_create_func(NODE_FUNC_COS, ast_make_var(v)))));
        case NODE_FUNC_EXP:
            return ast_create_func(NODE_FUNC_EXP, ast_make_var(v));
        case NODE_FUNC_LN:
            // v*ln(v) - v
            return ast_create_op(NODE_OP_SUB,
                ast_create_op(NODE_OP_MUL, ast_make_var(v),
                    ast_create_func(NODE_FUNC_LN, ast_make_var(v))),
                ast_make_var(v));
        default:
            return NULL;
    }
}

/* ===== u-substitution helpers ===== */

/* Flatten a MUL chain into a flat array of factor pointers. */
static int flatten_mul(const ASTNode *n, const ASTNode **factors, int max) {
    if (!n) return 0;
    if (n->type == NODE_PAREN) {
        return flatten_mul(ast_get_arg(n), factors, max);
    }
    if (n->type != NODE_OP_MUL) {
        if (max < 1) return 0;
        factors[0] = n;
        return 1;
    }
    int cnt = flatten_mul(ast_get_left(n),  factors,        max);
    cnt    += flatten_mul(ast_get_right(n), factors + cnt,  max - cnt);
    return cnt;
}

/* Build a freshly-cloned MUL chain from `factors` excluding index `skip`.
 * Returns NUM(1) if all factors are skipped. */
static ASTNode* build_product(const ASTNode **factors, int n, int skip) {
    ASTNode *result = NULL;
    for (int i = 0; i < n; i++) {
        if (i == skip) continue;
        ASTNode *c = ast_clone(factors[i]);
        result = result ? ast_create_op(NODE_OP_MUL, result, c) : c;
    }
    return result ? result : ast_create_num(1.0);
}

/* Check whether G == c * target where c is independent of v.
 * Returns a freshly allocated AST for c (cloned), or NULL on no match. */
static ASTNode* match_const_times(const ASTNode *G, const ASTNode *target, char v) {
    if (!G || !target) return NULL;

    if (ast_equal(G, target)) return ast_create_num(1.0);

    if (G->type == NODE_NUM && target->type == NODE_NUM
            && target->data.num_value != 0.0) {
        return ast_create_num(G->data.num_value / target->data.num_value);
    }

    /* G = k * target  (or target * k)  → c = k */
    if (G->type == NODE_OP_MUL) {
        const ASTNode *L = ast_get_left(G), *R = ast_get_right(G);
        if (!ast_contains_var(L, v) && ast_equal(R, target)) return ast_clone(L);
        if (!ast_contains_var(R, v) && ast_equal(L, target)) return ast_clone(R);
    }

    /* target = k * G  (or G * k)  → c = 1/k */
    if (target->type == NODE_OP_MUL) {
        const ASTNode *L = ast_get_left(target), *R = ast_get_right(target);
        if (!ast_contains_var(L, v) && ast_equal(G, R)) {
            return ast_simplify(ast_create_op(NODE_OP_DIV,
                ast_create_num(1.0), ast_clone(L)));
        }
        if (!ast_contains_var(R, v) && ast_equal(G, L)) {
            return ast_simplify(ast_create_op(NODE_OP_DIV,
                ast_create_num(1.0), ast_clone(R)));
        }
    }

    /* Both MULs with a shared symbolic factor: peel off the constant parts. */
    if (G->type == NODE_OP_MUL && target->type == NODE_OP_MUL) {
        const ASTNode *GL = ast_get_left(G),  *GR = ast_get_right(G);
        const ASTNode *TL = ast_get_left(target), *TR = ast_get_right(target);
        const ASTNode *gconst = NULL, *gvar = NULL, *tconst = NULL, *tvar = NULL;
        if (!ast_contains_var(GL, v))      { gconst = GL; gvar = GR; }
        else if (!ast_contains_var(GR, v)) { gconst = GR; gvar = GL; }
        if (!ast_contains_var(TL, v))      { tconst = TL; tvar = TR; }
        else if (!ast_contains_var(TR, v)) { tconst = TR; tvar = TL; }
        if (gconst && tconst && ast_equal(gvar, tvar)) {
            return ast_simplify(ast_create_op(NODE_OP_DIV,
                ast_clone(gconst), ast_clone(tconst)));
        }
    }

    return NULL;
}

/* Build the antiderivative AntiF(u) for FUNC types we know how to integrate. */
static ASTNode* antideriv_func_of(NodeType ftype, const ASTNode *u) {
    switch (ftype) {
        case NODE_FUNC_SIN:
            return ast_create_op(NODE_OP_SUB, ast_create_num(0.0),
                ast_create_func(NODE_FUNC_COS, ast_clone(u)));
        case NODE_FUNC_COS:
            return ast_create_func(NODE_FUNC_SIN, ast_clone(u));
        case NODE_FUNC_EXP:
            return ast_create_func(NODE_FUNC_EXP, ast_clone(u));
        case NODE_FUNC_TAN:
            return ast_create_op(NODE_OP_SUB, ast_create_num(0.0),
                ast_create_func(NODE_FUNC_LN,
                    ast_create_func(NODE_FUNC_ABS,
                        ast_create_func(NODE_FUNC_COS, ast_clone(u)))));
        case NODE_FUNC_LN:
            return ast_create_op(NODE_OP_SUB,
                ast_create_op(NODE_OP_MUL, ast_clone(u),
                    ast_create_func(NODE_FUNC_LN, ast_clone(u))),
                ast_clone(u));
        default:
            return NULL;
    }
}

/* Multiply `anti` by `c`; if c == 1 returns anti unchanged. Both consumed.
 * Reciprocal constants are folded into a stacked fraction so the result
 * displays as `anti/k` rather than `0.5 * anti`. */
static ASTNode* finish_usub(ASTNode *c, ASTNode *anti) {
    if (ast_num_eq(c, 1.0)) {
        ast_free_tree(c);
        return ast_simplify(anti);
    }
    if (c->type == NODE_OP_DIV && ast_num_eq(ast_get_left(c), 1.0)) {
        ASTNode *k = ast_clone(ast_get_right(c));
        ast_free_tree(c);
        return ast_simplify(ast_create_op(NODE_OP_DIV, anti, k));
    }
    if (c->type == NODE_NUM) {
        double cv = c->data.num_value;
        double inv = 1.0 / cv;
        if (cv != 0.0 && cv != 1.0 && inv == (long)inv) {
            long k = (long)inv;
            ast_free_tree(c);
            return ast_simplify(ast_create_op(NODE_OP_DIV,
                anti, ast_create_num((double)k)));
        }
    }
    return ast_simplify(ast_create_op(NODE_OP_MUL, c, anti));
}

/* Try ∫F · G dv where F is a FUNC(u) or POW(u, n) and G should be c · u'. */
static ASTNode* try_usub_func_pow(const ASTNode *F, const ASTNode *G, char v) {
    if (!F || !G) return NULL;
    if (F->type == NODE_PAREN) return try_usub_func_pow(ast_get_arg(F), G, v);

    NodeType ftype = F->type;
    const ASTNode *u = NULL, *expn = NULL;
    int is_pow = 0;

    if (ftype == NODE_FUNC_SIN || ftype == NODE_FUNC_COS ||
        ftype == NODE_FUNC_TAN || ftype == NODE_FUNC_EXP ||
        ftype == NODE_FUNC_LN) {
        u = ast_get_arg(F);
    } else if (ftype == NODE_OP_POW) {
        u    = ast_get_left(F);
        expn = ast_get_right(F);
        if (ast_contains_var(expn, v)) return NULL;
        is_pow = 1;
    } else {
        return NULL;
    }
    if (!u || !ast_contains_var(u, v)) return NULL;
    if (ast_is_var(u, v))              return NULL;   /* trivial case */

    ASTNode *u_prime = ast_simplify(ast_derivative(u, v));
    if (!u_prime) return NULL;

    ASTNode *c = match_const_times(G, u_prime, v);
    ast_free_tree(u_prime);
    if (!c) return NULL;

    ASTNode *anti;
    if (is_pow) {
        if (ast_num_eq(expn, -1.0)) {
            anti = ast_create_func(NODE_FUNC_LN,
                ast_create_func(NODE_FUNC_ABS, ast_clone(u)));
        } else {
            ASTNode *n1 = ast_simplify(ast_create_op(NODE_OP_ADD,
                ast_clone(expn), ast_create_num(1.0)));
            anti = ast_create_op(NODE_OP_DIV,
                ast_create_op(NODE_OP_POW, ast_clone(u), ast_clone(n1)),
                n1);
        }
    } else {
        anti = antideriv_func_of(ftype, u);
        if (!anti) { ast_free_tree(c); return NULL; }
    }
    return finish_usub(c, anti);
}

/* Try ∫u · G dv = c · u²/2 when G is c · u'.                                */
static ASTNode* try_identity_usub(const ASTNode *u_cand, const ASTNode *G, char v) {
    if (!u_cand || !G) return NULL;
    if (u_cand->type == NODE_PAREN) {
        return try_identity_usub(ast_get_arg(u_cand), G, v);
    }
    if (!ast_contains_var(u_cand, v)) return NULL;
    if (ast_is_var(u_cand, v))        return NULL;

    ASTNode *u_prime = ast_simplify(ast_derivative(u_cand, v));
    if (!u_prime) return NULL;

    ASTNode *c = match_const_times(G, u_prime, v);
    ast_free_tree(u_prime);
    if (!c) return NULL;

    ASTNode *anti = ast_create_op(NODE_OP_DIV,
        ast_create_op(NODE_OP_POW, ast_clone(u_cand), ast_create_num(2.0)),
        ast_create_num(2.0));
    return finish_usub(c, anti);
}

ASTNode* ast_integrate(const ASTNode* f, char v) {
    if (!f) return NULL;

    // Transparently unwrap explicit-paren markers.
    if (f->type == NODE_PAREN) {
        return ast_integrate(ast_get_arg(f), v);
    }

    // Rule 1: ∫c dv = c*v   (c independent of v)
    if (!ast_contains_var(f, v)) {
        return ast_create_op(NODE_OP_MUL, ast_clone(f), ast_make_var(v));
    }

    // Rule 2: ∫v dv = v^2/2
    if (ast_is_var(f, v)) {
        return ast_create_op(NODE_OP_DIV,
            ast_create_op(NODE_OP_POW, ast_make_var(v), ast_create_num(2.0)),
            ast_create_num(2.0));
    }

    // Rule 3: linearity over sum/difference
    if (f->type == NODE_OP_ADD || f->type == NODE_OP_SUB) {
        ASTNode* L = ast_integrate(ast_get_left(f), v);
        ASTNode* R = ast_integrate(ast_get_right(f), v);
        if (!L || !R) {
            ast_free_tree(L); ast_free_tree(R);
            return NULL;
        }
        return ast_create_op(f->type, L, R);
    }

    // Rule 4: constant factor — ∫c·g dv = c·∫g dv
    if (f->type == NODE_OP_MUL) {
        const ASTNode* L = ast_get_left(f);
        const ASTNode* R = ast_get_right(f);
        if (!ast_contains_var(L, v)) {
            ASTNode* IR = ast_integrate(R, v);
            if (!IR) return NULL;
            return ast_create_op(NODE_OP_MUL, ast_clone(L), IR);
        }
        if (!ast_contains_var(R, v)) {
            ASTNode* IL = ast_integrate(L, v);
            if (!IL) return NULL;
            return ast_create_op(NODE_OP_MUL, ast_clone(R), IL);
        }

        /* Both sides depend on v — attempt u-substitution.
         * Flatten chained MULs so `sin(x^2)*2*x` (parsed as
         * MUL(MUL(sin(x^2), 2), x)) still finds the right split. */
        const ASTNode *factors[16];
        int nf = flatten_mul(f, factors, 16);

        /* Pass 1: try each factor as f(u), where u is its inner expression. */
        for (int i = 0; i < nf; i++) {
            const ASTNode *F = factors[i];
            NodeType t = (F && F->type == NODE_PAREN)
                            ? ast_get_arg(F)->type : (F ? F->type : NODE_NUM);
            if (t != NODE_FUNC_SIN && t != NODE_FUNC_COS &&
                t != NODE_FUNC_TAN && t != NODE_FUNC_EXP &&
                t != NODE_FUNC_LN  && t != NODE_OP_POW) {
                continue;
            }
            ASTNode *G = build_product(factors, nf, i);
            ASTNode *res = try_usub_func_pow(F, G, v);
            ast_free_tree(G);
            if (res) return res;
        }

        /* Pass 2: try each factor as u itself (f(u) = u → result u^2/2). */
        for (int i = 0; i < nf; i++) {
            const ASTNode *u_cand = factors[i];
            if (!u_cand) continue;
            if (u_cand->type == NODE_NUM || u_cand->type == NODE_SYM) continue;
            ASTNode *G = build_product(factors, nf, i);
            ASTNode *res = try_identity_usub(u_cand, G, v);
            ast_free_tree(G);
            if (res) return res;
        }

        return NULL;
    }

    // Rule 5: ∫g/c dv = (∫g)/c when c independent of v
    if (f->type == NODE_OP_DIV) {
        const ASTNode* L = ast_get_left(f);
        const ASTNode* R = ast_get_right(f);
        if (!ast_contains_var(R, v)) {
            ASTNode* IL = ast_integrate(L, v);
            if (!IL) return NULL;
            return ast_create_op(NODE_OP_DIV, IL, ast_clone(R));
        }
        // ∫c/v dv = c*ln|v|  when L indep of v, R == v
        if (!ast_contains_var(L, v) && ast_is_var(R, v)) {
            return ast_create_op(NODE_OP_MUL,
                ast_clone(L),
                ast_create_func(NODE_FUNC_LN,
                    ast_create_func(NODE_FUNC_ABS, ast_make_var(v))));
        }

        /* u-sub: ∫(c·g')/g dv = c·ln|g|. */
        {
            ASTNode *u_prime = ast_simplify(ast_derivative(R, v));
            if (u_prime) {
                ASTNode *c = match_const_times(L, u_prime, v);
                ast_free_tree(u_prime);
                if (c) {
                    ASTNode *anti = ast_create_func(NODE_FUNC_LN,
                        ast_create_func(NODE_FUNC_ABS, ast_clone(R)));
                    return finish_usub(c, anti);
                }
            }
        }
        return NULL;
    }

    // Rule 6: power — ∫(linear-in-v)^n dv = (linear)^(n+1) / (a*(n+1)), n ≠ -1
    //         and  ∫v^(-1) dv = ln|v|
    if (f->type == NODE_OP_POW) {
        const ASTNode* base = ast_get_left(f);
        const ASTNode* exp  = ast_get_right(f);
        if (!ast_contains_var(exp, v)) {
            // Reciprocal: base == v and exp == -1
            if (ast_is_var(base, v) && ast_num_eq(exp, -1.0)) {
                return ast_create_func(NODE_FUNC_LN,
                    ast_create_func(NODE_FUNC_ABS, ast_make_var(v)));
            }
            // (linear)^n
            ASTNode *a, *b;
            if (extract_linear(base, v, &a, &b)) {
                // n == -1: ln|a*v + b| / a  (the general formula would
                // divide by n+1 == 0)
                if (ast_num_eq(exp, -1.0)) {
                    ASTNode* lin = ast_simplify(
                        ast_create_op(NODE_OP_ADD,
                            ast_create_op(NODE_OP_MUL, ast_clone(a), ast_make_var(v)),
                            b));
                    return ast_create_op(NODE_OP_DIV,
                        ast_create_func(NODE_FUNC_LN,
                            ast_create_func(NODE_FUNC_ABS, lin)),
                        a);
                }
                // n+1
                ASTNode* n1 = ast_simplify(
                    ast_create_op(NODE_OP_ADD, ast_clone(exp), ast_create_num(1.0)));
                // (a*v + b)^(n+1)
                ASTNode* lin = ast_simplify(
                    ast_create_op(NODE_OP_ADD,
                        ast_create_op(NODE_OP_MUL, ast_clone(a), ast_make_var(v)),
                        ast_clone(b)));
                ASTNode* numerator = ast_create_op(NODE_OP_POW, lin, ast_clone(n1));
                // denominator = a*(n+1)
                ASTNode* denom = ast_simplify(
                    ast_create_op(NODE_OP_MUL, a, n1));
                ast_free_tree(b);
                return ast_create_op(NODE_OP_DIV, numerator, denom);
            }
        }
        return NULL;
    }

    // Rule 7: elementary function of a linear expression
    if (node_is_func(f->type)) {
        const ASTNode* arg = ast_get_arg(f);
        // Bare variable argument: ∫g(v) dv directly
        if (ast_is_var(arg, v)) {
            return integrate_func_of_var(f->type, v);
        }
        // Linear u-sub: arg = a*v + b → F(a*v+b)/a
        ASTNode *a, *b;
        if (extract_linear(arg, v, &a, &b)) {
            // F(a*v + b) / a, where F is the antiderivative of f's function.
            ASTNode* lin = ast_simplify(
                ast_create_op(NODE_OP_ADD,
                    ast_create_op(NODE_OP_MUL, ast_clone(a), ast_make_var(v)),
                    ast_clone(b)));
            ASTNode* substituted = antideriv_func_of(f->type, lin);
            ast_free_tree(lin);
            ast_free_tree(b);
            if (!substituted) {
                ast_free_tree(a);
                return NULL;
            }
            // Divide by a
            return ast_create_op(NODE_OP_DIV, substituted, a);
        }
        return NULL;
    }

    return NULL;
}
