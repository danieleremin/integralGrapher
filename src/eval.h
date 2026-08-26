#ifndef EVAL_H
#define EVAL_H

#include "expr.h"

/* Numeric expression engine: the simplified AST is compiled once into a
 * flat postfix bytecode tape, then evaluated by a small stack machine.
 * This avoids per-sample recursion and heap pointer chasing — the grapher
 * evaluates f hundreds of times per frame while panning/zooming. */

#define EC_MAX_CODE   96      /* opcode+operand bytes (input capped at 64 chars) */
#define EC_MAX_CONSTS 24
#define EC_MAX_STACK  24

typedef enum {
    EC_CONST,   /* +1 operand byte: const-pool index                        */
    EC_X,
    EC_ADD, EC_SUB, EC_MUL, EC_DIV,
    EC_NEG,     /* pattern-matched from SUB(NUM 0, u)                       */
    EC_POWI,    /* +1 signed operand byte n, |n| in 1..12: repeated multiply
                   (negative n takes a reciprocal) — also the only correct
                   path for negative bases with integer exponents           */
    EC_POW,     /* general powf; base<0 -> NaN, 0^nonpositive -> NaN        */
    EC_SIN, EC_COS, EC_TAN, EC_EXP, EC_LN, EC_LOG, EC_SQRT, EC_ABS
} ECOp;

typedef struct {
    uint8_t code[EC_MAX_CODE];
    float   consts[EC_MAX_CONSTS];
    uint8_t ncode, nconsts;
} ExprCode;

/* Compile a *simplified* AST (see ast_simplify). Returns 0 on success.
 * On failure returns -1 and puts a short reason in errsym[32]: the unknown
 * symbol name, or "syntax" / "too long". Recognized symbols: x, pi, e. */
int ec_compile(const ASTNode *simplified, ExprCode *out, char errsym[32]);

/* Evaluate at x. Domain errors (div ~0, ln/log <= 0, sqrt < 0, bad pow)
 * yield NaN, never a trap — the grapher samples blindly. */
float ec_eval(const ExprCode *ec, float x);

/* Composite Simpson over [a,b] with n subintervals (forced even, >= 2),
 * Kahan-compensated summation. NaN samples are retried just off the point,
 * then counted as 0 with *warn_out set to 1. b < a integrates backwards. */
float ec_integrate(const ExprCode *ec, float a, float b, int n, uint8_t *warn_out);

#endif
