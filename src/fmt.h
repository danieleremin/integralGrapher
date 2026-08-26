#ifndef FMT_H
#define FMT_H

/* Compact %g-style float formatting. The CE toolchain's printf renders %g
 * as fixed 6-decimal %f ("0.000000"), which is useless for axis labels —
 * this prints up to 6 significant digits with trailing zeros trimmed, and
 * scientific notation outside [1e-4, 1e7). Returns the length written.
 * `out` must hold at least 16 bytes. */
int fmt_g(char *out, float v);

#endif
