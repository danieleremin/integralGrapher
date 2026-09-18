# TODO

## Test on the physical calculator

Everything below passed on CEmu (headless autotester screenshots plus the
host-side `tools/symtest` harness) but depends on real hardware timing, the
real keypad, the real LCD, or the OS heap, so it needs a TI-84 Plus CE.
Send `bin/INTGRPH.8xp` and `clibs.8xg` first.

- [ ] **`math` response time.** With a long integrand such as
      `sin(2x)+cos(3x)+ln(5x)+(x+1)^2+tan(6x)` on `[1, 2]`, press `math` and
      note how long the page takes to appear. The engine recurses and clones
      heavily; if it feels slow, the u-substitution search over products
      (`symbolic.c`, `ast_integrate` rule 4) is the place to look.
- [ ] **One press, one page.** From the graph, tap `math` once: the page must
      open exactly once. Press any key to close it: the graph must come back
      with the status bar intact and nothing else happening (no pan, no zoom,
      no toggle from the key that closed the page).
- [ ] **No leaked press into the editors.** Open the page, close it with
      `y=`, then on the graph press `y=` to enter the f editor: the editor
      must open with the text unchanged and no stray character inserted.
      Repeat closing the page with `enter`, `clear`, and `window`.
- [ ] **Works in every graph mode.** Press `math` while tracing (cursor
      stays where it was afterwards), with `b = x`, and with the F curve
      hidden (`graph`); confirm the graph redraws correctly each time.
- [ ] **Long session heap.** After a `2nd`+`mem` RAM reset, open and close
      the page 30+ times on `sin(x^2)*2x`, then re-edit f, then open it
      again. Any garbled page, hang, or "memory" error means a leak the host
      harness did not cover (most likely in `antideriv.c` or `eval.c`, since
      `symbolic.c` is counted by the harness).
- [ ] **Legibility on the real LCD.** Check the fraction bars, superscripts,
      `|…|` bars and the `+ C` baseline on `x^2`, `sin(2x)`, `1/(2x+1)` and
      `2x/(x^2+1)`, and the wrapped plain-text fallback on the long integrand
      above.
- [ ] **`G(a)` readout.** `1/x` with a = 1 shows `G(a) = 0`; with a = 0 it
      shows `G(a) = ? (undefined)`.
- [ ] **Help page 2** still fits with the new `math` line.

## Emulator-only follow-ups (not blocking)

- [ ] Fold common factors in results like `3x⁴/24` (gcd of numerator
      constant and denominator).
- [ ] The parser accepts a bare symbol after a number (`2x` without the
      editor's inserted `*`) and builds a broken tree. Harmless in the app
      because the editor always inserts `*` and validation rejects the
      result, but the tokenizer/parser could reject it outright.
