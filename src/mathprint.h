#ifndef MATHPRINT_H
#define MATHPRINT_H

#include "expr.h"

typedef struct {
    int w;          /* total width  in pixels */
    int h;          /* total height in pixels */
    int baseline;   /* y-offset (from top of bbox) of the text baseline used
                       to align this node horizontally with siblings */
} MPBox;

/* Measure the bounding box this node will occupy when rendered. */
void mp_measure(const ASTNode *node, MPBox *out);

/* Draw the node with its bbox top-left at (x, y_top). */
void mp_draw(const ASTNode *node, int x, int y_top);

/* Draw a stylised integral sign of height `h` with its top at (x, y_top).
   Advance width = 7. */
void mp_draw_integral_sign(int x, int y_top, int h);

#endif
