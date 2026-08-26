#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Full-screen expression line editor (adapted from cePort/src/main.c):
 * os_GetCSC key loop with a 2nd-modifier latch, live MathPrint preview with
 * raw-text fallback, implicit '*' inserted at append time (2x, (x+1)(x-2)),
 * double-buffered rendering. alpha opens the help screens. */

typedef struct {
    const char *title;   /* prompt drawn before the input, e.g. "f(x) = " */
    uint8_t allow_x;     /* 0: the x key is ignored (integration bounds)   */
    /* Extra validation on ENTER after a successful parse; return an error
     * message to keep editing, or NULL to accept. May be NULL. */
    const char* (*validate)(const char *text);
} InputCfg;

enum {
    INPUT_OK     = 0,    /* ENTER on text that parsed and validated        */
    INPUT_CANCEL = 1,    /* CLEAR on an empty line — keep the old value    */
    INPUT_QUIT   = 2     /* 2nd+MODE — quit the program                    */
};

/* Edit buf (capacity cap, incl. NUL) in place; buf may arrive preloaded. */
int input_line(const InputCfg *cfg, char *buf, int cap);

#endif
