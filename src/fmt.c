#include "fmt.h"
#include <math.h>

/* Write the 6 significant digits of m (in [100000, 999999], representing
 * mantissa*1e5) with the decimal point after `intdigits` digits, trimming
 * trailing fractional zeros. Returns chars written. */
static int put_digits(char *out, long m, int intdigits) {
    char d[8];
    for (int i = 5; i >= 0; i--) {
        d[i] = (char)('0' + (int)(m % 10));
        m /= 10;
    }
    int p = 0;
    int used = 0;
    /* integer part (pad with zeros when it's wider than 6 digits) */
    for (int i = 0; i < intdigits; i++) {
        out[p++] = (used < 6) ? d[used] : '0';
        used++;
    }
    /* fractional part, trimmed */
    int last = 5;
    while (last >= used && d[last] == '0') last--;
    if (last >= used) {
        out[p++] = '.';
        for (int i = used; i <= last; i++) out[p++] = d[i];
    }
    out[p] = '\0';
    return p;
}

int fmt_g(char *out, float v) {
    int p = 0;

    if (isnan(v)) { out[0] = '?'; out[1] = '\0'; return 1; }
    if (v < 0.0f) { out[p++] = '-'; v = -v; }
    if (isinf(v)) { out[p] = 'i'; out[p+1] = 'n'; out[p+2] = 'f'; out[p+3] = '\0'; return p + 3; }
    if (v == 0.0f) { out[0] = '0'; out[1] = '\0'; return 1; }

    /* normalize to t in [1, 10) */
    int e10 = 0;
    float t = v;
    while (t >= 10.0f) { t /= 10.0f; e10++; }
    while (t < 1.0f)   { t *= 10.0f; e10--; }

    long m = (long)(t * 100000.0f + 0.5f);   /* 6 significant digits */
    if (m >= 1000000L) { m /= 10; e10++; }

    if (e10 >= 7 || e10 <= -5) {
        /* scientific: d.ddddd e n */
        p += put_digits(out + p, m, 1);
        out[p++] = 'e';
        if (e10 < 0) { out[p++] = '-'; e10 = -e10; }
        char tmp[4];
        int n = 0;
        do { tmp[n++] = (char)('0' + e10 % 10); e10 /= 10; } while (e10);
        while (n) out[p++] = tmp[--n];
        out[p] = '\0';
        return p;
    }

    if (e10 < 0) {
        /* 0.00ddd... */
        out[p++] = '0';
        out[p++] = '.';
        for (int i = 1; i < -e10; i++) out[p++] = '0';
        /* all six digits are fractional; trim trailing zeros */
        char d[8];
        long mm = m;
        for (int i = 5; i >= 0; i--) { d[i] = (char)('0' + (int)(mm % 10)); mm /= 10; }
        int last = 5;
        while (last > 0 && d[last] == '0') last--;
        for (int i = 0; i <= last; i++) out[p++] = d[i];
        out[p] = '\0';
        return p;
    }

    return p + put_digits(out + p, m, e10 + 1);
}
