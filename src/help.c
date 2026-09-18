#include "help.h"

#include <keypadc.h>
#include <graphx.h>
#include <string.h>

#define COL_BG 0xFF
#define COL_FG 0x00

static const char *page_about[] = {
    "Graphs f(x), shades the area",
    "between a and b, and prints the",
    "value of the integral.",
    "",
    "GRAPH also overlays F(x), the",
    "running integral of f from a.",
    "Enter x as b to graph only F(x).",
    "Typing expressions:",
    " + - * / ^ ( ) and the keys for",
    " sin cos tan ln log",
    " 2nd+x^2=sqrt(   2nd+ln=exp(",
    " 2nd+^=pi  2nd+/=e  math=abs(",
    " Implicit multiply works:",
    "   2x, 3sin(x), (x+1)(x-2)",
    0
};

static const char *page_keys[] = {
    "Graph screen:",
    " arrows  pan (hold to glide)",
    " + / -   zoom in / out",
    " trace   trace cursor on/off",
    " graph   F(x) curve on/off",
    " math    show the antiderivative",
    " y=      edit f(x)",
    " window  edit bounds a and b",
    " zoom    reset the window",
    " mode    quit  (clear too)",
    "In trace mode: arrows move the",
    "cursor (2nd+arrows = fast),",
    "ENTER recenters, +/- zoom at",
    "the cursor.",
    "Editors: ENTER=ok  DEL=delete",
    " CLR=clear, then back",
    0
};

static const char *page_warn[] = {
    "Numbers are 32-bit floats, so",
    "about 7 digits. Zooming in",
    "stops when pixels run out of",
    "precision.",
    "",
    "\"!\" after the integral value:",
    "some sample points could not be",
    "evaluated (holes / asymptotes",
    "were counted as 0).",
    "",
    "Gaps in a curve mark domain",
    "holes or poles (1/x, tan, ln).",
    "",
    "F(x) is drawn at screen",
    "accuracy; the printed integral",
    "is computed more precisely.",
    0
};

static const char *titles[3] = { "ABOUT", "KEYS", "ACCURACY" };
static const char **pages[3] = { page_about, page_keys, page_warn };

static void render_page(int p) {
    gfx_FillScreen(COL_BG);
    gfx_SetColor(COL_FG);
    gfx_SetTextFGColor(COL_FG);
    gfx_SetTextBGColor(COL_BG);
    gfx_SetTextTransparentColor(COL_BG);

    gfx_PrintStringXY("Help - ", 4, 4);
    gfx_PrintStringXY(titles[p], 4 + 7 * 8, 4);
    gfx_PrintStringXY(p == 0 ? "1/3" : p == 1 ? "2/3" : "3/3", 320 - 4 - 3 * 8, 4);
    gfx_HorizLine(0, 16, 320);

    int y = 24;
    for (const char **line = pages[p]; *line; line++) {
        gfx_PrintStringXY(*line, 4, y);
        y += 11;
    }

    gfx_HorizLine(0, 240 - 16, 320);
    gfx_PrintStringXY("<> page   CLR/ENTER back", 4, 240 - 12);
    gfx_SwapDraw();
}

/* Input here uses keypadc, matching the graph screen. Mixing kb_Scan() with
 * os_GetCSC() in one screen makes the OS scancodes unreliable, and help is
 * reachable from both the graph (keypadc) and the editors (os_GetCSC), so
 * this screen owns the keypad outright and restores it on the way out. */
static uint8_t prevk[8];

static int edge(kb_lkey_t k) {
    return kb_IsDown(k) && !(prevk[k >> 8] & (uint8_t)k);
}

void help_show(void) {
    int p = 0;

    memset(prevk, 0xFF, sizeof prevk);   /* ignore keys already held */
    render_page(p);

    for (;;) {
        kb_Scan();

        if (edge(kb_KeyClear) || edge(kb_KeyEnter) ||
            edge(kb_KeyAlpha) || edge(kb_KeyMode)) {
            break;
        }
        if (edge(kb_KeyLeft) || edge(kb_KeyUp)) {
            p = (p + 2) % 3;
            render_page(p);
        } else if (edge(kb_KeyRight) || edge(kb_KeyDown)) {
            p = (p + 1) % 3;
            render_page(p);
        }

        for (int i = 1; i < 8; i++) prevk[i] = (uint8_t)kb_Data[i];
    }

    /* Let go before handing the keypad back, so the caller doesn't see the
     * same press again (the editors block on os_GetCSC). */
    do { kb_Scan(); } while (kb_AnyKey());
    kb_Reset();
}
