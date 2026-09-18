# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

INTGRPH — a TI-84 Plus CE program (C, eZ80, CE toolchain) that graphs f(x),
shades ∫ₐᵇ f, prints the value, and draws the running integral F(x) = ∫ₐˣ f as a
second curve. Typing `x` as `b` graphs F(x) alone. `math` on the graph screen
shows the symbolic antiderivative (rule-based engine ported from the sibling
`../integralCalc` project). README.md is the user manual and has the full key
map, accuracy table, and design rationale; read it before changing UI
behaviour.

## Build

Requires the CE C toolchain (CEdev, installed at `C:\CEdev`). Output is
`bin/INTGRPH.8xp`.

```
build.bat                                   # Windows; uses %CEDEV% or ..\CEDev
PATH="/c/CEdev/bin:$PATH" make              # Git Bash
make clean
```

`makefile` sets `-Wall -Wextra -Oz`, `HAS_PRINTF = NO` (no `printf` — see
`fmt.c`), `COMPRESSED = NO`. A clean build must be warning-free.

Source files use LF line endings. Python's default text mode on Windows
rewrites them to CRLF — write with `newline=''` or run `sed -i 's/\r$//'`.

## Testing

The symbolic engine has a host-side check: `tools/symtest/build.sh` compiles
`expr.c`, `symbolic.c` and `fmt.c` with any host gcc (on this machine the
Ruby devkit's: `PATH="/c/Ruby34-x64/msys64/ucrt64/bin:$PATH"`), integrates a
list of editor-style inputs, prints the results and fails on any leaked
allocation. Run it after touching `expr.c`'s simplifier or `symbolic.c`.

Everything else is verified on the emulator. Headless driving with screenshots:
`C:\CEdev\bin\cemu-autotester.exe -d <absolute path to config.json>` (the
autotester chdirs to the config's directory itself; a relative path fails with
"Couldn't change directory path"). The config needs `rom`, `transfer_files`
(**must include `clibs.8xg`** or the program dies with "Need LibLoad"),
`target: {name: "INTGRPH", isASM: true}`, a `sequence` that **starts with
`action|launch`, `delay|2500`**, and a `hashes` object keyed by the names used
in `hash|<name>` steps with `start: "vram_start"`, `size: "vram_16_size"`.
Key names: `xton` is the X key; `y=`, `window`, `zoom`, `trace`, `graph`,
`math`, `alpha`, `enter`, `del`, `clear`, digits, `+ - * / ^ ( )`,
`sin cos tan ln log`, arrows. Leave ~500 ms after a digit before `enter` — the
editors' `os_GetCSC` loop drops faster presses — and wait several seconds
after accepting `b` for a transcendental f (the Simpson pass runs first).
Give each `hash` step a bogus `expected_CRCs: ["1"]`: the "failure" dumps VRAM
(two 320×240 8bpp buffers back to back; the displayed one alternates with
`gfx_SwapDraw`, so two dumps with the same CRC can still be different screens —
prove a transition with a visible state change, not by CRC) to
`failure_hash<n>_num<n>_dump.bin`, convertible with PIL using the palette in
`grf_init_palette()` (indices 0 = black, 255 = white, 1–7 custom).

The interactive CEmu build is at `~/Downloads/CEmu-v2.0_win64_Qt6.exe`; ROM
and `clibs.8xg` are in `~/Downloads` (`clibs.8xg` is the release asset from
github.com/CE-Programming/libraries).

## Architecture

Screen flow lives in `main.c`: edit f → edit a → edit b → graph loop, with
the graph loop returning `GRF_REEDIT_F` / `GRF_REEDIT_AB` / `GRF_QUIT` to bounce
back into the editors. All state is one `GraphState G` (`graph.h`); the raw
text fields (`ftext`, `atext`, `btext`) are the source of truth for re-editing.

Pipeline for f: `expr.c` tokenizes/parses text into an N-ary AST and
`ast_simplify` constant-folds it (also strips `NODE_PAREN`); `eval.c`
compiles the simplified AST into a flat postfix bytecode tape (`ExprCode`)
run by a float stack machine (`ec_eval`), plus `ec_integrate` (composite
Simpson, Kahan sum, halved-resolution disagreement sets the `!` warning).
`mathprint.c` renders the *unsimplified* AST 2-D in the editor.

Symbolic path (on demand only, nothing cached in `GraphState`): `math` in the
graph loop calls `antideriv_show()` (`antideriv.c`), which re-parses `ftext`,
simplifies a clone, runs `ast_integrate()` (`symbolic.c`, a verbatim port of
integralCalc's `calculate_integral` plus `ast_derivative`/`ast_equal`),
simplifies the result, compiles it with `ec_compile` to evaluate G(a), draws
the page with `mathprint.c` (falling back to `ast_to_string` text when too
wide), and frees everything on exit. `ast_integrate` returns NULL when no
rule matches; the rule list is in README.md.

`graph.c` keeps one cached sample per screen column (`fcol`/`Fcol`, world
values) and their projected rows (`fy`/`Fy`, int16). F is one left-to-right
trapezoid sweep anchored at `(xref, Fref)`; panning `memmove`s the caches,
evaluates only newly exposed columns, and extends F incrementally (backwards
when panning left). Vertical pan re-projects with zero evaluations. Zoom is a
full recompute.

## Hard-won constraints (don't undo these)

- **Drawing loops are integer-only.** They run ~640×/frame and one software
  float compare on the eZ80 costs more than the rest of an iteration.
  `project_y` clamps off-screen samples to `Y_RAIL_LO/HI` and NaN to
  `Y_SENT` precisely so `draw_curve` and shading never touch floats.
- **AST children are stored right operand first.** Use `ast_get_left` /
  `ast_get_right` / `ast_get_arg`; never walk `data.children` for operand order.
- **One input model per screen.** Editors use `os_GetCSC`; the graph and help
  screens use keypadc (`kb_Scan`). Mixing them on one screen corrupts OS
  scancodes. `help_show()` drains the keypad and `kb_Reset()`s before
  returning to an `os_GetCSC` caller.
- **No `printf`.** CE's `%g` prints fixed `%f`; use `fmt_g` (`fmt.c`) for
  numbers and build status strings with `memcpy`/`fmt_g` into a local buffer.
- `double` is 32-bit on the CE; everything is `float` and precision is ~7
  digits (the zoom limit check in `grf_zoom` depends on this).
- Status-bar redraws are gated by `status_pending` (a count of 2, one per
  draw buffer) — call `status_changed()` whenever bar text changes.
- Implicit multiplication (`2x`, `(x+1)(x-2)`) is inserted by the *editor*
  at append time (`needs_implicit_mul` in `input.c`); the parser has none.
- Expressions are capped at 64 chars (`ftext[65]`, `EC_MAX_CODE`).
- `antideriv_show()` and `help_show()` are the only keypadc screens besides
  the graph loop; both end with the drain-and-`kb_Reset()` sequence and the
  caller re-`kb_Scan()`s, `keys_save()`s and `status_changed()`s afterwards.
- `symbolic.c` allocates freely (every rule clones); every `return NULL` path
  must free what it built, because the overlay may be opened many times per
  session on a small heap.
