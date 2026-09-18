# INTGRPH — Integral Grapher for the TI-84 Plus CE

Type a function; the calculator graphs it, shades the area between two bounds,
prints the value of the definite integral, and draws the running integral
F(x) = ∫ₐˣ f(t)dt as a second curve. Pan and zoom are fast enough to explore
with.

```
 +--------------------------------------------+
 |         ______                             |   blue    f(x)
 |   1   /        \              ,--- F(x)    |   shaded  area from a to b
 |     /############\        ,--'             |   purple  running integral
 |---/##############\\----,--'------------    |
 |  0    1    2    3   \,-'  5    6    7      |
 +--------------------------------------------+
 | [0,3.14159] = 2                     F:on   |
 | pan:arrows zoom:+/- help:alpha             |
 +--------------------------------------------+
```

## Usage

Run `prgmINTGRPH` from the `[prgm]` menu, or launch it from a shell like
Cesium. It asks for `f(x)`, then the bounds `a` and `b`, then shows the graph.

Press `[alpha]` at any time for help without leaving the program.

### Typing expressions

The usual keys work — `+ - * / ^ ( )`, the digits, `x` from `[X,T,θ,n]`, and
`sin` `cos` `tan` `ln` `log`. Beyond those:

| Key | Inserts |
|---|---|
| `2nd` + `x²` | `sqrt(` |
| `2nd` + `LN` | `exp(` |
| `2nd` + `^` | `pi` |
| `2nd` + `÷` | `e` |
| `math` | `abs(` |
| `x⁻¹` | `^-1` |
| `x²` | `^2` |

Multiplication is inserted for you where it is implied, so `2x`, `3sin(x)`, and
`(x+1)(x-2)` all parse. `ENTER` accepts, `DEL` backspaces, `CLEAR` empties the
line — and on an already-empty line it backs out to the previous prompt
(b → a → f), which quits from the very first one. The prompts start blank;
nothing is assumed for `a` or `b`.

The bounds `a` and `b` are typed the same way but may not contain `x`; `2pi`
and `sqrt(2)` are fine. The one exception is `b = x` on its own: the integral
is then a function of x, so the program graphs only F(x) = ∫ₐˣ f(t)dt — no
f curve, no shading — and the status bar reads `∫[a,x] = F(x)`.

### Graph screen

| Key | Action |
|---|---|
| arrows | Pan — hold to keep moving |
| `+` / `−` | Zoom in / out |
| `trace` | Trace cursor on/off |
| `graph` | Show/hide the F(x) curve (no effect when b = x) |
| `y=` | Edit f(x) |
| `window` | Edit the bounds a and b |
| `zoom` | Reset to the standard window |
| `alpha` | Help |
| `mode` / `clear` | Quit |

In trace mode the arrows move the cursor (hold `2nd` to move four times as
fast), pushing past either edge pans the window, `ENTER` recenters on the
cursor, and `+`/`−` zoom about it. The status bar then reads out `x`, `f(x)`,
and `F(x)` at the cursor.

### Reading the results

- **`!`** after the integral means the number is unreliable: some sample points
  could not be evaluated, or the interval contains a pole. It comes from a
  convergence test, not a guess — see below.
- **Gaps** in a curve are domain holes or asymptotes (`1/x`, `tan`, `ln`).
- **F(x) is drawn at screen accuracy**; the printed integral is computed far
  more precisely. Trust the number over the curve.

## Accuracy

Checked on the emulator against known values. Every one of these is exact to
the digits shown:

| Integral | Shows | Exact |
|---|---|---|
| ∫₀¹ x² dx | `0.333333` | 1/3 |
| ∫₀^π sin x dx | `2` | 2 |
| ∫₁² dx/x | `0.693147` | ln 2 |
| ∫₀¹ cos x dx | `0.841471` | sin 1 |
| ∫₀³ 0.5x² dx | `4.5` | 9/2 |
| ∫₀¹ (x+1)(x−2) dx | `-2.16667` | −13/6 |
| ∫₂¹ x² dx | `-2.33333` | −7/3, sign flipped |
| ∫₂² x² dx | `0` | 0 |
| ∫₀¹ ln x dx | `-1.00435 !` | −1, flagged |
| ∫₁² tan x dx | flagged `!` | divergent |

The trace readout and the accumulation curve agree with theory too: with
`f = cos(x)` and `a = 0`, F(x) lands exactly on `sin(x)`, and stays there after
panning out to x ≈ 28 and back.

## How it works

The screen is 320 columns wide and the program keeps one cached sample per
column. Four ideas make that fast enough to feel interactive:

1. **f(x) is compiled, not interpreted.** The parsed expression tree is turned
   once into a flat postfix bytecode tape (`src/eval.c`) run by a small float
   stack machine. Re-walking a heap-allocated tree for every one of the
   hundreds of samples per frame would cost far more.
2. **The running integral is one sweep, not one integral per pixel.** F(x)
   accumulates left to right with a trapezoid step per column, so the whole
   curve costs the same 320 evaluations that f already needed.
3. **Panning reuses the samples it already has.** Cached columns are shifted,
   only the handful newly scrolled into view get evaluated, and the
   accumulation is extended at the edge — panning left subtracts the
   trapezoids that panning right would have added. Vertical panning evaluates
   nothing at all, because world values and the screen rows they project to
   are cached separately.
4. **The drawing loops are strictly integer.** They run ~640 times a frame, and
   on this hardware one software-float comparison costs more than everything
   else in an iteration put together. Moving the off-screen and asymptote
   tests onto pre-clamped pixel rows took a redraw from ~106 ms to ~43 ms.
   Samples one pixel apart also mean a curve segment is just a filled column,
   which avoids a clipped line per segment.

The printed integral is computed separately, once per edit, with composite
Simpson (n = 512) and Kahan-compensated summation. The same pass repeats the
rule at half resolution; if the two disagree by more than 1%, the result is
marked `!`. That is what catches poles and under-resolved wiggle without
false-flagging functions that are merely large.

Everything is 32-bit float — the CE's `double` *is* 32-bit — which is where the
~7 digits and the zoom-in limit come from. Zooming stops with a message rather
than dissolving into noise once the window gets too narrow for the pixel
spacing to be representable.

Roughly, on real hardware: a full redraw is ~43 ms, so held-key panning runs
around 20 fps. Evaluating a transcendental costs a few milliseconds per point,
so a zoom step — which resamples all 320 columns — takes about a second for
something like `tan(x)` and is near-instant for a polynomial.

## Building

Requires the [CE C toolchain](https://ce-programming.github.io/toolchain/)
(CEdev). Then:

```
build.bat        (Windows; uses the CEDEV env var, or ..\CEDev)
```

or, with `$CEDEV/bin` on your PATH, just `make`. Output is `bin/INTGRPH.8xp`
(~33 KB). Send it to the calculator with TI Connect CE, or load it in
[CEmu](https://ce-programming.github.io/CEmu/).

**The C libraries (`clibs.8xg`) must be on the calculator too**, or the program
refuses to start with "Need LibLoad / clibs".

## Project layout

| File | Purpose |
|---|---|
| `src/main.c` | Screen flow: edit f, edit a and b, graph |
| `src/expr.c` | Tokenizer, parser, AST, constant folding |
| `src/eval.c` | Bytecode compiler, evaluator, Simpson quadrature |
| `src/graph.c` | Viewport, column caches, drawing, pan/zoom/trace loop |
| `src/input.c` | Keypad expression editor |
| `src/mathprint.c` | 2-D math rendering (fractions, powers, radicals) |
| `src/fmt.c` | Compact float formatting |
| `src/help.c` | In-app help pages |

About 2,900 lines of C. `expr.c` and `mathprint.c` are adapted from my symbolic
integral calculator; the numeric engine, graphing, and caching are new here.

Two things that are easy to trip over when reading the code: the AST stores its
children **right operand first** (`ast_get_left` / `ast_get_right` in `expr.c`
exist for exactly that reason), and a screen owns either keypadc (`kb_Scan`) or
the OS (`os_GetCSC`) but never both — mixing them makes the OS scancodes
unreliable.

`printf` is deliberately not linked (`HAS_PRINTF = NO`): the CE renders `%g` as
fixed `%f`, so `fmt.c` does compact float formatting by hand, which also saves
about 7 KB.

## Limitations

- No symbolic integration — this program is numeric only. `main.c`'s
  `refresh_integral()` marks where an antiderivative display would attach.
- No `asin` / `acos` / `atan`, and no variables other than `x` (plus the
  constants `pi` and `e`).
- Expressions are capped at 64 characters.
- Zooming is a full recompute, so nothing is cached across zoom levels — a deep
  zoom on a transcendental function takes a moment.

# Note
I vibecoded this
