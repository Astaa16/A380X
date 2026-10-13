/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD data-entry formats. Simulator-independent translation of the pinned
 * FlyByWire A380X MFD DataEntryFormats.tsx (docs/mfd-spec/02-data-formats-messages.md).
 * No XPLM, no OpenGL, no allocation; all buffers are caller-owned.
 */
#ifndef A380X_MFD_FORMATS_H
#define A380X_MFD_FORMATS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Errors and results (spec 02 §11.1) ---------------------------------------- */

typedef enum {
    MFD_ERR_NONE = 0,
    MFD_ERR_NOT_IN_DATABASE,
    MFD_ERR_NOT_YET_IMPLEMENTED,
    MFD_ERR_FORMAT_ERROR,
    MFD_ERR_ENTRY_OUT_OF_RANGE,
    /* Port extension: FMS entry functions report rule violations through the same type. */
    MFD_ERR_NOT_ALLOWED
} MfdFmsErrorType;

#define MFD_ERR_DETAILS_LEN 64
typedef struct {
    MfdFmsErrorType type;
    char details[MFD_ERR_DETAILS_LEN]; /* "" -> single-line message */
} MfdFmsError;

typedef enum { MFD_PARSE_ERROR = -1, MFD_PARSE_CLEAR = 0, MFD_PARSE_OK = 1 } MfdParseStatus;

typedef struct {
    char value[32];               /* UTF-8 */
    char lead[8];                 /* leading unit, "" if none */
    char trail[8];                /* trailing unit, "" if none */
    unsigned char is_placeholder; /* renderer substitutes U+25AF boxes when the field is mandatory */
} MfdFieldText;

/* ---- Helpers (spec 02 §11.2) ---------------------------------------------------- */

int    mfd_js_number(const char *s, double *out);
int    mfd_strict_number(const char *s, double *out);
double mfd_js_parse_float(const char *s);
void   mfd_js_to_fixed(double v, int digits, char *buf, size_t n);
void   mfd_js_to_string(double v, char *buf, size_t n);
void   mfd_pad_start(char *buf, size_t n, size_t width, char pad);
double mfd_js_round(double v);
void   mfd_err_set_format(MfdFmsError *e, const char *required_format, const char *unit);
void   mfd_err_set_range(MfdFmsError *e, const char *lo, const char *hi, const char *unit);
void   mfd_err_set_plain(MfdFmsError *e, MfdFmsErrorType t);

/* ---- Descriptor-driven dispatch (spec 02 §11.4) ----------------------------------- */

typedef enum {
    MFD_FMT_SPEED_KT, MFD_FMT_SPEED_MACH, MFD_FMT_ALT_OR_FL, MFD_FMT_ALTITUDE, MFD_FMT_FLIGHT_LEVEL,
    MFD_FMT_RADIO_ALT, MFD_FMT_TROPO, MFD_FMT_LENGTH, MFD_FMT_WEIGHT, MFD_FMT_PERCENT,
    MFD_FMT_TEMPERATURE, MFD_FMT_CRZ_TEMP, MFD_FMT_WIND_DIR, MFD_FMT_WIND_SPEED, MFD_FMT_TRIP_WIND,
    MFD_FMT_QNH, MFD_FMT_COST_INDEX, MFD_FMT_VERTICAL_SPEED, MFD_FMT_DESCENT_RATE, MFD_FMT_PAX_NBR,
    MFD_FMT_TIME_HHMM, MFD_FMT_TIME_HHMMSS, MFD_FMT_LATITUDE, MFD_FMT_HEADING, MFD_FMT_INBOUND_COURSE,
    MFD_FMT_HOLD_DIST, MFD_FMT_HOLD_TIME, MFD_FMT_FREQ_ILS, MFD_FMT_FREQ_VOR, MFD_FMT_FREQ_ADF,
    MFD_FMT_LS_COURSE, MFD_FMT_SQUAWK, MFD_FMT_RADIAL, MFD_FMT_RADIUS, MFD_FMT_RNP, MFD_FMT_FUEL_PENALTY,
    MFD_FMT_TXT_FIX, MFD_FMT_TXT_AIRPORT, MFD_FMT_TXT_NAVAID, MFD_FMT_TXT_AIRWAY, MFD_FMT_TXT_DROPDOWN,
    MFD_FMT_TXT_WAYPOINT, MFD_FMT_TXT_LONG_ALNUM,
    MFD_FMT_COUNT
} MfdFmtKind;

typedef struct {
    MfdFmtKind kind;
    double min, max;            /* storage units; refreshed by the page before parse */
    int    metric;              /* LENGTH / WEIGHT */
    int    max_digits;          /* 0 = default from table; LENGTH(5) and DROPDOWN(n) override */
    int    has_trans;           /* ALT_OR_FL */
    double trans;               /* ALT_OR_FL: ft, or FL if trans_is_fl */
    int    trans_is_fl;
    int    qnh_is_hpa;          /* QNH placeholder */
    const char *placeholder;    /* NULL = default; NAVAID deselected list uses "-" */
    unsigned char err_details;  /* 1 = pass details to the message (spec 02 §1.7) */
} MfdFmt;

/* A field value. Numeric formats use num; text formats use str. */
typedef struct { int has; double num; char str[25]; } MfdValue;

const char    *mfd_fmt_placeholder(const MfdFmt *f);
int            mfd_fmt_max_digits(const MfdFmt *f);
int            mfd_fmt_max_overflow(const MfdFmt *f);   /* FLIGHT_LEVEL: 2, else 0 */
const char    *mfd_fmt_error_unit(const MfdFmt *f);
const char    *mfd_fmt_lead_unit(const MfdFmt *f);      /* "" if none */
const char    *mfd_fmt_trail_unit(const MfdFmt *f);     /* "" if none */
MfdParseStatus mfd_fmt_parse(const MfdFmt *f, const char *in, MfdValue *out, MfdFmsError *err);
void           mfd_fmt_render(const MfdFmt *f, const MfdValue *v, MfdFieldText *out);

/* Text helpers (spec 02 §11.3). */
int mfd_is_place_format(const char *s);  /* ^[A-Z0-9]{2,7}$ */
int mfd_is_runway_format(const char *s); /* ^[A-Z]{4}[0-9]{2}[LCR]?$ */

/* ==== WP-B additions (appended; the declarations above are frozen) ============================
 *
 * Contract of the declarations above, as implemented in a380x_mfd_formats.c:
 *
 * mfd_js_number()      Faithful JS Number(): trims JS white space, accepts decimal (with exponent),
 *                      [+-]Infinity, unsigned 0x/0o/0b integers; "" -> 0. Returns 1 and *out, or 0 and
 *                      *out = NAN.
 * mfd_strict_number()  [PORT] default grammar [+-]?([0-9]+(\.[0-9]*)?|\.[0-9]+) inside optional ASCII
 *                      white space, same return convention. Blank input is NaN (JS gives 0).
 *                      mfd_fmt_parse() uses it unless the build defines MFD_FMT_FAITHFUL_JS_NUMBER
 *                      (spec 02 §2.1, Q1).
 * mfd_js_parse_float() JS parseFloat(): longest decimal prefix after leading white space, NAN if none.
 * mfd_js_to_fixed()    JS toFixed(): exact binary value, exact ties away from zero, "-" iff v < 0,
 *                      "NaN", "Infinity", |v| >= 1e21 -> toString(). Locale independent.
 * mfd_js_to_string()   JS Number toString() (shortest round trip, JS exponent rules).
 * mfd_pad_start()      JS padStart in place; never truncates the existing text (bounded by n).
 * mfd_js_round()       JS Math.round() = floor(v + 0.5), evaluated without the addition rounding error.
 * mfd_err_set_*()      Fill an MfdFmsError (details "FORMAT: <fmt>[ <unit>]" / "RNG: <lo> TO <hi>[ <unit>]",
 *                      empty unit -> no suffix; plain -> details "").
 *
 * MfdFmt.min / max     Used only by kinds whose FBW constructor takes a range: SPEED_KT, SPEED_MACH, ALT_OR_FL,
 *                      ALTITUDE, FLIGHT_LEVEL, RADIO_ALT, LENGTH, WEIGHT, PERCENT, TEMPERATURE, VERTICAL_SPEED,
 *                      DESCENT_RATE, HEADING, INBOUND_COURSE, HOLD_DIST, HOLD_TIME. min == max == 0 selects the
 *                      format default (spec 02 §3; [PORT] Q3: HEADING 0..359.9, INBOUND_COURSE 0..360, HOLD_DIST
 *                      0..99.9, HOLD_TIME 0..9.9). Every other kind has a fixed range and ignores min/max.
 * MfdFmt.metric        LENGTH: 1 = M, 0 = FT. WEIGHT: 1 = T (kg storage), 0 = KLB. Storage is always m / kg.
 * MfdFmt.max_digits    > 0 overrides the table for every kind; LENGTH and DROPDOWN placeholders are that many
 *                      dashes (DROPDOWN default 6).
 * MfdFmt.placeholder   non-NULL overrides the placeholder of any kind (also for clear-by-placeholder).
 * MfdFmt.qnh_is_hpa    QNH placeholder only ("----" when 1, "--.--" when 0); parse does not depend on it.
 * MfdFmt.err_details   not used by mfd_fmt_parse(), which always fills details when the format has them; the
 *                      caller passes it on: mfd_msg_show_error(q, &err, f->err_details).
 * mfd_fmt_parse()      MFD_PARSE_OK: out->has = 1 and out->num (numeric) or out->str (text) set.
 *                      MFD_PARSE_CLEAR: out set to "no value". MFD_PARSE_ERROR: *err filled, *out untouched.
 *                      err->type is MFD_ERR_NONE on success. in == NULL is treated as "".
 * mfd_fmt_render()     v == NULL or !v->has (text: empty str) -> placeholder with is_placeholder = 1 and the
 *                      units FBW shows while empty. FBW falsy rule kept for SPEED_MACH (0 -> placeholder);
 *                      [PORT] Q6: TIME_HHMM / TIME_HHMMSS render 0 as 00:00 / 00:00:00.
 * mfd_fmt_error_unit() FBW `unit` member (UTF-8; widths must count code points, "°C" is 2).
 * mfd_fmt_lead_unit(), mfd_fmt_trail_unit()
 *                      Units shown next to a non-null value in the base case. Value-dependent cases
 *                      (ALT_OR_FL at/above transition -> lead "FL", TRIP_WIND lead "TL"/"HD", RADIO_ALT NO DH)
 *                      need mfd_fmt_render().
 */

/* Unit constants ([PORT] spec 02 §8: exact definitions instead of msfs-sdk 0.453592). */
#define MFD_KG_PER_LB    0.45359237
#define MFD_M_PER_FT     0.3048
#define MFD_HPA_PER_INHG 33.863886666667 /* fbw-common units.ts inchOfMercuryToHectopascal */

/* QnhUtils.ts: a stored QNH below 100 is inHg, otherwise hPa. */
int    mfd_qnh_is_inhg(double qnh);
double mfd_qnh_to_millibar(double qnh);

/* Descriptor helpers. mfd_fmt_init() zeroes *f and sets kind, the default range, metric = 1,
 * qnh_is_hpa = 1 and err_details = 1 (the standard FBW error handler). */
void mfd_fmt_init(MfdFmt *f, MfdFmtKind kind);
/* Effective numeric range in storage units; returns 0 (and leaves *lo and *hi untouched) for kinds without a
 * single numeric range (QNH, SQUAWK, text kinds). */
int  mfd_fmt_range(const MfdFmt *f, double *lo, double *hi);
int  mfd_fmt_is_text(MfdFmtKind kind);

/* JS String.replace(token, value) with a string pattern: replaces the first occurrence only (no-op when
 * token is empty or absent). Returns 1 if replaced. The result is truncated to n - 1 bytes. */
int mfd_str_replace_first(char *buf, size_t n, const char *token, const char *value);

/* ---- Per-format functions (spec 02 §11.3) --------------------------------------------------------
 * Thin wrappers over mfd_fmt_parse()/mfd_fmt_render() with a descriptor from mfd_fmt_init().
 * Parse: on MFD_PARSE_OK *out is the value in storage units; on MFD_PARSE_CLEAR and MFD_PARSE_ERROR *out
 * is untouched. min == max == 0 selects the default range. */
#define MFD_FMT_DECL_DYN(name)                                                                        \
    MfdParseStatus mfd_fmt_parse_##name(const char *in, double min, double max, double *out,          \
                                        MfdFmsError *err);                                            \
    void mfd_fmt_render_##name(int has_value, double v, MfdFieldText *out);
#define MFD_FMT_DECL_FIXED(name)                                                                      \
    MfdParseStatus mfd_fmt_parse_##name(const char *in, double *out, MfdFmsError *err);               \
    void mfd_fmt_render_##name(int has_value, double v, MfdFieldText *out);

MFD_FMT_DECL_DYN(speed_kt)
MFD_FMT_DECL_DYN(speed_mach)
MFD_FMT_DECL_DYN(altitude)
MFD_FMT_DECL_DYN(flight_level)
MFD_FMT_DECL_DYN(radio_alt)      /* result 0 = NO DH */
MFD_FMT_DECL_DYN(percent)
MFD_FMT_DECL_DYN(temperature)
MFD_FMT_DECL_DYN(vertical_speed)
MFD_FMT_DECL_DYN(descent_rate)
MFD_FMT_DECL_DYN(heading)
MFD_FMT_DECL_DYN(inbound_course)
MFD_FMT_DECL_DYN(hold_dist)
MFD_FMT_DECL_DYN(hold_time)
MFD_FMT_DECL_FIXED(tropo)
MFD_FMT_DECL_FIXED(crz_temp)
MFD_FMT_DECL_FIXED(wind_dir)
MFD_FMT_DECL_FIXED(wind_speed)
MFD_FMT_DECL_FIXED(trip_wind)    /* + = tailwind */
MFD_FMT_DECL_FIXED(cost_index)
MFD_FMT_DECL_FIXED(pax_nbr)
MFD_FMT_DECL_FIXED(time_hhmm)    /* minutes */
MFD_FMT_DECL_FIXED(time_hhmmss)  /* seconds */
MFD_FMT_DECL_FIXED(latitude)
MFD_FMT_DECL_FIXED(freq_ils)
MFD_FMT_DECL_FIXED(freq_vor)
MFD_FMT_DECL_FIXED(freq_adf)
MFD_FMT_DECL_FIXED(ls_course)    /* negative = back course (never produced by the [PORT] parser) */
MFD_FMT_DECL_FIXED(squawk)       /* decimal-digit integer: code 0123 -> 123 */
MFD_FMT_DECL_FIXED(radial)
MFD_FMT_DECL_FIXED(radius)
MFD_FMT_DECL_FIXED(rnp)
MFD_FMT_DECL_FIXED(fuel_penalty)

#undef MFD_FMT_DECL_DYN
#undef MFD_FMT_DECL_FIXED

/* Formats with extra context. */
MfdParseStatus mfd_fmt_parse_alt_or_fl(const char *in, double min_ft, double max_ft, double *out,
                                       MfdFmsError *err);
void mfd_fmt_render_alt_or_fl(int has_value, double v_ft, int has_trans, double trans_ft_or_fl,
                              int trans_is_fl, MfdFieldText *out);
MfdParseStatus mfd_fmt_parse_length(const char *in, double min_m, double max_m, int metric, double *out_m,
                                    MfdFmsError *err);
void mfd_fmt_render_length(int has_value, double v_m, int metric, int max_digits, MfdFieldText *out);
MfdParseStatus mfd_fmt_parse_weight(const char *in, double min_kg, double max_kg, int metric, double *out_kg,
                                    MfdFmsError *err);
void mfd_fmt_render_weight(int has_value, double v_kg, int metric, MfdFieldText *out);
/* Result is inHg (< 100) or hPa; convert with mfd_qnh_to_millibar(). */
MfdParseStatus mfd_fmt_parse_qnh(const char *in, double *out, MfdFmsError *err);
void mfd_fmt_render_qnh(int has_value, double v, int is_hpa, MfdFieldText *out);

/* Text formats: kind is one of MFD_FMT_TXT_*. max_digits 0 = default, placeholder NULL = default.
 * On MFD_PARSE_OK the string is copied to out (truncated to n - 1); on MFD_PARSE_CLEAR out = "". */
MfdParseStatus mfd_fmt_parse_text(MfdFmtKind kind, const char *in, int max_digits, const char *placeholder,
                                  char *out, size_t n, MfdFmsError *err);
void mfd_fmt_render_text(const char *value_or_null, const char *placeholder, MfdFieldText *out);

#ifdef __cplusplus
}
#endif

#endif
