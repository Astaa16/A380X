/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unit tests for the MFD data-entry formats and the FMS message queue (WP-B):
 * every test vector of docs/mfd-spec/02-data-formats-messages.md §4 and the checks of §11.6.
 *
 * Build variants (all must pass):
 *   default                         [PORT] strict number grammar, FBW queue stacking
 *   -DMFD_FMT_FAITHFUL_JS_NUMBER    formats use faithful JS Number() (spec 02 §2.1)
 *   -DMFD_MSG_DEDUP_UNCLEARED       identical uncleared messages are replaced (spec 07 §11-4)
 */
#include "a380x_fms_messages.h"
#include "a380x_mfd_formats.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEG "\xC2\xB0"

static int g_checks, g_fail;

static void check_impl(int ok, const char *what, int line)
{
    g_checks++;
    if (!ok) {
        g_fail++;
        fprintf(stderr, "test_mfd_formats.c:%d: FAIL %s\n", line, what);
    }
}
#define CHECK(c) check_impl((c) != 0, #c, __LINE__)

static void check_str_impl(const char *got, const char *want, const char *what, int line)
{
    g_checks++;
    if (strcmp(got ? got : "(null)", want) != 0) {
        g_fail++;
        fprintf(stderr, "test_mfd_formats.c:%d: FAIL %s: got \"%s\", want \"%s\"\n", line, what,
                got ? got : "(null)", want);
    }
}
#define CHECK_STR(got, want) check_str_impl((got), (want), #got, __LINE__)

static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

static MfdFmt F(MfdFmtKind k)
{
    MfdFmt f;
    mfd_fmt_init(&f, k);
    return f;
}

static MfdFmt FR(MfdFmtKind k, double lo, double hi)
{
    MfdFmt f = F(k);
    f.min = lo;
    f.max = hi;
    return f;
}

static const char *status_name(MfdParseStatus s)
{
    return s == MFD_PARSE_OK ? "OK" : s == MFD_PARSE_CLEAR ? "CLEAR" : "ERROR";
}

/* Message text the standard field handler would show for an error. */
static void error_message(const MfdFmsError *e, int with_details, char *buf, size_t n)
{
    MfdMsgQueue q;
    const MfdMsgEntry *m;
    mfd_msg_init(&q);
    mfd_msg_show_error(&q, e, with_details);
    m = mfd_msg_footer_entry(&q);
    snprintf(buf, n, "%s", m ? m->text : "");
}

/* Parse must succeed with value `want`; when `val` is non-NULL the rendered text and units are checked. */
static void expect_ok(int line, const MfdFmt *f, const char *in, double want, const char *val, const char *lead,
                      const char *trail)
{
    MfdValue v;
    MfdFmsError e;
    char what[160];
    MfdParseStatus st;
    memset(&v, 0, sizeof v);
    st = mfd_fmt_parse(f, in, &v, &e);
    snprintf(what, sizeof what, "parse(kind %d, \"%s\") status %s", (int)f->kind, in, status_name(st));
    check_impl(st == MFD_PARSE_OK && v.has && e.type == MFD_ERR_NONE, what, line);
    if (st != MFD_PARSE_OK) {
        char msg[160];
        error_message(&e, 1, msg, sizeof msg);
        fprintf(stderr, "    message: \"%s\"\n", msg);
        return;
    }
    snprintf(what, sizeof what, "parse(kind %d, \"%s\") = %.10g, want %.10g", (int)f->kind, in, v.num, want);
    check_impl(near(v.num, want, 1e-9 * (fabs(want) > 1 ? fabs(want) : 1)) && (signbit(v.num) == signbit(want)),
               what, line);
    if (val) {
        MfdFieldText t;
        mfd_fmt_render(f, &v, &t);
        snprintf(what, sizeof what, "render(kind %d, \"%s\") value", (int)f->kind, in);
        check_str_impl(t.value, val, what, line);
        snprintf(what, sizeof what, "render(kind %d, \"%s\") lead", (int)f->kind, in);
        check_str_impl(t.lead, lead, what, line);
        snprintf(what, sizeof what, "render(kind %d, \"%s\") trail", (int)f->kind, in);
        check_str_impl(t.trail, trail, what, line);
        check_impl(!t.is_placeholder, "rendered value is not a placeholder", line);
    }
}
#define EXPECT_OK(f, in, want, val, lead, trail) expect_ok(__LINE__, &(f), (in), (want), (val), (lead), (trail))
#define EXPECT_VAL(f, in, want) expect_ok(__LINE__, &(f), (in), (want), NULL, NULL, NULL)

/* Parse must fail; `msg` is the message-area text the field shows (f->err_details decides the details). */
static void expect_err(int line, const MfdFmt *f, const char *in, const char *msg)
{
    MfdValue v;
    MfdFmsError e;
    char what[160], got[160];
    MfdParseStatus st;
    memset(&v, 0, sizeof v);
    v.num = 12345.0;
    st = mfd_fmt_parse(f, in, &v, &e);
    snprintf(what, sizeof what, "parse(kind %d, \"%s\") status %s (want ERROR, value %.10g)", (int)f->kind, in,
             status_name(st), v.num);
    check_impl(st == MFD_PARSE_ERROR, what, line);
    check_impl(v.num == 12345.0 && !v.has, "value untouched on error", line);
    if (st != MFD_PARSE_ERROR) return;
    error_message(&e, f->err_details, got, sizeof got);
    snprintf(what, sizeof what, "message for (kind %d, \"%s\")", (int)f->kind, in);
    check_str_impl(got, msg, what, line);
}
#define EXPECT_ERR(f, in, msg) expect_err(__LINE__, &(f), (in), (msg))

static void expect_clear(int line, const MfdFmt *f, const char *in)
{
    MfdValue v;
    MfdFmsError e;
    char what[120];
    MfdParseStatus st = mfd_fmt_parse(f, in, &v, &e);
    snprintf(what, sizeof what, "parse(kind %d, \"%s\") status %s (want CLEAR)", (int)f->kind, in,
             status_name(st));
    check_impl(st == MFD_PARSE_CLEAR && !v.has, what, line);
}
#define EXPECT_CLEAR(f, in) expect_clear(__LINE__, &(f), (in))

static void expect_txt(int line, const MfdFmt *f, const char *in, const char *want)
{
    MfdValue v;
    MfdFmsError e;
    char what[120];
    MfdParseStatus st = mfd_fmt_parse(f, in, &v, &e);
    snprintf(what, sizeof what, "text parse(kind %d, \"%s\") status %s", (int)f->kind, in, status_name(st));
    check_impl(st == MFD_PARSE_OK && v.has, what, line);
    if (st == MFD_PARSE_OK) check_str_impl(v.str, want, what, line);
}
#define EXPECT_TXT(f, in, want) expect_txt(__LINE__, &(f), (in), (want))

static void expect_render(int line, const MfdFmt *f, int has, double x, const char *val, const char *lead,
                          const char *trail, int is_ph)
{
    MfdValue v;
    MfdFieldText t;
    char what[120];
    memset(&v, 0, sizeof v);
    v.has = has;
    v.num = x;
    mfd_fmt_render(f, &v, &t);
    snprintf(what, sizeof what, "render(kind %d, has %d, %.10g) value", (int)f->kind, has, x);
    check_str_impl(t.value, val, what, line);
    snprintf(what, sizeof what, "render(kind %d, has %d, %.10g) lead", (int)f->kind, has, x);
    check_str_impl(t.lead, lead, what, line);
    snprintf(what, sizeof what, "render(kind %d, has %d, %.10g) trail", (int)f->kind, has, x);
    check_str_impl(t.trail, trail, what, line);
    snprintf(what, sizeof what, "render(kind %d, has %d, %.10g) is_placeholder", (int)f->kind, has, x);
    check_impl(t.is_placeholder == is_ph, what, line);
}
#define EXPECT_RENDER(f, has, x, val, lead, trail, ph)                                                       \
    expect_render(__LINE__, &(f), (has), (x), (val), (lead), (trail), (ph))

/* ---- JS emulation (spec 02 §2.1, §2.2, §11.6) --------------------------------------------------- */

static void to_fixed_is(int line, double v, int d, const char *want)
{
    char b[64], what[80];
    mfd_js_to_fixed(v, d, b, sizeof b);
    snprintf(what, sizeof what, "toFixed(%.17g, %d)", v, d);
    check_str_impl(b, want, what, line);
}
#define TO_FIXED(v, d, want) to_fixed_is(__LINE__, (v), (d), (want))

static void to_string_is(int line, double v, const char *want)
{
    char b[64], what[80];
    mfd_js_to_string(v, b, sizeof b);
    snprintf(what, sizeof what, "toString(%.17g)", v);
    check_str_impl(b, want, what, line);
}
#define TO_STRING(v, want) to_string_is(__LINE__, (v), (want))

static void number_is(int line, int strict, const char *s, int ok, double want)
{
    double v = 0;
    char what[96];
    int r = strict ? mfd_strict_number(s, &v) : mfd_js_number(s, &v);
    snprintf(what, sizeof what, "%s(\"%s\") = %d / %.17g", strict ? "strict" : "Number", s, r, v);
    if (ok)
        check_impl(r == 1 && (v == want || (isinf(want) && v == want)), what, line);
    else
        check_impl(r == 0 && isnan(v), what, line);
}
#define JS_NUM(s, want) number_is(__LINE__, 0, (s), 1, (want))
#define JS_NAN(s) number_is(__LINE__, 0, (s), 0, 0)
#define STRICT_NUM(s, want) number_is(__LINE__, 1, (s), 1, (want))
#define STRICT_NAN(s) number_is(__LINE__, 1, (s), 0, 0)

static void test_js_emulation(void)
{
    char b[16];

    /* §11.6 toFixed */
    TO_FIXED(0.5, 0, "1");
    TO_FIXED(2.5, 0, "3");
    TO_FIXED(-2.5, 0, "-3");
    TO_FIXED(300.25, 1, "300.3");
    TO_FIXED(1.005, 2, "1.00");
    TO_FIXED(289.982, 1, "290.0");
    /* more JS toFixed semantics (§2.2) */
    TO_FIXED(-0.0, 1, "0.0");
    TO_FIXED(-0.04, 1, "-0.0");
    TO_FIXED(0.125, 2, "0.13");
    TO_FIXED(1.25, 1, "1.3");
    TO_FIXED(-1.25, 1, "-1.3");
    TO_FIXED(1.45, 1, "1.4");
    TO_FIXED(1.35, 1, "1.4");
    TO_FIXED(9.5, 0, "10");
    TO_FIXED(99.5, 0, "100");
    TO_FIXED(-1.5, 0, "-2");
    TO_FIXED(0.0, 2, "0.00");
    TO_FIXED(123.456, 0, "123");
    TO_FIXED(1234.5678, 2, "1234.57");
    TO_FIXED(0.000001, 7, "0.0000010");
    TO_FIXED(5e-324, 2, "0.00");
    TO_FIXED(NAN, 2, "NaN");
    TO_FIXED(INFINITY, 2, "Infinity");
    TO_FIXED(-INFINITY, 0, "-Infinity");
    TO_FIXED(1e21, 2, "1e+21");
    TO_FIXED(4503599627370495.5, 0, "4503599627370496");

    /* toString */
    TO_STRING(250, "250");
    TO_STRING(0.84, "0.84");
    TO_STRING(-999, "-999");
    TO_STRING(0.89, "0.89");
    TO_STRING(0.1, "0.1");
    TO_STRING(100, "100");
    TO_STRING(0.0, "0");
    TO_STRING(-0.0, "0");
    TO_STRING(INFINITY, "Infinity");
    TO_STRING(-INFINITY, "-Infinity");
    TO_STRING(NAN, "NaN");
    TO_STRING(1e21, "1e+21");
    TO_STRING(1.23e22, "1.23e+22");
    TO_STRING(123456789012345680000.0, "123456789012345680000");
    TO_STRING(1e-7, "1e-7");
    TO_STRING(1.5e-7, "1.5e-7");
    TO_STRING(0.000001, "0.000001");
    TO_STRING(0.1 + 0.2, "0.30000000000000004");
    TO_STRING(1.0 / 3.0, "0.3333333333333333");
    TO_STRING(5e-324, "5e-324");
    TO_STRING(1.7976931348623157e308, "1.7976931348623157e+308");
    TO_STRING(-12.5, "-12.5");

    /* §11.6 Number() */
    JS_NUM(" 20", 20);
    JS_NUM("1E2", 100);
    JS_NUM("0X1F", 31);
    JS_NUM("1.", 1);
    JS_NUM(".5", 0.5);
    JS_NAN(".");
    JS_NAN("-");
    JS_NAN("2 0");
    JS_NAN("-0x1");
    /* more Number() (§2.1) */
    JS_NUM("", 0);
    JS_NUM("   ", 0);
    JS_NUM("+5", 5);
    JS_NUM("007", 7);
    JS_NUM(" \t12.5\n", 12.5);
    JS_NUM("\xC2\xA0" "7" "\xE2\x80\xA8", 7); /* NBSP, LINE SEPARATOR */
    JS_NUM("1.5e+2", 150);
    JS_NUM("-.5", -0.5);
    JS_NUM("0b101", 5);
    JS_NUM("0o17", 15);
    JS_NAN("0x");
    JS_NAN("0b");
    JS_NAN("0b2");
    JS_NAN("1e");
    JS_NAN("e5");
    JS_NAN("1_0");
    JS_NAN("12px");
    JS_NAN("infinity");
    JS_NAN("0x1G");
    JS_NUM("Infinity", INFINITY);
    JS_NUM("+Infinity", INFINITY);
    JS_NUM("-Infinity", -INFINITY);

    /* [PORT] strict grammar */
    STRICT_NUM(" 20 ", 20);
    STRICT_NUM("1.", 1);
    STRICT_NUM(".5", 0.5);
    STRICT_NUM("+5", 5);
    STRICT_NUM("-0.5", -0.5);
    STRICT_NUM("007", 7);
    STRICT_NAN("1E2");
    STRICT_NAN("0X1F");
    STRICT_NAN("Infinity");
    STRICT_NAN("");
    STRICT_NAN("  ");
    STRICT_NAN(".");
    STRICT_NAN("-");
    STRICT_NAN("2 0");
    STRICT_NAN("1.2.3");

    /* parseFloat */
    CHECK(mfd_js_parse_float("29.92X") == 29.92);
    CHECK(mfd_js_parse_float("0x1F") == 0.0);
    CHECK(mfd_js_parse_float("  -1.5e3abc") == -1500.0);
    CHECK(mfd_js_parse_float("1e") == 1.0);
    CHECK(mfd_js_parse_float(".5.") == 0.5);
    CHECK(mfd_js_parse_float("-.5") == -0.5);
    CHECK(mfd_js_parse_float("Infinityx") == INFINITY);
    CHECK(isnan(mfd_js_parse_float("abc")));
    CHECK(isnan(mfd_js_parse_float("")));
    CHECK(isnan(mfd_js_parse_float("+")));
    CHECK(isnan(mfd_js_parse_float(NULL)));

    /* padStart */
    snprintf(b, sizeof b, "-5");
    mfd_pad_start(b, sizeof b, 3, '0');
    CHECK_STR(b, "0-5");
    snprintf(b, sizeof b, "123");
    mfd_pad_start(b, sizeof b, 2, '0');
    CHECK_STR(b, "123");
    snprintf(b, sizeof b, "7");
    mfd_pad_start(b, sizeof b, 3, '0');
    CHECK_STR(b, "007");
    {
        char small[4] = "7";
        mfd_pad_start(small, sizeof small, 6, '0');
        CHECK_STR(small, "007");
    }

    /* Math.round */
    CHECK(mfd_js_round(2.5) == 3.0);
    CHECK(mfd_js_round(-2.5) == -2.0);
    CHECK(mfd_js_round(0.49999999999999994) == 0.0);
    CHECK(mfd_js_round(1.4999) == 1.0);
    CHECK(mfd_js_round(350.6) == 351.0);
    CHECK(mfd_js_round(-0.5) == 0.0);
    CHECK(isnan(mfd_js_round(NAN)));

    /* String.replace (first occurrence) */
    {
        char r[64] = "NEW CRZ ALT: HHHHH";
        CHECK(mfd_str_replace_first(r, sizeof r, "HHHHH", "35000"));
        CHECK_STR(r, "NEW CRZ ALT: 35000");
        snprintf(r, sizeof r, "a-b-c");
        CHECK(mfd_str_replace_first(r, sizeof r, "-", "+"));
        CHECK_STR(r, "a+b-c");
        CHECK(!mfd_str_replace_first(r, sizeof r, "X", "Y"));
        CHECK(!mfd_str_replace_first(r, sizeof r, "", "Y"));
        CHECK_STR(r, "a+b-c");
    }

    /* Error detail strings (§2.3, §11.2) */
    {
        MfdFmsError e;
        mfd_err_set_format(&e, "XXX", "KT");
        CHECK(e.type == MFD_ERR_FORMAT_ERROR);
        CHECK_STR(e.details, "FORMAT: XXX KT");
        mfd_err_set_format(&e, "XX", NULL);
        CHECK_STR(e.details, "FORMAT: XX");
        mfd_err_set_format(&e, "XX", "");
        CHECK_STR(e.details, "FORMAT: XX");
        mfd_err_set_range(&e, "0", "359", DEG);
        CHECK(e.type == MFD_ERR_ENTRY_OUT_OF_RANGE);
        CHECK_STR(e.details, "RNG: 0 TO 359 " DEG);
        mfd_err_set_plain(&e, MFD_ERR_NOT_IN_DATABASE);
        CHECK(e.type == MFD_ERR_NOT_IN_DATABASE && e.details[0] == '\0');
    }
}

/* ---- §4.1 simple numeric formats ------------------------------------------------------------------ */

static void test_simple_numeric(void)
{
    MfdFmt spd = FR(MFD_FMT_SPEED_KT, 90, 340);
    MfdFmt alt = FR(MFD_FMT_ALTITUDE, 1, 43000);
    MfdFmt zfwcg = FR(MFD_FMT_PERCENT, 23, 48);
    MfdFmt flex = FR(MFD_FMT_TEMPERATURE, 0, 99);
    MfdFmt oat = FR(MFD_FMT_TEMPERATURE, -99, 99);
    MfdFmt wdir = F(MFD_FMT_WIND_DIR);
    MfdFmt wspd = F(MFD_FMT_WIND_SPEED);
    MfdFmt ci = F(MFD_FMT_COST_INDEX);
    MfdFmt hdg = F(MFD_FMT_HEADING);
    MfdFmt hdg_fbw = FR(MFD_FMT_HEADING, 0, INFINITY);
    MfdFmt hold_t = F(MFD_FMT_HOLD_TIME);
    MfdFmt hold_d = F(MFD_FMT_HOLD_DIST);
    MfdFmt crs = F(MFD_FMT_INBOUND_COURSE);
    MfdFmt crs_fbw = FR(MFD_FMT_INBOUND_COURSE, 0, INFINITY);
    MfdFmt ils = F(MFD_FMT_FREQ_ILS);
    MfdFmt vor = F(MFD_FMT_FREQ_VOR);
    MfdFmt adf = F(MFD_FMT_FREQ_ADF);
    MfdFmt radial = F(MFD_FMT_RADIAL);
    MfdFmt radius = F(MFD_FMT_RADIUS);
    MfdFmt rnp = F(MFD_FMT_RNP);
    MfdFmt pax = F(MFD_FMT_PAX_NBR);
    MfdFmt vs = F(MFD_FMT_VERTICAL_SPEED);
    MfdFmt lat = F(MFD_FMT_LATITUDE);
    MfdFmt spd_def = F(MFD_FMT_SPEED_KT);

    EXPECT_OK(spd, "140", 140, "140", "", "KT");
    EXPECT_ERR(spd, "80", "ENTRY OUT OF RANGE\nRNG: 90 TO 340 KT");
    EXPECT_ERR(spd, "AB", "FORMAT ERROR\nFORMAT: XXX KT");
#ifdef MFD_FMT_FAITHFUL_JS_NUMBER
    EXPECT_VAL(spd, "1E2", 100); /* [FBW] */
    EXPECT_VAL(spd, "0x64", 100);
#else
    EXPECT_ERR(spd, "1E2", "FORMAT ERROR\nFORMAT: XXX KT"); /* [PORT strict] */
    EXPECT_ERR(spd, "0x64", "FORMAT ERROR\nFORMAT: XXX KT");
#endif
    EXPECT_CLEAR(spd, "");
    EXPECT_ERR(spd_def, "-1", "ENTRY OUT OF RANGE\nRNG: 0 TO Infinity KT"); /* FBW ctor defaults */
    EXPECT_OK(spd_def, "140.5", 140.5, "140.5", "", "KT");

    EXPECT_OK(alt, "5000", 5000, "5000", "", "FT");
    EXPECT_OK(alt, "50", 50, "50", "", "FT"); /* no x100 in this format */
    EXPECT_ERR(alt, "45000", "ENTRY OUT OF RANGE");
    EXPECT_ERR(alt, "AB", "FORMAT ERROR\nFORMAT: XXXXX FT");

    EXPECT_OK(zfwcg, "30", 30, "30.0", "", "%");
    EXPECT_ERR(zfwcg, "50", "ENTRY OUT OF RANGE\nRNG: 23.0 TO 48.0 %");
    EXPECT_ERR(zfwcg, "X", "FORMAT ERROR\nFORMAT: XX.X %");

    EXPECT_OK(flex, "50", 50, "+50", "", DEG "C");
    EXPECT_ERR(flex, "-5", "ENTRY OUT OF RANGE\nRNG: 0 TO 99 " DEG "C");
    EXPECT_ERR(flex, "A", "FORMAT ERROR\nFORMAT: +/-XX " DEG "C");
    EXPECT_OK(oat, "-15", -15, "-15", "", DEG "C");
    EXPECT_OK(oat, "0", 0, "+0", "", DEG "C");

    EXPECT_OK(wdir, "45", 45, "045", "", DEG);
    EXPECT_ERR(wdir, "360", "ENTRY OUT OF RANGE\nRNG: 0 TO 359 " DEG);
    EXPECT_ERR(wdir, "A", "FORMAT ERROR\nFORMAT: XXX " DEG);
    EXPECT_OK(wspd, "5", 5, "005", "", "KT");
    EXPECT_ERR(wspd, "300", "ENTRY OUT OF RANGE\nRNG: 0 TO 250 KT");

    EXPECT_OK(ci, "50", 50, "50", "", "");
    EXPECT_ERR(ci, "-1", "ENTRY OUT OF RANGE\nRNG: 0 TO 999");
    EXPECT_ERR(ci, "A", "FORMAT ERROR\nFORMAT: XXX");
    EXPECT_ERR(ci, "1000", "ENTRY OUT OF RANGE\nRNG: 0 TO 999");

    EXPECT_OK(hdg, "123.4", 123.4, "123.4", "", DEG "T");
    EXPECT_ERR(hdg_fbw, "-5", "ENTRY OUT OF RANGE\nRNG: 00.0 TO Infinity " DEG); /* [FBW] defaults */
    EXPECT_VAL(hdg_fbw, "400", 400);                                            /* [FBW] accepted */
    EXPECT_ERR(hdg, "-5", "ENTRY OUT OF RANGE\nRNG: 00.0 TO 359.9 " DEG);       /* [PORT] Q3 */
    EXPECT_ERR(hdg, "400", "ENTRY OUT OF RANGE\nRNG: 00.0 TO 359.9 " DEG);
    EXPECT_ERR(hdg, "X", "FORMAT ERROR\nFORMAT: XXX.X " DEG);

    EXPECT_OK(hold_t, "1.5", 1.5, "1.5", "", "MN");
    EXPECT_ERR(hold_t, "12", "ENTRY OUT OF RANGE\nRNG: 0.0 TO 9.9 MN"); /* [PORT] Q3 */
    EXPECT_ERR(hold_t, "A", "FORMAT ERROR\nFORMAT: X.X MN");
    EXPECT_OK(hold_d, "12.5", 12.5, "12.5", "", "NM");
    EXPECT_ERR(hold_d, "100", "ENTRY OUT OF RANGE\nRNG: 0.0 TO 99.9 NM");
    EXPECT_OK(crs, "90", 90, "090", "", DEG); /* [PORT] padded */
    EXPECT_ERR(crs, "361", "ENTRY OUT OF RANGE\nRNG: 0 TO 360 " DEG);
    EXPECT_ERR(crs_fbw, "-1", "ENTRY OUT OF RANGE\nRNG: 0 TO Infinity " DEG);
    EXPECT_ERR(crs, "A", "FORMAT ERROR\nFORMAT: XXX " DEG);

    EXPECT_OK(ils, "109.5", 109.5, "109.50", "", "");
    EXPECT_OK(ils, "109.33", 109.33, "109.33", "", ""); /* no channel spacing check */
    EXPECT_ERR(ils, "112", "ENTRY OUT OF RANGE\nRNG: 108.00 TO 111.95");
    EXPECT_ERR(ils, "AB", "FORMAT ERROR\nFORMAT: XXX.XX");
    EXPECT_ERR(vor, "118", "ENTRY OUT OF RANGE\nRNG: 108.00 TO 117.95");
    EXPECT_OK(vor, "117.95", 117.95, "117.95", "", "");
    EXPECT_OK(adf, "350", 350, "350.0", "", "");
    EXPECT_ERR(adf, "100", "ENTRY OUT OF RANGE\nRNG: 190.0 TO 1750.0");
    EXPECT_ERR(adf, "A", "FORMAT ERROR\nFORMAT: XXXX.X");

    EXPECT_OK(radial, "45", 45, "045", "", DEG);
    EXPECT_ERR(radial, "361", "ENTRY OUT OF RANGE\nRNG: 0 TO 360 " DEG);
    EXPECT_ERR(radial, "A", "FORMAT ERROR\nFORMAT: XXX " DEG);
    EXPECT_OK(radius, "25", 25, "25", "", "NM");
    EXPECT_ERR(radius, "0", "ENTRY OUT OF RANGE\nRNG: 1 TO 9999 NM");
    EXPECT_ERR(radius, "A", "FORMAT ERROR\nFORMAT: XXXX NM");

    EXPECT_OK(rnp, "0.3", 0.3, "0.30", "", "NM");
    EXPECT_OK(rnp, "11", 11, "11.0", "", "NM");
    EXPECT_OK(rnp, "10", 10, "10.00", "", "NM"); /* `>` not `>=` */
    EXPECT_ERR(rnp, "25", "ENTRY OUT OF RANGE\nRNG: 0.01 TO 20.0 NM");
    EXPECT_ERR(rnp, "0", "ENTRY OUT OF RANGE\nRNG: 0.01 TO 20.0 NM");
    EXPECT_ERR(rnp, "A", "FORMAT ERROR\nFORMAT: X.XX NM");

    EXPECT_OK(pax, "150", 150, "150", "", "");
    EXPECT_ERR(pax, "1000", "ENTRY OUT OF RANGE\nRNG: 0 TO 999");
    EXPECT_ERR(pax, "A", "FORMAT ERROR\nFORMAT: XXX");

    EXPECT_OK(vs, "1500", 1500, "1500", "", "FT/MN"); /* [N/A] */
    EXPECT_ERR(vs, "-5", "ENTRY OUT OF RANGE");
    EXPECT_ERR(vs, "A", "FORMAT ERROR\nFORMAT: +/-XXXX FT/MN");
    EXPECT_OK(lat, "45", 45, "45", "", ""); /* [N/A] */
    EXPECT_ERR(lat, "91", "ENTRY OUT OF RANGE");
    EXPECT_ERR(lat, "N45", "FORMAT ERROR");
}

/* ---- §4.2 SpeedMachFormat ---------------------------------------------------------------------- */

static void test_mach(void)
{
    MfdFmt m = FR(MFD_FMT_SPEED_MACH, 0.1, 0.89);
    EXPECT_OK(m, "84", 0.84, ".84", "", "");
    EXPECT_OK(m, ".84", 0.84, ".84", "", "");
    EXPECT_OK(m, "8", 0.8, ".80", "", "");
    EXPECT_OK(m, "1", 0.1, ".10", "", "");
    EXPECT_OK(m, "100", 0.1, ".10", "", "");
    EXPECT_OK(m, "0.85", 0.85, ".85", "", "");
    EXPECT_ERR(m, "9", "ENTRY OUT OF RANGE\nRNG: 0.1 TO 0.89");
    EXPECT_ERR(m, "05", "ENTRY OUT OF RANGE\nRNG: 0.1 TO 0.89");
    EXPECT_ERR(m, "089", "ENTRY OUT OF RANGE\nRNG: 0.1 TO 0.89");
    EXPECT_ERR(m, "0", "ENTRY OUT OF RANGE\nRNG: 0.1 TO 0.89");
    EXPECT_ERR(m, ".95", "ENTRY OUT OF RANGE\nRNG: 0.1 TO 0.89"); /* [PORT] Q2 (FBW: FORMAT ERROR) */
    EXPECT_ERR(m, "1.2", "ENTRY OUT OF RANGE\nRNG: 0.1 TO 0.89"); /* [PORT] Q2 (FBW: FORMAT ERROR) */
    EXPECT_ERR(m, "AB", "FORMAT ERROR\nFORMAT: XX");
    EXPECT_RENDER(m, 0, 0, ".--", "", "", 1);
    EXPECT_RENDER(m, 1, 0, ".--", "", "", 1); /* FBW `!value` */
}

/* ---- §4.3 AltitudeOrFlightLevelFormat ------------------------------------------------------------ */

static void test_alt_or_fl(void)
{
    MfdFmt a = FR(MFD_FMT_ALT_OR_FL, 0, 43000);
    MfdFmt fl = a;
    a.has_trans = 1;
    a.trans = 5000;
    a.trans_is_fl = 0;
    fl.has_trans = 1;
    fl.trans = 50;
    fl.trans_is_fl = 1;

    EXPECT_OK(a, "350", 35000, "350", "FL", "");
    EXPECT_OK(a, "35", 3500, "3500", "", "FT");
    EXPECT_OK(a, "5000", 5000, "5000", "", "FT"); /* not > 5000 */
    EXPECT_OK(a, "5100", 5100, "051", "FL", "");
    EXPECT_ERR(a, "440", "ENTRY OUT OF RANGE");
    EXPECT_ERR(a, "-50", "ENTRY OUT OF RANGE");
    EXPECT_ERR(a, "FL350", "FORMAT ERROR\nFORMAT: FOR ALT XXXXX FOR FL XXX");
    EXPECT_RENDER(fl, 1, 5000, "050", "FL", "", 0); /* isFl: >= */
    EXPECT_RENDER(fl, 1, 4900, "4900", "", "FT", 0);
    {
        MfdFmt none = FR(MFD_FMT_ALT_OR_FL, 0, 43000); /* no transition known */
        EXPECT_RENDER(none, 1, 35000, "35000", "", "FT", 0);
        EXPECT_RENDER(none, 0, 0, "-----", "", "FT", 1);
    }
#ifdef MFD_FMT_FAITHFUL_JS_NUMBER
    EXPECT_VAL(a, "   ", 0); /* [FBW] Number("   ") * 100 */
#else
    EXPECT_ERR(a, "   ", "FORMAT ERROR\nFORMAT: FOR ALT XXXXX FOR FL XXX");
#endif
}

/* ---- §4.4 FlightLevelFormat -------------------------------------------------------------------- */

static void test_flight_level(void)
{
    MfdFmt f = FR(MFD_FMT_FLIGHT_LEVEL, 0, 430);
    EXPECT_OK(f, "350", 350, "350", "FL", "");
    EXPECT_OK(f, "FL350", 350, "350", "FL", "");
    EXPECT_OK(f, "fl30", 30, "030", "FL", "");
    EXPECT_OK(f, "45", 45, "045", "FL", "");
    EXPECT_ERR(f, "450", "ENTRY OUT OF RANGE");
    EXPECT_ERR(f, "35000", "ENTRY OUT OF RANGE");
    EXPECT_ERR(f, "FLX", "FORMAT ERROR\nFORMAT: FL XXX");
    EXPECT_ERR(f, "350.6", "FORMAT ERROR\nFORMAT: FL XXX"); /* [PORT] Q9 (FBW: 350.6 shown 351) */
    EXPECT_RENDER(f, 1, 350.6, "351", "FL", "", 0);         /* render of a stored decimal FL */
    EXPECT_RENDER(f, 0, 0, "---", "FL", "", 1);
}

/* ---- §4.5 RadioAltitudeFormat, §4.6 TropoFormat ---------------------------------------------------- */

static void test_radio_alt_tropo(void)
{
    MfdFmt ra = FR(MFD_FMT_RADIO_ALT, 1, 43000);
    MfdFmt ra_def = F(MFD_FMT_RADIO_ALT);
    MfdFmt tr = F(MFD_FMT_TROPO);
    EXPECT_OK(ra, "200", 200, "200", "", "FT");
    EXPECT_OK(ra, "NO", 0, "NO DH", "", "");
    EXPECT_OK(ra, "NODH", 0, "NO DH", "", "");
    EXPECT_OK(ra, "NONE", 0, "NO DH", "", "");
    EXPECT_OK(ra, "NO DH", 0, "NO DH", "", "");
    EXPECT_ERR(ra, "0", "ENTRY OUT OF RANGE");
    EXPECT_ERR(ra_def, "0", "ENTRY OUT OF RANGE"); /* default min 1 */
    EXPECT_ERR(ra, "ABC", "FORMAT ERROR");
    EXPECT_RENDER(ra, 0, 0, "-----", "", "FT", 1);

    EXPECT_OK(tr, "360", 36000, "36000", "", "FT");
    EXPECT_OK(tr, "36090", 36090, "36090", "", "FT");
    EXPECT_ERR(tr, "5", "ENTRY OUT OF RANGE");
    EXPECT_ERR(tr, "ABC", "FORMAT ERROR\nFORMAT: FOR ALT XXXXX FOR FL XXX");
    EXPECT_RENDER(tr, 0, 0, "-----", "", "FT", 1);
}

/* ---- §4.7 LengthFormat, §4.8 WeightFormat ------------------------------------------------------- */

typedef struct {
    double lo, hi;
    const char *metric, *imperial;
} WeightRange;

static void test_length_weight(void)
{
    MfdFmt m = FR(MFD_FMT_LENGTH, 1, 4000);
    MfdFmt ft = m;
    MfdFmt zfw = FR(MFD_FMT_WEIGHT, 270000, 373000);
    MfdFmt zfw_lb = zfw;
    static const WeightRange ranges[] = {
        {270000, 373000, "RNG: 270.0 TO 373.0 T", "RNG: 595.2 TO 822.3 KLB"},   /* ZFW */
        {0, 289982, "RNG: 0.0 TO 290.0 T", "RNG: 0.0 TO 639.3 KLB"},            /* BLOCK */
        {0, 9889, "RNG: 0.0 TO 9.9 T", "RNG: 0.0 TO 21.8 KLB"},                 /* TAXI */
        {0, 50000, "RNG: 0.0 TO 50.0 T", "RNG: 0.0 TO 110.2 KLB"},              /* RTE RSV */
        {0, 289982, "RNG: 0.0 TO 290.0 T", "RNG: 0.0 TO 639.3 KLB"},            /* ALTN */
        {0, 100018, "RNG: 0.0 TO 100.0 T", "RNG: 0.0 TO 220.5 KLB"},            /* FINAL */
        {0, 512000, "RNG: 0.0 TO 512.0 T", "RNG: 0.0 TO 1128.8 KLB"},           /* JTSN GW */
        {0, 250000, "RNG: 0.0 TO 250.0 T", "RNG: 0.0 TO 551.2 KLB"},            /* MIN FUEL AT DEST */
    };
    size_t i;
    ft.metric = 0;
    zfw_lb.metric = 0;

    EXPECT_OK(m, "500", 500, "500", "", "M");
    EXPECT_OK(ft, "1000", 304.8, "1000", "", "FT");
    EXPECT_ERR(m, "0", "ENTRY OUT OF RANGE");
    EXPECT_ERR(m, "5000", "ENTRY OUT OF RANGE");
    EXPECT_ERR(ft, "AB", "FORMAT ERROR\nFORMAT: XXXX FT");
    EXPECT_ERR(m, "AB", "FORMAT ERROR\nFORMAT: XXXX M");
    EXPECT_RENDER(m, 0, 0, "----", "", "M", 1);
    EXPECT_RENDER(ft, 0, 0, "----", "", "FT", 1);
    EXPECT_RENDER(ft, 1, 100, "328", "", "FT", 0);
    {
        MfdFmt m5 = m;
        m5.max_digits = 5; /* OANS required stopping distance */
        EXPECT_RENDER(m5, 0, 0, "-----", "", "M", 1);
        CHECK(mfd_fmt_max_digits(&m5) == 5);
        CHECK_STR(mfd_fmt_placeholder(&m5), "-----");
    }

    EXPECT_OK(zfw, "300", 300000, "300.0", "", "T");
    EXPECT_ERR(zfw, "250", "ENTRY OUT OF RANGE\nRNG: 270.0 TO 373.0 T");
    EXPECT_OK(zfw_lb, "661", 661.0 * MFD_KG_PER_LB * 1000.0, "661.0", "", "KLB");
    EXPECT_ERR(zfw, "A", "FORMAT ERROR\nFORMAT: XXX.X T");
    EXPECT_ERR(zfw_lb, "A", "FORMAT ERROR\nFORMAT: XXX.X KLB");
    EXPECT_RENDER(zfw, 0, 0, "---.-", "", "T", 1);
    EXPECT_RENDER(zfw_lb, 0, 0, "---.-", "", "KLB", 1);
    for (i = 0; i < sizeof ranges / sizeof ranges[0]; i++) {
        MfdFmt w = FR(MFD_FMT_WEIGHT, ranges[i].lo, ranges[i].hi);
        char want[96];
        snprintf(want, sizeof want, "ENTRY OUT OF RANGE\n%s", ranges[i].metric);
        EXPECT_ERR(w, "99999", want);
        w.metric = 0;
        snprintf(want, sizeof want, "ENTRY OUT OF RANGE\n%s", ranges[i].imperial);
        EXPECT_ERR(w, "99999", want);
    }
    {
        MfdFmt w = F(MFD_FMT_WEIGHT); /* defaults 0..+inf */
        EXPECT_ERR(w, "-1", "ENTRY OUT OF RANGE\nRNG: 0.0 TO Infinity T");
    }
}

/* ---- §4.9 CrzTemp, §4.10 TripWind ------------------------------------------------------------------ */

static void test_crz_temp_trip_wind(void)
{
    MfdFmt t = F(MFD_FMT_CRZ_TEMP);
    MfdFmt w = F(MFD_FMT_TRIP_WIND);
    EXPECT_OK(t, "50", -50, "-50", "", DEG "C");
    EXPECT_OK(t, "+5", 5, "+5", "", DEG "C");
    EXPECT_OK(t, "-50", -50, "-50", "", DEG "C");
    EXPECT_OK(t, "0", 0, "+0", "", DEG "C");
    EXPECT_ERR(t, "100", "ENTRY OUT OF RANGE\nRNG: -99 TO 99 C");
    EXPECT_ERR(t, "A", "FORMAT ERROR\nFORMAT: +/-XXX C");

    EXPECT_OK(w, "HD20", -20, "020", "HD", "");
    EXPECT_OK(w, "H20", -20, "020", "HD", "");
    EXPECT_OK(w, "-20", -20, "020", "HD", "");
    EXPECT_OK(w, "TL20", 20, "020", "TL", "");
    EXPECT_OK(w, "T20", 20, "020", "TL", "");
    EXPECT_OK(w, "+20", 20, "020", "TL", "");
    EXPECT_OK(w, "20", 20, "020", "TL", "");
    EXPECT_OK(w, "HD 20", -20, "020", "HD", "");
    EXPECT_ERR(w, "HD300", "ENTRY OUT OF RANGE\nRNG: -250 TO 250 KT");
    EXPECT_ERR(w, "X20", "FORMAT ERROR\nFORMAT: +/-XXX");
    EXPECT_RENDER(w, 0, 0, "-----", "", "", 1);
    EXPECT_RENDER(w, 1, 0, "000", "TL", "", 0);
#ifdef MFD_FMT_FAITHFUL_JS_NUMBER
    EXPECT_VAL(w, "H", -0.0); /* [FBW] Number('') = 0 -> -0 */
#else
    EXPECT_ERR(w, "H", "FORMAT ERROR\nFORMAT: +/-XXX");
#endif
}

/* ---- §4.11 QnhFormat and QnhUtils (§9.2) -------------------------------------------------------- */

static void test_qnh(void)
{
    MfdFmt q = F(MFD_FMT_QNH);
    MfdFmt q_in = q;
    q_in.qnh_is_hpa = 0;
    EXPECT_OK(q, "1013", 1013, "1013", "", "");
    EXPECT_OK(q, "999", 999, "999", "", "");
    EXPECT_OK(q, "2992", 29.92, "29.92", "", "");
    EXPECT_OK(q, "29.92", 29.92, "29.92", "", "");
    EXPECT_OK(q, "2200", 22.0, "22.00", "", "");
    EXPECT_OK(q, "3248", 32.48, "32.48", "", "");
    EXPECT_ERR(q, "2199", "ENTRY OUT OF RANGE\nRNG: 745 TO 1100");
    EXPECT_ERR(q, "33.00", "ENTRY OUT OF RANGE\nRNG: 22.00 TO 32.48");
    EXPECT_ERR(q, "10.13", "ENTRY OUT OF RANGE\nRNG: 22.00 TO 32.48");
    EXPECT_ERR(q, "3300", "ENTRY OUT OF RANGE\nRNG: 745 TO 1100");
    EXPECT_ERR(q, "700", "ENTRY OUT OF RANGE\nRNG: 745 TO 1100");
    EXPECT_ERR(q, "29.9", "FORMAT ERROR\nFORMAT: XXXX");
    EXPECT_ERR(q, "101.3", "FORMAT ERROR\nFORMAT: XXXX");
    EXPECT_ERR(q, "ABC", "FORMAT ERROR\nFORMAT: XXXX");
    EXPECT_RENDER(q, 0, 0, "----", "", "", 1);
    EXPECT_RENDER(q_in, 0, 0, "--.--", "", "", 1);
    CHECK_STR(mfd_fmt_placeholder(&q_in), "--.--");

    CHECK(mfd_qnh_is_inhg(29.92));
    CHECK(!mfd_qnh_is_inhg(100));
    CHECK(!mfd_qnh_is_inhg(1013));
    CHECK(near(mfd_qnh_to_millibar(29.92), 1013.207489, 1e-6));
    CHECK(mfd_qnh_to_millibar(1013) == 1013.0);
}

/* ---- §4.12 DescentRate, §4.14/4.15 Time, §4.16 LsCourse, §4.17 Squawk, §4.18 FuelPenalty -------- */

static void test_misc_numeric(void)
{
    MfdFmt dr = FR(MFD_FMT_DESCENT_RATE, -999, -100);
    MfdFmt hm = F(MFD_FMT_TIME_HHMM);
    MfdFmt hms = F(MFD_FMT_TIME_HHMMSS);
    MfdFmt ls = F(MFD_FMT_LS_COURSE);
    MfdFmt sq = F(MFD_FMT_SQUAWK);
    MfdFmt fp = F(MFD_FMT_FUEL_PENALTY);

    EXPECT_OK(dr, "350", -350, "-350", "", "FT/MN");
    EXPECT_OK(dr, "-350", -350, "-350", "", "FT/MN");
    EXPECT_ERR(dr, "50", "ENTRY OUT OF RANGE\nRNG: -999 TO -100 FT/MN");
    EXPECT_ERR(dr, "1500", "ENTRY OUT OF RANGE\nRNG: -999 TO -100 FT/MN");
    EXPECT_ERR(dr, "0", "ENTRY OUT OF RANGE\nRNG: -999 TO -100 FT/MN");
    EXPECT_ERR(dr, "A", "FORMAT ERROR\nFORMAT: -XXXX FT/MN");

    EXPECT_OK(hm, "130", 90, "01:30", "", "");
    EXPECT_OK(hm, "45", 45, "00:45", "", "");
    EXPECT_OK(hm, "1:30", 90, "01:30", "", "");
    EXPECT_OK(hm, "01:30", 90, "01:30", "", ""); /* round trip of the formatted text */
    EXPECT_OK(hm, "0", 0, "00:00", "", "");      /* [PORT] Q6 (FBW shows --:--) */
    EXPECT_ERR(hm, "200", "ENTRY OUT OF RANGE");
    EXPECT_ERR(hm, "60", "ENTRY OUT OF RANGE");
    EXPECT_ERR(hm, "AB", "FORMAT ERROR\nFORMAT: HHMM");
    EXPECT_RENDER(hm, 0, 0, "--:--", "", "", 1);
#ifdef MFD_FMT_FAITHFUL_JS_NUMBER
    EXPECT_VAL(hm, ":", 0); /* [FBW] Number('') = 0 */
#else
    EXPECT_ERR(hm, ":", "FORMAT ERROR\nFORMAT: HHMM");
#endif

    EXPECT_OK(hms, "1230", 45000, "12:30:00", "", "");
    EXPECT_OK(hms, "123045", 45045, "12:30:45", "", "");
    EXPECT_OK(hms, "12:3045", 45045, "12:30:45", "", ""); /* first ':' removed */
    EXPECT_ERR(hms, "123", "FORMAT ERROR\nFORMAT: HHMMSS"); /* [PORT] Q5 (FBW clears the field) */
    EXPECT_ERR(hms, "2500", "ENTRY OUT OF RANGE");          /* [PORT] Q5 (FBW clears the field) */
    EXPECT_ERR(hms, "126000", "ENTRY OUT OF RANGE");
    EXPECT_ERR(hms, "ABCD", "FORMAT ERROR\nFORMAT: HHMMSS");
    EXPECT_RENDER(hms, 1, 0, "00:00:00", "", "", 0); /* [PORT] Q6 */
    EXPECT_RENDER(hms, 1, 3661, "01:01:01", "", "", 0);
    EXPECT_RENDER(hms, 0, 0, "--:--:--", "", "", 1);

    EXPECT_OK(ls, "109", 109, "F109", "", DEG);
    EXPECT_OK(ls, "F109", 109, "F109", "", DEG);
    EXPECT_OK(ls, "5", 5, "F005", "", DEG);
    EXPECT_ERR(ls, "B109", "FORMAT ERROR\nFORMAT: FXXX " DEG);
    EXPECT_ERR(ls, "1090", "FORMAT ERROR\nFORMAT: FXXX " DEG); /* [PORT] Q7 (FBW: 109) */
    EXPECT_ERR(ls, "-5", "FORMAT ERROR\nFORMAT: FXXX " DEG);   /* [PORT] Q7 (FBW: B005) */
    EXPECT_ERR(ls, "400", "ENTRY OUT OF RANGE\nRNG: 0 TO 360 " DEG);
    EXPECT_ERR(ls, "F09", "FORMAT ERROR\nFORMAT: FXXX " DEG);
    EXPECT_RENDER(ls, 1, -109, "B109", "", DEG, 0);
    EXPECT_RENDER(ls, 0, 0, "----", "", DEG, 1);

    EXPECT_OK(sq, "7000", 7000, "7000", "", "");
    EXPECT_OK(sq, "0123", 123, "0123", "", "");
    EXPECT_OK(sq, "0000", 0, "0000", "", ""); /* [PORT] Q8 accepted */
    EXPECT_ERR(sq, "7800", "FORMAT ERROR");
    EXPECT_ERR(sq, "123", "FORMAT ERROR");
    EXPECT_ERR(sq, "+123", "FORMAT ERROR");
    {
        /* SURV page: FORMAT ERROR is replaced by SQWK CODE NOT VALID */
        MfdMsgQueue q;
        MfdFmsError e;
        MfdValue v;
        mfd_msg_init(&q);
        CHECK(mfd_fmt_parse(&sq, "7800", &v, &e) == MFD_PARSE_ERROR && e.type == MFD_ERR_FORMAT_ERROR);
        mfd_msg_add(&q, MFD_MSG_SQWK_CODE_NOT_VALID, NULL, NULL, NULL, NULL, NULL);
        CHECK(mfd_msg_footer_entry(&q) != NULL);
        CHECK_STR(mfd_msg_footer_entry(&q)->text, "SQWK CODE NOT VALID");
    }

    EXPECT_OK(fp, "5", 5, "+005.0", "", "%");
    EXPECT_OK(fp, "+12.5", 12.5, "+012.5", "", "%");
    EXPECT_OK(fp, "999.9", 999.9, "+999.9", "", "%");
    EXPECT_ERR(fp, "12.55", "FORMAT ERROR\nFORMAT: +NNN.N %");
    EXPECT_ERR(fp, "-5", "FORMAT ERROR\nFORMAT: +NNN.N %");
    fp.err_details = 0; /* DATA/STATUS handler drops the details */
    EXPECT_ERR(fp, "12.55", "FORMAT ERROR");
    EXPECT_ERR(fp, "-5", "FORMAT ERROR");
    EXPECT_RENDER(fp, 0, 0, "+000.0", "", "%", 1);
}

/* ---- §4.13 Text formats ------------------------------------------------------------------------- */

static void test_text(void)
{
    MfdFmt fix = F(MFD_FMT_TXT_FIX);
    MfdFmt fix_info = fix;
    MfdFmt apt = F(MFD_FMT_TXT_AIRPORT);
    MfdFmt nav = F(MFD_FMT_TXT_NAVAID);
    MfdFmt nav_desel = nav;
    MfdFmt awy = F(MFD_FMT_TXT_AIRWAY);
    MfdFmt dd = F(MFD_FMT_TXT_DROPDOWN);
    MfdFmt dd_def = dd;
    MfdFmt wpt = F(MFD_FMT_TXT_WAYPOINT);
    MfdFmt alnum = F(MFD_FMT_TXT_LONG_ALNUM);
    char out[32];
    MfdFmsError e;
    MfdFieldText t;

    fix_info.err_details = 0;
    nav_desel.placeholder = "-";
    dd.max_digits = 7;

    EXPECT_TXT(fix, "ABCDE", "ABCDE");
    EXPECT_TXT(fix, "EDDF25C", "EDDF25C");
    EXPECT_ERR(fix, "A", "FORMAT ERROR\nFORMAT: XXXXX");
    EXPECT_ERR(fix_info, "A", "FORMAT ERROR"); /* FIX INFO drops details */
    EXPECT_ERR(fix, "abc", "FORMAT ERROR\nFORMAT: XXXXX");
    EXPECT_CLEAR(fix, "   ");
    EXPECT_CLEAR(fix, "");

    EXPECT_TXT(apt, "EDDF", "EDDF");
    EXPECT_ERR(apt, "EDD", "FORMAT ERROR\nFORMAT: AAAA");
    EXPECT_TXT(apt, "1234", "1234");
    EXPECT_CLEAR(apt, "----");
    EXPECT_CLEAR(apt, "");

    EXPECT_TXT(nav, "TGO", "TGO");
    EXPECT_CLEAR(nav, "----");
    EXPECT_CLEAR(nav_desel, "-");
    EXPECT_TXT(nav_desel, "----", "----");
    EXPECT_TXT(awy, "UL602", "UL602");
    EXPECT_CLEAR(awy, "---");
    EXPECT_TXT(dd, "WPT01", "WPT01");
    EXPECT_CLEAR(dd, "-------");
    CHECK_STR(mfd_fmt_placeholder(&dd), "-------");
    CHECK_STR(mfd_fmt_placeholder(&dd_def), "------");
    CHECK(mfd_fmt_max_digits(&dd) == 7);
    EXPECT_TXT(wpt, "KABEL", "KABEL");
    EXPECT_CLEAR(wpt, "-------");
    EXPECT_TXT(alnum, "AFR123", "AFR123");
    EXPECT_CLEAR(alnum, "----------");
    EXPECT_ERR(dd, "ABCDEFGHIJKLMNOPQRSTUVWXY", "FORMAT ERROR"); /* 25 chars: cannot be stored */
    EXPECT_TXT(dd, "ABCDEFGHIJKLMNOPQRSTUVWX", "ABCDEFGHIJKLMNOPQRSTUVWX");

    {
        MfdValue v;
        memset(&v, 0, sizeof v);
        mfd_fmt_render(&apt, &v, &t);
        CHECK_STR(t.value, "----");
        CHECK(t.is_placeholder);
        v.has = 1;
        snprintf(v.str, sizeof v.str, "EDDF");
        mfd_fmt_render(&apt, &v, &t);
        CHECK_STR(t.value, "EDDF");
        CHECK(!t.is_placeholder);
        mfd_fmt_render(&nav_desel, NULL, &t);
        CHECK_STR(t.value, "-");
    }

    CHECK(mfd_is_place_format("AB"));
    CHECK(mfd_is_place_format("ABCDEF7"));
    CHECK(!mfd_is_place_format("A"));
    CHECK(!mfd_is_place_format("ABCDEFGH"));
    CHECK(!mfd_is_place_format("ab"));
    CHECK(!mfd_is_place_format("AB-C"));
    CHECK(mfd_is_runway_format("EDDF25"));
    CHECK(mfd_is_runway_format("EDDF25C"));
    CHECK(!mfd_is_runway_format("EDDF25X"));
    CHECK(!mfd_is_runway_format("EDD25"));
    CHECK(!mfd_is_runway_format("EDDF2"));

    CHECK(mfd_fmt_parse_text(MFD_FMT_TXT_AIRPORT, "EDDF", 0, NULL, out, sizeof out, &e) == MFD_PARSE_OK);
    CHECK_STR(out, "EDDF");
    CHECK(mfd_fmt_parse_text(MFD_FMT_TXT_AIRPORT, "EDD", 0, NULL, out, sizeof out, &e) == MFD_PARSE_ERROR);
    CHECK_STR(e.details, "FORMAT: AAAA");
    CHECK(mfd_fmt_parse_text(MFD_FMT_TXT_NAVAID, "-", 0, "-", out, sizeof out, &e) == MFD_PARSE_CLEAR);
    CHECK_STR(out, "");
    CHECK(mfd_fmt_parse_text(MFD_FMT_SPEED_KT, "140", 0, NULL, out, sizeof out, &e) == MFD_PARSE_ERROR);
    mfd_fmt_render_text(NULL, "----", &t);
    CHECK_STR(t.value, "----");
    CHECK(t.is_placeholder);
    mfd_fmt_render_text("LFPG", "----", &t);
    CHECK_STR(t.value, "LFPG");
    CHECK(!t.is_placeholder);
}

/* ---- Descriptor metadata, null rendering of every kind ------------------------------------------- */

static void test_descriptor(void)
{
    MfdFmt f;
    double lo = -1, hi = -1;
    int k;

    /* every kind renders its placeholder while empty and parses '' as clear */
    for (k = 0; k < MFD_FMT_COUNT; k++) {
        MfdFieldText t;
        MfdValue v;
        MfdFmsError e;
        f = F((MfdFmtKind)k);
        mfd_fmt_render(&f, NULL, &t);
        CHECK(t.is_placeholder);
        CHECK(strcmp(t.value, mfd_fmt_placeholder(&f)) == 0);
        CHECK(mfd_fmt_max_digits(&f) >= 3);
        CHECK(mfd_fmt_parse(&f, "", &v, &e) == MFD_PARSE_CLEAR);
        CHECK(mfd_fmt_parse(&f, NULL, &v, &e) == MFD_PARSE_CLEAR);
    }

    f = F(MFD_FMT_SPEED_KT);
    EXPECT_RENDER(f, 0, 0, "---", "", "KT", 1);
    f = F(MFD_FMT_HEADING);
    EXPECT_RENDER(f, 0, 0, "---.-", "", DEG "T", 1);
    CHECK(mfd_fmt_range(&f, &lo, &hi) && lo == 0 && hi == 359.9);
    f = F(MFD_FMT_RADIAL);
    EXPECT_RENDER(f, 0, 0, "---", "", "", 1); /* no unit while empty */
    CHECK_STR(mfd_fmt_trail_unit(&f), DEG);
    f = F(MFD_FMT_RADIUS);
    EXPECT_RENDER(f, 0, 0, "----", "", "", 1);
    f = F(MFD_FMT_RNP);
    EXPECT_RENDER(f, 0, 0, "--.-", "", "", 1);
    f = F(MFD_FMT_HOLD_TIME);
    EXPECT_RENDER(f, 0, 0, "-.-", "", "MN", 1);
    f = F(MFD_FMT_PERCENT);
    EXPECT_RENDER(f, 1, 30, "30.0", "", "%", 0);
    f = F(MFD_FMT_TEMPERATURE);
    EXPECT_RENDER(f, 1, -0.4, "-0", "", DEG "C", 0); /* JS (-0.4).toFixed(0) = "-0" */
    f = F(MFD_FMT_SQUAWK);
    EXPECT_RENDER(f, 1, 0, "0000", "", "", 0);
    f = F(MFD_FMT_COST_INDEX);
    EXPECT_RENDER(f, 0, 0, "---", "", "", 1);

    f = F(MFD_FMT_FLIGHT_LEVEL);
    CHECK(mfd_fmt_max_overflow(&f) == 2);
    CHECK(mfd_fmt_max_digits(&f) == 3);
    CHECK_STR(mfd_fmt_error_unit(&f), "FL");
    CHECK_STR(mfd_fmt_lead_unit(&f), "FL");
    CHECK_STR(mfd_fmt_trail_unit(&f), "");
    f = F(MFD_FMT_SPEED_KT);
    CHECK(mfd_fmt_max_overflow(&f) == 0);
    f.max_digits = 4;
    CHECK(mfd_fmt_max_digits(&f) == 4);
    f = F(MFD_FMT_TEMPERATURE);
    CHECK_STR(mfd_fmt_error_unit(&f), DEG "C");
    f = F(MFD_FMT_TRIP_WIND);
    CHECK_STR(mfd_fmt_error_unit(&f), "KT");
    f = F(MFD_FMT_ALT_OR_FL);
    CHECK_STR(mfd_fmt_error_unit(&f), "");
    CHECK_STR(mfd_fmt_trail_unit(&f), "FT");
    f = F(MFD_FMT_WEIGHT);
    CHECK_STR(mfd_fmt_trail_unit(&f), "T");
    f.metric = 0;
    CHECK_STR(mfd_fmt_trail_unit(&f), "KLB");
    f = F(MFD_FMT_HEADING);
    CHECK_STR(mfd_fmt_trail_unit(&f), DEG "T");

    /* ranges: caller range, min == max == 0 -> default, fixed kinds ignore min/max */
    f = FR(MFD_FMT_SPEED_KT, 90, 340);
    CHECK(mfd_fmt_range(&f, &lo, &hi) && lo == 90 && hi == 340);
    f = FR(MFD_FMT_SPEED_KT, 0, 0);
    CHECK(mfd_fmt_range(&f, &lo, &hi) && lo == 0 && isinf(hi));
    f = FR(MFD_FMT_WIND_DIR, 10, 20);
    CHECK(mfd_fmt_range(&f, &lo, &hi) && lo == 0 && hi == 359);
    f = F(MFD_FMT_QNH);
    CHECK(!mfd_fmt_range(&f, &lo, &hi));
    f = F(MFD_FMT_TXT_FIX);
    CHECK(!mfd_fmt_range(&f, &lo, &hi));
    CHECK(mfd_fmt_is_text(MFD_FMT_TXT_FIX) && mfd_fmt_is_text(MFD_FMT_TXT_LONG_ALNUM));
    CHECK(!mfd_fmt_is_text(MFD_FMT_FUEL_PENALTY));

    /* zero-initialised descriptor: default range and no details (caller decides) */
    {
        MfdFmt z;
        memset(&z, 0, sizeof z);
        z.kind = MFD_FMT_HOLD_DIST;
        EXPECT_ERR(z, "100", "ENTRY OUT OF RANGE");
        z.err_details = 1;
        EXPECT_ERR(z, "100", "ENTRY OUT OF RANGE\nRNG: 0.0 TO 99.9 NM");
    }

    /* invalid kind */
    {
        MfdFieldText t;
        MfdValue v;
        MfdFmsError e;
        memset(&f, 0, sizeof f);
        f.kind = MFD_FMT_COUNT;
        CHECK(mfd_fmt_parse(&f, "1", &v, &e) == MFD_PARSE_ERROR && e.type == MFD_ERR_FORMAT_ERROR);
        mfd_fmt_render(&f, NULL, &t);
        CHECK_STR(t.value, "");
        CHECK_STR(mfd_fmt_placeholder(&f), "");
        CHECK(mfd_fmt_parse(NULL, "1", &v, &e) == MFD_PARSE_ERROR);
    }
}

/* ---- §11.3 per-format wrappers -------------------------------------------------------------------- */

static void test_wrappers(void)
{
    double v = -1;
    MfdFmsError e;
    MfdFieldText t;

    CHECK(mfd_fmt_parse_speed_kt("140", 90, 340, &v, &e) == MFD_PARSE_OK && v == 140);
    v = -1;
    CHECK(mfd_fmt_parse_speed_kt("80", 90, 340, &v, &e) == MFD_PARSE_ERROR && v == -1);
    CHECK_STR(e.details, "RNG: 90 TO 340 KT");
    CHECK(mfd_fmt_parse_speed_kt("", 90, 340, &v, &e) == MFD_PARSE_CLEAR && v == -1);
    CHECK(mfd_fmt_parse_heading("-5", 0, 0, &v, &e) == MFD_PARSE_ERROR);
    CHECK_STR(e.details, "RNG: 00.0 TO 359.9 " DEG);
    CHECK(mfd_fmt_parse_speed_mach("84", 0.1, 0.89, &v, &e) == MFD_PARSE_OK && v == 0.84);
    CHECK(mfd_fmt_parse_qnh("2992", &v, &e) == MFD_PARSE_OK && v == 29.92);
    CHECK(mfd_fmt_parse_trip_wind("HD20", &v, &e) == MFD_PARSE_OK && v == -20);
    CHECK(mfd_fmt_parse_squawk("7000", &v, &e) == MFD_PARSE_OK && v == 7000);
    CHECK(mfd_fmt_parse_length("1000", 1, 4000, 0, &v, &e) == MFD_PARSE_OK && near(v, 304.8, 1e-9));
    CHECK(mfd_fmt_parse_weight("300", 270000, 373000, 1, &v, &e) == MFD_PARSE_OK && v == 300000);
    CHECK(mfd_fmt_parse_alt_or_fl("350", 0, 43000, &v, &e) == MFD_PARSE_OK && v == 35000);
    CHECK(mfd_fmt_parse_flight_level("FL350", 0, 430, &v, &e) == MFD_PARSE_OK && v == 350);
    CHECK(mfd_fmt_parse_tropo("360", &v, &e) == MFD_PARSE_OK && v == 36000);
    CHECK(mfd_fmt_parse_time_hhmm("130", &v, &e) == MFD_PARSE_OK && v == 90);
    CHECK(mfd_fmt_parse_crz_temp("50", &v, &e) == MFD_PARSE_OK && v == -50);
    CHECK(mfd_fmt_parse_descent_rate("350", -999, -100, &v, &e) == MFD_PARSE_OK && v == -350);
    CHECK(mfd_fmt_parse_radio_alt("NO", 1, 43000, &v, &e) == MFD_PARSE_OK && v == 0);

    mfd_fmt_render_weight(1, 300000, 1, &t);
    CHECK_STR(t.value, "300.0");
    CHECK_STR(t.trail, "T");
    mfd_fmt_render_weight(1, 661.0 * MFD_KG_PER_LB * 1000.0, 0, &t);
    CHECK_STR(t.value, "661.0");
    CHECK_STR(t.trail, "KLB");
    mfd_fmt_render_alt_or_fl(1, 35000, 1, 5000, 0, &t);
    CHECK_STR(t.value, "350");
    CHECK_STR(t.lead, "FL");
    mfd_fmt_render_alt_or_fl(1, 5000, 1, 50, 1, &t);
    CHECK_STR(t.value, "050");
    mfd_fmt_render_length(1, 304.8, 0, 4, &t);
    CHECK_STR(t.value, "1000");
    CHECK_STR(t.trail, "FT");
    mfd_fmt_render_length(0, 0, 1, 5, &t);
    CHECK_STR(t.value, "-----");
    mfd_fmt_render_qnh(0, 0, 0, &t);
    CHECK_STR(t.value, "--.--");
    mfd_fmt_render_qnh(1, 1013, 1, &t);
    CHECK_STR(t.value, "1013");
    mfd_fmt_render_time_hhmm(1, 90, &t);
    CHECK_STR(t.value, "01:30");
    mfd_fmt_render_speed_kt(0, 0, &t);
    CHECK_STR(t.value, "---");
    CHECK_STR(t.trail, "KT");
    mfd_fmt_render_trip_wind(1, -20, &t);
    CHECK_STR(t.lead, "HD");
    CHECK_STR(t.value, "020");
    mfd_fmt_render_rnp(1, 0.3, &t);
    CHECK_STR(t.value, "0.30");
}

/* ---- Messages: catalogue and queue (spec 02 §10, §11.5, §11.6) --------------------------------------- */

static int g_flag_a, g_flag_b, g_clear_calls;
static int resolved_a(void *ctx)
{
    (void)ctx;
    return g_flag_a;
}
static int resolved_b(void *ctx)
{
    (void)ctx;
    return g_flag_b;
}
static void on_clear_count(void *ctx)
{
    g_clear_calls++;
    if (ctx) *(int *)ctx += 1;
}
static MfdMsgQueue *g_reentrant_q;
static void on_clear_reentrant(void *ctx)
{
    (void)ctx;
    mfd_msg_remove_id(g_reentrant_q, MFD_MSG_FORMAT_ERROR);
    mfd_msg_add(g_reentrant_q, MFD_MSG_NOT_ALLOWED, NULL, NULL, NULL, NULL, NULL);
}

static void test_catalogue(void)
{
    int i;
    char b[128];
    for (i = 0; i < MFD_MSG_COUNT; i++) {
        CHECK(mfd_msg_defs[i].text != NULL);
        CHECK(mfd_msg_defs[i].text[0] != '\0' || i == MFD_MSG_EMPTY_MESSAGE);
        if (mfd_msg_defs[i].token) CHECK(strstr(mfd_msg_defs[i].text, mfd_msg_defs[i].token) != NULL);
        if (mfd_msg_is_atccom((MfdMsgId)i)) CHECK(!mfd_msg_defs[i].type2 && !mfd_msg_defs[i].amber);
    }
    CHECK(MFD_MSG_COUNT == 94);
    CHECK_STR(mfd_msg_defs[MFD_MSG_COM_FPLN_RECEIVED].text, "COMPANY F-PLN RECEIVED\nWAITING FOR INSERTION");
    CHECK_STR(mfd_msg_defs[MFD_MSG_CHECK_TO_DATA].text, "CHECK T.O. DATA");
    CHECK(mfd_msg_defs[MFD_MSG_CHECK_TO_DATA].type2 && mfd_msg_defs[MFD_MSG_CHECK_TO_DATA].amber);
    CHECK_STR(mfd_msg_defs[MFD_MSG_TO_SPEED_TOO_LOW].text, "T.O SPEED TOO LOW - CHECK TOW & T.O DATA");
    CHECK(!mfd_msg_defs[MFD_MSG_FORMAT_ERROR].type2 && !mfd_msg_defs[MFD_MSG_FORMAT_ERROR].amber);
    CHECK(mfd_msg_defs[MFD_MSG_NAV_PRIMARY_LOST].amber && !mfd_msg_defs[MFD_MSG_NAV_PRIMARY].amber);
    CHECK(mfd_msg_defs[MFD_MSG_RELOAD_PLANE_APPLY].type2 && mfd_msg_defs[MFD_MSG_RELOAD_PLANE_APPLY].amber);
    CHECK_STR(mfd_msg_defs[MFD_MSG_SIMBRIEF_NO_USER].text, "NO SIMBRIEF PILOT ID PROVIDED");
    CHECK_STR(mfd_msg_defs[MFD_MSG_FLT_NBR_MISSING].text, "ENTER ATC FLT NBR");
    CHECK_STR(mfd_msg_defs[MFD_MSG_ATC_DATIS_NO_REPLY].text, "D-ATIS NO REPLY");
    CHECK_STR(mfd_msg_defs[MFD_MSG_ATC_SENDING_MAYDAY_WILL_SWITCH_ADSC_TO_EMERGENCY].text,
              "SENDING MAYDAY WILL SWITCH ADS-C TO EMERGENCY");
    CHECK(mfd_msg_is_atccom(MFD_MSG_ATC_PRINTING) && !mfd_msg_is_atccom(MFD_MSG_SIMBRIEF_NO_USER));
    CHECK(!mfd_msg_is_atccom(MFD_MSG_COUNT));

    CHECK(mfd_msg_build_text(MFD_MSG_XXX_IS_DESELECTED, "TGO", NULL, b, sizeof b));
    CHECK_STR(b, "TGO IS DESELECTED");
    CHECK(mfd_msg_build_text(MFD_MSG_CSTR_DEL_UP_TO_WPT, "ABCDE", NULL, b, sizeof b));
    CHECK_STR(b, "CONSTRAINTS BEFORE ABCDE : DELETED");
    CHECK(mfd_msg_build_text(MFD_MSG_NEW_CRZ_ALT, "", NULL, b, sizeof b));
    CHECK_STR(b, "NEW CRZ ALT: HHHHH"); /* empty token value: no replacement */
    CHECK(mfd_msg_build_text(MFD_MSG_FORMAT_ERROR, "IGNORED", "FORMAT: XXX KT", b, sizeof b));
    CHECK_STR(b, "FORMAT ERROR\nFORMAT: XXX KT");
    CHECK(!mfd_msg_build_text(MFD_MSG_FORMAT_ERROR, NULL, "FORMAT: XXX KT", b, 8));
    CHECK_STR(b, "FORMAT ");
}

static void test_queue(void)
{
    MfdMsgQueue q;
    MfdFmsError e;
    const MfdMsgEntry *rows[8];
    int ctx_count = 0, i, n;
    unsigned v0;

    mfd_msg_init(&q);
    CHECK(q.count == 0 && mfd_msg_footer_entry(&q) == NULL && q.version == 1);
    mfd_msg_clear_latest(&q); /* no-op */
    CHECK(q.count == 0 && q.version == 1);

    /* §11.6: Type I twice -> 2 entries; clear -> 1 left */
    mfd_err_set_format(&e, "XXX", "KT");
    mfd_msg_show_error(&q, &e, 1);
    mfd_msg_show_error(&q, &e, 1);
    CHECK_STR(q.e[0].text, "FORMAT ERROR\nFORMAT: XXX KT");
    CHECK(q.e[0].bg == MFD_MSG_BG_WHITE && !q.e[0].type2 && q.e[0].id == MFD_MSG_FORMAT_ERROR);
#ifdef MFD_MSG_DEDUP_UNCLEARED
    CHECK(q.count == 1); /* spec 07 §11-4: identical uncleared text replaced */
    mfd_msg_add(&q, MFD_MSG_FORMAT_ERROR, NULL, NULL, NULL, NULL, NULL); /* different text */
#endif
    CHECK(q.count == 2);
    v0 = q.version;
    mfd_msg_clear_latest(&q);
    CHECK(q.count == 1 && q.version != v0);
    mfd_msg_clear_latest(&q);
    CHECK(q.count == 0 && mfd_msg_footer_entry(&q) == NULL);

    /* Type II: clear marks cleared (stays in MSG LIST), calls on_clear; re-add replaces the cleared entry */
    mfd_msg_add(&q, MFD_MSG_NOT_IN_DATABASE, NULL, NULL, NULL, NULL, NULL);
    mfd_msg_add(&q, MFD_MSG_CHECK_TO_DATA, NULL, NULL, NULL, on_clear_count, &ctx_count);
    CHECK(q.count == 2 && q.e[0].id == MFD_MSG_CHECK_TO_DATA && q.e[0].bg == MFD_MSG_BG_AMBER && q.e[0].type2);
    CHECK(mfd_msg_footer_entry(&q) == &q.e[0]);
    mfd_msg_clear_latest(&q);
    CHECK(q.count == 2 && q.e[0].cleared && g_clear_calls == 1 && ctx_count == 1);
    CHECK(mfd_msg_footer_entry(&q) != NULL && mfd_msg_footer_entry(&q)->id == MFD_MSG_NOT_IN_DATABASE);
    mfd_msg_add(&q, MFD_MSG_CHECK_TO_DATA, NULL, NULL, NULL, NULL, NULL);
    CHECK(q.count == 2 && q.e[0].id == MFD_MSG_CHECK_TO_DATA && !q.e[0].cleared);
    CHECK(q.e[1].id == MFD_MSG_NOT_IN_DATABASE);
    CHECK(mfd_msg_has_id(&q, MFD_MSG_CHECK_TO_DATA) && !mfd_msg_has_id(&q, MFD_MSG_CHECK_ZFW));
    mfd_msg_clear_latest(&q); /* CHECK T.O. DATA -> cleared */
    mfd_msg_clear_latest(&q); /* NOT IN DATABASE -> deleted */
    CHECK(q.count == 1 && q.e[0].cleared && mfd_msg_footer_entry(&q) == NULL);
    mfd_msg_clear_latest(&q); /* nothing uncleared */
    CHECK(q.count == 1);

    /* update removes resolved Type II entries; adjacent resolved entries are not skipped (Q16) */
    mfd_msg_init(&q);
    g_flag_a = g_flag_b = 0;
    mfd_msg_add(&q, MFD_MSG_DEST_EFOB_BELOW_MIN, NULL, NULL, resolved_a, NULL, NULL);
    mfd_msg_add(&q, MFD_MSG_SPD_LIM_EXCEEDED, NULL, NULL, resolved_a, NULL, NULL);
    mfd_msg_add(&q, MFD_MSG_V_TO_DISAGREE, NULL, NULL, resolved_b, NULL, NULL);
    mfd_msg_add(&q, MFD_MSG_FORMAT_ERROR, NULL, NULL, resolved_a, NULL, NULL); /* Type I: callback dropped */
    CHECK(q.e[0].is_resolved == NULL && q.e[0].on_clear == NULL);
    mfd_msg_update(&q);
    CHECK(q.count == 4);
    g_flag_a = 1;
    mfd_msg_update(&q);
    CHECK(q.count == 2 && q.e[0].id == MFD_MSG_FORMAT_ERROR && q.e[1].id == MFD_MSG_V_TO_DISAGREE);
    g_flag_b = 1;
    mfd_msg_clear_latest(&q); /* FORMAT ERROR deleted */
    mfd_msg_clear_latest(&q); /* V1/VR/V2 cleared */
    CHECK(q.count == 1 && q.e[0].cleared);
    mfd_msg_update(&q); /* cleared entries are resolved too */
    CHECK(q.count == 0);

    /* capacity: drop the oldest cleared entry, else the oldest entry (Q15) */
    mfd_msg_init(&q);
    mfd_msg_add(&q, MFD_MSG_STEP_AHEAD, NULL, NULL, NULL, NULL, NULL);   /* oldest */
    mfd_msg_add(&q, MFD_MSG_STEP_DELETED, NULL, NULL, NULL, NULL, NULL); /* will be cleared */
    mfd_msg_add(&q, MFD_MSG_TD_REACHED, NULL, NULL, NULL, NULL, NULL);   /* will be cleared */
    for (i = 0; i < MFD_MSG_QUEUE_CAP - 3; i++) {
        char alt[16];
        snprintf(alt, sizeof alt, "%d", 10000 + i * 100);
        mfd_msg_add(&q, MFD_MSG_NEW_CRZ_ALT, alt, NULL, NULL, NULL, NULL);
    }
    CHECK(q.count == MFD_MSG_QUEUE_CAP);
    for (i = MFD_MSG_QUEUE_CAP - 1; i >= 0; i--)
        if (q.e[i].id == MFD_MSG_STEP_DELETED || q.e[i].id == MFD_MSG_TD_REACHED) q.e[i].cleared = 1;
    mfd_msg_add(&q, MFD_MSG_CHECK_ZFW, NULL, NULL, NULL, NULL, NULL);
    CHECK(q.count == MFD_MSG_QUEUE_CAP && q.e[0].id == MFD_MSG_CHECK_ZFW);
    CHECK(!mfd_msg_has_id(&q, MFD_MSG_STEP_DELETED) && mfd_msg_has_id(&q, MFD_MSG_TD_REACHED));
    CHECK(mfd_msg_has_id(&q, MFD_MSG_STEP_AHEAD));
    mfd_msg_add(&q, MFD_MSG_ENTER_DEST_DATA, NULL, NULL, NULL, NULL, NULL);
    CHECK(!mfd_msg_has_id(&q, MFD_MSG_TD_REACHED) && mfd_msg_has_id(&q, MFD_MSG_STEP_AHEAD));
    mfd_msg_add(&q, MFD_MSG_INITIALIZE_ZFW_ZFWCG, NULL, NULL, NULL, NULL, NULL); /* none cleared: oldest */
    CHECK(q.count == MFD_MSG_QUEUE_CAP && !mfd_msg_has_id(&q, MFD_MSG_STEP_AHEAD));
    CHECK(q.e[0].id == MFD_MSG_INITIALIZE_ZFW_ZFWCG);

    /* remove by text (first match) and by id ([PORT] Q13) */
    CHECK(!mfd_msg_remove_text(&q, "NEW CRZ ALT: HHHHH")); /* FBW bug: template text never matches */
    CHECK(mfd_msg_remove_text(&q, "NEW CRZ ALT: 10500"));
    CHECK(!mfd_msg_remove_text(&q, "NEW CRZ ALT: 10500"));
    n = mfd_msg_remove_id(&q, MFD_MSG_NEW_CRZ_ALT);
    CHECK(n == MFD_MSG_QUEUE_CAP - 3 - 1); /* 13 added, none evicted, one removed by text */
    CHECK(!mfd_msg_has_id(&q, MFD_MSG_NEW_CRZ_ALT));
    CHECK(mfd_msg_remove_id(&q, MFD_MSG_NEW_CRZ_ALT) == 0);

    /* MSG LIST rows: newest first, cleared included */
    mfd_msg_init(&q);
    mfd_msg_add(&q, MFD_MSG_STEP_AHEAD, NULL, NULL, NULL, NULL, NULL);
    mfd_msg_add(&q, MFD_MSG_STEP_DELETED, NULL, NULL, NULL, NULL, NULL);
    mfd_msg_add(&q, MFD_MSG_NOT_ALLOWED, NULL, NULL, NULL, NULL, NULL);
    mfd_msg_clear_latest(&q); /* NOT ALLOWED deleted */
    mfd_msg_clear_latest(&q); /* STEP DELETED cleared */
    CHECK(mfd_msg_list(&q, NULL, 0) == 2);
    n = mfd_msg_list(&q, rows, 4);
    CHECK(n == 2 && rows[0]->id == MFD_MSG_STEP_DELETED && rows[0]->cleared && rows[1]->id == MFD_MSG_STEP_AHEAD);
    CHECK(mfd_msg_list(&q, rows, 1) == 1 && rows[0]->id == MFD_MSG_STEP_DELETED);
    CHECK(mfd_msg_footer_entry(&q) == &q.e[1]);

    /* showFmsErrorMessage mapping */
    mfd_msg_init(&q);
    mfd_err_set_range(&e, "90", "340", "KT");
    mfd_msg_show_error(&q, &e, 1);
    CHECK_STR(q.e[0].text, "ENTRY OUT OF RANGE\nRNG: 90 TO 340 KT");
    mfd_msg_show_error(&q, &e, 0);
    CHECK_STR(q.e[0].text, "ENTRY OUT OF RANGE");
    snprintf(e.details, sizeof e.details, "SOMETHING");
    e.type = MFD_ERR_NOT_IN_DATABASE;
    mfd_msg_show_error(&q, &e, 1);
    CHECK_STR(q.e[0].text, "NOT IN DATABASE");
    e.type = MFD_ERR_NOT_YET_IMPLEMENTED;
    mfd_msg_show_error(&q, &e, 1);
    CHECK_STR(q.e[0].text, "NOT YET IMPLEMENTED");
    mfd_err_set_plain(&e, MFD_ERR_NOT_ALLOWED);
    mfd_msg_show_error(&q, &e, 1);
    CHECK_STR(q.e[0].text, "NOT ALLOWED");
    n = q.count;
    mfd_err_set_plain(&e, MFD_ERR_NONE);
    mfd_msg_show_error(&q, &e, 1);
    mfd_msg_show_error(&q, NULL, 1);
    CHECK(q.count == n);

    /* token substitution in the queue; Type I with token */
    mfd_msg_add(&q, MFD_MSG_XXX_IS_DESELECTED, "TGO", NULL, NULL, NULL, NULL);
    CHECK_STR(q.e[0].text, "TGO IS DESELECTED");
    mfd_msg_add(&q, MFD_MSG_NEW_THR_RED_ALT, "1500", NULL, NULL, NULL, NULL);
    CHECK_STR(q.e[0].text, "NEW THR RED ALT: 1500");

    /* on_clear may modify the queue */
    mfd_msg_init(&q);
    g_reentrant_q = &q;
    mfd_msg_add(&q, MFD_MSG_FORMAT_ERROR, NULL, NULL, NULL, NULL, NULL);
    mfd_msg_add(&q, MFD_MSG_TD_REACHED, NULL, NULL, NULL, on_clear_reentrant, NULL);
    mfd_msg_clear_latest(&q);
    CHECK(q.count == 2 && q.e[0].id == MFD_MSG_NOT_ALLOWED && q.e[1].id == MFD_MSG_TD_REACHED && q.e[1].cleared);

    /* reset keeps the version monotonic */
    v0 = q.version;
    mfd_msg_reset(&q);
    CHECK(q.count == 0 && q.version == v0 + 1);

    /* invalid ids are ignored */
    mfd_msg_add(&q, MFD_MSG_COUNT, NULL, NULL, NULL, NULL, NULL);
    CHECK(q.count == 0);

    /* ATCCOM queue: same rules, NX error messages, ATCCOM catalogue */
    {
        MfdMsgQueue atc;
        mfd_msg_init(&atc);
        mfd_msg_add(&atc, MFD_MSG_ATC_DATIS_NO_REPLY, NULL, NULL, NULL, NULL, NULL);
        mfd_err_set_format(&e, "AAAA", NULL);
        mfd_msg_show_error(&atc, &e, 1);
        CHECK(atc.count == 2 && atc.e[0].id == MFD_MSG_FORMAT_ERROR);
        CHECK_STR(atc.e[0].text, "FORMAT ERROR\nFORMAT: AAAA");
        CHECK_STR(atc.e[1].text, "D-ATIS NO REPLY");
        mfd_msg_clear_latest(&atc);
        mfd_msg_clear_latest(&atc);
        CHECK(atc.count == 0);
    }
}

/* ---- Locale independence ------------------------------------------------------------------------------ */

static void test_locale(void)
{
    static const char *names[] = {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "German_Germany.1252"};
    size_t i;
    const char *ok = NULL;
    for (i = 0; i < sizeof names / sizeof names[0] && !ok; i++) ok = setlocale(LC_NUMERIC, names[i]);
    if (!ok) {
        printf("test_mfd_formats: comma-decimal locale unavailable, locale test skipped\n");
        return;
    }
    {
        MfdFmt q = F(MFD_FMT_QNH);
        MfdFmt w = FR(MFD_FMT_WEIGHT, 270000, 373000);
        double v;
        TO_FIXED(300.25, 1, "300.3");
        TO_FIXED(1234.5678, 2, "1234.57");
        TO_STRING(0.84, "0.84");
        TO_STRING(1.5e-7, "1.5e-7");
        CHECK(mfd_js_number("29.92", &v) && v == 29.92);
        CHECK(mfd_strict_number("0.84", &v) && v == 0.84);
        CHECK(mfd_js_parse_float("29.92X") == 29.92);
        EXPECT_OK(q, "29.92", 29.92, "29.92", "", "");
        EXPECT_ERR(w, "250", "ENTRY OUT OF RANGE\nRNG: 270.0 TO 373.0 T");
    }
    setlocale(LC_NUMERIC, "C");
}

int main(void)
{
    test_js_emulation();
    test_simple_numeric();
    test_mach();
    test_alt_or_fl();
    test_flight_level();
    test_radio_alt_tropo();
    test_length_weight();
    test_crz_temp_trip_wind();
    test_qnh();
    test_misc_numeric();
    test_text();
    test_descriptor();
    test_wrappers();
    test_catalogue();
    test_queue();
    test_locale();
    printf("test_mfd_formats: %d/%d checks passed%s%s\n", g_checks - g_fail, g_checks,
#ifdef MFD_FMT_FAITHFUL_JS_NUMBER
           " [faithful JS Number]",
#else
           "",
#endif
#ifdef MFD_MSG_DEDUP_UNCLEARED
           " [dedup uncleared]"
#else
           ""
#endif
    );
    return g_fail ? 1 : 0;
}
