/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X navigation database service. See a380x_navdata.h and docs/mfd-spec/11-xplane-navdata.md.
 * Plain C11 + stdio. Robust to CRLF, a UTF-8 BOM, over-long and malformed lines (skipped and counted).
 */
#include "a380x_navdata.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEG2RAD (A380X_NAV_PI / 180.0)
#define RAD2DEG (180.0 / A380X_NAV_PI)
#define LINE_MAX_LEN 1200

/* ============================================================================================ */
/* Internal records                                                                             */
/* ============================================================================================ */

typedef struct {            /* one fix / navaid / airport, 40 bytes */
    float   lat, lon;
    int32_t next;           /* ident hash chain */
    int32_t extra;          /* NavExtra index (navaids) or AptMeta index (airports), -1 */
    char    ident[8];
    char    area[5];        /* terminal airport, "" = enroute */
    char    region[3];
    uint8_t kind;
    uint8_t flags;
    char    wpt_type[3];
} NavPoint;

typedef struct {
    float   elev_ft, freq, range_nm, station_var, loc_true, loc_mag, gs_angle;
    float   dme_lat, dme_lon;
    int32_t name_off;       /* string pool offset, -1 */
    char    runway[4];      /* LOC runway designator */
} NavExtra;

typedef struct {
    double lat, lon;
    float  elev_ft, ta_ft, tl_ft, longest_ft;
    char   icao[8];
    char   region[3];
    char   cls;
} AptMeta;

typedef struct {            /* airway segment, one per airway name */
    char    id1[8], id2[8];
    char    rg1[3], rg2[3];
    uint8_t t1, t2;         /* 11 fix, 2 NDB, 3 VHF */
    char    dir;            /* 'N', 'F', 'B' */
    uint8_t level;          /* bit0 low, bit1 high */
    uint16_t base, top;     /* hundreds of feet */
    int32_t name;
    int32_t next_in_name;
} AwySeg;

typedef struct {
    char    name[8];
    int32_t first_seg;
    int32_t next;           /* name hash chain */
    int32_t seg_count;
} AwyName;

typedef struct {
    A380XNavHold h;
    int32_t next;
} HoldRec;

typedef struct {            /* pending earth_nav secondary row (DME, GS) */
    int     row;
    float   lat, lon, elev, freq, range, value;
    char    ident[8], area[5], region[3];
    int32_t name_off;
} NavPending;

typedef struct {
    char icao[A380X_NAV_ICAO_LEN];
    A380XNavAirport *apt;   /* NULL = negative cache entry */
    unsigned long last_use;
} CifpSlot;

struct A380XNavdata {
    A380XNavdataHost host;
    int    has_host;
    char  *custom_dir, *default_dir;
    int    cifp_cap;
    size_t bytes;
    int    loaded, load_failed;
    int    malformed;
    double load_ms;
    int    custom_fix, custom_nav, custom_awy, custom_meta;
    /* points */
    NavPoint *pts; int pt_count, pt_cap;
    NavExtra *ext; int ext_count, ext_cap;
    AptMeta  *apt; int apt_count, apt_cap;
    int32_t  *bucket; uint32_t bucket_mask;
    char     *pool; int pool_len, pool_cap;
    int       waypoints, navaids;
    int32_t  *vor_idx; int vor_count;       /* VORs with a station declination */
    /* airways */
    AwySeg  *seg; int seg_count, seg_cap;
    AwyName *awn; int awn_count, awn_cap;
    int32_t *awn_bucket; uint32_t awn_mask;
    /* holds (lazy) */
    int      holds_loaded;
    HoldRec *hold; int hold_count, hold_cap;
    int32_t *hold_bucket; uint32_t hold_mask;
    /* cycle */
    A380XNavCycle cycle;
    int cycle_done;
    /* CIFP */
    CifpSlot *slots; int slot_count;
    unsigned long use_clock;
    int cifp_loads;
    uint32_t generation;
    /* magvar estimate cache, 1 degree cells (lazy) */
    float *var_grid;
    /* airway scratch */
    int32_t *scr_seg; int scr_seg_cap;
};

/* ============================================================================================ */
/* Memory accounting                                                                            */
/* ============================================================================================ */

typedef union { size_t size; double d; void *p; long double ld; } AllocHdr;

static void *nav_alloc(A380XNavdata *nav, size_t size)
{
    AllocHdr *h = (AllocHdr *)malloc(sizeof(AllocHdr) + size);
    if (!h) return NULL;
    h->size = size;
    nav->bytes += size;
    return h + 1;
}
static void nav_free(A380XNavdata *nav, void *p)
{
    if (!p) return;
    AllocHdr *h = (AllocHdr *)p - 1;
    nav->bytes -= h->size;
    free(h);
}
static void *nav_realloc(A380XNavdata *nav, void *p, size_t size)
{
    if (!p) return nav_alloc(nav, size);
    AllocHdr *h = (AllocHdr *)p - 1;
    size_t old = h->size;
    AllocHdr *n = (AllocHdr *)realloc(h, sizeof(AllocHdr) + size);
    if (!n) return NULL;
    n->size = size;
    nav->bytes = nav->bytes - old + size;
    return n + 1;
}
static void *nav_calloc(A380XNavdata *nav, size_t count, size_t size)
{
    void *p = nav_alloc(nav, count * size);
    if (p) memset(p, 0, count * size);
    return p;
}
/* Grows *arr so that index `need` fits. Returns 0 on failure. */
static int grow(A380XNavdata *nav, void **arr, int *cap, int need, size_t elem)
{
    if (need < *cap) return 1;
    int nc = *cap ? *cap : 64;
    while (nc <= need) nc *= 2;
    void *n = nav_realloc(nav, *arr, (size_t)nc * elem);
    if (!n) return 0;
    *arr = n;
    *cap = nc;
    return 1;
}
static char *nav_strdup(A380XNavdata *nav, const char *s)
{
    size_t n = strlen(s);
    char *d = (char *)nav_alloc(nav, n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

/* ============================================================================================ */
/* Small string / number helpers                                                                */
/* ============================================================================================ */

static void copy_str(char *dst, size_t cap, const char *src, size_t len)
{
    if (!cap) return;
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}
static void copy_trim(char *dst, size_t cap, const char *src)
{
    while (*src == ' ' || *src == '\t') src++;
    size_t len = strlen(src);
    while (len && (src[len - 1] == ' ' || src[len - 1] == '\t')) len--;
    copy_str(dst, cap, src, len);
}
static int is_blank(const char *s)
{
    for (; *s; s++) if (*s != ' ' && *s != '\t') return 0;
    return 1;
}
static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    return h;
}

/* Locale-independent decimal parser: [+-]digits[.digits]. Returns 1 if the whole token parsed. */
static int parse_num(const char *s, size_t len, double *out)
{
    size_t i = 0;
    int neg = 0, digits = 0;
    double v = 0.0;
    while (i < len && s[i] == ' ') i++;
    if (i < len && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
    while (i < len && s[i] >= '0' && s[i] <= '9') { v = v * 10.0 + (s[i] - '0'); i++; digits++; }
    if (i < len && s[i] == '.') {
        double scale = 0.1;
        i++;
        while (i < len && s[i] >= '0' && s[i] <= '9') { v += (s[i] - '0') * scale; scale *= 0.1; i++; digits++; }
    }
    while (i < len && s[i] == ' ') i++;
    if (!digits || i != len) return 0;
    *out = neg ? -v : v;
    return 1;
}
static int parse_cstr(const char *s, double *out) { return parse_num(s, strlen(s), out); }

/* Whitespace tokenizer that does not modify the line. */
typedef struct { const char *p[32]; int len[32]; int n; } Tokens;
static void tokenize(const char *line, Tokens *t)
{
    t->n = 0;
    const char *s = line;
    while (*s && t->n < 32) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        t->p[t->n] = s;
        while (*s && *s != ' ' && *s != '\t') s++;
        t->len[t->n] = (int)(s - t->p[t->n]);
        t->n++;
    }
}
static int tok_num(const Tokens *t, int i, double *out) { return i < t->n && parse_num(t->p[i], (size_t)t->len[i], out); }
static void tok_str(const Tokens *t, int i, char *dst, size_t cap)
{
    if (i >= t->n) { if (cap) dst[0] = 0; return; }
    copy_str(dst, cap, t->p[i], (size_t)t->len[i]);
}
/* Rest of the line starting at token i (names with spaces). */
static void tok_rest(const Tokens *t, int i, char *dst, size_t cap)
{
    if (i >= t->n) { if (cap) dst[0] = 0; return; }
    copy_trim(dst, cap, t->p[i]);
}

/* ============================================================================================ */
/* Line reader (CRLF, BOM, over-long lines)                                                     */
/* ============================================================================================ */

typedef struct {
    FILE *f;
    char  buf[LINE_MAX_LEN];
    long  line_no;
    int   too_long;
} LineReader;

static FILE *open_file(const char *dir, const char *name)
{
    char path[1024];
    if (!dir || !*dir) return NULL;
    size_t dl = strlen(dir);
    int sep = dl && (dir[dl - 1] == '/' || dir[dl - 1] == '\\');
    if ((size_t)snprintf(path, sizeof path, "%s%s%s", dir, sep ? "" : "/", name) >= sizeof path) return NULL;
    return fopen(path, "rb");
}
static int lr_open(LineReader *lr, const char *dir, const char *name)
{
    memset(lr, 0, sizeof *lr);
    lr->f = open_file(dir, name);
    if (lr->f) setvbuf(lr->f, NULL, _IOFBF, 1 << 16);
    return lr->f != NULL;
}
static void lr_close(LineReader *lr) { if (lr->f) fclose(lr->f); lr->f = NULL; }
/* Returns the next line without trailing CR/LF/space, or NULL at EOF. too_long is set for lines
 * longer than the buffer (the remainder is discarded). */
static char *lr_next(LineReader *lr)
{
    if (!lr->f) return NULL;
    if (!fgets(lr->buf, sizeof lr->buf, lr->f)) return NULL;
    lr->line_no++;
    size_t n = strlen(lr->buf);
    lr->too_long = 0;
    if (n && lr->buf[n - 1] != '\n' && !feof(lr->f)) {
        int c;
        lr->too_long = 1;
        while ((c = fgetc(lr->f)) != EOF && c != '\n') {}
    }
    while (n && (lr->buf[n - 1] == '\n' || lr->buf[n - 1] == '\r' || lr->buf[n - 1] == ' ' || lr->buf[n - 1] == '\t'))
        lr->buf[--n] = 0;
    char *s = lr->buf;
    if (lr->line_no == 1 && (uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF) s += 3;
    return s;
}

static void nav_log(A380XNavdata *nav, const char *msg)
{
    if (nav->has_host && nav->host.log) nav->host.log(nav->host.ctx, msg);
}

/* ============================================================================================ */
/* Geodesy                                                                                      */
/* ============================================================================================ */

double a380x_geo_norm360(double d)
{
    if (!isfinite(d)) return d;
    d = fmod(d, 360.0);
    if (d < 0) d += 360.0;
    return d;
}
double a380x_geo_angle_diff(double from, double to)
{
    double d = fmod(to - from, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}
double a380x_geo_distance_nm(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD;
    double dp = p2 - p1, dl = (lon2 - lon1) * DEG2RAD;
    double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    if (a > 1.0) a = 1.0;
    return 2.0 * atan2(sqrt(a), sqrt(1.0 - a)) * A380X_NAV_EARTH_NM;
}
double a380x_geo_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD, dl = (lon2 - lon1) * DEG2RAD;
    double y = sin(dl) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    return a380x_geo_norm360(atan2(y, x) * RAD2DEG);
}
void a380x_geo_destination(double lat, double lon, double brg, double dist, double *olat, double *olon)
{
    double d = dist / A380X_NAV_EARTH_NM, b = brg * DEG2RAD, p1 = lat * DEG2RAD, l1 = lon * DEG2RAD;
    double p2 = asin(sin(p1) * cos(d) + cos(p1) * sin(d) * cos(b));
    double l2 = l1 + atan2(sin(b) * sin(d) * cos(p1), cos(d) - sin(p1) * sin(p2));
    *olat = p2 * RAD2DEG;
    double lo = fmod(l2 * RAD2DEG + 540.0, 360.0) - 180.0;
    *olon = lo;
}
void a380x_geo_along_cross(double lat1, double lon1, double lat2, double lon2, double plat, double plon,
                           double *along, double *cross)
{
    double d13 = a380x_geo_distance_nm(lat1, lon1, plat, plon) / A380X_NAV_EARTH_NM;
    double t13 = a380x_geo_bearing_deg(lat1, lon1, plat, plon) * DEG2RAD;
    double t12 = a380x_geo_bearing_deg(lat1, lon1, lat2, lon2) * DEG2RAD;
    double s = sin(d13) * sin(t13 - t12);
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    double xt = asin(s);
    double c = cos(xt);
    double at = c > 1e-12 ? cos(d13) / c : 1.0;
    if (at > 1) at = 1;
    if (at < -1) at = -1;
    at = acos(at);
    if (cos(t13 - t12) < 0) at = -at;
    if (along) *along = at * A380X_NAV_EARTH_NM;
    if (cross) *cross = xt * A380X_NAV_EARTH_NM;
}

typedef struct { double x, y, z; } V3;
static V3 v3_ll(double lat, double lon)
{
    double p = lat * DEG2RAD, l = lon * DEG2RAD;
    V3 v = { cos(p) * cos(l), cos(p) * sin(l), sin(p) };
    return v;
}
static V3 v3_cross(V3 a, V3 b) { V3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; return r; }
static double v3_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static double v3_len(V3 a) { return sqrt(v3_dot(a, a)); }
static V3 v3_dir(double lat, double lon, double crs)   /* unit tangent of a course */
{
    double p = lat * DEG2RAD, l = lon * DEG2RAD, c = crs * DEG2RAD;
    V3 north = { -sin(p) * cos(l), -sin(p) * sin(l), cos(p) };
    V3 east = { -sin(l), cos(l), 0 };
    V3 d = { north.x * cos(c) + east.x * sin(c), north.y * cos(c) + east.y * sin(c), north.z * cos(c) + east.z * sin(c) };
    return d;
}
static int geo_intersect_ex(double lat1, double lon1, double crs1, double lat2, double lon2, double crs2,
                            int ahead2, double *olat, double *olon)
{
    V3 p1 = v3_ll(lat1, lon1), p2 = v3_ll(lat2, lon2);
    V3 d1 = v3_dir(lat1, lon1, crs1), d2 = v3_dir(lat2, lon2, crs2);
    V3 g1 = v3_cross(p1, d1), g2 = v3_cross(p2, d2);
    V3 i = v3_cross(g1, g2);
    double n = v3_len(i);
    if (n < 1e-9) return 0;
    i.x /= n; i.y /= n; i.z /= n;
    if (v3_dot(i, d1) < 0) { i.x = -i.x; i.y = -i.y; i.z = -i.z; }
    if (v3_dot(i, d1) <= 0) return 0;
    if (ahead2 && v3_dot(i, d2) <= 0) return 0;
    *olat = asin(i.z > 1 ? 1 : (i.z < -1 ? -1 : i.z)) * RAD2DEG;
    *olon = atan2(i.y, i.x) * RAD2DEG;
    return 1;
}
int a380x_geo_intersect(double lat1, double lon1, double crs1, double lat2, double lon2, double crs2,
                        double *olat, double *olon)
{
    return geo_intersect_ex(lat1, lon1, crs1, lat2, lon2, crs2, 0, olat, olon);
}

/* ============================================================================================ */
/* Point table and hash                                                                         */
/* ============================================================================================ */

static int pool_add(A380XNavdata *nav, const char *s)
{
    if (!s || !*s) return -1;
    int n = (int)strlen(s) + 1;
    if (!grow(nav, (void **)&nav->pool, &nav->pool_cap, nav->pool_len + n, 1)) return -1;
    memcpy(nav->pool + nav->pool_len, s, (size_t)n);
    int off = nav->pool_len;
    nav->pool_len += n;
    return off;
}

static int add_point(A380XNavdata *nav, const NavPoint *p)
{
    if (!grow(nav, (void **)&nav->pts, &nav->pt_cap, nav->pt_count, sizeof(NavPoint))) return -1;
    nav->pts[nav->pt_count] = *p;
    nav->pts[nav->pt_count].next = -1;
    return nav->pt_count++;
}

static void build_hash(A380XNavdata *nav)
{
    uint32_t n = 1024;
    while (n < (uint32_t)nav->pt_count + (uint32_t)nav->pt_count / 2) n <<= 1;
    nav_free(nav, nav->bucket);
    nav->bucket = (int32_t *)nav_alloc(nav, n * sizeof(int32_t));
    if (!nav->bucket) { nav->bucket_mask = 0; return; }
    for (uint32_t i = 0; i < n; i++) nav->bucket[i] = -1;
    nav->bucket_mask = n - 1;
    /* insert in reverse so chains are in file order */
    for (int i = nav->pt_count - 1; i >= 0; i--) {
        uint32_t h = hash_str(nav->pts[i].ident) & nav->bucket_mask;
        nav->pts[i].next = nav->bucket[h];
        nav->bucket[h] = i;
    }
}
static int32_t chain_head(const A380XNavdata *nav, const char *ident)
{
    if (!nav->bucket) return -1;
    return nav->bucket[hash_str(ident) & nav->bucket_mask];
}

/* ============================================================================================ */
/* Global file loaders                                                                          */
/* ============================================================================================ */

static int header_cycle(const char *line, char out[5])
{
    const char *p = strstr(line, "data cycle ");
    if (!p) return 0;
    p += 11;
    for (int i = 0; i < 4; i++) if (p[i] < '0' || p[i] > '9') return 0;
    memcpy(out, p, 4);
    out[4] = 0;
    return 1;
}

static int load_fix(A380XNavdata *nav, const char *dir)
{
    LineReader lr;
    if (!lr_open(&lr, dir, "earth_fix.dat")) return 0;
    char *line;
    Tokens t;
    while ((line = lr_next(&lr)) != NULL) {
        if (lr.line_no <= 2) continue;          /* "I" + version line */
        if (!line[0]) continue;
        if (!strcmp(line, "99")) break;
        if (lr.too_long) { nav->malformed++; continue; }
        tokenize(line, &t);
        double lat, lon;
        if (t.n < 5 || !tok_num(&t, 0, &lat) || !tok_num(&t, 1, &lon) || fabs(lat) > 90.0 || fabs(lon) > 180.0
            || t.len[2] < 1 || t.len[2] > 7) { nav->malformed++; continue; }
        NavPoint p;
        memset(&p, 0, sizeof p);
        p.lat = (float)lat; p.lon = (float)lon; p.extra = -1;
        p.kind = A380X_NAV_WAYPOINT;
        tok_str(&t, 2, p.ident, sizeof p.ident);
        char area[16];
        tok_str(&t, 3, area, sizeof area);
        if (strcmp(area, "ENRT")) { copy_str(p.area, sizeof p.area, area, strlen(area)); p.flags |= A380X_NAVF_TERMINAL; }
        tok_str(&t, 4, p.region, sizeof p.region);
        double ty;
        if (tok_num(&t, 5, &ty)) {
            uint32_t v = (uint32_t)ty;
            p.wpt_type[0] = (char)(v & 0xFF); p.wpt_type[1] = (char)((v >> 8) & 0xFF); p.wpt_type[2] = (char)((v >> 16) & 0xFF);
        }
        if (add_point(nav, &p) < 0) break;
        nav->waypoints++;
    }
    lr_close(&lr);
    return 1;
}

static int name_has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), s = strlen(suffix);
    return n >= s && !strcmp(name + n - s, suffix);
}

static int load_nav(A380XNavdata *nav, const char *dir, char cycle_out[5])
{
    LineReader lr;
    if (!lr_open(&lr, dir, "earth_nav.dat")) return 0;
    NavPending *pend = NULL;
    int pend_count = 0, pend_cap = 0;
    char *line;
    Tokens t;
    while ((line = lr_next(&lr)) != NULL) {
        if (lr.line_no == 2) { header_cycle(line, cycle_out); continue; }
        if (lr.line_no < 2 || !line[0]) continue;
        if (!strcmp(line, "99")) break;
        if (lr.too_long) { nav->malformed++; continue; }
        tokenize(line, &t);
        double row, lat, lon, elev, freq, range, value;
        if (t.n < 10 || !tok_num(&t, 0, &row) || !tok_num(&t, 1, &lat) || !tok_num(&t, 2, &lon)
            || !tok_num(&t, 3, &elev) || !tok_num(&t, 4, &freq) || !tok_num(&t, 5, &range) || !tok_num(&t, 6, &value)
            || fabs(lat) > 90.0 || fabs(lon) > 180.0 || t.len[7] < 1 || t.len[7] > 7) { nav->malformed++; continue; }
        int r = (int)row;
        char ident[8], area[16], region[3], name[A380X_NAV_NAME_LEN], rwy[8] = "";
        tok_str(&t, 7, ident, sizeof ident);
        tok_str(&t, 8, area, sizeof area);
        tok_str(&t, 9, region, sizeof region);
        if (!strcmp(area, "ENRT")) area[0] = 0;
        if (r == 4 || r == 5 || r == 6) { tok_str(&t, 10, rwy, sizeof rwy); tok_rest(&t, 11, name, sizeof name); }
        else tok_rest(&t, 10, name, sizeof name);
        if (r == 2 || r == 3 || r == 4 || r == 5) {
            NavPoint p;
            memset(&p, 0, sizeof p);
            p.lat = (float)lat; p.lon = (float)lon;
            copy_str(p.ident, sizeof p.ident, ident, strlen(ident));
            copy_str(p.area, sizeof p.area, area, strlen(area));
            copy_str(p.region, sizeof p.region, region, strlen(region));
            if (area[0]) p.flags |= A380X_NAVF_TERMINAL;
            NavExtra e;
            memset(&e, 0, sizeof e);
            e.elev_ft = (float)elev; e.range_nm = (float)range;
            e.station_var = NAN; e.loc_true = NAN; e.loc_mag = NAN; e.gs_angle = NAN;
            e.dme_lat = (float)lat; e.dme_lon = (float)lon;
            if (r == 2) { p.kind = A380X_NAV_NDB; e.freq = (float)freq; }
            else if (r == 3) {
                e.freq = (float)(freq / 100.0);
                e.station_var = (float)value;
                if (name_has_suffix(name, "TACAN")) { p.kind = A380X_NAV_DME; p.flags |= A380X_NAVF_TACAN; }
                else {
                    p.kind = A380X_NAV_VOR;
                    p.flags |= A380X_NAVF_VOR_ONLY;
                    if (strstr(name, "VORTAC")) p.flags |= A380X_NAVF_TACAN;
                }
            } else {
                p.kind = A380X_NAV_LOC;
                e.freq = (float)(freq / 100.0);
                e.loc_true = (float)fmod(value, 360.0);
                e.loc_mag = (float)floor(value / 360.0);
                copy_str(e.runway, sizeof e.runway, rwy, strlen(rwy));
            }
            e.name_off = pool_add(nav, name);
            if (!grow(nav, (void **)&nav->ext, &nav->ext_cap, nav->ext_count, sizeof(NavExtra))) break;
            nav->ext[nav->ext_count] = e;
            p.extra = nav->ext_count++;
            if (add_point(nav, &p) < 0) break;
            nav->navaids++;
        } else if (r == 6 || r == 12 || r == 13) {
            if (!grow(nav, (void **)&pend, &pend_cap, pend_count, sizeof(NavPending))) break;
            NavPending *q = &pend[pend_count++];
            memset(q, 0, sizeof *q);
            q->row = r; q->lat = (float)lat; q->lon = (float)lon; q->elev = (float)elev;
            q->freq = (float)(freq / 100.0); q->range = (float)range; q->value = (float)value;
            copy_str(q->ident, sizeof q->ident, ident, strlen(ident));
            copy_str(q->area, sizeof q->area, area, strlen(area));
            copy_str(q->region, sizeof q->region, region, strlen(region));
            q->name_off = (r == 13) ? pool_add(nav, name) : -1;
        } /* 7/8/9 markers, 14 FPAP, 15 GLS, 16 LTP: not used by the FMS */
    }
    lr_close(&lr);
    /* hash primary records, then merge secondaries */
    build_hash(nav);
    int added = 0;
    for (int k = 0; k < pend_count; k++) {
        NavPending *q = &pend[k];
        int32_t best = -1;
        double bestd = 1e9;
        for (int32_t i = chain_head(nav, q->ident); i >= 0; i = nav->pts[i].next) {
            NavPoint *p = &nav->pts[i];
            if (strcmp(p->ident, q->ident) || strcmp(p->region, q->region)) continue;
            if (q->row == 6) {
                if (p->kind != A380X_NAV_LOC || strcmp(p->area, q->area)) continue;
            } else {
                if (q->area[0]) { if (p->kind != A380X_NAV_LOC || strcmp(p->area, q->area)) continue; }
                else if (p->kind != A380X_NAV_VOR) continue;
            }
            double d = a380x_geo_distance_nm(p->lat, p->lon, q->lat, q->lon);
            if (d < bestd) { bestd = d; best = i; }
        }
        if (q->row == 6) {
            if (best >= 0) {
                NavPoint *p = &nav->pts[best];
                p->flags |= A380X_NAVF_GS;
                nav->ext[p->extra].gs_angle = (float)(floor(q->value / 1000.0) / 100.0);
            }
            continue;
        }
        if (best >= 0 && (nav->pts[best].kind == A380X_NAV_LOC || bestd < 3.0)) {
            NavPoint *p = &nav->pts[best];
            p->flags |= A380X_NAVF_DME;
            p->flags &= (uint8_t)~A380X_NAVF_VOR_ONLY;
            nav->ext[p->extra].dme_lat = q->lat;
            nav->ext[p->extra].dme_lon = q->lon;
            continue;
        }
        /* standalone DME / TACAN */
        NavPoint p;
        memset(&p, 0, sizeof p);
        p.lat = q->lat; p.lon = q->lon; p.kind = A380X_NAV_DME; p.flags = A380X_NAVF_DME;
        copy_str(p.ident, sizeof p.ident, q->ident, strlen(q->ident));
        copy_str(p.area, sizeof p.area, q->area, strlen(q->area));
        copy_str(p.region, sizeof p.region, q->region, strlen(q->region));
        if (q->area[0]) p.flags |= A380X_NAVF_TERMINAL;
        NavExtra e;
        memset(&e, 0, sizeof e);
        e.elev_ft = q->elev; e.freq = q->freq; e.range_nm = q->range;
        e.station_var = NAN; e.loc_true = NAN; e.loc_mag = NAN; e.gs_angle = NAN;
        e.dme_lat = q->lat; e.dme_lon = q->lon; e.name_off = q->name_off;
        if (e.name_off >= 0 && strstr(nav->pool + e.name_off, "TACAN")) p.flags |= A380X_NAVF_TACAN;
        if (!grow(nav, (void **)&nav->ext, &nav->ext_cap, nav->ext_count, sizeof(NavExtra))) break;
        nav->ext[nav->ext_count] = e;
        p.extra = nav->ext_count++;
        if (add_point(nav, &p) < 0) break;
        nav->navaids++;
        added++;
    }
    nav_free(nav, pend);
    (void)added;
    return 1;
}

static int load_aptmeta(A380XNavdata *nav, const char *dir)
{
    LineReader lr;
    if (!lr_open(&lr, dir, "earth_aptmeta.dat")) return 0;
    char *line;
    Tokens t;
    while ((line = lr_next(&lr)) != NULL) {
        if (lr.line_no <= 2 || !line[0]) continue;
        if (!strcmp(line, "99")) break;
        if (lr.too_long) { nav->malformed++; continue; }
        tokenize(line, &t);
        double lat, lon, elev;
        if (t.n < 5 || t.len[0] < 2 || t.len[0] > 7 || !tok_num(&t, 2, &lat) || !tok_num(&t, 3, &lon)
            || !tok_num(&t, 4, &elev) || fabs(lat) > 90 || fabs(lon) > 180) { nav->malformed++; continue; }
        AptMeta a;
        memset(&a, 0, sizeof a);
        tok_str(&t, 0, a.icao, sizeof a.icao);
        tok_str(&t, 1, a.region, sizeof a.region);
        a.lat = lat; a.lon = lon; a.elev_ft = (float)elev;
        a.cls = t.n > 5 ? t.p[5][0] : 0;
        double v;
        a.longest_ft = tok_num(&t, 6, &v) && v > 0 ? (float)v : NAN;
        a.ta_ft = tok_num(&t, 8, &v) && v > 0 ? (float)v : NAN;
        a.tl_ft = NAN;
        if (t.n > 9) {
            if (t.len[9] > 2 && t.p[9][0] == 'F' && t.p[9][1] == 'L' && parse_num(t.p[9] + 2, (size_t)t.len[9] - 2, &v) && v > 0)
                a.tl_ft = (float)(v * 100.0);
            else if (tok_num(&t, 9, &v) && v > 0) a.tl_ft = (float)v;
        }
        if (!grow(nav, (void **)&nav->apt, &nav->apt_cap, nav->apt_count, sizeof(AptMeta))) break;
        nav->apt[nav->apt_count] = a;
        NavPoint p;
        memset(&p, 0, sizeof p);
        p.lat = (float)lat; p.lon = (float)lon; p.kind = A380X_NAV_AIRPORT; p.extra = nav->apt_count;
        copy_str(p.ident, sizeof p.ident, a.icao, strlen(a.icao));
        copy_str(p.region, sizeof p.region, a.region, strlen(a.region));
        nav->apt_count++;
        if (add_point(nav, &p) < 0) break;
    }
    lr_close(&lr);
    return 1;
}

/* ---------------- airways ---------------- */

static int32_t awy_name_find(const A380XNavdata *nav, const char *name)
{
    if (!nav->awn_bucket) return -1;
    for (int32_t i = nav->awn_bucket[hash_str(name) & nav->awn_mask]; i >= 0; i = nav->awn[i].next)
        if (!strcmp(nav->awn[i].name, name)) return i;
    return -1;
}
static void awy_rehash(A380XNavdata *nav)
{
    uint32_t n = 256;
    while (n < (uint32_t)nav->awn_count * 2u) n <<= 1;
    nav_free(nav, nav->awn_bucket);
    nav->awn_bucket = (int32_t *)nav_alloc(nav, n * sizeof(int32_t));
    if (!nav->awn_bucket) return;
    nav->awn_mask = n - 1;
    for (uint32_t i = 0; i < n; i++) nav->awn_bucket[i] = -1;
    for (int i = 0; i < nav->awn_count; i++) {
        uint32_t h = hash_str(nav->awn[i].name) & nav->awn_mask;
        nav->awn[i].next = nav->awn_bucket[h];
        nav->awn_bucket[h] = i;
    }
}
static int32_t awy_name_get(A380XNavdata *nav, const char *name)
{
    int32_t i = awy_name_find(nav, name);
    if (i >= 0) return i;
    if (!grow(nav, (void **)&nav->awn, &nav->awn_cap, nav->awn_count, sizeof(AwyName))) return -1;
    AwyName *a = &nav->awn[nav->awn_count];
    memset(a, 0, sizeof *a);
    copy_str(a->name, sizeof a->name, name, strlen(name));
    a->first_seg = -1;
    i = nav->awn_count++;
    if (!nav->awn_bucket || (uint32_t)nav->awn_count * 2u > nav->awn_mask + 1u) awy_rehash(nav);
    else {
        uint32_t h = hash_str(a->name) & nav->awn_mask;
        a->next = nav->awn_bucket[h];
        nav->awn_bucket[h] = i;
    }
    return i;
}

static int load_awy(A380XNavdata *nav, const char *dir)
{
    LineReader lr;
    if (!lr_open(&lr, dir, "earth_awy.dat")) return 0;
    char *line;
    Tokens t;
    while ((line = lr_next(&lr)) != NULL) {
        if (lr.line_no <= 2 || !line[0]) continue;
        if (!strcmp(line, "99")) break;
        if (lr.too_long) { nav->malformed++; continue; }
        tokenize(line, &t);
        double t1, t2, lvl, base, top;
        if (t.n < 11 || !tok_num(&t, 2, &t1) || !tok_num(&t, 5, &t2) || !tok_num(&t, 7, &lvl) || !tok_num(&t, 8, &base)
            || !tok_num(&t, 9, &top) || t.len[0] > 7 || t.len[3] > 7 || t.len[6] != 1) { nav->malformed++; continue; }
        AwySeg s;
        memset(&s, 0, sizeof s);
        tok_str(&t, 0, s.id1, sizeof s.id1); tok_str(&t, 1, s.rg1, sizeof s.rg1);
        tok_str(&t, 3, s.id2, sizeof s.id2); tok_str(&t, 4, s.rg2, sizeof s.rg2);
        s.t1 = (uint8_t)t1; s.t2 = (uint8_t)t2; s.dir = t.p[6][0];
        s.level = (uint8_t)(lvl == 2 ? 2 : 1);
        s.base = (uint16_t)(base < 0 ? 0 : base); s.top = (uint16_t)(top < 0 ? 0 : top);
        char names[64];
        tok_str(&t, 10, names, sizeof names);
        char *nm = names;
        while (nm && *nm) {
            char *dash = strchr(nm, '-');
            if (dash) *dash = 0;
            if (*nm) {
                int32_t ni = awy_name_get(nav, nm);
                if (ni < 0) break;
                /* merge the low/high duplicate listed on consecutive lines */
                int32_t h = nav->awn[ni].first_seg;
                if (h >= 0 && !strcmp(nav->seg[h].id1, s.id1) && !strcmp(nav->seg[h].id2, s.id2)
                    && !strcmp(nav->seg[h].rg1, s.rg1) && !strcmp(nav->seg[h].rg2, s.rg2)) {
                    nav->seg[h].level |= s.level;
                    if (s.base < nav->seg[h].base) nav->seg[h].base = s.base;
                    if (s.top > nav->seg[h].top) nav->seg[h].top = s.top;
                } else {
                    if (!grow(nav, (void **)&nav->seg, &nav->seg_cap, nav->seg_count, sizeof(AwySeg))) break;
                    s.name = ni;
                    s.next_in_name = nav->awn[ni].first_seg;
                    nav->seg[nav->seg_count] = s;
                    nav->awn[ni].first_seg = nav->seg_count++;
                    nav->awn[ni].seg_count++;
                }
            }
            nm = dash ? dash + 1 : NULL;
        }
    }
    lr_close(&lr);
    return 1;
}

/* ---------------- cycle ---------------- */

static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civil_from_days(long z, int *y, int *m, int *d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    long dd = doy - (153 * mp + 2) / 5 + 1;
    long mm = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (mm <= 2));
    *m = (int)mm;
    *d = (int)dd;
}
int a380x_nav_airac_dates(const char *yynn, int *ey, int *em, int *ed, int *xy, int *xm, int *xd)
{
    if (!yynn || strlen(yynn) != 4) return -1;
    for (int i = 0; i < 4; i++) if (yynn[i] < '0' || yynn[i] > '9') return -1;
    int yy = (yynn[0] - '0') * 10 + (yynn[1] - '0');
    int nn = (yynn[2] - '0') * 10 + (yynn[3] - '0');
    if (nn < 1 || nn > 14) return -1;
    long epoch = days_from_civil(2020, 1, 2);
    long jan1 = days_from_civil(2000 + yy, 1, 1);
    long k0 = (jan1 - epoch + 27) / 28;          /* ceil for jan1 >= epoch */
    if (jan1 < epoch) k0 = -((epoch - jan1) / 28);
    long eff = epoch + (k0 + nn - 1) * 28;
    civil_from_days(eff, ey, em, ed);
    civil_from_days(eff + 28, xy, xm, xd);
    if (*ey != 2000 + yy) return -1;
    return 0;
}

static void read_cycle(A380XNavdata *nav, const char *nav_header_cycle)
{
    A380XNavCycle *c = &nav->cycle;
    memset(c, 0, sizeof *c);
    snprintf(c->provider, sizeof c->provider, "XP");
    c->from_custom_data = nav->custom_nav;
    if (nav->custom_nav) {
        FILE *f = open_file(nav->custom_dir, "cycle.json");
        if (f) {
            char buf[512];
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[n] = 0;
            const char *p = strstr(buf, "\"cycle\"");
            if (p && (p = strchr(p + 7, '"')) != NULL) {
                p++;
                if (strlen(p) >= 4 && p[4] == '"') { memcpy(c->airac, p, 4); c->airac[4] = 0; }
            }
            p = strstr(buf, "\"revision\"");
            if (p && (p = strchr(p + 10, '"')) != NULL) {
                p++;
                const char *e = strchr(p, '"');
                if (e) copy_str(c->revision, sizeof c->revision, p, (size_t)(e - p));
            }
            if (c->airac[0]) snprintf(c->source_file, sizeof c->source_file, "cycle.json");
        }
    }
    if (!c->airac[0] && nav_header_cycle && nav_header_cycle[0]) {
        memcpy(c->airac, nav_header_cycle, 5);
        snprintf(c->source_file, sizeof c->source_file, "earth_nav.dat");
    }
    if (c->airac[0] && !a380x_nav_airac_dates(c->airac, &c->eff_year, &c->eff_month, &c->eff_day,
                                               &c->exp_year, &c->exp_month, &c->exp_day))
        c->valid = 1;
}

/* ---------------- global load ---------------- */

static int file_exists(const char *dir, const char *name)
{
    FILE *f = open_file(dir, name);
    if (f) fclose(f);
    return f != NULL;
}

int a380x_nav_load_global(A380XNavdata *nav)
{
    if (!nav) return -1;
    if (nav->loaded) return 0;
    if (nav->load_failed) return -1;
    clock_t t0 = clock();
    nav->custom_fix = file_exists(nav->custom_dir, "earth_fix.dat");
    nav->custom_nav = file_exists(nav->custom_dir, "earth_nav.dat");
    nav->custom_awy = file_exists(nav->custom_dir, "earth_awy.dat");
    nav->custom_meta = file_exists(nav->custom_dir, "earth_aptmeta.dat");
    int ok_fix = load_fix(nav, nav->custom_fix ? nav->custom_dir : nav->default_dir);
    char cyc[5] = "";
    int ok_nav = load_nav(nav, nav->custom_nav ? nav->custom_dir : nav->default_dir, cyc);
    load_aptmeta(nav, nav->custom_meta ? nav->custom_dir : nav->default_dir);
    load_awy(nav, nav->custom_awy ? nav->custom_dir : nav->default_dir);
    /* shrink the growth slack (realloc to the exact size cannot fail in practice; keep old on failure) */
#define SHRINK(arr, cnt, cap, T) do { if ((cnt) > 0 && (cnt) < (cap)) { void *q_ = nav_realloc(nav, (arr), (size_t)(cnt) * sizeof(T)); \
        if (q_) { (arr) = (T *)q_; (cap) = (cnt); } } } while (0)
    SHRINK(nav->pts, nav->pt_count, nav->pt_cap, NavPoint);
    SHRINK(nav->ext, nav->ext_count, nav->ext_cap, NavExtra);
    SHRINK(nav->apt, nav->apt_count, nav->apt_cap, AptMeta);
    SHRINK(nav->seg, nav->seg_count, nav->seg_cap, AwySeg);
    SHRINK(nav->awn, nav->awn_count, nav->awn_cap, AwyName);
    SHRINK(nav->pool, nav->pool_len, nav->pool_cap, char);
#undef SHRINK
    build_hash(nav);
    /* VOR list for the variation estimate */
    int nv = 0;
    for (int i = 0; i < nav->pt_count; i++)
        if ((nav->pts[i].kind == A380X_NAV_VOR) && nav->pts[i].extra >= 0 && isfinite(nav->ext[nav->pts[i].extra].station_var)) nv++;
    nav->vor_idx = (int32_t *)nav_alloc(nav, (size_t)(nv ? nv : 1) * sizeof(int32_t));
    nav->vor_count = 0;
    if (nav->vor_idx)
        for (int i = 0; i < nav->pt_count; i++)
            if ((nav->pts[i].kind == A380X_NAV_VOR) && nav->pts[i].extra >= 0 && isfinite(nav->ext[nav->pts[i].extra].station_var))
                nav->vor_idx[nav->vor_count++] = i;
    read_cycle(nav, cyc);
    nav->cycle_done = 1;
    nav->load_ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    if (!ok_fix && !ok_nav) {
        nav->load_failed = 1;
        nav_log(nav, "A380X navdata: no earth_fix.dat/earth_nav.dat in Custom Data or default data");
        return -1;
    }
    nav->loaded = 1;
    if (nav->malformed) {
        char msg[128];
        snprintf(msg, sizeof msg, "A380X navdata: %d malformed lines skipped", nav->malformed);
        nav_log(nav, msg);
    }
    return 0;
}

static int ensure(A380XNavdata *nav) { return nav && (nav->loaded || a380x_nav_load_global(nav) == 0); }

/* ============================================================================================ */
/* Create / destroy                                                                             */
/* ============================================================================================ */

static char *derive_dir(A380XNavdata *nav, const char *root, const char *sub)
{
    char buf[1024];
    if (!root || !*root) return nav_strdup(nav, "");
    size_t n = strlen(root);
    int sep = root[n - 1] == '/' || root[n - 1] == '\\';
    snprintf(buf, sizeof buf, "%s%s%s", root, sep ? "" : "/", sub);
    return nav_strdup(nav, buf);
}

A380XNavdata *a380x_nav_create(const A380XNavdataConfig *cfg, const A380XNavdataHost *host)
{
    A380XNavdata *nav = (A380XNavdata *)calloc(1, sizeof *nav);
    if (!nav) return NULL;
    if (host) { nav->host = *host; nav->has_host = 1; }
    const char *root = cfg ? cfg->xplane_root : NULL;
    nav->custom_dir = (cfg && cfg->custom_data_dir) ? nav_strdup(nav, cfg->custom_data_dir) : derive_dir(nav, root, "Custom Data");
    nav->default_dir = (cfg && cfg->default_data_dir) ? nav_strdup(nav, cfg->default_data_dir) : derive_dir(nav, root, "Resources/default data");
    nav->cifp_cap = (cfg && cfg->cifp_cache_size > 0) ? cfg->cifp_cache_size : 8;
    nav->slots = (CifpSlot *)nav_calloc(nav, (size_t)nav->cifp_cap, sizeof(CifpSlot));
    if (!nav->custom_dir || !nav->default_dir || !nav->slots) { a380x_nav_destroy(nav); return NULL; }
    return nav;
}

static void free_airport(A380XNavdata *nav, A380XNavAirport *a)
{
    if (!a) return;
    nav_free(nav, a->runways);
    nav_free(nav, a->procs);
    nav_free(nav, a->trans);
    nav_free(nav, a->legs);
    nav_free(nav, a);
}

static void free_global(A380XNavdata *nav)
{
    nav_free(nav, nav->pts); nav->pts = NULL; nav->pt_count = nav->pt_cap = 0;
    nav_free(nav, nav->ext); nav->ext = NULL; nav->ext_count = nav->ext_cap = 0;
    nav_free(nav, nav->apt); nav->apt = NULL; nav->apt_count = nav->apt_cap = 0;
    nav_free(nav, nav->bucket); nav->bucket = NULL; nav->bucket_mask = 0;
    nav_free(nav, nav->pool); nav->pool = NULL; nav->pool_len = nav->pool_cap = 0;
    nav_free(nav, nav->vor_idx); nav->vor_idx = NULL; nav->vor_count = 0;
    nav_free(nav, nav->seg); nav->seg = NULL; nav->seg_count = nav->seg_cap = 0;
    nav_free(nav, nav->awn); nav->awn = NULL; nav->awn_count = nav->awn_cap = 0;
    nav_free(nav, nav->awn_bucket); nav->awn_bucket = NULL; nav->awn_mask = 0;
    nav_free(nav, nav->hold); nav->hold = NULL; nav->hold_count = nav->hold_cap = 0;
    nav_free(nav, nav->hold_bucket); nav->hold_bucket = NULL; nav->hold_mask = 0;
    nav_free(nav, nav->var_grid); nav->var_grid = NULL;
    nav_free(nav, nav->scr_seg); nav->scr_seg = NULL; nav->scr_seg_cap = 0;
    for (int i = 0; i < nav->slot_count; i++) { free_airport(nav, nav->slots[i].apt); memset(&nav->slots[i], 0, sizeof nav->slots[i]); }
    nav->slot_count = 0;
    nav->holds_loaded = 0;
    nav->loaded = nav->load_failed = 0;
    nav->waypoints = nav->navaids = 0;
    nav->malformed = 0;
    nav->cycle_done = 0;
}

void a380x_nav_reload(A380XNavdata *nav)
{
    if (!nav) return;
    free_global(nav);
    nav->generation++;
}

void a380x_nav_destroy(A380XNavdata *nav)
{
    if (!nav) return;
    free_global(nav);
    nav_free(nav, nav->slots);
    nav_free(nav, nav->custom_dir);
    nav_free(nav, nav->default_dir);
    free(nav);
}

const A380XNavCycle *a380x_nav_cycle(A380XNavdata *nav)
{
    if (!nav) return NULL;
    ensure(nav);
    return &nav->cycle;
}

void a380x_nav_stats(A380XNavdata *nav, A380XNavStats *out)
{
    memset(out, 0, sizeof *out);
    if (!nav) return;
    out->loaded = nav->loaded;
    out->waypoints = nav->waypoints;
    out->navaids = nav->navaids;
    out->airports_meta = nav->apt_count;
    out->airway_segments = nav->seg_count;
    out->airway_names = nav->awn_count;
    out->holds = nav->hold_count;
    for (int i = 0; i < nav->slot_count; i++) if (nav->slots[i].apt) out->cifp_cached++;
    out->cifp_loads = nav->cifp_loads;
    out->malformed_lines = nav->malformed;
    out->bytes = nav->bytes;
    out->global_load_ms = nav->load_ms;
    out->custom_fix = nav->custom_fix; out->custom_nav = nav->custom_nav;
    out->custom_awy = nav->custom_awy; out->custom_meta = nav->custom_meta;
}

/* ============================================================================================ */
/* Lookups                                                                                      */
/* ============================================================================================ */

static void fill_from_point(const A380XNavdata *nav, int32_t idx, A380XNavFix *out)
{
    const NavPoint *p = &nav->pts[idx];
    memset(out, 0, sizeof *out);
    out->kind = (A380XNavKind)p->kind;
    out->flags = p->flags;
    copy_str(out->ident, sizeof out->ident, p->ident, strlen(p->ident));
    copy_str(out->region, sizeof out->region, p->region, strlen(p->region));
    copy_str(out->airport, sizeof out->airport, p->area, strlen(p->area));
    out->lat = p->lat; out->lon = p->lon;
    out->dme_lat = p->lat; out->dme_lon = p->lon;
    out->elev_ft = NAN; out->station_var = NAN; out->loc_true_brg = NAN; out->loc_mag_brg = NAN; out->gs_angle_deg = NAN;
    if (p->kind == A380X_NAV_WAYPOINT && (p->wpt_type[0] || p->wpt_type[1] || p->wpt_type[2])) {
        for (int i = 0; i < 3; i++) out->wpt_type[i] = p->wpt_type[i] ? p->wpt_type[i] : ' ';
        out->wpt_type[3] = 0;
    }
    out->db_id = idx;
    if (p->kind == A380X_NAV_AIRPORT && p->extra >= 0) {
        const AptMeta *a = &nav->apt[p->extra];
        out->lat = a->lat; out->lon = a->lon; out->dme_lat = a->lat; out->dme_lon = a->lon;
        out->elev_ft = a->elev_ft;
    } else if (p->extra >= 0) {
        const NavExtra *e = &nav->ext[p->extra];
        out->elev_ft = e->elev_ft; out->freq = e->freq; out->range_nm = e->range_nm;
        out->station_var = e->station_var; out->loc_true_brg = e->loc_true; out->loc_mag_brg = e->loc_mag;
        out->gs_angle_deg = e->gs_angle; out->dme_lat = e->dme_lat; out->dme_lon = e->dme_lon;
        if (e->name_off >= 0) copy_trim(out->name, sizeof out->name, nav->pool + e->name_off);
    }
}

static int host_airport(A380XNavdata *nav, const char *icao, A380XNavAirportInfo *out)
{
    if (!nav->has_host || !nav->host.airport_info) return 0;
    A380XNavAirportInfo h;
    memset(&h, 0, sizeof h);
    h.lat = h.lon = NAN; h.elev_ft = NAN;
    if (!nav->host.airport_info(nav->host.ctx, icao, &h)) return 0;
    *out = h;
    return 1;
}

int a380x_nav_find(A380XNavdata *nav, const char *ident, unsigned mask, A380XNavFix *out, int max)
{
    if (!ident || !*ident || !ensure(nav)) return 0;
    int count = 0, apt_found = 0;
    for (int32_t i = chain_head(nav, ident); i >= 0; i = nav->pts[i].next) {
        const NavPoint *p = &nav->pts[i];
        if (strcmp(p->ident, ident) || !(mask & A380X_NAVMASK(p->kind))) continue;
        if (p->kind == A380X_NAV_AIRPORT) apt_found = 1;
        if (out && count < max) {
            fill_from_point(nav, i, &out[count]);
            if (p->kind == A380X_NAV_AIRPORT) {
                A380XNavAirportInfo h;
                if (host_airport(nav, ident, &h)) copy_str(out[count].name, sizeof out[count].name, h.name, strlen(h.name));
            }
        }
        count++;
    }
    if ((mask & A380X_NAVMASK(A380X_NAV_AIRPORT)) && !apt_found) {
        A380XNavAirportInfo h;
        if (host_airport(nav, ident, &h) && isfinite(h.lat) && isfinite(h.lon)) {
            if (out && count < max) {
                A380XNavFix *f = &out[count];
                memset(f, 0, sizeof *f);
                f->kind = A380X_NAV_AIRPORT; f->db_id = -1;
                copy_str(f->ident, sizeof f->ident, ident, strlen(ident));
                copy_str(f->region, sizeof f->region, h.region, strlen(h.region));
                copy_str(f->name, sizeof f->name, h.name, strlen(h.name));
                f->lat = f->dme_lat = h.lat; f->lon = f->dme_lon = h.lon; f->elev_ft = h.elev_ft;
                f->station_var = f->loc_true_brg = f->loc_mag_brg = f->gs_angle_deg = NAN;
            }
            count++;
        }
    }
    return count;
}

void a380x_nav_sort_nearest(A380XNavFix *f, int n, double lat, double lon)
{
    for (int i = 0; i < n; i++)
        f[i].dist_nm = (isfinite(lat) && isfinite(lon)) ? a380x_geo_distance_nm(lat, lon, f[i].lat, f[i].lon) : 0.0;
    for (int i = 1; i < n; i++) {        /* stable insertion sort (lists are short) */
        A380XNavFix v = f[i];
        int j = i - 1;
        while (j >= 0 && f[j].dist_nm > v.dist_nm) { f[j + 1] = f[j]; j--; }
        f[j + 1] = v;
    }
}

int a380x_nav_find_near(A380XNavdata *nav, const char *ident, unsigned mask, double lat, double lon,
                        A380XNavFix *out, int max)
{
    if (!ident || !ensure(nav)) return 0;
    int total = a380x_nav_find(nav, ident, mask, NULL, 0);
    if (!total || !out || max <= 0) return total;
    if (total <= max) {
        int n = a380x_nav_find(nav, ident, mask, out, max);
        a380x_nav_sort_nearest(out, n, lat, lon);
        return total;
    }
    /* more matches than room: collect all on the heap, sort, copy the nearest */
    A380XNavFix *all = (A380XNavFix *)malloc((size_t)total * sizeof *all);
    if (!all) return 0;
    int n = a380x_nav_find(nav, ident, mask, all, total);
    a380x_nav_sort_nearest(all, n, lat, lon);
    memcpy(out, all, (size_t)max * sizeof *out);
    free(all);
    return total;
}

static int kind_compatible(A380XNavKind want, uint8_t have)
{
    if (want == A380X_NAV_NONE) return 1;
    if (want == A380X_NAV_VOR) return have == A380X_NAV_VOR || have == A380X_NAV_DME;  /* VHF navaid */
    if (want == A380X_NAV_DME) return have == A380X_NAV_DME || have == A380X_NAV_VOR;
    return want == have;
}

int a380x_nav_resolve(A380XNavdata *nav, const char *ident, const char *region, A380XNavKind kind,
                      const char *airport, double rlat, double rlon, A380XNavFix *out)
{
    if (!ident || !*ident || !ensure(nav)) return 0;
    int has_ref = isfinite(rlat) && isfinite(rlon);
    const char *rg = region ? region : "";
    const char *ap = airport ? airport : "";
    /* pass 0: region + area preference; pass 1: any region */
    for (int pass = 0; pass < 2; pass++) {
        int32_t best = -1;
        int best_score = -1;
        double best_d = 1e30;
        for (int32_t i = chain_head(nav, ident); i >= 0; i = nav->pts[i].next) {
            const NavPoint *p = &nav->pts[i];
            if (strcmp(p->ident, ident) || !kind_compatible(kind, p->kind)) continue;
            if (pass == 0 && rg[0] && strcmp(p->region, rg)) continue;
            int score = 0;
            if (ap[0]) score = !strcmp(p->area, ap) ? 2 : (p->area[0] ? 0 : 1);
            else score = p->area[0] ? 0 : 1;
            if (kind == A380X_NAV_VOR && p->kind == A380X_NAV_VOR) score += 1;  /* VOR over a same-named DME */
            double d = has_ref ? a380x_geo_distance_nm(rlat, rlon, p->lat, p->lon) : 0.0;
            if (score > best_score || (score == best_score && d < best_d)) { best = i; best_score = score; best_d = d; }
        }
        if (best >= 0) {
            if (out) fill_from_point(nav, best, out);
            return 1;
        }
        if (!rg[0]) break;
    }
    if (kind == A380X_NAV_AIRPORT || kind == A380X_NAV_NONE) {
        A380XNavAirportInfo h;
        if (host_airport(nav, ident, &h) && isfinite(h.lat)) {
            if (out) {
                memset(out, 0, sizeof *out);
                out->kind = A380X_NAV_AIRPORT; out->db_id = -1;
                copy_str(out->ident, sizeof out->ident, ident, strlen(ident));
                copy_str(out->name, sizeof out->name, h.name, strlen(h.name));
                out->lat = out->dme_lat = h.lat; out->lon = out->dme_lon = h.lon; out->elev_ft = h.elev_ft;
                out->station_var = out->loc_true_brg = out->loc_mag_brg = out->gs_angle_deg = NAN;
            }
            return 1;
        }
    }
    return 0;
}

int a380x_nav_nearby(A380XNavdata *nav, double lat, double lon, double radius, unsigned mask, A380XNavFix *out, int max)
{
    if (!ensure(nav)) return 0;
    int count = 0;
    double dlat = radius / 60.0 + 0.01;
    double coslat = cos(lat * DEG2RAD);
    double dlon = coslat > 0.01 ? dlat / coslat : 360.0;
    for (int i = 0; i < nav->pt_count; i++) {
        const NavPoint *p = &nav->pts[i];
        if (!(mask & A380X_NAVMASK(p->kind))) continue;
        if (fabs(p->lat - lat) > dlat) continue;
        double dl = fabs(fmod(p->lon - lon + 540.0, 360.0) - 180.0);
        if (dl > dlon) continue;
        double d = a380x_geo_distance_nm(lat, lon, p->lat, p->lon);
        if (d > radius) continue;
        if (out && count < max) { fill_from_point(nav, i, &out[count]); out[count].dist_nm = d; }
        else if (out && max > 0) {
            /* keep the nearest `max`: replace the farthest if closer */
            int far = 0;
            for (int k = 1; k < max; k++) if (out[k].dist_nm > out[far].dist_nm) far = k;
            if (d < out[far].dist_nm) { fill_from_point(nav, i, &out[far]); out[far].dist_nm = d; }
        }
        count++;
    }
    if (out) {
        int n = count < max ? count : max;
        a380x_nav_sort_nearest(out, n, lat, lon);
    }
    return count;
}

/* ---------------- magnetic variation ---------------- */

static double estimate_var(A380XNavdata *nav, double lat, double lon)
{
    if (!nav->vor_count) return 0.0;
    if (!nav->var_grid) {
        nav->var_grid = (float *)nav_alloc(nav, 180 * 360 * sizeof(float));
        if (!nav->var_grid) return 0.0;
        for (int i = 0; i < 180 * 360; i++) nav->var_grid[i] = NAN;
    }
    int r = (int)floor(lat + 90.0), c = (int)floor(lon + 180.0);
    if (r < 0) r = 0;
    if (r > 179) r = 179;
    if (c < 0) c = 0;
    if (c > 359) c = 359;
    float *cell = &nav->var_grid[r * 360 + c];
    if (isfinite(*cell)) return *cell;
    double clat = r - 90.0 + 0.5, clon = c - 180.0 + 0.5;
    /* inverse-distance weighted mean of the 4 nearest VOR declinations */
    double bd[4] = { 1e30, 1e30, 1e30, 1e30 };
    double bv[4] = { 0, 0, 0, 0 };
    for (int k = 0; k < nav->vor_count; k++) {
        const NavPoint *p = &nav->pts[nav->vor_idx[k]];
        if (fabs(p->lat - clat) > 15.0) continue;
        double d = a380x_geo_distance_nm(clat, clon, p->lat, p->lon);
        int slot = -1;
        for (int j = 0; j < 4; j++) if (d < bd[j] && (slot < 0 || bd[j] > bd[slot])) slot = j;
        if (slot >= 0) { bd[slot] = d; bv[slot] = nav->ext[p->extra].station_var; }
    }
    double sw = 0, sv = 0;
    for (int j = 0; j < 4; j++) if (bd[j] < 600.0) { double w = 1.0 / (bd[j] + 5.0); sw += w; sv += w * bv[j]; }
    *cell = (float)(sw > 0 ? sv / sw : 0.0);
    return *cell;
}

double a380x_nav_magvar(A380XNavdata *nav, double lat, double lon)
{
    if (!nav || !isfinite(lat) || !isfinite(lon)) return 0.0;
    if (nav->has_host && nav->host.magvar) {
        double v;
        if (nav->host.magvar(nav->host.ctx, lat, lon, &v) && isfinite(v)) return v;
    }
    if (!ensure(nav)) return 0.0;
    return estimate_var(nav, lat, lon);
}

/* ---------------- airports ---------------- */

static int32_t find_aptmeta(A380XNavdata *nav, const char *icao)
{
    for (int32_t i = chain_head(nav, icao); i >= 0; i = nav->pts[i].next)
        if (nav->pts[i].kind == A380X_NAV_AIRPORT && !strcmp(nav->pts[i].ident, icao)) return nav->pts[i].extra;
    return -1;
}

static double loc_var_at_airport(A380XNavdata *nav, const char *icao, int *n_out)
{
    double sum = 0;
    int n = 0;
    /* LOCs of an airport share no ident; scan the points with a matching area (fast enough: only
     * on CIFP load). */
    for (int i = 0; i < nav->pt_count; i++) {
        const NavPoint *p = &nav->pts[i];
        if (p->kind != A380X_NAV_LOC || strcmp(p->area, icao) || p->extra < 0) continue;
        const NavExtra *e = &nav->ext[p->extra];
        if (!isfinite(e->loc_true) || !isfinite(e->loc_mag)) continue;
        sum += a380x_geo_angle_diff(e->loc_mag, e->loc_true);
        n++;
    }
    *n_out = n;
    return n ? sum / n : 0.0;
}

int a380x_nav_airport_info(A380XNavdata *nav, const char *icao, A380XNavAirportInfo *out)
{
    if (!icao || !*icao || !ensure(nav)) return 0;
    A380XNavAirportInfo a;
    memset(&a, 0, sizeof a);
    copy_str(a.icao, sizeof a.icao, icao, strlen(icao));
    a.lat = a.lon = NAN; a.elev_ft = NAN; a.trans_alt_ft = NAN; a.trans_level_ft = NAN; a.longest_rwy_ft = NAN;
    int found = 0;
    int32_t m = find_aptmeta(nav, icao);
    if (m >= 0) {
        const AptMeta *am = &nav->apt[m];
        a.lat = am->lat; a.lon = am->lon; a.elev_ft = am->elev_ft;
        a.trans_alt_ft = am->ta_ft; a.trans_level_ft = am->tl_ft; a.longest_rwy_ft = am->longest_ft;
        copy_str(a.region, sizeof a.region, am->region, strlen(am->region));
        a.from_aptmeta = 1;
        found = 1;
    }
    A380XNavAirportInfo h;
    if (host_airport(nav, icao, &h)) {
        copy_str(a.name, sizeof a.name, h.name, strlen(h.name));
        if (!found && isfinite(h.lat) && isfinite(h.lon)) {
            a.lat = h.lat; a.lon = h.lon; a.elev_ft = h.elev_ft;
            if (h.region[0]) copy_str(a.region, sizeof a.region, h.region, strlen(h.region));
            a.from_host = 1;
            found = 1;
        }
    }
    if (found) {
        int cached = 0;
        for (int i = 0; i < nav->slot_count; i++)
            if (!strcmp(nav->slots[i].icao, icao) && nav->slots[i].apt) {
                a.has_cifp = nav->slots[i].apt->info.has_cifp;
                a.magvar_deg = nav->slots[i].apt->info.magvar_deg;
                cached = 1;
            }
        double hv;
        if (cached) { /* variation already computed with the CIFP load */ }
        else if (nav->has_host && nav->host.magvar && nav->host.magvar(nav->host.ctx, a.lat, a.lon, &hv) && isfinite(hv))
            a.magvar_deg = (float)hv;
        else {
            int n;
            double v = loc_var_at_airport(nav, icao, &n);
            a.magvar_deg = (float)(n ? v : estimate_var(nav, a.lat, a.lon));
        }
    }
    if (out) *out = a;
    return found;
}

void a380x_nav_ref_clear(A380XNavRef *r)
{
    memset(r, 0, sizeof *r);
    r->lat = r->lon = NAN;
    r->station_var = NAN;
}
void a380x_nav_ref_from_fix(A380XNavRef *r, const A380XNavFix *f)
{
    a380x_nav_ref_clear(r);
    if (!f) return;
    copy_str(r->ident, sizeof r->ident, f->ident, strlen(f->ident));
    copy_str(r->region, sizeof r->region, f->region, strlen(f->region));
    r->kind = (uint8_t)f->kind;
    r->flags = (uint8_t)f->flags;
    r->lat = f->lat; r->lon = f->lon;
    r->freq = f->freq;
    r->station_var = f->station_var;
}

int a380x_nav_make_latlon(double lat, double lon, const char *ident, A380XNavFix *out)
{
    if (!out || !isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 || fabs(lon) > 180) return 0;
    memset(out, 0, sizeof *out);
    out->kind = A380X_NAV_USER;
    out->db_id = -1;
    copy_str(out->ident, sizeof out->ident, ident ? ident : "", ident ? strlen(ident) : 0);
    out->lat = out->dme_lat = lat; out->lon = out->dme_lon = lon;
    out->elev_ft = NAN; out->station_var = out->loc_true_brg = out->loc_mag_brg = out->gs_angle_deg = NAN;
    return 1;
}
int a380x_nav_make_pbd(A380XNavdata *nav, const A380XNavFix *place, double brg_mag, double dist,
                       const char *ident, A380XNavFix *out)
{
    if (!place || !isfinite(place->lat) || !isfinite(brg_mag) || !isfinite(dist) || dist < 0) return 0;
    double var = a380x_nav_magvar(nav, place->lat, place->lon);
    double lat, lon;
    a380x_geo_destination(place->lat, place->lon, a380x_geo_norm360(brg_mag + var), dist, &lat, &lon);
    return a380x_nav_make_latlon(lat, lon, ident, out);
}
int a380x_nav_make_pbx(A380XNavdata *nav, const A380XNavFix *p1, double b1, const A380XNavFix *p2, double b2,
                       const char *ident, A380XNavFix *out)
{
    if (!p1 || !p2 || !isfinite(p1->lat) || !isfinite(p2->lat)) return 0;
    double v1 = a380x_nav_magvar(nav, p1->lat, p1->lon), v2 = a380x_nav_magvar(nav, p2->lat, p2->lon);
    double lat, lon;
    if (!geo_intersect_ex(p1->lat, p1->lon, a380x_geo_norm360(b1 + v1), p2->lat, p2->lon, a380x_geo_norm360(b2 + v2), 1, &lat, &lon))
        return 0;
    return a380x_nav_make_latlon(lat, lon, ident, out);
}

/* ============================================================================================ */
/* Holds                                                                                        */
/* ============================================================================================ */

static void load_holds(A380XNavdata *nav)
{
    nav->holds_loaded = 1;
    const char *dir = file_exists(nav->custom_dir, "earth_hold.dat") ? nav->custom_dir : nav->default_dir;
    LineReader lr;
    if (!lr_open(&lr, dir, "earth_hold.dat")) return;
    char *line;
    Tokens t;
    while ((line = lr_next(&lr)) != NULL) {
        if (lr.line_no <= 2 || !line[0]) continue;
        if (!strcmp(line, "99")) break;
        if (lr.too_long) { nav->malformed++; continue; }
        tokenize(line, &t);
        double ty, crs, tm, ds, mn, mx, sp;
        if (t.n < 11 || t.len[0] > 7 || !tok_num(&t, 3, &ty) || !tok_num(&t, 4, &crs) || !tok_num(&t, 5, &tm)
            || !tok_num(&t, 6, &ds) || !tok_num(&t, 8, &mn) || !tok_num(&t, 9, &mx) || !tok_num(&t, 10, &sp)
            || t.len[7] != 1) { nav->malformed++; continue; }
        if (!grow(nav, (void **)&nav->hold, &nav->hold_cap, nav->hold_count, sizeof(HoldRec))) break;
        HoldRec *h = &nav->hold[nav->hold_count];
        memset(h, 0, sizeof *h);
        a380x_nav_ref_clear(&h->h.fix);
        tok_str(&t, 0, h->h.fix.ident, sizeof h->h.fix.ident);
        tok_str(&t, 1, h->h.fix.region, sizeof h->h.fix.region);
        char area[16];
        tok_str(&t, 2, area, sizeof area);
        if (strcmp(area, "ENRT")) copy_str(h->h.airport, sizeof h->h.airport, area, strlen(area));
        int ity = (int)ty;
        A380XNavKind k = ity == 2 ? A380X_NAV_NDB : (ity == 3 ? A380X_NAV_VOR : A380X_NAV_WAYPOINT);
        h->h.fix.kind = (uint8_t)k;
        h->h.inbound_mag = (float)crs; h->h.leg_time_min = (float)tm; h->h.leg_dist_nm = (float)ds;
        h->h.turn = t.p[7][0]; h->h.min_alt_ft = (float)mn; h->h.max_alt_ft = (float)mx; h->h.speed_kt = (float)sp;
        nav->hold_count++;
    }
    lr_close(&lr);
    uint32_t n = 256;
    while (n < (uint32_t)nav->hold_count * 2u) n <<= 1;
    nav->hold_bucket = (int32_t *)nav_alloc(nav, n * sizeof(int32_t));
    if (!nav->hold_bucket) return;
    nav->hold_mask = n - 1;
    for (uint32_t i = 0; i < n; i++) nav->hold_bucket[i] = -1;
    for (int i = nav->hold_count - 1; i >= 0; i--) {
        uint32_t h = hash_str(nav->hold[i].h.fix.ident) & nav->hold_mask;
        nav->hold[i].next = nav->hold_bucket[h];
        nav->hold_bucket[h] = i;
    }
}

int a380x_nav_find_holds(A380XNavdata *nav, const char *ident, const char *region, A380XNavHold *out, int max)
{
    if (!ident || !ensure(nav)) return 0;
    if (!nav->holds_loaded) load_holds(nav);
    if (!nav->hold_bucket) return 0;
    int count = 0;
    for (int32_t i = nav->hold_bucket[hash_str(ident) & nav->hold_mask]; i >= 0; i = nav->hold[i].next) {
        HoldRec *h = &nav->hold[i];
        if (strcmp(h->h.fix.ident, ident) || (region && region[0] && strcmp(h->h.fix.region, region))) continue;
        if (!isfinite(h->h.fix.lat)) {
            A380XNavFix f;
            if (a380x_nav_resolve(nav, h->h.fix.ident, h->h.fix.region, (A380XNavKind)h->h.fix.kind, h->h.airport, NAN, NAN, &f)) {
                h->h.fix.lat = f.lat; h->h.fix.lon = f.lon;
            }
        }
        if (out && count < max) out[count] = h->h;
        count++;
    }
    return count;
}

/* ============================================================================================ */
/* Airways                                                                                      */
/* ============================================================================================ */

typedef struct {
    char id[8], rg[3];
    uint8_t type;
    int32_t adj_first;      /* into edge arrays */
    int32_t parent, depth;
    double lat, lon;        /* NAN until resolved */
} AwyNode;

typedef struct {
    AwyNode *node; int n, cap;
    int32_t *e_to, *e_next; int ne, ecap;
} AwyGraph;

static void graph_free(AwyGraph *g) { free(g->node); free(g->e_to); free(g->e_next); memset(g, 0, sizeof *g); }

static int graph_node(AwyGraph *g, const char *id, const char *rg, uint8_t type)
{
    for (int i = 0; i < g->n; i++)
        if (g->node[i].type == type && !strcmp(g->node[i].id, id) && !strcmp(g->node[i].rg, rg)) return i;
    if (g->n >= g->cap) {
        int nc = g->cap ? g->cap * 2 : 64;
        AwyNode *nn = (AwyNode *)realloc(g->node, (size_t)nc * sizeof *nn);
        if (!nn) return -1;
        g->node = nn; g->cap = nc;
    }
    AwyNode *n = &g->node[g->n];
    memset(n, 0, sizeof *n);
    copy_str(n->id, sizeof n->id, id, strlen(id));
    copy_str(n->rg, sizeof n->rg, rg, strlen(rg));
    n->type = type; n->adj_first = -1; n->parent = -1; n->depth = -1;
    n->lat = n->lon = NAN;
    return g->n++;
}
static int graph_edge(AwyGraph *g, int a, int b)
{
    if (g->ne + 2 > g->ecap) {
        int nc = g->ecap ? g->ecap * 2 : 128;
        int32_t *t = (int32_t *)realloc(g->e_to, (size_t)nc * sizeof *t);
        if (!t) return 0;
        g->e_to = t;
        int32_t *x = (int32_t *)realloc(g->e_next, (size_t)nc * sizeof *x);
        if (!x) return 0;
        g->e_next = x;
        g->ecap = nc;
    }
    g->e_to[g->ne] = b; g->e_next[g->ne] = g->node[a].adj_first; g->node[a].adj_first = g->ne++;
    g->e_to[g->ne] = a; g->e_next[g->ne] = g->node[b].adj_first; g->node[b].adj_first = g->ne++;
    return 1;
}
static int build_graph(A380XNavdata *nav, const char *name, AwyGraph *g)
{
    memset(g, 0, sizeof *g);
    int32_t ni = awy_name_find(nav, name);
    if (ni < 0) return 0;
    for (int32_t s = nav->awn[ni].first_seg; s >= 0; s = nav->seg[s].next_in_name) {
        const AwySeg *sg = &nav->seg[s];
        int a = graph_node(g, sg->id1, sg->rg1, sg->t1);
        int b = graph_node(g, sg->id2, sg->rg2, sg->t2);
        if (a < 0 || b < 0 || !graph_edge(g, a, b)) { graph_free(g); return 0; }
    }
    return 1;
}
static A380XNavKind awy_kind(uint8_t t) { return t == 2 ? A380X_NAV_NDB : (t == 3 ? A380X_NAV_VOR : A380X_NAV_WAYPOINT); }
static void graph_resolve(A380XNavdata *nav, AwyNode *n)
{
    if (isfinite(n->lat)) return;
    A380XNavFix f;
    if (a380x_nav_resolve(nav, n->id, n->rg, awy_kind(n->type), "", NAN, NAN, &f)) { n->lat = f.lat; n->lon = f.lon; }
}
static void node_ref(A380XNavdata *nav, AwyNode *n, A380XNavRef *r)
{
    a380x_nav_ref_clear(r);
    A380XNavFix f;
    if (a380x_nav_resolve(nav, n->id, n->rg, awy_kind(n->type), "", NAN, NAN, &f)) a380x_nav_ref_from_fix(r, &f);
    else {
        copy_str(r->ident, sizeof r->ident, n->id, strlen(n->id));
        copy_str(r->region, sizeof r->region, n->rg, strlen(n->rg));
        r->kind = (uint8_t)awy_kind(n->type);
    }
}
static int node_matches(const AwyNode *n, const A380XNavRef *f)
{
    if (strcmp(n->id, f->ident)) return 0;
    if (f->region[0] && strcmp(n->rg, f->region)) return 0;
    return 1;
}
static int graph_start(A380XNavdata *nav, AwyGraph *g, const A380XNavRef *from)
{
    int best = -1;
    double bd = 1e30;
    for (int i = 0; i < g->n; i++) {
        if (!node_matches(&g->node[i], from)) continue;
        double d = 0;
        if (isfinite(from->lat)) {
            graph_resolve(nav, &g->node[i]);
            d = isfinite(g->node[i].lat) ? a380x_geo_distance_nm(from->lat, from->lon, g->node[i].lat, g->node[i].lon) : 1e20;
            if (d > 5.0) continue;  /* same ident elsewhere */
        }
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
/* BFS; fills order[] (visit order) and returns the number of visited nodes. */
static int graph_bfs(AwyGraph *g, int start, int *order)
{
    for (int i = 0; i < g->n; i++) { g->node[i].parent = -1; g->node[i].depth = -1; }
    int head = 0, tail = 0;
    order[tail++] = start;
    g->node[start].depth = 0;
    while (head < tail) {
        int u = order[head++];
        for (int32_t e = g->node[u].adj_first; e >= 0; e = g->e_next[e]) {
            int v = g->e_to[e];
            if (g->node[v].depth >= 0) continue;
            g->node[v].depth = g->node[u].depth + 1;
            g->node[v].parent = u;
            order[tail++] = v;
        }
    }
    return tail;
}

int a380x_nav_airway_exists(A380XNavdata *nav, const char *name)
{
    return name && ensure(nav) && awy_name_find(nav, name) >= 0;
}

int a380x_nav_airway_contains(A380XNavdata *nav, const char *name, const A380XNavRef *fix)
{
    if (!name || !fix || !ensure(nav)) return 0;
    AwyGraph g;
    if (!build_graph(nav, name, &g)) return 0;
    int s = graph_start(nav, &g, fix);
    graph_free(&g);
    return s >= 0;
}

int a380x_nav_airway_path(A380XNavdata *nav, const char *name, const A380XNavRef *from, const char *to_ident,
                          A380XNavRef *out, int max)
{
    if (!name || !from || !to_ident || !ensure(nav)) return -1;
    AwyGraph g;
    if (!build_graph(nav, name, &g)) return -1;
    int s = graph_start(nav, &g, from);
    if (s < 0) { graph_free(&g); return -2; }
    int *order = (int *)malloc((size_t)g.n * sizeof(int));
    if (!order) { graph_free(&g); return -1; }
    int nv = graph_bfs(&g, s, order);
    int target = -1;
    for (int k = 1; k < nv; k++) if (!strcmp(g.node[order[k]].id, to_ident)) { target = order[k]; break; }
    if (target < 0) { free(order); graph_free(&g); return -3; }
    int len = g.node[target].depth;
    int written = len < max ? len : max;
    int v = target;
    for (int k = len - 1; k >= 0; k--) {
        if (k < written) node_ref(nav, &g.node[v], &out[k]);
        v = g.node[v].parent;
    }
    free(order);
    graph_free(&g);
    return written;
}

int a380x_nav_airway_intersection(A380XNavdata *nav, const char *awy1, const A380XNavRef *from, const char *awy2,
                                  A380XNavRef *out)
{
    if (!awy1 || !awy2 || !from || !ensure(nav)) return 0;
    AwyGraph g1, g2;
    if (!build_graph(nav, awy1, &g1)) return 0;
    if (!build_graph(nav, awy2, &g2)) { graph_free(&g1); return 0; }
    int found = 0;
    int s = graph_start(nav, &g1, from);
    if (s >= 0) {
        int *order = (int *)malloc((size_t)g1.n * sizeof(int));
        if (order) {
            int nv = graph_bfs(&g1, s, order);
            for (int k = 1; k < nv && !found; k++) {
                AwyNode *n = &g1.node[order[k]];
                for (int j = 0; j < g2.n; j++)
                    if (!strcmp(g2.node[j].id, n->id) && !strcmp(g2.node[j].rg, n->rg) && g2.node[j].type == n->type) {
                        if (out) node_ref(nav, n, out);
                        found = 1;
                        break;
                    }
            }
            free(order);
        }
    }
    graph_free(&g1);
    graph_free(&g2);
    return found;
}

int a380x_nav_airways_at(A380XNavdata *nav, const A380XNavRef *fix, char (*names)[A380X_NAV_IDENT_LEN], int max)
{
    if (!fix || !ensure(nav)) return 0;
    int count = 0;
    int32_t *seen = (int32_t *)calloc((size_t)(nav->awn_count ? nav->awn_count : 1), sizeof(int32_t));
    if (!seen) return 0;
    for (int i = 0; i < nav->seg_count; i++) {
        const AwySeg *s = &nav->seg[i];
        int m1 = !strcmp(s->id1, fix->ident) && (!fix->region[0] || !strcmp(s->rg1, fix->region));
        int m2 = !strcmp(s->id2, fix->ident) && (!fix->region[0] || !strcmp(s->rg2, fix->region));
        if ((m1 || m2) && !seen[s->name]) { seen[s->name] = 1; count++; }
    }
    if (names && max > 0) {
        int n = 0;
        for (int i = 0; i < nav->awn_count; i++) {
            if (!seen[i]) continue;
            /* insertion into sorted output */
            if (n < max) n++;
            else if (strcmp(nav->awn[i].name, names[n - 1]) >= 0) continue;
            int j = n - 1;
            while (j > 0 && strcmp(names[j - 1], nav->awn[i].name) > 0) { memcpy(names[j], names[j - 1], A380X_NAV_IDENT_LEN); j--; }
            copy_str(names[j], A380X_NAV_IDENT_LEN, nav->awn[i].name, strlen(nav->awn[i].name));
        }
    }
    free(seen);
    return count;
}

/* ============================================================================================ */
/* CIFP                                                                                         */
/* ============================================================================================ */

static const char *const LEG_NAMES[A380X_LEG_COUNT] = {
    "??", "IF", "TF", "CF", "DF", "AF", "RF", "HF", "HA", "HM", "CA", "CD", "CI", "CR",
    "FA", "FC", "FD", "FM", "PI", "VA", "VD", "VI", "VM", "VR"
};
const char *a380x_leg_type_name(A380XLegType t) { return (t > 0 && t < A380X_LEG_COUNT) ? LEG_NAMES[t] : "??"; }
A380XLegType a380x_leg_type_parse(const char *s)
{
    if (!s || !s[0] || !s[1]) return A380X_LEG_NONE;
    for (int i = 1; i < A380X_LEG_COUNT; i++) if (s[0] == LEG_NAMES[i][0] && s[1] == LEG_NAMES[i][1]) return (A380XLegType)i;
    return A380X_LEG_NONE;
}
int a380x_leg_is_xf(A380XLegType t)
{
    return t == A380X_LEG_IF || t == A380X_LEG_TF || t == A380X_LEG_CF || t == A380X_LEG_DF || t == A380X_LEG_AF
        || t == A380X_LEG_RF || t == A380X_LEG_HF;
}
int a380x_leg_is_hx(A380XLegType t) { return t == A380X_LEG_HA || t == A380X_LEG_HF || t == A380X_LEG_HM; }
int a380x_leg_is_vectors(A380XLegType t) { return t == A380X_LEG_FM || t == A380X_LEG_VM; }
int a380x_leg_starts_at_fix(A380XLegType t) { return t == A380X_LEG_FA || t == A380X_LEG_FC || t == A380X_LEG_FD || t == A380X_LEG_FM; }
int a380x_leg_is_heading(A380XLegType t)
{
    return t == A380X_LEG_VA || t == A380X_LEG_VD || t == A380X_LEG_VI || t == A380X_LEG_VM || t == A380X_LEG_VR;
}

/* Parse helpers for fixed-width CIFP fields (already split, untrimmed). */
static void ftrim(const char *f, char *out, size_t cap) { copy_trim(out, cap, f ? f : ""); }
static float f_num_scaled(const char *f, double scale)
{
    char b[16];
    ftrim(f, b, sizeof b);
    double v;
    if (!b[0] || !parse_cstr(b, &v)) return NAN;
    return (float)(v * scale);
}
static float f_alt(const char *f)
{
    char b[16];
    ftrim(f, b, sizeof b);
    double v;
    if (!b[0]) return NAN;
    if (b[0] == 'F' && b[1] == 'L') return parse_cstr(b + 2, &v) ? (float)(v * 100.0) : NAN;
    return parse_cstr(b, &v) ? (float)v : NAN;  /* "UNKNN", "NESTB" -> NAN */
}
static float f_rnp(const char *f)
{
    char b[8];
    ftrim(f, b, sizeof b);
    if (strlen(b) != 3) return NAN;
    for (int i = 0; i < 3; i++) if (b[i] < '0' || b[i] > '9') return NAN;
    double mant = (b[0] - '0') * 10 + (b[1] - '0');
    return (float)(mant * pow(10.0, -(b[2] - '0')));
}
static int parse_dms(const char *f, int lon, double *out)
{
    char b[16];
    ftrim(f, b, sizeof b);
    size_t need = lon ? 10 : 9;
    if (strlen(b) != need) return 0;
    char h = b[0];
    if ((!lon && h != 'N' && h != 'S') || (lon && h != 'E' && h != 'W')) return 0;
    for (size_t i = 1; i < need; i++) if (b[i] < '0' || b[i] > '9') return 0;
    int dl = lon ? 3 : 2;
    const char *p = b + 1;
    int deg = 0;
    for (int i = 0; i < dl; i++) deg = deg * 10 + (p[i] - '0');
    p += dl;
    int mn = (p[0] - '0') * 10 + (p[1] - '0');
    int sc = (p[2] - '0') * 10 + (p[3] - '0');
    int cs = (p[4] - '0') * 10 + (p[5] - '0');
    double v = deg + mn / 60.0 + (sc + cs / 100.0) / 3600.0;
    if (h == 'S' || h == 'W') v = -v;
    *out = v;
    return 1;
}

/* Splits on ',' and ';' in place. Fields keep their padding. */
static int split_fields(char *s, char **f, int max)
{
    int n = 0;
    f[n++] = s;
    for (; *s && n < max; s++) {
        if (*s == ',' || *s == ';') { *s = 0; f[n++] = s + 1; }
    }
    if (n > 1 && is_blank(f[n - 1])) n--;  /* trailing ';' */
    return n;
}

typedef struct {               /* parse-time transition */
    A380XNavProcTrans t;
    char proc[A380X_NAV_IDENT_LEN];
    A380XProcKind kind;
    char qual[4];
    int order;
} TmpTrans;

typedef struct {
    A380XNavRunway *rwy; int nrwy, crwy;
    TmpTrans *tr; int ntr, ctr;
    A380XNavProcLeg *leg; int nleg, cleg;
} CifpBuild;

static void ref_from_fields(A380XNavRef *r, const char *id, const char *rg)
{
    a380x_nav_ref_clear(r);
    ftrim(id, r->ident, sizeof r->ident);
    ftrim(rg, r->region, sizeof r->region);
}

static A380XProcTransRole role_for(A380XProcKind k, char rt, const char *trans)
{
    if (k == A380X_PROC_APPROACH) return rt == 'A' ? A380X_TRANS_APPR_VIA : A380X_TRANS_APPR_FINAL;
    if (k == A380X_PROC_SID) {
        switch (rt) {
        case '0': return A380X_TRANS_ENGINE_OUT;
        case '1': case '4': case 'F': case 'T': return A380X_TRANS_RUNWAY;
        case '3': case '6': case 'S': case 'V': return A380X_TRANS_ENROUTE;
        default: return A380X_TRANS_COMMON;
        }
    }
    switch (rt) {
    case '1': case '4': case '7': case 'F': return A380X_TRANS_ENROUTE;
    case '3': case '6': case '9': case 'S': return A380X_TRANS_RUNWAY;
    default: (void)trans; return A380X_TRANS_COMMON;
    }
}

static int cmp_tmptrans(const void *a, const void *b)
{
    const TmpTrans *x = (const TmpTrans *)a, *y = (const TmpTrans *)b;
    if (x->kind != y->kind) return (int)x->kind - (int)y->kind;
    int c = strcmp(x->proc, y->proc);
    if (c) return c;
    return x->order - y->order;
}
static int cmp_rwy(const void *a, const void *b)
{
    return strcmp(((const A380XNavRunway *)a)->ident, ((const A380XNavRunway *)b)->ident);
}

static A380XApproachType appr_type_from(char c)
{
    switch (c) {
    case 'B': return A380X_APPR_LOC_BACKCOURSE;
    case 'D': return A380X_APPR_VORDME;
    case 'F': return A380X_APPR_FMS;
    case 'G': return A380X_APPR_IGS;
    case 'I': return A380X_APPR_ILS;
    case 'J': return A380X_APPR_GLS;
    case 'L': return A380X_APPR_LOC;
    case 'M': return A380X_APPR_MLS;
    case 'N': return A380X_APPR_NDB;
    case 'P': return A380X_APPR_GPS;
    case 'Q': return A380X_APPR_NDBDME;
    case 'R': case 'H': return A380X_APPR_RNAV;
    case 'S': return A380X_APPR_VORDME;
    case 'T': return A380X_APPR_TACAN;
    case 'U': return A380X_APPR_SDF;
    case 'V': return A380X_APPR_VOR;
    case 'W': return A380X_APPR_MLS_A;
    case 'X': return A380X_APPR_LDA;
    case 'Y': return A380X_APPR_MLS_BC;
    default: return A380X_APPR_UNKNOWN;
    }
}

/* "I27R" -> runway "RW27R", multiple 0; "R22LX" -> "RW22L", 'X'; "R19-Y" -> "RW19", 'Y'; "VDMA" -> "" */
static void parse_appr_ident(const char *id, char *rwy, size_t cap, char *multiple)
{
    rwy[0] = 0;
    *multiple = 0;
    size_t n = strlen(id);
    if (n < 3 || id[1] < '0' || id[1] > '9' || id[2] < '0' || id[2] > '9') {
        const char *dash = strchr(id, '-');
        if (dash && dash[1]) *multiple = dash[1];
        return;
    }
    size_t k = 3;
    char side = 0;
    if (k < n && (id[k] == 'L' || id[k] == 'R' || id[k] == 'C')) {
        /* "R28RF": side R then multiple F; "R22LX" side L */
        side = id[k];
        k++;
    }
    if (k < n && id[k] == '-') k++;
    if (k < n) *multiple = id[k];
    if (side) snprintf(rwy, cap, "RW%c%c%c", id[1], id[2], side);
    else snprintf(rwy, cap, "RW%c%c", id[1], id[2]);
}

static void resolve_leg_ref(A380XNavdata *nav, A380XNavRef *r, const char *sect, const char *sub, const char *icao,
                            double alat, double alon, const CifpBuild *b, int is_fix, uint8_t *unresolved)
{
    if (!r->ident[0]) return;
    char s = sect && sect[0] ? sect[0] : ' ';
    char u = sub && sub[0] ? sub[0] : ' ';
    A380XNavKind k = A380X_NAV_NONE;
    const char *ap = "";
    if (s == 'P' && u == 'G') {                       /* runway of this airport */
        for (int i = 0; i < b->nrwy; i++)
            if (!strcmp(b->rwy[i].ident, r->ident)) {
                r->kind = A380X_NAV_RUNWAY; r->lat = b->rwy[i].thr_lat; r->lon = b->rwy[i].thr_lon;
                if (!isfinite(r->lat) && unresolved && is_fix) *unresolved = 1;
                return;
            }
        if (unresolved && is_fix) *unresolved = 1;
        return;
    }
    if (s == 'P' && u == 'A') k = A380X_NAV_AIRPORT;
    else if (s == 'P' && u == 'C') { k = A380X_NAV_WAYPOINT; ap = icao; }
    else if (s == 'P' && u == 'N') { k = A380X_NAV_NDB; ap = icao; }
    else if (s == 'P' && (u == 'I' || u == 'T')) { k = A380X_NAV_LOC; ap = icao; }
    else if (s == 'E' && (u == 'A' || u == ' ')) k = A380X_NAV_WAYPOINT;
    else if (s == 'D' && u == 'B') k = A380X_NAV_NDB;
    else if (s == 'D') k = A380X_NAV_VOR;
    A380XNavFix f;
    /* exact kind first (procedures may reach far: enroute transitions), then any kind nearby */
    int ok = a380x_nav_resolve(nav, r->ident, r->region, k, ap, alat, alon, &f);
    if (ok && isfinite(alat) && a380x_geo_distance_nm(alat, alon, f.lat, f.lon) > 1500.0) ok = 0;
    if (!ok && k != A380X_NAV_NONE) {
        ok = a380x_nav_resolve(nav, r->ident, r->region, A380X_NAV_NONE, ap, alat, alon, &f);
        if (ok && isfinite(alat) && a380x_geo_distance_nm(alat, alon, f.lat, f.lon) > 400.0) ok = 0;
    }
    if (ok) {
        char keep_rg[A380X_NAV_REGION_LEN];
        memcpy(keep_rg, r->region, sizeof keep_rg);
        a380x_nav_ref_from_fix(r, &f);
        if (keep_rg[0]) memcpy(r->region, keep_rg, sizeof keep_rg);
    } else if (unresolved && is_fix) *unresolved = 1;
}

static void cifp_parse_leg(char **f, int n, A380XNavProcLeg *L)
{
    memset(L, 0, sizeof *L);
    a380x_nav_ref_clear(&L->fix);
    a380x_nav_ref_clear(&L->navaid);
    a380x_nav_ref_clear(&L->center);
#define FLD(i) ((i) < n ? f[i] : "")
    ref_from_fields(&L->fix, FLD(4), FLD(5));
    {   /* 4-char description code, positional */
        const char *d = FLD(8);
        size_t dl = strlen(d);
        for (int i = 0; i < 4; i++) L->desc[i] = (size_t)i < dl ? d[i] : ' ';
        L->desc[4] = 0;
    }
    char tmp[16];
    ftrim(FLD(9), tmp, sizeof tmp);
    L->turn = (tmp[0] == 'L' || tmp[0] == 'R' || tmp[0] == 'E') ? tmp[0] : 0;
    L->rnp_nm = f_rnp(FLD(10));
    ftrim(FLD(11), tmp, sizeof tmp);
    L->type = a380x_leg_type_parse(tmp);
    ftrim(FLD(12), tmp, sizeof tmp);
    L->turn_valid = tmp[0] == 'Y';
    ref_from_fields(&L->navaid, FLD(13), FLD(14));
    L->arc_radius_nm = f_num_scaled(FLD(17), 0.001);
    L->theta_deg = f_num_scaled(FLD(18), 0.1);
    L->rho_nm = f_num_scaled(FLD(19), 0.1);
    ftrim(FLD(20), tmp, sizeof tmp);
    L->course_deg = NAN;
    if (tmp[0]) {
        size_t cl = strlen(tmp);
        double v;
        if (tmp[cl - 1] == 'T') {
            tmp[cl - 1] = 0;
            if (parse_cstr(tmp, &v)) { L->course_deg = (float)(cl - 1 >= 4 ? v / 10.0 : v); L->course_true = 1; }
        } else if (parse_cstr(tmp, &v)) L->course_deg = (float)(v / 10.0);
    }
    ftrim(FLD(21), tmp, sizeof tmp);
    L->dist_nm = NAN; L->time_min = NAN;
    if (tmp[0] == 'T') { double v; if (parse_cstr(tmp + 1, &v)) L->time_min = (float)(v / 10.0); }
    else if (tmp[0]) { double v; if (parse_cstr(tmp, &v)) L->dist_nm = (float)(v / 10.0); }
    ftrim(FLD(22), tmp, sizeof tmp);
    L->alt1_ft = f_alt(FLD(23));
    L->alt2_ft = f_alt(FLD(24));
    L->alt_desc = tmp[0] ? tmp[0] : (isfinite(L->alt1_ft) ? '@' : 0);
    if (!isfinite(L->alt1_ft) && !isfinite(L->alt2_ft)) L->alt_desc = 0;
    L->trans_alt_ft = f_alt(FLD(25));
    ftrim(FLD(26), tmp, sizeof tmp);
    L->speed_kt = f_num_scaled(FLD(27), 1.0);
    L->spd_desc = isfinite(L->speed_kt) ? (tmp[0] ? tmp[0] : '@') : 0;
    L->vertical_angle_deg = f_num_scaled(FLD(28), 0.01);
    ref_from_fields(&L->center, FLD(30), FLD(31));
    L->overfly = L->desc[1] == 'Y' || L->desc[1] == 'B';
    L->missed_start = L->desc[2] == 'M';
#undef FLD
}

static void runway_finish(A380XNavdata *nav, A380XNavRunway *rw, int n, const char *icao, double var)
{
    for (int i = 0; i < n; i++) {
        A380XNavRunway *r = &rw[i];
        int opp_num = (r->number + 18 - 1) % 36 + 1;
        char opp_side = r->side == 'L' ? 'R' : (r->side == 'R' ? 'L' : r->side);
        char opp[8];
        if (opp_side) snprintf(opp, sizeof opp, "RW%02d%c", opp_num, opp_side);
        else snprintf(opp, sizeof opp, "RW%02d", opp_num);
        const A380XNavRunway *o = NULL;
        for (int j = 0; j < n; j++) if (!strcmp(rw[j].ident, opp)) { o = &rw[j]; break; }
        /* localizer from earth_nav (ident + airport) */
        if (r->ls_ident[0]) {
            for (int32_t k = chain_head(nav, r->ls_ident); k >= 0; k = nav->pts[k].next) {
                const NavPoint *p = &nav->pts[k];
                if (p->kind != A380X_NAV_LOC || strcmp(p->ident, r->ls_ident) || strcmp(p->area, icao) || p->extra < 0) continue;
                const NavExtra *e = &nav->ext[p->extra];
                r->ls_freq_mhz = e->freq;
                r->ls_course_mag = e->loc_mag;
                r->ls_has_gs = (p->flags & A380X_NAVF_GS) != 0;
                r->gs_angle_deg = e->gs_angle;
                if (!o && !strcmp(e->runway, r->designator) && isfinite(e->loc_true)) r->true_brg = e->loc_true;
                break;
            }
        }
        if (o && isfinite(r->thr_lat) && isfinite(o->thr_lat)) {
            double d_ft = a380x_geo_distance_nm(r->thr_lat, r->thr_lon, o->thr_lat, o->thr_lon) * 6076.115;
            r->true_brg = (float)a380x_geo_bearing_deg(r->thr_lat, r->thr_lon, o->thr_lat, o->thr_lon);
            r->length_ft = (float)(d_ft + (isfinite(r->disp_thr_ft) ? r->disp_thr_ft : 0) + (isfinite(o->disp_thr_ft) ? o->disp_thr_ft : 0));
        }
        if (isfinite(r->true_brg) && isfinite(r->thr_lat)) {
            r->mag_brg = (float)a380x_geo_norm360(r->true_brg - var);
            double back = a380x_geo_norm360(r->true_brg + 180.0);
            double dnm = isfinite(r->disp_thr_ft) ? r->disp_thr_ft / 6076.115 : 0.0;
            a380x_geo_destination(r->thr_lat, r->thr_lon, back, dnm, &r->start_lat, &r->start_lon);
        }
    }
}

static A380XNavAirport *cifp_load(A380XNavdata *nav, const char *icao)
{
    A380XNavAirportInfo info;
    int known = a380x_nav_airport_info(nav, icao, &info);
    char fname[32];
    snprintf(fname, sizeof fname, "CIFP/%s.dat", icao);
    LineReader lr;
    int have = lr_open(&lr, nav->custom_dir, fname);
    if (!have) have = lr_open(&lr, nav->default_dir, fname);
    if (!have && !known) return NULL;
    CifpBuild b;
    memset(&b, 0, sizeof b);
    int malformed = 0, order = 0;
    char *line;
    char *f[48];
    char cur_key[48] = "";
    while (have && (line = lr_next(&lr)) != NULL) {
        if (!line[0]) continue;
        if (lr.too_long) { malformed++; continue; }
        char *colon = strchr(line, ':');
        if (!colon) { malformed++; continue; }
        *colon = 0;
        const char *rec = line;
        int n = split_fields(colon + 1, f, 48);
        if (!strcmp(rec, "RWY")) {
            if (n < 4) { malformed++; continue; }
            A380XNavRunway r;
            memset(&r, 0, sizeof r);
            ftrim(f[0], r.ident, sizeof r.ident);
            if (strncmp(r.ident, "RW", 2) || strlen(r.ident) < 4 || r.ident[2] < '0' || r.ident[2] > '9') { malformed++; continue; }
            /* about 5 % of the airports carry no threshold group: keep the runway, position unknown */
            if (n < 10 || !parse_dms(f[8], 0, &r.thr_lat) || !parse_dms(f[9], 1, &r.thr_lon)) { r.thr_lat = NAN; r.thr_lon = NAN; }
            copy_str(r.designator, sizeof r.designator, r.ident + 2, strlen(r.ident + 2));
            r.number = (r.designator[0] - '0') * 10 + (r.designator[1] - '0');
            r.side = (r.designator[2] == 'L' || r.designator[2] == 'R' || r.designator[2] == 'C') ? r.designator[2] : 0;
            r.gradient_pct = f_num_scaled(f[1], 0.001);
            r.thr_elev_ft = f_num_scaled(f[3], 1.0);
            ftrim(n > 5 ? f[5] : "", r.ls_ident, sizeof r.ls_ident);
            char cat[8];
            ftrim(n > 6 ? f[6] : "", cat, sizeof cat);
            r.ls_category = (cat[0] >= '1' && cat[0] <= '3') ? cat[0] - '0' : (r.ls_ident[0] ? 4 : 0);
            r.tch_ft = f_num_scaled(n > 7 ? f[7] : "", 1.0);
            r.disp_thr_ft = f_num_scaled(n > 10 ? f[10] : "", 1.0);
            r.true_brg = r.mag_brg = r.length_ft = NAN;
            r.ls_course_mag = NAN; r.gs_angle_deg = NAN;
            r.start_lat = r.thr_lat; r.start_lon = r.thr_lon;
            if (!grow(nav, (void **)&b.rwy, &b.crwy, b.nrwy, sizeof(A380XNavRunway))) break;
            b.rwy[b.nrwy++] = r;
            continue;
        }
        A380XProcKind kind;
        if (!strcmp(rec, "SID")) kind = A380X_PROC_SID;
        else if (!strcmp(rec, "STAR")) kind = A380X_PROC_STAR;
        else if (!strcmp(rec, "APPCH")) kind = A380X_PROC_APPROACH;
        else continue;   /* PRDAT, helicopter records, future records */
        if (n < 23) { malformed++; continue; }
        char rt[4], proc[16], trans[16];
        ftrim(f[1], rt, sizeof rt);
        ftrim(f[2], proc, sizeof proc);
        ftrim(f[3], trans, sizeof trans);
        if (!rt[0] || !proc[0] || strlen(proc) >= A380X_NAV_IDENT_LEN || strlen(trans) >= A380X_NAV_IDENT_LEN) { malformed++; continue; }
        A380XNavProcLeg L;
        cifp_parse_leg(f, n, &L);
        if (L.type == A380X_LEG_NONE) { malformed++; continue; }
        char key[48];
        snprintf(key, sizeof key, "%d|%s|%s|%s", (int)kind, proc, rt, trans);
        if (strcmp(key, cur_key) || !b.ntr) {
            if (!grow(nav, (void **)&b.tr, &b.ctr, b.ntr, sizeof(TmpTrans))) break;
            TmpTrans *t = &b.tr[b.ntr++];
            memset(t, 0, sizeof *t);
            copy_str(t->t.ident, sizeof t->t.ident, trans, strlen(trans));
            t->t.route_type = rt[0];
            t->t.role = role_for(kind, rt[0], trans);
            t->t.first_leg = b.nleg;
            copy_str(t->proc, sizeof t->proc, proc, strlen(proc));
            t->kind = kind;
            t->order = order++;
            if (kind == A380X_PROC_APPROACH && n > 37) {
                char q[4];
                ftrim(f[35], q, sizeof q); t->qual[0] = q[0] ? q[0] : ' ';
                ftrim(f[36], q, sizeof q); t->qual[1] = q[0] ? q[0] : ' ';
                ftrim(f[37], q, sizeof q); t->qual[2] = q[0] ? q[0] : ' ';
            }
            memcpy(cur_key, key, sizeof cur_key);
        }
        /* fix sections are resolved after the runways are known */
        if (!grow(nav, (void **)&b.leg, &b.cleg, b.nleg, sizeof(A380XNavProcLeg))) break;
        /* stash the section/subsection codes in unused bytes of the refs until resolution */
        L.fix.flags = (uint8_t)(f[6][0] ? f[6][0] : ' ');
        L.fix.kind = (uint8_t)(f[7][0] ? f[7][0] : ' ');
        L.navaid.flags = (uint8_t)((n > 15 && f[15][0]) ? f[15][0] : ' ');
        L.navaid.kind = (uint8_t)((n > 16 && f[16][0]) ? f[16][0] : ' ');
        L.center.flags = (uint8_t)((n > 32 && f[32][0]) ? f[32][0] : ' ');
        L.center.kind = (uint8_t)((n > 33 && f[33][0]) ? f[33][0] : ' ');
        b.leg[b.nleg++] = L;
        b.tr[b.ntr - 1].t.leg_count++;
    }
    lr_close(&lr);

    A380XNavAirport *a = (A380XNavAirport *)nav_calloc(nav, 1, sizeof *a);
    if (!a) {
        nav_free(nav, b.rwy); nav_free(nav, b.tr); nav_free(nav, b.leg);
        return NULL;
    }
    a->info = info;
    if (!known) { copy_str(a->info.icao, sizeof a->info.icao, icao, strlen(icao)); a->info.lat = a->info.lon = NAN; a->info.elev_ft = NAN;
                  a->info.trans_alt_ft = a->info.trans_level_ft = a->info.longest_rwy_ft = NAN; }
    a->info.has_cifp = (uint8_t)have;
    if (!isfinite(a->info.lat) && b.nrwy) {   /* no reference point: mean of the thresholds */
        double sl = 0, so = 0;
        int k = 0;
        for (int i = 0; i < b.nrwy; i++) if (isfinite(b.rwy[i].thr_lat)) { sl += b.rwy[i].thr_lat; so += b.rwy[i].thr_lon; k++; }
        if (k) { a->info.lat = sl / k; a->info.lon = so / k; }
        if (!isfinite(a->info.elev_ft)) a->info.elev_ft = b.rwy[0].thr_elev_ft;
        if (k) a->info.magvar_deg = (float)a380x_nav_magvar(nav, a->info.lat, a->info.lon);
    }
    double alat = a->info.lat, alon = a->info.lon;
    runway_finish(nav, b.rwy, b.nrwy, icao, a->info.magvar_deg);
    /* resolve leg references */
    for (int i = 0; i < b.nleg; i++) {
        A380XNavProcLeg *L = &b.leg[i];
        char s1[2] = { (char)L->fix.flags, 0 }, u1[2] = { (char)L->fix.kind, 0 };
        char s2[2] = { (char)L->navaid.flags, 0 }, u2[2] = { (char)L->navaid.kind, 0 };
        char s3[2] = { (char)L->center.flags, 0 }, u3[2] = { (char)L->center.kind, 0 };
        L->fix.flags = 0; L->fix.kind = 0; L->navaid.flags = 0; L->navaid.kind = 0; L->center.flags = 0; L->center.kind = 0;
        resolve_leg_ref(nav, &L->fix, s1, u1, icao, alat, alon, &b, 1, &L->fix_unresolved);
        resolve_leg_ref(nav, &L->navaid, s2, u2, icao, alat, alon, &b, 0, NULL);
        resolve_leg_ref(nav, &L->center, s3, u3, icao, alat, alon, &b, 0, NULL);
        if (L->fix_unresolved) a->unresolved_fixes++;
    }
    /* group transitions into procedures (stable: kind, ident, file order); drop duplicate runs */
    qsort(b.tr, (size_t)b.ntr, sizeof(TmpTrans), cmp_tmptrans);
    qsort(b.rwy, (size_t)b.nrwy, sizeof(A380XNavRunway), cmp_rwy);
    a->runways = b.rwy; a->runway_count = b.nrwy;
    a->legs = b.leg; a->leg_count = b.nleg;
    a->trans = (A380XNavProcTrans *)nav_alloc(nav, (size_t)(b.ntr ? b.ntr : 1) * sizeof(A380XNavProcTrans));
    a->procs = (A380XNavProcedure *)nav_alloc(nav, (size_t)(b.ntr ? b.ntr : 1) * sizeof(A380XNavProcedure));
    if (!a->trans || !a->procs) { nav_free(nav, b.tr); free_airport(nav, a); return NULL; }
    for (int i = 0; i < b.ntr; i++) {
        TmpTrans *t = &b.tr[i];
        A380XNavProcedure *p = a->proc_count ? &a->procs[a->proc_count - 1] : NULL;
        if (!p || p->kind != t->kind || strcmp(p->ident, t->proc)) {
            p = &a->procs[a->proc_count++];
            memset(p, 0, sizeof *p);
            copy_str(p->ident, sizeof p->ident, t->proc, strlen(t->proc));
            p->kind = t->kind;
            p->first_trans = a->trans_count;
            if (t->kind == A380X_PROC_APPROACH) parse_appr_ident(p->ident, p->runway, sizeof p->runway, &p->multiple);
        }
        int dup = 0;
        for (int k = p->first_trans; k < a->trans_count; k++)
            if (a->trans[k].route_type == t->t.route_type && !strcmp(a->trans[k].ident, t->t.ident)) dup = 1;
        if (dup) { malformed += t->t.leg_count; continue; }
        a->trans[a->trans_count++] = t->t;
        p->trans_count++;
        if (t->kind == A380X_PROC_APPROACH && t->t.role == A380X_TRANS_APPR_FINAL && t->t.route_type != 'Z') {
            p->appr_code = t->t.route_type;
            p->appr_type = appr_type_from(t->t.route_type);
            memcpy(p->qualifiers, t->qual, 3);
            p->qualifiers[3] = 0;
            p->rnp_ar = t->qual[1] == 'P';
        }
    }
    for (int i = 0; i < a->proc_count; i++)
        if (a->procs[i].kind == A380X_PROC_APPROACH && !a->procs[i].appr_code) {
            a->procs[i].appr_code = a->procs[i].ident[0];
            a->procs[i].appr_type = appr_type_from(a->procs[i].ident[0]);
        }
    nav_free(nav, b.tr);
    a->malformed_lines = malformed;
    nav->malformed += malformed;
    a->generation = ++nav->generation;
    nav->cifp_loads += have;
    return a;
}

const A380XNavAirport *a380x_nav_airport(A380XNavdata *nav, const char *icao)
{
    if (!icao || !*icao || strlen(icao) >= A380X_NAV_ICAO_LEN || !ensure(nav)) return NULL;
    nav->use_clock++;
    for (int i = 0; i < nav->slot_count; i++)
        if (!strcmp(nav->slots[i].icao, icao)) { nav->slots[i].last_use = nav->use_clock; return nav->slots[i].apt; }
    A380XNavAirport *a = cifp_load(nav, icao);
    int slot;
    if (nav->slot_count < nav->cifp_cap) slot = nav->slot_count++;
    else {
        slot = 0;
        for (int i = 1; i < nav->slot_count; i++) if (nav->slots[i].last_use < nav->slots[slot].last_use) slot = i;
        free_airport(nav, nav->slots[slot].apt);
    }
    copy_str(nav->slots[slot].icao, sizeof nav->slots[slot].icao, icao, strlen(icao));
    nav->slots[slot].apt = a;
    nav->slots[slot].last_use = nav->use_clock;
    return a;
}

const A380XNavRunway *a380x_nav_runway(const A380XNavAirport *apt, const char *ident)
{
    if (!apt || !ident) return NULL;
    char want[A380X_NAV_IDENT_LEN];
    if (!strncmp(ident, "RW", 2)) copy_str(want, sizeof want, ident, strlen(ident));
    else snprintf(want, sizeof want, "RW%s", ident);
    for (int i = 0; i < apt->runway_count; i++) if (!strcmp(apt->runways[i].ident, want)) return &apt->runways[i];
    return NULL;
}

const A380XNavProcedure *a380x_nav_procedure(const A380XNavAirport *apt, A380XProcKind kind, const char *ident)
{
    if (!apt || !ident) return NULL;
    for (int i = 0; i < apt->proc_count; i++)
        if (apt->procs[i].kind == kind && !strcmp(apt->procs[i].ident, ident)) return &apt->procs[i];
    return NULL;
}
int a380x_nav_procedure_count(const A380XNavAirport *apt, A380XProcKind kind)
{
    int n = 0;
    if (apt) for (int i = 0; i < apt->proc_count; i++) if (apt->procs[i].kind == kind) n++;
    return n;
}
const A380XNavProcedure *a380x_nav_procedure_at(const A380XNavAirport *apt, A380XProcKind kind, int index)
{
    if (!apt || index < 0) return NULL;
    for (int i = 0; i < apt->proc_count; i++)
        if (apt->procs[i].kind == kind && index-- == 0) return &apt->procs[i];
    return NULL;
}
int a380x_nav_proc_transitions(const A380XNavAirport *apt, const A380XNavProcedure *p, A380XProcTransRole role,
                               const A380XNavProcTrans **out, int max)
{
    int n = 0;
    if (!apt || !p) return 0;
    for (int i = 0; i < p->trans_count; i++) {
        const A380XNavProcTrans *t = &apt->trans[p->first_trans + i];
        if (t->role != role) continue;
        if (out && n < max) out[n] = t;
        n++;
    }
    return n;
}
int a380x_nav_trans_matches_runway(const char *trans, const char *rwy)
{
    if (!trans || !trans[0] || !strcmp(trans, "ALL")) return 1;
    if (!rwy || !rwy[0]) return 0;
    if (!strcmp(trans, rwy)) return 1;
    size_t n = strlen(trans);
    if (n == 5 && trans[4] == 'B' && !strncmp(trans, "RW", 2) && !strncmp(trans, rwy, 4)) return 1;
    return 0;
}
static int is_rwy_ident(const char *s) { return s && s[0] == 'R' && s[1] == 'W' && s[2] >= '0' && s[2] <= '9'; }
int a380x_nav_sid_serves_runway(const A380XNavAirport *apt, const A380XNavProcedure *p, const char *rwy)
{
    if (!apt || !p) return 0;
    if (!rwy || !rwy[0]) return 1;
    int specific = 0;
    for (int i = 0; i < p->trans_count; i++) {
        const A380XNavProcTrans *t = &apt->trans[p->first_trans + i];
        if (t->role == A380X_TRANS_RUNWAY || (t->role == A380X_TRANS_COMMON && is_rwy_ident(t->ident))) {
            if (!is_rwy_ident(t->ident) && strcmp(t->ident, "ALL")) continue;
            specific = 1;
            if (a380x_nav_trans_matches_runway(t->ident, rwy)) return 1;
        }
    }
    return !specific;
}
int a380x_nav_star_serves_runway(const A380XNavAirport *apt, const A380XNavProcedure *p, const char *rwy)
{
    if (!apt || !p) return 0;
    if (!rwy || !rwy[0]) return 1;
    int specific = 0;
    for (int i = 0; i < p->trans_count; i++) {
        const A380XNavProcTrans *t = &apt->trans[p->first_trans + i];
        if (t->role == A380X_TRANS_RUNWAY || (t->role == A380X_TRANS_COMMON && is_rwy_ident(t->ident))) {
            specific = 1;
            if (a380x_nav_trans_matches_runway(t->ident, rwy)) return 1;
        }
    }
    return !specific;
}

static const char *appr_type_name(A380XApproachType t)
{
    switch (t) {
    case A380X_APPR_ILS: return "ILS";
    case A380X_APPR_GLS: return "GLS";
    case A380X_APPR_IGS: return "IGS";
    case A380X_APPR_LOC: return "LOC";
    case A380X_APPR_LOC_BACKCOURSE: return "BAC";
    case A380X_APPR_LDA: return "LDA";
    case A380X_APPR_SDF: return "SDF";
    case A380X_APPR_GPS: return "GPS";
    case A380X_APPR_RNAV: return "RNV";
    case A380X_APPR_VOR: case A380X_APPR_VORDME: case A380X_APPR_VORTAC: return "VOR";
    case A380X_APPR_NDB: case A380X_APPR_NDBDME: return "NDB";
    case A380X_APPR_FMS: return "RNAV";
    case A380X_APPR_MLS: case A380X_APPR_MLS_A: case A380X_APPR_MLS_BC: return "MLS";
    case A380X_APPR_TACAN: return "TAC";
    default: return "";
    }
}
void a380x_nav_approach_name(const A380XNavProcedure *p, int rnp_suffix, char *out, size_t cap)
{
    if (!out || !cap) return;
    out[0] = 0;
    if (!p) return;
    const char *desig = p->runway[0] ? p->runway + 2 : "";
    char mult[3] = "";
    if (p->multiple) { mult[0] = '-'; mult[1] = p->multiple; mult[2] = 0; }
    snprintf(out, cap, "%s%s%s%s", appr_type_name(p->appr_type), desig, mult, (rnp_suffix && p->rnp_ar) ? " (RNP)" : "");
}
int a380x_nav_approach_type_order(A380XApproachType t)
{
    switch (t) {   /* ARR:25-46 */
    case A380X_APPR_MLS: return 0;
    case A380X_APPR_MLS_A: return 1;
    case A380X_APPR_MLS_BC: return 2;
    case A380X_APPR_ILS: return 3;
    case A380X_APPR_GLS: return 4;
    case A380X_APPR_IGS: return 5;
    case A380X_APPR_LOC: return 6;
    case A380X_APPR_LOC_BACKCOURSE: return 7;
    case A380X_APPR_LDA: return 8;
    case A380X_APPR_SDF: return 9;
    case A380X_APPR_FMS: return 10;
    case A380X_APPR_GPS: return 11;
    case A380X_APPR_RNAV: return 12;
    case A380X_APPR_VORDME: case A380X_APPR_VORTAC: case A380X_APPR_TACAN: return 13;
    case A380X_APPR_VOR: return 14;
    case A380X_APPR_NDBDME: return 15;
    case A380X_APPR_NDB: return 16;
    default: return 17;
    }
}
