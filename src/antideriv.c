#include "antideriv.h"
#include "symbolic.h"
#include "mathprint.h"
#include "fmt.h"

#include <graphx.h>
#include <keypadc.h>
#include <string.h>
#include <math.h>

#define COL_BG   0xFF
#define COL_FG   0x00
#define SCREEN_W 320
#define CW       8
#define CH       8
#define LINE_PX  11             /* text row pitch, as in help.c */
#define MARGIN   6
#define FOOTER_Y (240 - 16)
#define TEXT_COLS ((SCREEN_W - 2 * MARGIN) / CW)   /* 38 */

/* Print s char-wrapped at TEXT_COLS columns from (x, y); returns the y
 * below the last row. Stops rather than running into the footer. */
static int put_wrapped(const char *s, int x, int y) {
    char row[TEXT_COLS + 1];
    int len = (int)strlen(s), pos = 0;
    while (pos < len && y + CH <= FOOTER_Y) {
        int n = len - pos;
        if (n > TEXT_COLS) n = TEXT_COLS;
        memcpy(row, s + pos, n);
        row[n] = '\0';
        gfx_PrintStringXY(row, x, y);
        y += LINE_PX;
        pos += n;
    }
    return y;
}

/* Layout follows integralCalc's result screen: the integral sign grows with
 * the integrand, and inline text next to a MathPrint box sits on the box's
 * own baseline so " + C" doesn't drift beside a stacked fraction. */
static void render(const GraphState *g, const ASTNode *disp, const ASTNode *G,
                   int ga_ok, float Ga) {
    char buf[64];
    int p;

    gfx_FillScreen(COL_BG);
    gfx_SetColor(COL_FG);
    gfx_SetTextFGColor(COL_FG);
    gfx_SetTextBGColor(COL_BG);
    gfx_SetTextTransparentColor(COL_BG);

    gfx_PrintStringXY("Antiderivative", 4, 4);
    gfx_HorizLine(0, 16, SCREEN_W);

    /* ---- line 1:  ∫ f(x) dx ------------------------------------------ */
    MPBox ib;
    mp_measure(disp, &ib);
    int int_h = ib.h + 4;
    if (int_h < 18) int_h = 18;

    int x = MARGIN, y = 24;
    mp_draw_integral_sign(x, y, int_h);
    x += 7 + 6;
    if (x + ib.w + 6 + 2 * CW <= SCREEN_W - 2) {
        mp_draw(disp, x, y + (int_h - ib.h) / 2);
        gfx_PrintStringXY("dx", x + ib.w + 6, y + int_h - CH - 1);
        y += int_h + 8;
    } else {
        /* Too wide for the 2-D layout: the user's own text, wrapped. */
        int_h = 18;
        y = put_wrapped(g->ftext, x, y + 5);
        gfx_PrintStringXY("dx", x, y);
        y += LINE_PX + 6;
    }

    /* ---- line 2:  = G(x) + C   /  No rule found ----------------------- */
    if (!G) {
        gfx_PrintStringXY("= ?   No rule found", MARGIN, y);
        y += LINE_PX * 2;
        gfx_PrintStringXY("The symbolic rules don't cover", MARGIN, y); y += LINE_PX;
        gfx_PrintStringXY("this f; the graph still shows", MARGIN, y); y += LINE_PX;
        gfx_PrintStringXY("the numeric F(x).", MARGIN, y);
    } else {
        MPBox rb;
        mp_measure(G, &rb);
        int rx = MARGIN;
        if (rx + 2 * CW + rb.w + 4 * CW <= SCREEN_W - 2) {
            int text_y = y + rb.baseline - CH;
            gfx_PrintStringXY("= ", rx, text_y);
            rx += 2 * CW;
            mp_draw(G, rx, y);
            rx += rb.w;
            gfx_PrintStringXY(" + C", rx, text_y);
            y += (rb.h > CH ? rb.h : CH) + 10;
        } else {
            char txt[200];
            memcpy(txt, "= ", 2);
            ast_to_string(G, txt + 2, (int)sizeof txt - 2 - 4);
            strcat(txt, " + C");
            y = put_wrapped(txt, rx, y) + 4;
        }

        /* ---- lines 3/4: tie G to the curve the grapher draws ---------- */
        gfx_PrintStringXY("F(x) = G(x) - G(a)", MARGIN, y);
        y += LINE_PX;
        p = 0;
        memcpy(buf + p, "a = ", 4); p += 4;
        p += fmt_g(buf + p, g->a);
        memcpy(buf + p, "   G(a) = ", 10); p += 10;
        if (!ga_ok) {
            memcpy(buf + p, "n/a", 4); p += 3;
        } else {
            p += fmt_g(buf + p, Ga);            /* "?" for NaN */
            if (isnan(Ga)) { memcpy(buf + p, " (undefined)", 13); p += 12; }
        }
        buf[p] = '\0';
        gfx_PrintStringXY(buf, MARGIN, y);
    }

    gfx_HorizLine(0, FOOTER_Y, SCREEN_W);
    gfx_PrintStringXY("any key: back", 4, FOOTER_Y + 4);
    gfx_SwapDraw();
}

/* Same keypad discipline as help.c: keypadc only, keys held on entry are
 * ignored, and the keypad is released and reset before returning. */
static uint8_t prevk[8];

void antideriv_show(const GraphState *g) {
    ASTNode *disp = parser_init_from_string(g->ftext);   /* validated text */
    if (!disp) return;
    ASTNode *simp = ast_simplify(ast_clone(disp));
    ASTNode *G    = ast_integrate(simp, 'x');
    int   ga_ok = 0;
    float Ga    = 0.0f;
    if (G) {
        G = ast_simplify(G);
        ExprCode code;
        char sym[32];
        if (ec_compile(G, &code, sym) == 0) {
            Ga = ec_eval(&code, g->a);
            ga_ok = 1;
        }
    }

    memset(prevk, 0xFF, sizeof prevk);
    render(g, disp, G, ga_ok, Ga);

    for (;;) {
        kb_Scan();
        int hit = 0;
        for (int i = 1; i < 8; i++) {
            if ((uint8_t)kb_Data[i] & (uint8_t)~prevk[i]) hit = 1;
            prevk[i] = (uint8_t)kb_Data[i];
        }
        if (hit) break;
    }

    ast_free_tree(G);
    ast_free_tree(simp);
    ast_free_tree(disp);

    do { kb_Scan(); } while (kb_AnyKey());
    kb_Reset();
}
