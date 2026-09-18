/* Host-side check of the symbolic engine (no calculator needed).
 *
 * Runs the exact pipeline antideriv.c uses — parse, simplify a clone,
 * ast_integrate, simplify, ast_to_string — over a list of integrands,
 * prints each result, and verifies that every malloc has been freed once
 * the trees are released. expr.c and symbolic.c are compiled with
 * -Dmalloc=h_malloc -Dfree=h_free so all AST allocation is counted.
 *
 * Build and run with build.sh (Git Bash) — needs any host gcc.
 *
 * Inputs are written the way the editor stores them: implicit
 * multiplication is already expanded to '*' (input.c does that), so "2x"
 * is "2*x" here. A few malformed strings at the end confirm the pipeline
 * survives what the parser makes of them (the app never passes those on:
 * validate_f rejects anything ec_compile can't take). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "expr.h"
#include "symbolic.h"

static long live, total;
void *h_malloc(size_t n) { total++; live++; return malloc(n); }
void  h_free(void *p) { if (p) live--; free(p); }

static const char *cases[] = {
    "5", "x", "x^2", "2*x", "x^2+3*x+1", "x^-1", "1/x", "3/x",
    "(2*x+1)^3", "sin(x)", "cos(x)", "tan(x)", "exp(x)", "ln(x)",
    "sin(2*x)", "cos(3*x+1)", "exp(4*x)", "ln(5*x)", "tan(6*x)",
    "sin(x^2)*2*x", "cos(x^2)*x", "x*exp(x^2)",
    "(x^2+1)^3*2*x", "(x^2+1)*2*x", "2*x/(x^2+1)", "x/(x^2+1)",
    "(3*x+2)^5+sin(4*x)", "sin(2*x)+cos(3*x)+ln(5*x)+(x+1)^2+tan(6*x)",
    "pi*x", "e^x", "x^pi", "2^x", "x^x", "sqrt(x)", "log(x)", "abs(x)",
    "(x^2+1)^2", "ln(x)/x", "sin(x)^2", "x*sin(x)", "1/(x^2+1)",
    "sin(x)*cos(x)", "x*x", "x*x*x", "(x+1)*(x-2)",
    "-x", "-sin(x)", "0-x^2", "x/2", "x/2/3", "(x^3/3)/2",
    "exp(x)*sin(exp(x))", "cos(ln(x))/x", "x^2*exp(x^3)",
    "1/(2*x+1)", "(2*x+1)^-1", "3/(2*x+1)^2", "(x^2)^3",
    "2*sin(x)", "sin(x)*2", "x*2", "2*x^2", "x^2/2*3", "3*x^3/6",
    "2x", "x2", "(2x+1)^3", "sin(x)x", "x+", "(x",
    NULL
};

int main(void) {
    int fails = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 0; cases[i]; i++) {
        long before = live;
        char out[256];
        ASTNode *disp = parser_init_from_string(cases[i]);
        if (!disp) { printf("%-42s  PARSE ERROR\n", cases[i]); continue; }
        ASTNode *simp = ast_simplify(ast_clone(disp));
        ASTNode *G = ast_integrate(simp, 'x');
        if (G) { G = ast_simplify(G); ast_to_string(G, out, sizeof out); }
        else strcpy(out, "(no rule)");
        printf("%-42s  %s\n", cases[i], out);
        ast_free_tree(G); ast_free_tree(simp); ast_free_tree(disp);
        if (live != before) {
            printf("    ^^^ LEAK: %ld block(s) not freed\n", live - before);
            fails++;
        }
    }
    printf("\nallocations: %ld total, %ld live at exit, %d leaking case(s)\n",
           total, live, fails);
    return fails || live ? 1 : 0;
}
