#ifndef SYMBOLIC_H
#define SYMBOLIC_H

#include "expr.h"

/* Symbolic engine, ported from the cePort integral calculator
 * (integralCalc/src/integrate.c). Everything works on the AST from expr.h;
 * results are fresh trees the caller frees with ast_free_tree. Inputs are
 * never consumed. Feed *simplified* trees (ast_simplify): the matchers are
 * structural and NODE_PAREN is only unwrapped at the top of each rule. */

/* Structural equality (commutative under ADD and MUL at each level). */
int      ast_equal(const ASTNode* a, const ASTNode* b);

/* d/d`var`. Unsimplified. NULL when no rule applies (abs, f(x)^g(x)). */
ASTNode* ast_derivative(const ASTNode* node, char var);

/* Antiderivative w.r.t. `var`, without the constant. NULL when the rule set
 * (constant, power of a linear base, linearity, constant factor, elementary
 * function of a linear argument, u-substitution over products and
 * quotients) does not cover the integrand. Run ast_simplify on the result
 * before displaying it. */
ASTNode* ast_integrate(const ASTNode* integrand, char var);

#endif
