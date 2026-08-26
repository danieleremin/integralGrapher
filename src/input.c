#include "input.h"
#include "expr.h"
#include "mathprint.h"
#include "help.h"

#include <ti/getcsc.h>
#include <graphx.h>
#include <string.h>
#include <ctype.h>

#define COL_BG    0xFF      /* xlibc white */
#define COL_FG    0x00      /* xlibc black */

#define SCREEN_W  320
#define SCREEN_H  240
#define CW        8
#define CH        8

#define EDIT_LINE_Y   48
#define STATUS_LINE_Y 212

/* Block until a key is pressed, then wait for release. */
static uint8_t wait_key(void) {
    uint8_t k;
    while (!(k = os_GetCSC()));
    while (os_GetCSC());
    return k;
}

static void render_edit(const InputCfg *cfg, const char *input, const char *msg) {
    gfx_FillScreen(COL_BG);
    gfx_SetColor(COL_FG);
    gfx_SetTextFGColor(COL_FG);
    gfx_SetTextBGColor(COL_BG);
    gfx_SetTextTransparentColor(COL_BG);

    gfx_PrintStringXY("Integral Grapher", 4, 4);
    gfx_HorizLine(0, 16, SCREEN_W);
    gfx_HorizLine(0, STATUS_LINE_Y - 4, SCREEN_W);

    gfx_PrintStringXY(cfg->title, 4, EDIT_LINE_Y);
    int input_x = 4 + (int)strlen(cfg->title) * CW;
    int cur_x   = input_x;

    if (input[0] != '\0') {
        ASTNode *parsed = parser_init_from_string(input);
        MPBox b;
        if (parsed) mp_measure(parsed, &b);
        if (parsed && input_x + b.w < SCREEN_W - 2) {
            int yt = EDIT_LINE_Y + CH - b.baseline;
            mp_draw(parsed, input_x, yt);
            cur_x = input_x + b.w;
        } else {
            /* Raw text fallback (malformed prefix, or too wide for the
             * MathPrint layout). Keep the tail — and the cursor — visible. */
            int maxch = (SCREEN_W - 2 - input_x) / CW - 1;
            int len = (int)strlen(input);
            const char *shown = (len > maxch) ? input + (len - maxch) : input;
            gfx_PrintStringXY(shown, input_x, EDIT_LINE_Y);
            cur_x = input_x + (int)strlen(shown) * CW;
        }
        if (parsed) ast_free_tree(parsed);
    }

    gfx_VertLine(cur_x, EDIT_LINE_Y, CH);

    if (msg) {
        gfx_PrintStringXY(msg, 4, STATUS_LINE_Y);
    } else {
        gfx_PrintStringXY("ENTER=ok  DEL=del  CLR=clear/back", 4, STATUS_LINE_Y);
    }
    gfx_PrintStringXY("alpha=help  2nd+MODE=quit", 4, STATUS_LINE_Y + 12);

    gfx_SwapDraw();
}

/* Would appending `ap` after the current buffer start a new primary right
 * after a finished one? Then a '*' belongs between them (TI-style implicit
 * multiplication, resolved at append time because the parser has none). */
static int needs_implicit_mul(const char *buf, int len, const char *ap) {
    if (len == 0) return 0;
    char last = buf[len - 1];
    char c = ap[0];

    int atom_end = isdigit((unsigned char)last) || last == '.' || last == ')' ||
                   last == 'x' || last == 'i' || last == 'e';
    if (!atom_end) return 0;

    int primary_start = isdigit((unsigned char)c) || c == '.' || c == '(' ||
                        isalpha((unsigned char)c);
    if (!primary_start) return 0;

    /* "12" + "3" or "1" + "." continues the same number */
    int last_numeric = isdigit((unsigned char)last) || last == '.';
    int ap_numeric   = isdigit((unsigned char)c)    || c == '.';
    if (last_numeric && ap_numeric) return 0;

    return 1;
}

int input_line(const InputCfg *cfg, char *buf, int cap) {
    int len = (int)strlen(buf);
    int is2nd = 0;
    const char *msg = NULL;

    while (os_GetCSC());   /* drain stale presses */
    render_edit(cfg, buf, msg);

    for (;;) {
        uint8_t k = wait_key();
        const char *append = NULL;

        if (is2nd) {
            is2nd = 0;
            switch (k) {
                case sk_Mode:   return INPUT_QUIT;           /* 2nd+MODE = quit  */
                case sk_Square: append = "sqrt(";  break;    /* 2nd+x^2  = sqrt( */
                case sk_Ln:     append = "exp(";   break;    /* 2nd+LN   = exp(  */
                case sk_Power:  append = "pi";     break;    /* 2nd+^    = pi    */
                case sk_Div:    append = "e";      break;    /* 2nd+/    = e     */
                default:        continue;
            }
        } else {
            switch (k) {
                case sk_2nd:   is2nd = 1; continue;
                case sk_Alpha:
                    help_show();
                    while (os_GetCSC());   /* help used the raw keypad */
                    msg = NULL;
                    render_edit(cfg, buf, msg);
                    continue;
                case sk_Clear:
                    if (len == 0) return INPUT_CANCEL;
                    len = 0; buf[0] = '\0';
                    msg = NULL;
                    render_edit(cfg, buf, msg);
                    continue;
                case sk_Del:
                    if (len > 0) buf[--len] = '\0';
                    msg = NULL;
                    render_edit(cfg, buf, msg);
                    continue;

                case sk_Enter: {
                    if (len == 0) continue;
                    ASTNode *parsed = parser_init_from_string(buf);
                    if (!parsed) {
                        msg = "Can't parse that - check syntax";
                        render_edit(cfg, buf, msg);
                        continue;
                    }
                    ast_free_tree(parsed);
                    if (cfg->validate) {
                        const char *err = cfg->validate(buf);
                        if (err) {
                            msg = err;
                            render_edit(cfg, buf, msg);
                            continue;
                        }
                    }
                    return INPUT_OK;
                }

                case sk_0:        append = "0"; break;
                case sk_1:        append = "1"; break;
                case sk_2:        append = "2"; break;
                case sk_3:        append = "3"; break;
                case sk_4:        append = "4"; break;
                case sk_5:        append = "5"; break;
                case sk_6:        append = "6"; break;
                case sk_7:        append = "7"; break;
                case sk_8:        append = "8"; break;
                case sk_9:        append = "9"; break;
                case sk_DecPnt:   append = ".";  break;
                case sk_GraphVar:
                    if (!cfg->allow_x) {
                        msg = "x is not allowed here";
                        render_edit(cfg, buf, msg);
                        continue;
                    }
                    append = "x";
                    break;
                case sk_Add:      append = "+";  break;
                case sk_Sub:      append = "-";  break;
                case sk_Chs:      append = "-";  break;
                case sk_Mul:      append = "*";  break;
                case sk_Div:      append = "/";  break;
                case sk_Power:    append = "^";  break;
                case sk_Recip:    append = "^-1"; break;
                case sk_LParen:   append = "(";  break;
                case sk_RParen:   append = ")";  break;
                case sk_Sin:      append = "sin("; break;
                case sk_Cos:      append = "cos("; break;
                case sk_Tan:      append = "tan("; break;
                case sk_Ln:       append = "ln(";  break;
                case sk_Log:      append = "log("; break;
                case sk_Math:     append = "abs("; break;
                case sk_Square:   append = "^2";   break;
                default: continue;
            }
        }

        if (append) {
            int alen = (int)strlen(append);
            int star = needs_implicit_mul(buf, len, append);
            if (len + star + alen < cap) {
                if (star) buf[len++] = '*';
                memcpy(buf + len, append, (size_t)alen + 1);
                len += alen;
                msg = NULL;
            }
        }
        render_edit(cfg, buf, msg);
    }
}
