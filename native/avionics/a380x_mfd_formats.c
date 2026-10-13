/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD data-entry formats (WP-B). Simulator-independent C11 translation of the pinned FlyByWire
 * A380X MFD DataEntryFormats.tsx and QnhUtils.ts, with the [PORT] decisions of
 * docs/mfd-spec/02-data-formats-messages.md §12. No XPLM, no OpenGL, no allocation.
 *
 * JS emulation (Number(), parseFloat(), toFixed(), toString(), padStart(), Math.round()) is
 * locale independent: numbers are printed/parsed with '.' whatever LC_NUMERIC the host has set.
 */
#include "a380x_mfd_formats.h"

#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG "\xC2\xB0"

/* ---- Small string helpers ---------------------------------------------------------------------- */

/* Copies src into dst (size n), truncating on a UTF-8 code-point boundary. */
static void copy_str(char *dst, size_t n, const char *src)
{
    size_t len;
    if (!dst || n == 0) return;
    if (!src) src = "";
    len = strlen(src);
    if (len >= n) {
        len = n - 1;
        while (len > 0 && (((unsigned char)src[len]) & 0xC0u) == 0x80u) len--;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void append_str(char *dst, size_t n, const char *src)
{
    size_t len;
    if (!dst || n == 0) return;
    len = strlen(dst);
    if (len >= n) return;
    copy_str(dst + len, n - len, src);
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_upper(char c) { return c >= 'A' && c <= 'Z'; }

static int is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/* Byte length of the JS StrWhiteSpaceChar (WhiteSpace or LineTerminator) starting at s, 0 if none. */
static size_t js_ws_len(const unsigned char *s)
{
    unsigned c = s[0];
    if (c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D || c == 0x20) return 1;
    if (c == 0xC2 && s[1] == 0xA0) return 2;                             /* U+00A0 */
    if (c == 0xE1 && s[1] == 0x9A && s[2] == 0x80) return 3;             /* U+1680 */
    if (c == 0xE2 && s[1] == 0x80 &&
        ((s[2] >= 0x80 && s[2] <= 0x8A) || s[2] == 0xA8 || s[2] == 0xA9 || s[2] == 0xAF))
        return 3;                                                        /* U+2000-200A, 2028, 2029, 202F */
    if (c == 0xE2 && s[1] == 0x81 && s[2] == 0x9F) return 3;             /* U+205F */
    if (c == 0xE3 && s[1] == 0x80 && s[2] == 0x80) return 3;             /* U+3000 */
    if (c == 0xEF && s[1] == 0xBB && s[2] == 0xBF) return 3;             /* U+FEFF */
    return 0;
}

/* JS String.prototype.trim() bounds: [*b, *e). */
static void js_trim(const char *s, size_t *b, size_t *e)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t len = strlen(s), i = 0, end;
    while (i < len) {
        size_t w = js_ws_len(u + i);
        if (!w) break;
        i += w;
    }
    *b = i;
    end = i;
    while (i < len) {
        size_t w = js_ws_len(u + i);
        if (w) {
            i += w;
        } else {
            i++;
            end = i;
        }
    }
    *e = end;
}

/* ---- Locale-independent decimal conversion --------------------------------------------------- */

static const char *locale_point(void)
{
    const struct lconv *lc = localeconv();
    return (lc && lc->decimal_point && lc->decimal_point[0]) ? lc->decimal_point : ".";
}

static int point_is_dot(const char *dp) { return dp[0] == '.' && dp[1] == '\0'; }

/* Replaces the locale decimal point produced by printf with '.'. */
static void normalize_point(char *s)
{
    const char *dp = locale_point();
    char *p;
    size_t dl;
    if (point_is_dot(dp)) return;
    p = strstr(s, dp);
    if (!p) return;
    dl = strlen(dp);
    *p = '.';
    memmove(p + 1, p + dl, strlen(p + dl) + 1);
}

/* strtod() of a validated decimal literal s[0..len) written with '.'. */
static double dec_to_double(const char *s, size_t len)
{
    char buf[512];
    const char *dp = locale_point();
    size_t dl, o = 0, i;
    if (point_is_dot(dp)) {
        if (len < sizeof buf) {
            memcpy(buf, s, len);
            buf[len] = '\0';
            return strtod(buf, NULL);
        }
        return strtod(s, NULL); /* long literal: strtod stops where the validated literal ends */
    }
    dl = strlen(dp);
    for (i = 0; i < len; i++) {
        if (s[i] == '.') {
            if (o + dl >= sizeof buf) return NAN;
            memcpy(buf + o, dp, dl);
            o += dl;
        } else {
            if (o + 1 >= sizeof buf) return NAN;
            buf[o++] = s[i];
        }
    }
    buf[o] = '\0';
    return strtod(buf, NULL);
}

/* Length of the StrDecimalLiteral (without Infinity) at s[0..len), 0 if none. */
static size_t scan_decimal(const char *s, size_t len, int allow_exp)
{
    size_t i = 0, int_start, nint, nfrac = 0;
    if (i < len && (s[i] == '+' || s[i] == '-')) i++;
    int_start = i;
    while (i < len && is_digit(s[i])) i++;
    nint = i - int_start;
    if (i < len && s[i] == '.') {
        size_t j = i + 1;
        while (j < len && is_digit(s[j])) j++;
        nfrac = j - i - 1;
        if (nint > 0 || nfrac > 0) i = j;
    }
    if (nint == 0 && nfrac == 0) return 0;
    if (allow_exp && i < len && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1, ds;
        if (j < len && (s[j] == '+' || s[j] == '-')) j++;
        ds = j;
        while (j < len && is_digit(s[j])) j++;
        if (j > ds) i = j;
    }
    return i;
}

static int digit_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ---- JS emulation (spec 02 §2.1, §2.2) ------------------------------------------------------------ */

int mfd_js_number(const char *s, double *out)
{
    double v = NAN;
    int ok = 0;
    size_t b, e, len;
    const char *p;
    if (!s) s = "";
    js_trim(s, &b, &e);
    p = s + b;
    len = e - b;
    if (len == 0) {
        v = 0.0;
        ok = 1;
    } else if (len > 2 && p[0] == '0' &&
               (p[1] == 'x' || p[1] == 'X' || p[1] == 'o' || p[1] == 'O' || p[1] == 'b' || p[1] == 'B')) {
        int base = (p[1] == 'x' || p[1] == 'X') ? 16 : (p[1] == 'o' || p[1] == 'O') ? 8 : 2;
        double acc = 0.0;
        size_t i;
        for (i = 2; i < len; i++) {
            int d = digit_value(p[i]);
            if (d < 0 || d >= base) break;
            acc = acc * base + d;
        }
        if (i == len) {
            v = acc;
            ok = 1;
        }
    } else {
        size_t i = 0;
        int neg = 0;
        if (p[0] == '+' || p[0] == '-') {
            neg = p[0] == '-';
            i = 1;
        }
        if (len - i == 8 && memcmp(p + i, "Infinity", 8) == 0) {
            v = neg ? -INFINITY : INFINITY;
            ok = 1;
        } else if (scan_decimal(p, len, 1) == len) {
            v = dec_to_double(p, len);
            ok = 1;
        }
    }
    if (out) *out = v;
    return ok;
}

int mfd_strict_number(const char *s, double *out)
{
    double v = NAN;
    int ok = 0;
    size_t b = 0, e;
    if (!s) s = "";
    e = strlen(s);
    while (b < e && is_ascii_space(s[b])) b++;
    while (e > b && is_ascii_space(s[e - 1])) e--;
    if (e > b && scan_decimal(s + b, e - b, 0) == e - b) {
        v = dec_to_double(s + b, e - b);
        ok = 1;
    }
    if (out) *out = v;
    return ok;
}

double mfd_js_parse_float(const char *s)
{
    size_t b, e, len, n, i = 0;
    const char *p;
    if (!s) return NAN;
    js_trim(s, &b, &e);
    p = s + b;
    len = strlen(p);
    if (len > 0 && (p[0] == '+' || p[0] == '-')) i = 1;
    if (len - i >= 8 && memcmp(p + i, "Infinity", 8) == 0) return p[0] == '-' ? -INFINITY : INFINITY;
    n = scan_decimal(p, len, 1);
    if (n == 0) return NAN;
    return dec_to_double(p, n);
}

void mfd_js_to_string(double v, char *buf, size_t n)
{
    char out[64], tmp[64], dig[24];
    if (!buf || n == 0) return;
    if (isnan(v)) {
        copy_str(buf, n, "NaN");
        return;
    }
    if (v == 0.0) {
        copy_str(buf, n, "0");
        return;
    }
    if (isinf(v)) {
        copy_str(buf, n, v > 0 ? "Infinity" : "-Infinity");
        return;
    }
    {
        double a = fabs(v);
        int p, k = 0, e10, nexp;
        const char *q;
        char *o = out;
        size_t room = sizeof out;
        for (p = 1; p <= 17; p++) {
            snprintf(tmp, sizeof tmp, "%.*e", p - 1, a);
            if (strtod(tmp, NULL) == a) break;
        }
        for (q = tmp; *q && *q != 'e' && *q != 'E'; q++)
            if (is_digit(*q) && k < (int)sizeof dig - 1) dig[k++] = *q;
        e10 = (*q) ? atoi(q + 1) : 0;
        while (k > 1 && dig[k - 1] == '0') k--;
        dig[k] = '\0';
        nexp = e10 + 1;
        if (v < 0) {
            *o++ = '-';
            room--;
        }
        if (k <= nexp && nexp <= 21) {
            snprintf(o, room, "%s%.*s", dig, nexp - k, "000000000000000000000");
        } else if (0 < nexp && nexp <= 21) {
            snprintf(o, room, "%.*s.%s", nexp, dig, dig + nexp);
        } else if (-6 < nexp && nexp <= 0) {
            snprintf(o, room, "0.%.*s%s", -nexp, "000000", dig);
        } else {
            int x = nexp - 1;
            snprintf(o, room, "%c%s%s" "e%c%d", dig[0], k > 1 ? "." : "", dig + 1, x >= 0 ? '+' : '-',
                     x >= 0 ? x : -x);
        }
        copy_str(buf, n, out);
    }
}

/* True when a (> 0) is exactly halfway between two multiples of 10^-d. a = m * 2^e with m a 53-bit
 * integer; a * 10^d = m * 5^d * 2^(e+d) has fractional part 1/2 iff e + d < 0 and the lowest set bit of
 * m (5^d is odd) sits exactly at position -(e + d) - 1. */
static int is_exact_tie(double a, int d)
{
    int ex, E, tz = 0;
    uint64_t m;
    if (!(a > 0.0) || !isfinite(a)) return 0;
    m = (uint64_t)ldexp(frexp(a, &ex), 53);
    E = ex - 53 + d;
    if (E >= 0 || m == 0) return 0;
    while (!(m & 1u)) {
        m >>= 1;
        tz++;
    }
    return tz == -E - 1;
}

/* Adds one unit in the last place of a non-negative decimal string (digits and at most one '.'). */
static void decimal_increment(char *s, size_t n)
{
    size_t len = strlen(s), i = len;
    while (i > 0) {
        i--;
        if (s[i] == '.') continue;
        if (s[i] == '9') {
            s[i] = '0';
        } else {
            s[i]++;
            return;
        }
    }
    if (len + 2 <= n) { /* carry out of the leading digit */
        memmove(s + 1, s, len + 1);
        s[0] = '1';
    }
}

void mfd_js_to_fixed(double v, int digits, char *buf, size_t n)
{
    char tmp[200];
    double a;
    if (!buf || n == 0) return;
    if (isnan(v)) {
        copy_str(buf, n, "NaN");
        return;
    }
    if (fabs(v) >= 1e21) { /* includes +-Infinity */
        mfd_js_to_string(v, buf, n);
        return;
    }
    if (digits < 0) digits = 0;
    if (digits > 100) digits = 100;
    a = fabs(v);
    if (is_exact_tie(a, digits)) {
        /* a has exactly digits + 1 decimals: print them exactly, drop the final 5, round up. */
        size_t len;
        snprintf(tmp, sizeof tmp, "%.*f", digits + 1, a);
        normalize_point(tmp);
        len = strlen(tmp);
        if (len > 0) tmp[--len] = '\0';
        if (len > 0 && tmp[len - 1] == '.') tmp[--len] = '\0';
        decimal_increment(tmp, sizeof tmp);
    } else {
        snprintf(tmp, sizeof tmp, "%.*f", digits, a);
        normalize_point(tmp);
    }
    copy_str(buf, n, v < 0 ? "-" : "");
    append_str(buf, n, tmp);
}

void mfd_pad_start(char *buf, size_t n, size_t width, char pad)
{
    size_t len, add;
    if (!buf || n == 0) return;
    len = strlen(buf);
    if (len >= width) return;
    add = width - len;
    if (len + add + 1 > n) add = (n > len + 1) ? n - 1 - len : 0;
    if (!add) return;
    memmove(buf + add, buf, len + 1);
    memset(buf, pad, add);
}

double mfd_js_round(double v)
{
    double r;
    if (!isfinite(v)) return v;
    r = floor(v);
    if (v - r >= 0.5) r += 1.0; /* v - floor(v) is exact */
    return r;
}

int mfd_str_replace_first(char *buf, size_t n, const char *token, const char *value)
{
    char tmp[512];
    char *p;
    size_t pre;
    if (!buf || n == 0 || !token || !token[0]) return 0;
    p = strstr(buf, token);
    if (!p) return 0;
    pre = (size_t)(p - buf);
    if (pre >= sizeof tmp) return 0;
    memcpy(tmp, buf, pre);
    tmp[pre] = '\0';
    append_str(tmp, sizeof tmp, value ? value : "");
    append_str(tmp, sizeof tmp, p + strlen(token));
    copy_str(buf, n, tmp);
    return 1;
}

/* ---- Errors (spec 02 §2.3) ---------------------------------------------------------------------- */

void mfd_err_set_format(MfdFmsError *e, const char *required_format, const char *unit)
{
    if (!e) return;
    e->type = MFD_ERR_FORMAT_ERROR;
    copy_str(e->details, sizeof e->details, "FORMAT: ");
    append_str(e->details, sizeof e->details, required_format ? required_format : "");
    if (unit && unit[0]) {
        append_str(e->details, sizeof e->details, " ");
        append_str(e->details, sizeof e->details, unit);
    }
}

void mfd_err_set_range(MfdFmsError *e, const char *lo, const char *hi, const char *unit)
{
    if (!e) return;
    e->type = MFD_ERR_ENTRY_OUT_OF_RANGE;
    copy_str(e->details, sizeof e->details, "RNG: ");
    append_str(e->details, sizeof e->details, lo ? lo : "");
    append_str(e->details, sizeof e->details, " TO ");
    append_str(e->details, sizeof e->details, hi ? hi : "");
    if (unit && unit[0]) {
        append_str(e->details, sizeof e->details, " ");
        append_str(e->details, sizeof e->details, unit);
    }
}

void mfd_err_set_plain(MfdFmsError *e, MfdFmsErrorType t)
{
    if (!e) return;
    e->type = t;
    e->details[0] = '\0';
}

/* ---- QNH (QnhUtils.ts) --------------------------------------------------------------------------- */

int mfd_qnh_is_inhg(double qnh) { return qnh < 100.0; }

double mfd_qnh_to_millibar(double qnh) { return mfd_qnh_is_inhg(qnh) ? qnh * MFD_HPA_PER_INHG : qnh; }

/* ---- Text pattern helpers ------------------------------------------------------------------------ */

int mfd_is_place_format(const char *s)
{
    size_t i, len;
    if (!s) return 0;
    len = strlen(s);
    if (len < 2 || len > 7) return 0;
    for (i = 0; i < len; i++)
        if (!is_upper(s[i]) && !is_digit(s[i])) return 0;
    return 1;
}

int mfd_is_runway_format(const char *s)
{
    size_t len;
    if (!s) return 0;
    len = strlen(s);
    if (len != 6 && len != 7) return 0;
    if (!is_upper(s[0]) || !is_upper(s[1]) || !is_upper(s[2]) || !is_upper(s[3])) return 0;
    if (!is_digit(s[4]) || !is_digit(s[5])) return 0;
    return len == 6 || s[6] == 'L' || s[6] == 'C' || s[6] == 'R';
}

/* ---- Format table (spec 02 §3, §4) ---------------------------------------------------------------- */

enum { RANGE_NONE, RANGE_DYN, RANGE_FIXED };
enum { RT_STR, RT_FIX0, RT_FIX1, RT_FIX2, RT_PAD4_FIX1 };

typedef struct {
    const char   *placeholder;
    unsigned char max_digits, max_overflow;
    unsigned char range;           /* RANGE_*: DYN = caller range (min == max == 0 -> default) */
    double        min, max;        /* default (DYN) or fixed range, storage units */
    const char   *err_unit;        /* FBW `unit` member */
    const char   *lead, *trail;    /* units next to a value, base case */
    unsigned char units_when_null; /* units also shown next to the placeholder */
    const char   *fmt;             /* FORMAT detail text; NULL -> plain FORMAT ERROR */
    const char   *fmt_unit;
    unsigned char plain_range;     /* ENTRY OUT OF RANGE without details */
    unsigned char rng_lo, rng_hi;  /* RT_*: how min / max are printed in the RANGE detail */
    const char   *rng_unit;
} FmtDef;

#define INF_ INFINITY
#define FMT_ALT_FL "FOR ALT XXXXX FOR FL XXX"

/* clang-format off */
static const FmtDef fmt_defs[MFD_FMT_COUNT] = {
/*                         placeholder  dig ovf range        min     max      err_unit lead  trail      null fmt          fmt_unit plain lo       hi       rng_unit */
[MFD_FMT_SPEED_KT]       = {"---",       3, 0, RANGE_DYN,    0,      INF_,    "KT",    "",   "KT",      1, "XXX",        "KT",    0, RT_STR,  RT_STR,  "KT"},
[MFD_FMT_SPEED_MACH]     = {".--",       3, 0, RANGE_DYN,    0,      INF_,    "",      "",   "",        0, "XX",         NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_ALT_OR_FL]      = {"-----",     5, 0, RANGE_DYN,    0,      43000,   "",      "",   "FT",      1, FMT_ALT_FL,   NULL,    1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_ALTITUDE]       = {"-----",     5, 0, RANGE_DYN,    0,      43000,   "FT",    "",   "FT",      1, "XXXXX",      "FT",    1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_FLIGHT_LEVEL]   = {"---",       3, 2, RANGE_DYN,    0,      430,     "FL",    "FL", "",        1, "FL XXX",     NULL,    1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_RADIO_ALT]      = {"-----",     5, 0, RANGE_DYN,    1,      43000,   "",      "",   "FT",      1, NULL,         NULL,    1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TROPO]          = {"-----",     5, 0, RANGE_FIXED,  1000,   60000,   "FT",    "",   "FT",      1, FMT_ALT_FL,   NULL,    1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_LENGTH]         = {"----",      4, 0, RANGE_DYN,    0,      INF_,    "",      "",   "M",       1, "XXXX",       "M",     1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_WEIGHT]         = {"---.-",     5, 0, RANGE_DYN,    0,      INF_,    "",      "",   "T",       1, "XXX.X",      "T",     0, RT_FIX1, RT_FIX1, "T"},
[MFD_FMT_PERCENT]        = {"--.-",      4, 0, RANGE_DYN,    0,      INF_,    "%",     "",   "%",       1, "XX.X",       "%",     0, RT_FIX1, RT_FIX1, "%"},
[MFD_FMT_TEMPERATURE]    = {"---",       3, 0, RANGE_DYN,    0,      INF_,    DEG "C", "",   DEG "C",   1, "+/-XX",      DEG "C", 0, RT_STR,  RT_STR,  DEG "C"},
[MFD_FMT_CRZ_TEMP]       = {"---",       3, 0, RANGE_FIXED, -99,     99,      DEG "C", "",   DEG "C",   1, "+/-XXX",     "C",     0, RT_STR,  RT_STR,  "C"},
[MFD_FMT_WIND_DIR]       = {"---",       3, 0, RANGE_FIXED,  0,      359,     DEG,     "",   DEG,       1, "XXX",        DEG,     0, RT_STR,  RT_STR,  DEG},
[MFD_FMT_WIND_SPEED]     = {"---",       3, 0, RANGE_FIXED,  0,      250,     "KT",    "",   "KT",      1, "XXX",        "KT",    0, RT_STR,  RT_STR,  "KT"},
[MFD_FMT_TRIP_WIND]      = {"-----",     5, 0, RANGE_FIXED, -250,    250,     "KT",    "TL", "",        0, "+/-XXX",     NULL,    0, RT_STR,  RT_STR,  "KT"},
[MFD_FMT_QNH]            = {"----",      5, 0, RANGE_NONE,   0,      0,       "",      "",   "",        0, "XXXX",       NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_COST_INDEX]     = {"---",       3, 0, RANGE_FIXED,  0,      999,     "",      "",   "",        0, "XXX",        NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_VERTICAL_SPEED] = {"---",       4, 0, RANGE_DYN,    0,      INF_,    "FT/MN", "",   "FT/MN",   1, "+/-XXXX",    "FT/MN", 1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_DESCENT_RATE]   = {"----",      4, 0, RANGE_DYN,    0,      INF_,    "FT/MN", "",   "FT/MN",   1, "-XXXX",      "FT/MN", 0, RT_STR,  RT_STR,  "FT/MN"},
[MFD_FMT_PAX_NBR]        = {"---",       3, 0, RANGE_FIXED,  0,      999,     "",      "",   "",        0, "XXX",        NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TIME_HHMM]      = {"--:--",     4, 0, RANGE_FIXED,  0,      90,      "",      "",   "",        0, "HHMM",       NULL,    1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TIME_HHMMSS]    = {"--:--:--",  6, 0, RANGE_FIXED,  0,      86400,   "",      "",   "",        0, "HHMMSS",     NULL,    1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_LATITUDE]       = {"----.--",   7, 0, RANGE_FIXED, -90,     90,      "",      "",   "",        0, NULL,         NULL,    1, RT_STR,  RT_STR,  NULL},
[MFD_FMT_HEADING]        = {"---.-",     5, 0, RANGE_DYN,    0,      359.9,   DEG,     "",   DEG "T",   1, "XXX.X",      DEG,     0, RT_PAD4_FIX1, RT_FIX1, DEG},
[MFD_FMT_INBOUND_COURSE] = {"---",       4, 0, RANGE_DYN,    0,      360,     DEG,     "",   DEG,       1, "XXX",        DEG,     0, RT_FIX0, RT_FIX0, DEG},
[MFD_FMT_HOLD_DIST]      = {"--.-",      4, 0, RANGE_DYN,    0,      99.9,    "NM",    "",   "NM",      1, "XX.X",       "NM",    0, RT_FIX1, RT_FIX1, "NM"},
[MFD_FMT_HOLD_TIME]      = {"-.-",       3, 0, RANGE_DYN,    0,      9.9,     "MN",    "",   "MN",      1, "X.X",        "MN",    0, RT_FIX1, RT_FIX1, "MN"},
[MFD_FMT_FREQ_ILS]       = {"---.--",    6, 0, RANGE_FIXED,  108,    111.95,  "",      "",   "",        0, "XXX.XX",     NULL,    0, RT_FIX2, RT_FIX2, NULL},
[MFD_FMT_FREQ_VOR]       = {"---.--",    6, 0, RANGE_FIXED,  108,    117.95,  "",      "",   "",        0, "XXX.XX",     NULL,    0, RT_FIX2, RT_FIX2, NULL},
[MFD_FMT_FREQ_ADF]       = {"----.-",    6, 0, RANGE_FIXED,  190,    1750,    "",      "",   "",        0, "XXXX.X",     NULL,    0, RT_FIX1, RT_FIX1, NULL},
[MFD_FMT_LS_COURSE]      = {"----",      4, 0, RANGE_FIXED,  0,      360,     DEG,     "",   DEG,       1, "FXXX",       DEG,     0, RT_STR,  RT_FIX0, DEG},
[MFD_FMT_SQUAWK]         = {"----",      4, 0, RANGE_NONE,   0,      0,       "",      "",   "",        0, NULL,         NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_RADIAL]         = {"---",       4, 0, RANGE_FIXED,  0,      360,     DEG,     "",   DEG,       0, "XXX",        DEG,     0, RT_STR,  RT_FIX0, DEG},
[MFD_FMT_RADIUS]         = {"----",      4, 0, RANGE_FIXED,  1,      9999,    "NM",    "",   "NM",      0, "XXXX",       "NM",    0, RT_STR,  RT_STR,  "NM"},
[MFD_FMT_RNP]            = {"--.-",      4, 0, RANGE_FIXED,  0.01,   20.0,    "NM",    "",   "NM",      0, "X.XX",       "NM",    0, RT_FIX2, RT_FIX1, "NM"},
[MFD_FMT_FUEL_PENALTY]   = {"+000.0",    6, 0, RANGE_FIXED,  0,      999.9,   "%",     "",   "%",       1, "+NNN.N",     "%",     0, RT_FIX1, RT_FIX1, "%"},
[MFD_FMT_TXT_FIX]        = {"-------",   7, 0, RANGE_NONE,   0,      0,       "",      "",   "",        0, "XXXXX",      NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TXT_AIRPORT]    = {"----",      4, 0, RANGE_NONE,   0,      0,       "",      "",   "",        0, "AAAA",       NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TXT_NAVAID]     = {"----",      4, 0, RANGE_NONE,   0,      0,       "",      "",   "",        0, NULL,         NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TXT_AIRWAY]     = {"---",       5, 0, RANGE_NONE,   0,      0,       "",      "",   "",        0, NULL,         NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TXT_DROPDOWN]   = {"------",    6, 0, RANGE_NONE,   0,      0,       "",      "",   "",        0, NULL,         NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TXT_WAYPOINT]   = {"-------",   7, 0, RANGE_NONE,   0,      0,       "",      "",   "",        0, NULL,         NULL,    0, RT_STR,  RT_STR,  NULL},
[MFD_FMT_TXT_LONG_ALNUM] = {"----------",10, 0, RANGE_NONE,  0,      0,       "",      "",   "",        0, NULL,         NULL,    0, RT_STR,  RT_STR,  NULL},
};
/* clang-format on */

/* SpeedMachFormat compares against the constant Mmo, not the caller's max (DEF:151). */
#define MFD_MMO 0.89

static const char DASHES[] = "------------------------"; /* 24 = longest free-text entry */

static int kind_valid(const MfdFmt *f) { return f && (int)f->kind >= 0 && f->kind < MFD_FMT_COUNT; }

int mfd_fmt_is_text(MfdFmtKind kind) { return kind >= MFD_FMT_TXT_FIX && kind <= MFD_FMT_TXT_LONG_ALNUM; }

void mfd_fmt_init(MfdFmt *f, MfdFmtKind kind)
{
    if (!f) return;
    memset(f, 0, sizeof *f);
    f->kind = kind;
    if ((int)kind >= 0 && kind < MFD_FMT_COUNT && fmt_defs[kind].range != RANGE_NONE) {
        f->min = fmt_defs[kind].min;
        f->max = fmt_defs[kind].max;
    }
    f->metric = 1;
    f->qnh_is_hpa = 1;
    f->placeholder = NULL;
    f->err_details = 1;
}

int mfd_fmt_range(const MfdFmt *f, double *lo, double *hi)
{
    const FmtDef *d;
    double a, b;
    if (!kind_valid(f)) return 0;
    d = &fmt_defs[f->kind];
    if (d->range == RANGE_NONE) return 0;
    if (d->range == RANGE_DYN && !(f->min == 0.0 && f->max == 0.0)) {
        a = f->min;
        b = f->max;
    } else {
        a = d->min;
        b = d->max;
    }
    if (lo) *lo = a;
    if (hi) *hi = b;
    return 1;
}

int mfd_fmt_max_digits(const MfdFmt *f)
{
    if (!kind_valid(f)) return 0;
    if (f->max_digits > 0) return f->max_digits;
    return fmt_defs[f->kind].max_digits;
}

int mfd_fmt_max_overflow(const MfdFmt *f)
{
    if (!kind_valid(f)) return 0;
    return fmt_defs[f->kind].max_overflow;
}

const char *mfd_fmt_placeholder(const MfdFmt *f)
{
    if (!kind_valid(f)) return "";
    if (f->placeholder) return f->placeholder;
    if (f->kind == MFD_FMT_QNH) return f->qnh_is_hpa ? "----" : "--.--";
    if (f->kind == MFD_FMT_LENGTH || f->kind == MFD_FMT_TXT_DROPDOWN) {
        int nd = mfd_fmt_max_digits(f);
        if (nd < 0) nd = 0;
        if (nd > (int)(sizeof DASHES - 1)) nd = (int)(sizeof DASHES - 1);
        return DASHES + (sizeof DASHES - 1 - (size_t)nd);
    }
    return fmt_defs[f->kind].placeholder;
}

const char *mfd_fmt_error_unit(const MfdFmt *f)
{
    if (!kind_valid(f)) return "";
    return fmt_defs[f->kind].err_unit;
}

static const char *length_unit(int metric) { return metric ? "M" : "FT"; }
static const char *weight_unit(int metric) { return metric ? "T" : "KLB"; }

const char *mfd_fmt_lead_unit(const MfdFmt *f)
{
    if (!kind_valid(f)) return "";
    return fmt_defs[f->kind].lead;
}

const char *mfd_fmt_trail_unit(const MfdFmt *f)
{
    if (!kind_valid(f)) return "";
    if (f->kind == MFD_FMT_LENGTH) return length_unit(f->metric);
    if (f->kind == MFD_FMT_WEIGHT) return weight_unit(f->metric);
    return fmt_defs[f->kind].trail;
}

/* ---- Parse ------------------------------------------------------------------------------------- */

/* Number() used by the formats: [PORT] strict grammar unless MFD_FMT_FAITHFUL_JS_NUMBER (spec 02 §2.1). */
static double num(const char *s)
{
    double v;
#ifdef MFD_FMT_FAITHFUL_JS_NUMBER
    mfd_js_number(s, &v);
#else
    mfd_strict_number(s, &v);
#endif
    return v;
}

/* num() of s[0..len). */
static double num_n(const char *s, size_t len)
{
    char b[64];
    if (len >= sizeof b) return NAN;
    memcpy(b, s, len);
    b[len] = '\0';
    return num(b);
}

static void range_text(double v, int mode, char *buf, size_t n)
{
    switch (mode) {
    case RT_FIX0: mfd_js_to_fixed(v, 0, buf, n); break;
    case RT_FIX1: mfd_js_to_fixed(v, 1, buf, n); break;
    case RT_FIX2: mfd_js_to_fixed(v, 2, buf, n); break;
    case RT_PAD4_FIX1:
        mfd_js_to_fixed(v, 1, buf, n);
        mfd_pad_start(buf, n, 4, '0');
        break;
    default: mfd_js_to_string(v, buf, n); break;
    }
}

static MfdParseStatus ok_num(MfdValue *out, double v, MfdFmsError *err)
{
    if (out) {
        memset(out, 0, sizeof *out);
        out->has = 1;
        out->num = v;
    }
    if (err) mfd_err_set_plain(err, MFD_ERR_NONE);
    return MFD_PARSE_OK;
}

static MfdParseStatus ok_str(MfdValue *out, const char *s, MfdFmsError *err)
{
    if (strlen(s) >= sizeof out->str) { /* cannot be stored: never typable (24-character limit) */
        mfd_err_set_plain(err, MFD_ERR_FORMAT_ERROR);
        return MFD_PARSE_ERROR;
    }
    if (out) {
        memset(out, 0, sizeof *out);
        out->has = 1;
        copy_str(out->str, sizeof out->str, s);
    }
    if (err) mfd_err_set_plain(err, MFD_ERR_NONE);
    return MFD_PARSE_OK;
}

static MfdParseStatus clear(MfdValue *out, MfdFmsError *err)
{
    if (out) memset(out, 0, sizeof *out);
    if (err) mfd_err_set_plain(err, MFD_ERR_NONE);
    return MFD_PARSE_CLEAR;
}

static MfdParseStatus format_err(const FmtDef *d, const char *fmt_unit, MfdFmsError *err)
{
    if (d->fmt)
        mfd_err_set_format(err, d->fmt, fmt_unit);
    else
        mfd_err_set_plain(err, MFD_ERR_FORMAT_ERROR);
    return MFD_PARSE_ERROR;
}

static MfdParseStatus range_err(const FmtDef *d, double lo, double hi, MfdFmsError *err)
{
    char a[32], b[32];
    if (d->plain_range) {
        mfd_err_set_plain(err, MFD_ERR_ENTRY_OUT_OF_RANGE);
    } else {
        range_text(lo, d->rng_lo, a, sizeof a);
        range_text(hi, d->rng_hi, b, sizeof b);
        mfd_err_set_range(err, a, b, d->rng_unit);
    }
    return MFD_PARSE_ERROR;
}

/* FBW patterns P1/P2/P3 (spec 02 §2.4). P1 and P2 give identical results; P3 = plain_range. */
static MfdParseStatus finish(const FmtDef *d, double n, double lo, double hi, MfdValue *out, MfdFmsError *err)
{
    if (!isnan(n) && n >= lo && n <= hi) return ok_num(out, n, err);
    if (n > hi || n < lo) return range_err(d, lo, hi, err);
    return format_err(d, d->fmt_unit, err);
}

static int match_fl(const char *s, const char **digits)
{
    size_t len = strlen(s), i;
    if (len < 3 || len > 5) return 0;
    if (!((s[0] == 'F' || s[0] == 'f') && (s[1] == 'L' || s[1] == 'l'))) return 0;
    for (i = 2; i < len; i++)
        if (!is_digit(s[i])) return 0;
    *digits = s + 2;
    return 1;
}

/* QnhFormat.QNH_REGEX_INCHES = ^\d{2}\.?\d{2}$ */
static int match_qnh_inches(const char *s)
{
    size_t len = strlen(s);
    if (len == 4) return is_digit(s[0]) && is_digit(s[1]) && is_digit(s[2]) && is_digit(s[3]);
    if (len == 5)
        return is_digit(s[0]) && is_digit(s[1]) && s[2] == '.' && is_digit(s[3]) && is_digit(s[4]);
    return 0;
}

/* ^\d{min,max}$ */
static int match_digits(const char *s, size_t min, size_t max)
{
    size_t len = strlen(s), i;
    if (len < min || len > max) return 0;
    for (i = 0; i < len; i++)
        if (!is_digit(s[i])) return 0;
    return 1;
}

/* [PORT] LsCourse: ^(F[0-9]{3}|[0-9]{1,3})$ (spec 02 §4.16, Q7). */
static int match_ls_course(const char *s, const char **digits)
{
    if (s[0] == 'F') {
        if (!match_digits(s + 1, 3, 3)) return 0;
        *digits = s + 1;
        return 1;
    }
    if (!match_digits(s, 1, 3)) return 0;
    *digits = s;
    return 1;
}

/* FuelPenaltyPercentFormat: ^[+]?\d{1,3}(?:\.\d)?$ */
static int match_fuel_penalty(const char *s)
{
    size_t i = 0, d0;
    if (s[i] == '+') i++;
    d0 = i;
    while (is_digit(s[i])) i++;
    if (i - d0 < 1 || i - d0 > 3) return 0;
    if (s[i] == '\0') return 1;
    return s[i] == '.' && is_digit(s[i + 1]) && s[i + 2] == '\0';
}

/* input.replace(':', '') */
static int remove_first_colon(const char *in, char *out, size_t n)
{
    const char *c = strchr(in, ':');
    size_t len = strlen(in);
    if (len >= n) return 0;
    if (!c) {
        memcpy(out, in, len + 1);
    } else {
        size_t pre = (size_t)(c - in);
        memcpy(out, in, pre);
        memcpy(out + pre, c + 1, len - pre);
    }
    return 1;
}

static MfdParseStatus parse_text(const MfdFmt *f, const FmtDef *d, const char *in, MfdValue *out,
                                 MfdFmsError *err)
{
    const char *ph = mfd_fmt_placeholder(f);
    if (f->kind == MFD_FMT_TXT_FIX) {
        size_t b, e;
        js_trim(in, &b, &e);
        if (e == b) return clear(out, err);
        if (mfd_is_place_format(in) || mfd_is_runway_format(in)) return ok_str(out, in, err);
        return format_err(d, NULL, err);
    }
    if (in[0] == '\0' || strcmp(in, ph) == 0) return clear(out, err);
    if (f->kind == MFD_FMT_TXT_AIRPORT && strlen(in) != 4) return format_err(d, NULL, err);
    return ok_str(out, in, err);
}

MfdParseStatus mfd_fmt_parse(const MfdFmt *f, const char *in, MfdValue *out, MfdFmsError *err)
{
    const FmtDef *d;
    double lo = 0.0, hi = 0.0, n;
    if (err) mfd_err_set_plain(err, MFD_ERR_NONE);
    if (!kind_valid(f)) {
        mfd_err_set_plain(err, MFD_ERR_FORMAT_ERROR);
        return MFD_PARSE_ERROR;
    }
    if (!in) in = "";
    d = &fmt_defs[f->kind];
    if (mfd_fmt_is_text(f->kind)) return parse_text(f, d, in, out, err);
    if (in[0] == '\0') return clear(out, err);
    mfd_fmt_range(f, &lo, &hi);

    switch (f->kind) {
    case MFD_FMT_SPEED_MACH: {
        /* [PORT] Q2: reinterpret "84" as .84 only when the entry has no '.' (FBW: any entry). */
        n = num(in);
        if (n > MFD_MMO && !strchr(in, '.')) {
            char b[64];
            size_t len = strlen(in);
            if (len + 3 > sizeof b) return format_err(d, NULL, err);
            b[0] = '0';
            b[1] = '.';
            memcpy(b + 2, in, len + 1);
            n = num(b);
        }
        return finish(d, n, lo, hi, out, err);
    }
    case MFD_FMT_ALT_OR_FL:
    case MFD_FMT_TROPO:
        n = strlen(in) <= 3 ? num(in) * 100.0 : num(in); /* FL shorthand */
        return finish(d, n, lo, hi, out, err);

    case MFD_FMT_FLIGHT_LEVEL: {
        const char *digits;
        n = num(in);
        if (isnan(n) && match_fl(in, &digits)) n = num(digits);
        if (n > hi || n < lo) return range_err(d, lo, hi, err);
        if (isnan(n) || n != floor(n)) return format_err(d, NULL, err); /* [PORT] Q9: integer FL only */
        return ok_num(out, n, err);
    }
    case MFD_FMT_RADIO_ALT:
        if (!strcmp(in, "NO DH") || !strcmp(in, "NODH") || !strcmp(in, "NONE") || !strcmp(in, "NO"))
            return ok_num(out, 0.0, err); /* RADIO_ALTITUDE_NODH_VALUE */
        return finish(d, num(in), lo, hi, out, err);

    case MFD_FMT_LENGTH:
        n = num(in) * (f->metric ? 1.0 : MFD_M_PER_FT);
        if (!isnan(n) && n >= lo && n <= hi) return ok_num(out, n, err);
        if (n > hi || n < lo) return range_err(d, lo, hi, err);
        return format_err(d, length_unit(f->metric), err);

    case MFD_FMT_WEIGHT: {
        const char *unit = weight_unit(f->metric);
        double k = f->metric ? 1.0 : MFD_KG_PER_LB;
        char a[32], b[32];
        n = num(in) * k * 1000.0;
        if (isnan(n)) return format_err(d, unit, err);
        if (n <= hi && n >= lo) return ok_num(out, n, err);
        mfd_js_to_fixed(lo / k / 1000.0, 1, a, sizeof a);
        mfd_js_to_fixed(hi / k / 1000.0, 1, b, sizeof b);
        mfd_err_set_range(err, a, b, unit);
        return MFD_PARSE_ERROR;
    }
    case MFD_FMT_CRZ_TEMP:
        n = num(in);
        if (n > 0 && in[0] != '+') n = -n; /* unsigned entries are negative (ISA-cold) */
        return finish(d, n, lo, hi, out, err);

    case MFD_FMT_DESCENT_RATE:
        n = num(in);
        if (n > 0) n = -n;
        return finish(d, n, lo, hi, out, err);

    case MFD_FMT_TRIP_WIND: {
        double sign = 1.0;
        const char *rest = in;
        if (!strncmp(in, "HD", 2)) {
            sign = -1.0;
            rest = in + 2;
        } else if (in[0] == '-' || in[0] == 'H') {
            sign = -1.0;
            rest = in + 1;
        } else if (!strncmp(in, "TL", 2)) {
            rest = in + 2;
        } else if (in[0] == '+' || in[0] == 'T') {
            rest = in + 1;
        }
        return finish(d, sign * num(rest), lo, hi, out, err);
    }
    case MFD_FMT_QNH: {
        double p = mfd_js_parse_float(in);
        char a[16], b[16];
        if (isnan(p)) return format_err(d, NULL, err);
        if (match_qnh_inches(in)) { /* inches first (NNNN or NN.NN) */
            int has_dot = strchr(in, '.') != NULL;
            double inches = has_dot ? p : p / 100.0;
            if (inches <= 32.48 && inches >= 22.0) return ok_num(out, inches, err);
            if (has_dot) {
                mfd_js_to_fixed(22.0, 2, a, sizeof a);
                mfd_js_to_fixed(32.48, 2, b, sizeof b);
                mfd_err_set_range(err, a, b, NULL);
                return MFD_PARSE_ERROR;
            }
        }
        if (match_digits(in, 1, 4)) {
            if (p > 1100.0 || p < 745.0) {
                mfd_js_to_string(745.0, a, sizeof a);
                mfd_js_to_string(1100.0, b, sizeof b);
                mfd_err_set_range(err, a, b, NULL);
                return MFD_PARSE_ERROR;
            }
            return ok_num(out, p, err);
        }
        return format_err(d, NULL, err);
    }
    case MFD_FMT_TIME_HHMM: {
        char s[64];
        size_t len;
        double minutes, hours = 0.0;
        if (!remove_first_colon(in, s, sizeof s)) return format_err(d, NULL, err);
        len = strlen(s);
        minutes = num(len >= 2 ? s + len - 2 : s); /* slice(-2) */
        if (len > 2) hours = num_n(s, len - 2);
        if (minutes < 0 || minutes > 59 || hours < 0 || hours > 23) {
            mfd_err_set_plain(err, MFD_ERR_ENTRY_OUT_OF_RANGE);
            return MFD_PARSE_ERROR;
        }
        return finish(d, minutes + hours * 60.0, lo, hi, out, err);
    }
    case MFD_FMT_TIME_HHMMSS: {
        char s[64];
        size_t len;
        double h, m, sec = 0.0;
        if (!remove_first_colon(in, s, sizeof s)) return format_err(d, NULL, err);
        len = strlen(s);
        if (len < 4) return format_err(d, NULL, err); /* [PORT] Q5: FBW silently clears */
        h = num_n(s, 2);
        m = num_n(s + 2, 2);
        if (len > 4) sec = num(s + len - 2);
        if (sec < 0 || sec > 59 || m < 0 || m > 59 || h < 0 || h > 23) { /* [PORT] Q5: FBW clears */
            mfd_err_set_plain(err, MFD_ERR_ENTRY_OUT_OF_RANGE);
            return MFD_PARSE_ERROR;
        }
        return finish(d, sec + m * 60.0 + h * 3600.0, lo, hi, out, err);
    }
    case MFD_FMT_LS_COURSE: {
        const char *digits;
        if (!match_ls_course(in, &digits)) return format_err(d, d->fmt_unit, err);
        return finish(d, num(digits), lo, hi, out, err);
    }
    case MFD_FMT_SQUAWK:
        if (match_digits(in, 4, 4) && strspn(in, "01234567") == 4) return ok_num(out, num(in), err);
        return format_err(d, NULL, err);

    case MFD_FMT_FUEL_PENALTY:
        if (!match_fuel_penalty(in)) return format_err(d, d->fmt_unit, err);
        return finish(d, num(in), lo, hi, out, err);

    default: /* simple P1/P2/P3 formats */
        return finish(d, num(in), lo, hi, out, err);
    }
}

/* ---- Render ------------------------------------------------------------------------------------ */

static void fixed(double v, int digits, char *buf, size_t n) { mfd_js_to_fixed(v, digits, buf, n); }

static void fixed_pad(double v, int digits, size_t width, char *buf, size_t n)
{
    mfd_js_to_fixed(v, digits, buf, n);
    mfd_pad_start(buf, n, width, '0');
}

void mfd_fmt_render(const MfdFmt *f, const MfdValue *v, MfdFieldText *out)
{
    const FmtDef *d;
    char t[64];
    int has;
    double x;
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!kind_valid(f)) return;
    d = &fmt_defs[f->kind];

    if (mfd_fmt_is_text(f->kind)) {
        if (v && v->has && v->str[0]) {
            copy_str(out->value, sizeof out->value, v->str);
        } else {
            copy_str(out->value, sizeof out->value, mfd_fmt_placeholder(f));
            out->is_placeholder = 1;
        }
        return;
    }

    has = v && v->has;
    x = has ? v->num : 0.0;
    if (f->kind == MFD_FMT_SPEED_MACH && x == 0.0) has = 0; /* FBW `!value` */
    if (!has) {
        copy_str(out->value, sizeof out->value, mfd_fmt_placeholder(f));
        out->is_placeholder = 1;
        if (d->units_when_null) {
            copy_str(out->lead, sizeof out->lead, mfd_fmt_lead_unit(f));
            copy_str(out->trail, sizeof out->trail, mfd_fmt_trail_unit(f));
        }
        return;
    }
    copy_str(out->lead, sizeof out->lead, mfd_fmt_lead_unit(f));
    copy_str(out->trail, sizeof out->trail, mfd_fmt_trail_unit(f));

    switch (f->kind) {
    case MFD_FMT_SPEED_KT:
    case MFD_FMT_COST_INDEX:
    case MFD_FMT_VERTICAL_SPEED:
    case MFD_FMT_DESCENT_RATE: mfd_js_to_string(x, t, sizeof t); break;

    case MFD_FMT_SPEED_MACH: {
        char *dot;
        fixed(x, 2, t, sizeof t);
        dot = strchr(t, '.');
        if (dot)
            memmove(t, dot, strlen(dot) + 1); /* "." + toFixed(2).split('.')[1] */
        else
            copy_str(t, sizeof t, ".");
        break;
    }
    case MFD_FMT_ALT_OR_FL: {
        double tr = f->trans_is_fl ? f->trans * 100.0 : f->trans;
        /* [PORT] Q10: the transition applies immediately. */
        if (f->has_trans && ((!f->trans_is_fl && x > tr) || (f->trans_is_fl && x >= tr))) {
            fixed_pad(x / 100.0, 0, 3, t, sizeof t);
            copy_str(out->lead, sizeof out->lead, "FL");
            out->trail[0] = '\0';
        } else {
            fixed(x, 0, t, sizeof t);
        }
        break;
    }
    case MFD_FMT_RADIO_ALT:
        if (x == 0.0) {
            copy_str(t, sizeof t, "NO DH");
            out->trail[0] = '\0';
        } else {
            fixed(x, 0, t, sizeof t);
        }
        break;

    case MFD_FMT_FLIGHT_LEVEL: fixed_pad(mfd_js_round(x), 0, 3, t, sizeof t); break;
    case MFD_FMT_LENGTH: fixed(f->metric ? x : x / MFD_M_PER_FT, 0, t, sizeof t); break;
    case MFD_FMT_WEIGHT: fixed((f->metric ? x : x / MFD_KG_PER_LB) / 1000.0, 1, t, sizeof t); break;

    case MFD_FMT_TEMPERATURE:
    case MFD_FMT_CRZ_TEMP: {
        char b[48];
        fixed(x, 0, b, sizeof b);
        copy_str(t, sizeof t, x >= 0 ? "+" : "");
        append_str(t, sizeof t, b);
        break;
    }
    case MFD_FMT_WIND_DIR:
    case MFD_FMT_WIND_SPEED:
    case MFD_FMT_RADIAL:
    case MFD_FMT_INBOUND_COURSE: /* [PORT] padded like RadialFormat (spec 02 §4.1) */
        fixed_pad(x, 0, 3, t, sizeof t);
        break;

    case MFD_FMT_TRIP_WIND:
        fixed_pad(fabs(x), 0, 3, t, sizeof t);
        copy_str(out->lead, sizeof out->lead, x >= 0 ? "TL" : "HD");
        break;

    case MFD_FMT_QNH: fixed(x, x < 745.0 ? 2 : 0, t, sizeof t); break;

    case MFD_FMT_TIME_HHMM: { /* [PORT] Q6: 0 renders 00:00 */
        char h[24], m[24];
        fixed_pad(fabs(floor(x / 60.0)), 0, 2, h, sizeof h);
        fixed_pad(fabs(fmod(x, 60.0)), 0, 2, m, sizeof m);
        snprintf(t, sizeof t, "%s:%s", h, m);
        break;
    }
    case MFD_FMT_TIME_HHMMSS: { /* [PORT] Q6: 0 renders 00:00:00 */
        char h[20], m[20], s[20]; /* 3 x 19 + 2 separators fit t[64] (MinGW -Wformat-truncation) */
        fixed_pad(fabs(floor(x / 3600.0)), 0, 2, h, sizeof h);
        fixed_pad(fabs(fmod(floor(x / 60.0), 60.0)), 0, 2, m, sizeof m);
        fixed_pad(fabs(fmod(x, 60.0)), 0, 2, s, sizeof s);
        snprintf(t, sizeof t, "%s:%s:%s", h, m, s);
        break;
    }
    case MFD_FMT_LS_COURSE: {
        char b[48];
        fixed_pad(fabs(x), 0, 3, b, sizeof b);
        copy_str(t, sizeof t, x < 0 ? "B" : "F");
        append_str(t, sizeof t, b);
        break;
    }
    case MFD_FMT_SQUAWK: fixed_pad(x, 0, 4, t, sizeof t); break;
    case MFD_FMT_RNP: fixed(x, x > 10.0 ? 1 : 2, t, sizeof t); break;

    case MFD_FMT_FUEL_PENALTY: {
        char b[48];
        fixed_pad(x, 1, 5, b, sizeof b);
        copy_str(t, sizeof t, "+");
        append_str(t, sizeof t, b);
        break;
    }
    case MFD_FMT_PERCENT:
    case MFD_FMT_HEADING:
    case MFD_FMT_HOLD_DIST:
    case MFD_FMT_HOLD_TIME:
    case MFD_FMT_FREQ_ADF: fixed(x, 1, t, sizeof t); break;

    case MFD_FMT_FREQ_ILS:
    case MFD_FMT_FREQ_VOR: fixed(x, 2, t, sizeof t); break;

    default: /* ALTITUDE, TROPO, PAX_NBR, LATITUDE, RADIUS */
        fixed(x, 0, t, sizeof t);
        break;
    }
    copy_str(out->value, sizeof out->value, t);
}

/* ---- Per-format wrappers (spec 02 §11.3) ----------------------------------------------------------- */

static MfdParseStatus parse_num_wrapper(const MfdFmt *f, const char *in, double *out, MfdFmsError *err)
{
    MfdValue v;
    MfdParseStatus st = mfd_fmt_parse(f, in, &v, err);
    if (st == MFD_PARSE_OK && out) *out = v.num;
    return st;
}

static void render_num_wrapper(const MfdFmt *f, int has_value, double x, MfdFieldText *out)
{
    MfdValue v;
    memset(&v, 0, sizeof v);
    v.has = has_value;
    v.num = x;
    mfd_fmt_render(f, &v, out);
}

#define MFD_FMT_DEF_RENDER(name, KIND)                                                                \
    void mfd_fmt_render_##name(int has_value, double v, MfdFieldText *out)                            \
    {                                                                                                 \
        MfdFmt f;                                                                                     \
        mfd_fmt_init(&f, KIND);                                                                       \
        render_num_wrapper(&f, has_value, v, out);                                                    \
    }
#define MFD_FMT_DEF_DYN(name, KIND)                                                                   \
    MfdParseStatus mfd_fmt_parse_##name(const char *in, double min, double max, double *out,          \
                                        MfdFmsError *err)                                             \
    {                                                                                                 \
        MfdFmt f;                                                                                     \
        mfd_fmt_init(&f, KIND);                                                                       \
        f.min = min;                                                                                  \
        f.max = max;                                                                                  \
        return parse_num_wrapper(&f, in, out, err);                                                   \
    }                                                                                                 \
    MFD_FMT_DEF_RENDER(name, KIND)
#define MFD_FMT_DEF_FIXED(name, KIND)                                                                 \
    MfdParseStatus mfd_fmt_parse_##name(const char *in, double *out, MfdFmsError *err)                \
    {                                                                                                 \
        MfdFmt f;                                                                                     \
        mfd_fmt_init(&f, KIND);                                                                       \
        return parse_num_wrapper(&f, in, out, err);                                                   \
    }                                                                                                 \
    MFD_FMT_DEF_RENDER(name, KIND)

MFD_FMT_DEF_DYN(speed_kt, MFD_FMT_SPEED_KT)
MFD_FMT_DEF_DYN(speed_mach, MFD_FMT_SPEED_MACH)
MFD_FMT_DEF_DYN(altitude, MFD_FMT_ALTITUDE)
MFD_FMT_DEF_DYN(flight_level, MFD_FMT_FLIGHT_LEVEL)
MFD_FMT_DEF_DYN(radio_alt, MFD_FMT_RADIO_ALT)
MFD_FMT_DEF_DYN(percent, MFD_FMT_PERCENT)
MFD_FMT_DEF_DYN(temperature, MFD_FMT_TEMPERATURE)
MFD_FMT_DEF_DYN(vertical_speed, MFD_FMT_VERTICAL_SPEED)
MFD_FMT_DEF_DYN(descent_rate, MFD_FMT_DESCENT_RATE)
MFD_FMT_DEF_DYN(heading, MFD_FMT_HEADING)
MFD_FMT_DEF_DYN(inbound_course, MFD_FMT_INBOUND_COURSE)
MFD_FMT_DEF_DYN(hold_dist, MFD_FMT_HOLD_DIST)
MFD_FMT_DEF_DYN(hold_time, MFD_FMT_HOLD_TIME)
MFD_FMT_DEF_FIXED(tropo, MFD_FMT_TROPO)
MFD_FMT_DEF_FIXED(crz_temp, MFD_FMT_CRZ_TEMP)
MFD_FMT_DEF_FIXED(wind_dir, MFD_FMT_WIND_DIR)
MFD_FMT_DEF_FIXED(wind_speed, MFD_FMT_WIND_SPEED)
MFD_FMT_DEF_FIXED(trip_wind, MFD_FMT_TRIP_WIND)
MFD_FMT_DEF_FIXED(cost_index, MFD_FMT_COST_INDEX)
MFD_FMT_DEF_FIXED(pax_nbr, MFD_FMT_PAX_NBR)
MFD_FMT_DEF_FIXED(time_hhmm, MFD_FMT_TIME_HHMM)
MFD_FMT_DEF_FIXED(time_hhmmss, MFD_FMT_TIME_HHMMSS)
MFD_FMT_DEF_FIXED(latitude, MFD_FMT_LATITUDE)
MFD_FMT_DEF_FIXED(freq_ils, MFD_FMT_FREQ_ILS)
MFD_FMT_DEF_FIXED(freq_vor, MFD_FMT_FREQ_VOR)
MFD_FMT_DEF_FIXED(freq_adf, MFD_FMT_FREQ_ADF)
MFD_FMT_DEF_FIXED(ls_course, MFD_FMT_LS_COURSE)
MFD_FMT_DEF_FIXED(squawk, MFD_FMT_SQUAWK)
MFD_FMT_DEF_FIXED(radial, MFD_FMT_RADIAL)
MFD_FMT_DEF_FIXED(radius, MFD_FMT_RADIUS)
MFD_FMT_DEF_FIXED(rnp, MFD_FMT_RNP)
MFD_FMT_DEF_FIXED(fuel_penalty, MFD_FMT_FUEL_PENALTY)

MfdParseStatus mfd_fmt_parse_alt_or_fl(const char *in, double min_ft, double max_ft, double *out,
                                       MfdFmsError *err)
{
    MfdFmt f;
    mfd_fmt_init(&f, MFD_FMT_ALT_OR_FL);
    f.min = min_ft;
    f.max = max_ft;
    return parse_num_wrapper(&f, in, out, err);
}

void mfd_fmt_render_alt_or_fl(int has_value, double v_ft, int has_trans, double trans_ft_or_fl, int trans_is_fl,
                              MfdFieldText *out)
{
    MfdFmt f;
    mfd_fmt_init(&f, MFD_FMT_ALT_OR_FL);
    f.has_trans = has_trans;
    f.trans = trans_ft_or_fl;
    f.trans_is_fl = trans_is_fl;
    render_num_wrapper(&f, has_value, v_ft, out);
}

MfdParseStatus mfd_fmt_parse_length(const char *in, double min_m, double max_m, int metric, double *out_m,
                                    MfdFmsError *err)
{
    MfdFmt f;
    mfd_fmt_init(&f, MFD_FMT_LENGTH);
    f.min = min_m;
    f.max = max_m;
    f.metric = metric;
    return parse_num_wrapper(&f, in, out_m, err);
}

void mfd_fmt_render_length(int has_value, double v_m, int metric, int max_digits, MfdFieldText *out)
{
    MfdFmt f;
    mfd_fmt_init(&f, MFD_FMT_LENGTH);
    f.metric = metric;
    f.max_digits = max_digits;
    render_num_wrapper(&f, has_value, v_m, out);
}

MfdParseStatus mfd_fmt_parse_weight(const char *in, double min_kg, double max_kg, int metric, double *out_kg,
                                    MfdFmsError *err)
{
    MfdFmt f;
    mfd_fmt_init(&f, MFD_FMT_WEIGHT);
    f.min = min_kg;
    f.max = max_kg;
    f.metric = metric;
    return parse_num_wrapper(&f, in, out_kg, err);
}

void mfd_fmt_render_weight(int has_value, double v_kg, int metric, MfdFieldText *out)
{
    MfdFmt f;
    mfd_fmt_init(&f, MFD_FMT_WEIGHT);
    f.metric = metric;
    render_num_wrapper(&f, has_value, v_kg, out);
}

MfdParseStatus mfd_fmt_parse_qnh(const char *in, double *out, MfdFmsError *err)
{
    MfdFmt f;
    mfd_fmt_init(&f, MFD_FMT_QNH);
    return parse_num_wrapper(&f, in, out, err);
}

void mfd_fmt_render_qnh(int has_value, double v, int is_hpa, MfdFieldText *out)
{
    MfdFmt f;
    mfd_fmt_init(&f, MFD_FMT_QNH);
    f.qnh_is_hpa = is_hpa;
    render_num_wrapper(&f, has_value, v, out);
}

MfdParseStatus mfd_fmt_parse_text(MfdFmtKind kind, const char *in, int max_digits, const char *placeholder,
                                  char *out, size_t n, MfdFmsError *err)
{
    MfdFmt f;
    MfdValue v;
    MfdParseStatus st;
    if (!mfd_fmt_is_text(kind)) {
        mfd_err_set_plain(err, MFD_ERR_FORMAT_ERROR);
        return MFD_PARSE_ERROR;
    }
    mfd_fmt_init(&f, kind);
    f.max_digits = max_digits;
    f.placeholder = placeholder;
    st = mfd_fmt_parse(&f, in, &v, err);
    if (st == MFD_PARSE_OK) copy_str(out, n, v.str);
    if (st == MFD_PARSE_CLEAR) copy_str(out, n, "");
    return st;
}

void mfd_fmt_render_text(const char *value_or_null, const char *placeholder, MfdFieldText *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (value_or_null && value_or_null[0]) {
        copy_str(out->value, sizeof out->value, value_or_null);
    } else {
        copy_str(out->value, sizeof out->value, placeholder ? placeholder : "");
        out->is_placeholder = 1;
    }
}
