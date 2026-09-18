#ifndef ANTIDERIV_H
#define ANTIDERIV_H

#include "graph.h"

/* Full-screen page showing the symbolic antiderivative of the current f:
 *   ∫ f(x) dx = G(x) + C          (2-D MathPrint, raw text if too wide)
 *   F(x) = G(x) - G(a),  G(a) = … (G compiled and evaluated at a)
 * or "No rule found". Everything is computed on entry and freed on exit.
 * Blocks until any key. keypadc only; drains the keypad and kb_Reset()s
 * before returning, like help_show(). Caller repaints its own screen. */
void antideriv_show(const GraphState *g);

#endif
