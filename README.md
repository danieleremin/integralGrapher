# INTGRPH — Integral Grapher for the TI-84 Plus CE

Type a function; the calculator graphs it, shades the area between two bounds,
prints the value of the definite integral, and draws the running integral
F(x) = ∫ₐˣ f(t)dt as a second curve. Pan and zoom are fast enough to explore
with. `math` shows the antiderivative in closed form when the built-in rules
can find one.

```
 +--------------------------------------------+
 |         ______                             |   blue    f(x)
 |   1   /        \              ,--- F(x)    |   shaded  area from a to b
 |     /############\        ,--'             |   purple  running integral
 |---/##############\\----,--'------------    |
 |  0    1    2    3   \,-'  5    6    7      |
 +--------------------------------------------+
 | [0,3.14159] = 2                     F:on   |
 | pan:arrows zoom:+/- help:alpha sym:math    |
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
| `math` | Show the antiderivative (see below) |
| `mode` / `clear` | Quit |

In trace mode the arrows move the cursor (hold `2nd` to move four times as
fast), pushing past either edge pans the window, `ENTER` recenters on the
cursor, and `+`/`−` zoom about it. The status bar then reads out `x`, `f(x)`,
and `F(x)` at the cursor.

### The antiderivative

`math` on the graph screen opens a page with the symbolic result:

```
 ∫ x² dx
 =  x³/3 + C
 F(x) = G(x) - G(a)
 a = 0   G(a) = 0
```

The first two lines are the indefinite integral, drawn the same 2-D way as
the editor draws f (stacked fractions, superscripts, `|…|`). The last two tie
it to the purple curve: F(x) on the graph is exactly G(x) − G(a), and G(a) is
evaluated for you (`?` if G is undefined at a, e.g. `ln|x|` at a = 0). Any key
returns to the graph. When the result is too wide for the 2-D layout it is
printed as plain text instead.

The integrator is the rule set from my symbolic integral calculator, tried in
this order — first match wins, otherwise the page says `No rule found`:

1. **Constant** — ∫c dx = c·x
2. **Variable** — ∫x dx = x²/2
3. **Linearity** — sums and differences term by term
4. **Constant factor** — ∫c·f dx = c·∫f dx
5. **u-substitution over a product** — ∫f(g)·g′ dx for f ∈ {sin, cos, tan,
   exp, ln}, ∫gⁿ·g′ dx, and ∫g·g′ dx = g²/2, with any constant multiple of
   g′ (∫cos(x²)·x dx works even though (x²)′ = 2x)
6. **u-substitution over a quotient** — ∫c·g′/g dx = c·ln|g|, plus ∫c/x dx
7. **Power of a linear base** — ∫(ax+b)ⁿ dx, including n = −1 → ln|ax+b|
8. **Elementary function of a linear argument** — sin, cos, tan, exp, ln of
   (ax+b), divided by a

| f(x) | Shows |
|---|---|
| `x^2` | `x³/3 + C` |
| `2x` | `x² + C` |
| `(2x+1)^3` | `(2x+1)⁴/8 + C` |
| `sin(x^2)*2x` | `-cos(x²) + C` |
| `2x/(x^2+1)` | `ln\|x²+1\| + C` |
| `1/x` | `ln\|x\| + C` |
| `abs(x)` | `No rule found` |

What it cannot do: expand products (`(x²+1)²` is not distributed), integrate
by parts, partial fractions, trig identities (`sin(x)²`), or anything through
`sqrt`, `log`, `abs` (no antiderivative rule for those). Matching is
structural, so `ln(x)/x` misses u = ln(x) because `1/x` and `x^-1` are not
recognised as the same thing. The numeric F(x) on the graph is unaffected by
any of this.

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
(~46 KB). Send it to the calculator with TI Connect CE, or load it in
[CEmu](https://ce-programming.github.io/CEmu/).

**The C libraries (`clibs.8xg`) must be on the calculator too**, or the program
refuses to start with "Need LibLoad / clibs".

## Project layout

| File | Purpose |
|---|---|
| `src/main.c` | Screen flow: edit f, edit a and b, graph |
| `src/expr.c` | Tokenizer, parser, AST, constant folding, text printer |
| `src/symbolic.c` | Symbolic derivative and antiderivative rules |
| `src/antideriv.c` | The `math` antiderivative page |
| `src/eval.c` | Bytecode compiler, evaluator, Simpson quadrature |
| `src/graph.c` | Viewport, column caches, drawing, pan/zoom/trace loop |
| `src/input.c` | Keypad expression editor |
| `src/mathprint.c` | 2-D math rendering (fractions, powers, radicals) |
| `src/fmt.c` | Compact float formatting |
| `src/help.c` | In-app help pages |

About 3,800 lines of C. `expr.c`, `symbolic.c` and `mathprint.c` are adapted
from my symbolic integral calculator; the numeric engine, graphing, and caching
are new here.

Two things that are easy to trip over when reading the code: the AST stores its
children **right operand first** (`ast_get_left` / `ast_get_right` in `expr.c`
exist for exactly that reason), and a screen owns either keypadc (`kb_Scan`) or
the OS (`os_GetCSC`) but never both — mixing them makes the OS scancodes
unreliable.

`printf` is deliberately not linked (`HAS_PRINTF = NO`): the CE renders `%g` as
fixed `%f`, so `fmt.c` does compact float formatting by hand, which also saves
about 7 KB.

## Limitations

- The symbolic integrator is rule-based, not a CAS — see "The antiderivative"
  above for what it does and doesn't cover. The graph never depends on it.
- No `asin` / `acos` / `atan`, and no variables other than `x` (plus the
  constants `pi` and `e`).
- Expressions are capped at 64 characters.
- Zooming is a full recompute, so nothing is cached across zoom levels — a deep
  zoom on a transcendental function takes a moment.

# Note
I vibecoded this
