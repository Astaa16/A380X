/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X navigation database service (WP-D, docs/mfd-spec/11-xplane-navdata.md).
 *
 * Reads the installed X-Plane 12 navigation data files and nothing else:
 *   earth_fix.dat (XP-FIX1200)      waypoints, enroute and terminal
 *   earth_nav.dat (XP-NAV1200)      VOR, DME, TACAN, NDB, ILS/LOC (+GS, DME-ILS merged)
 *   earth_awy.dat (XP-AWY1100)      airway segments
 *   earth_hold.dat (XP-HOLD1140)    published holdings (optional, loaded on first use)
 *   earth_aptmeta.dat (XP-APT1210)  airport reference point, elevation, TA/TL (optional)
 *   CIFP/<ICAO>.dat (XP-CIFP1101)   runways, SIDs, STARs, approaches (per airport, on demand)
 *   cycle.json / file headers       AIRAC cycle for DATA/STATUS
 * `<root>/Custom Data/` overrides `<root>/Resources/default data/` file by file (and CIFP airport
 * by airport), exactly like X-Plane.
 *
 * Simulator independent C11: no XPLM, no OpenGL, stdio only. Anything the files lack (airport name,
 * airport reference point when earth_aptmeta.dat has none, magnetic variation) comes through the
 * A380XNavdataHost callback table, which the X-Plane adapter implements with XPLMFindNavAid /
 * XPLMGetNavAidInfo / XPLMGetMagneticVariation and tests implement with a fake.
 *
 * Memory: global files are loaded once (first lookup, or a380x_nav_load_global), ~18 MB heap for a
 * full world database (~26 MB peak RSS while loading); CIFP is cached per airport in an LRU (default 8).
 * Everything is freed by a380x_nav_destroy. Not thread safe: call from one thread (the flight loop),
 * or load on a worker thread before the first flight-loop use and never concurrently.
 *
 * Units: latitude/longitude degrees (north/east positive), distances NM, altitudes feet, courses
 * degrees, frequencies MHz (VHF) or kHz (NDB). "Magnetic variation" is east positive:
 * magnetic = true - variation.
 */
#ifndef A380X_NAVDATA_H
#define A380X_NAVDATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define A380X_NAV_IDENT_LEN   8   /* fix/navaid/airway/procedure ident incl. NUL (data uses <= 7) */
#define A380X_NAV_REGION_LEN  3   /* ICAO region code, e.g. "EG", "K6" */
#define A380X_NAV_ICAO_LEN    8   /* airport ident incl. NUL ("EGLL", "00AN") */
#define A380X_NAV_NAME_LEN   40
#define A380X_NAV_PI          3.14159265358979323846
#define A380X_NAV_EARTH_NM    3440.065 /* mean earth radius, NM */

typedef struct A380XNavdata A380XNavdata; /* opaque */

/* ------------------------------------------------------------------------------------------ */
/* Basic records                                                                               */
/* ------------------------------------------------------------------------------------------ */

typedef enum {
    A380X_NAV_NONE = 0,
    A380X_NAV_WAYPOINT,  /* earth_fix.dat */
    A380X_NAV_VOR,       /* VOR, VOR/DME, VORTAC (flags tell DME/TACAN) */
    A380X_NAV_DME,       /* standalone DME or TACAN */
    A380X_NAV_NDB,
    A380X_NAV_LOC,       /* ILS localizer (flag GS when a glide slope exists), LOC, LDA, SDF */
    A380X_NAV_AIRPORT,
    A380X_NAV_RUNWAY,    /* CIFP runway threshold */
    A380X_NAV_USER       /* pilot defined: lat/lon, place/bearing/distance, place-bearing/place-bearing */
} A380XNavKind;

/* Kind masks for the lookup functions. */
#define A380X_NAVMASK(k)      (1u << (unsigned)(k))
#define A380X_NAVMASK_NAVAIDS (A380X_NAVMASK(A380X_NAV_VOR) | A380X_NAVMASK(A380X_NAV_DME) | \
                               A380X_NAVMASK(A380X_NAV_NDB) | A380X_NAVMASK(A380X_NAV_LOC))
#define A380X_NAVMASK_ALL     0xFFFFFFFFu
/* What a pilot "place" entry (INSERT NEXT WPT, DIR TO, AIRWAYS TO, FIX INFO) may resolve to:
 * waypoints, VOR/DME/NDB and airports (LOCs are excluded like the MSFS facility search). */
#define A380X_NAVMASK_PLACE   (A380X_NAVMASK(A380X_NAV_WAYPOINT) | A380X_NAVMASK(A380X_NAV_VOR) | \
                               A380X_NAVMASK(A380X_NAV_DME) | A380X_NAVMASK(A380X_NAV_NDB) | \
                               A380X_NAVMASK(A380X_NAV_AIRPORT))

enum {                                 /* A380XNavFix.flags / A380XNavRef.flags */
    A380X_NAVF_DME      = 1u << 0,     /* VOR has a co-located DME, LOC has a DME-ILS */
    A380X_NAVF_TACAN    = 1u << 1,     /* TACAN or VORTAC */
    A380X_NAVF_GS       = 1u << 2,     /* LOC has a glide slope (ILS) */
    A380X_NAVF_TERMINAL = 1u << 3,     /* terminal-area record (airport != "") */
    A380X_NAVF_VOR_ONLY = 1u << 4      /* VOR without DME */
};

/* Full lookup result (copy, owns no pointers; safe to keep). */
typedef struct {
    A380XNavKind kind;
    uint16_t flags;
    char   ident[A380X_NAV_IDENT_LEN];
    char   region[A380X_NAV_REGION_LEN];
    char   airport[A380X_NAV_ICAO_LEN];   /* terminal area airport, "" for enroute */
    double lat, lon;
    float  elev_ft;        /* NAN if unknown */
    float  freq;           /* VOR/DME/LOC MHz, NDB kHz, 0 if none */
    float  range_nm;       /* 0 if unknown */
    float  station_var;    /* VOR/DME: slaved variation (station declination, east +); NAN otherwise */
    float  loc_true_brg;   /* LOC: front course true; NAN otherwise */
    float  loc_mag_brg;    /* LOC: front course magnetic (integer degrees in the data); NAN otherwise */
    float  gs_angle_deg;   /* LOC with GS: glide path angle; NAN otherwise */
    double dme_lat, dme_lon; /* VOR/LOC with DME: DME antenna position (else = lat/lon) */
    char   wpt_type[4];    /* earth_fix ARINC 424 waypoint type columns 1..3, e.g. "W E" */
    char   name[A380X_NAV_NAME_LEN];  /* navaid / airport name, "" for waypoints */
    int32_t db_id;         /* internal record id (>= 0) or -1 for USER/RUNWAY/host airports */
    double dist_nm;        /* filled by the "near" lookups / a380x_nav_sort_nearest, else 0 */
} A380XNavFix;

/* Compact fix reference stored in procedure legs and flight-plan legs. lat/lon are NAN when the
 * reference is empty (ident "") or could not be resolved. */
typedef struct {
    char    ident[A380X_NAV_IDENT_LEN];
    char    region[A380X_NAV_REGION_LEN];
    uint8_t kind;          /* A380XNavKind */
    uint8_t flags;         /* A380X_NAVF_* (low 8 bits) */
    double  lat, lon;
    float   freq;          /* copied from the navaid, 0 for fixes */
    float   station_var;   /* VOR declination, NAN otherwise */
} A380XNavRef;

/* ------------------------------------------------------------------------------------------ */
/* Configuration, host callbacks, cycle                                                        */
/* ------------------------------------------------------------------------------------------ */

typedef struct {
    char   icao[A380X_NAV_ICAO_LEN];
    char   region[A380X_NAV_REGION_LEN];
    char   name[A380X_NAV_NAME_LEN];
    double lat, lon;            /* airport reference point; NAN if unknown */
    float  elev_ft;             /* NAN if unknown */
    float  trans_alt_ft;        /* database transition altitude, NAN if none */
    float  trans_level_ft;      /* database transition level in FEET (FL180 -> 18000), NAN if none */
    float  longest_rwy_ft;      /* earth_aptmeta, NAN if unknown */
    float  magvar_deg;          /* variation used for this airport's procedures (east +) */
    uint8_t has_cifp;           /* a CIFP file exists (set once the airport has been loaded) */
    uint8_t from_aptmeta;       /* position/elevation came from earth_aptmeta.dat */
    uint8_t from_host;          /* position/elevation/name came from the host callback */
} A380XNavAirportInfo;

typedef struct {
    void *ctx;
    /* Airport reference point / elevation / name. Return 1 and fill `out` (lat, lon, elev_ft,
     * name; other fields may be left untouched) when found, else 0. Called when earth_aptmeta.dat
     * has no record for the airport, and to obtain the name. X-Plane adapter: XPLMFindNavAid(NULL,
     * icao, NULL, NULL, NULL, xplm_Nav_Airport) + XPLMGetNavAidInfo (height is in metres there).
     * May be NULL. */
    int  (*airport_info)(void *ctx, const char *icao, A380XNavAirportInfo *out);
    /* Magnetic variation (declination, east positive) at a position. Return 1 when valid.
     * X-Plane adapter: XPLMGetMagneticVariation(lat, lon) (XPLM300+). When NULL or returning 0,
     * the service estimates it from ILS localizer courses at the airport or the nearest VOR's
     * station declination (see spec 11 §8). */
    int  (*magvar)(void *ctx, double lat, double lon, double *out_deg);
    /* Optional diagnostics sink (malformed lines summary, missing files). May be NULL. */
    void (*log)(void *ctx, const char *message);
} A380XNavdataHost;

typedef struct {
    /* X-Plane root (XPLMGetSystemPath). Custom Data = <root>/Custom Data, default data =
     * <root>/Resources/default data. Either may be overridden below. '/' works on Windows too. */
    const char *xplane_root;
    const char *custom_data_dir;   /* NULL = derived from xplane_root; "" = none */
    const char *default_data_dir;  /* NULL = derived from xplane_root; "" = none */
    int cifp_cache_size;           /* airports kept in the CIFP LRU; <= 0 means 8 */
} A380XNavdataConfig;

typedef struct {
    int  valid;
    char airac[5];        /* "2609" */
    char revision[8];     /* cycle.json "revision", "" if unknown */
    char provider[16];    /* "XP" (X-Plane format data), see spec 03 §5.4 serial */
    int  eff_year, eff_month, eff_day;   /* effective from (UTC) */
    int  exp_year, exp_month, exp_day;   /* first day of the next cycle (FBW DatabaseIdent.effectiveTo) */
    int  from_custom_data;               /* 1 when Custom Data provides the global files */
    char source_file[16];                /* "cycle.json" or "earth_nav.dat" */
} A380XNavCycle;

typedef struct {
    int    loaded;                   /* global files loaded */
    int    waypoints, navaids, airports_meta, airway_segments, airway_names, holds;
    int    cifp_cached, cifp_loads;  /* airports in the LRU, CIFP files parsed so far */
    int    malformed_lines;          /* lines skipped by the parsers */
    size_t bytes;                    /* heap bytes currently owned by the service */
    double global_load_ms;           /* CPU time of the last global load */
    int    custom_fix, custom_nav, custom_awy, custom_meta; /* 1 = file came from Custom Data */
} A380XNavStats;

/* Creates the service; does not read any file yet. Strings in cfg are copied. host may be NULL. */
A380XNavdata *a380x_nav_create(const A380XNavdataConfig *cfg, const A380XNavdataHost *host);
void          a380x_nav_destroy(A380XNavdata *nav);
/* Loads fixes, navaids, airways and airport metadata (idempotent). Lookups call it implicitly.
 * Returns 0 on success, -1 if neither data directory has earth_fix.dat and earth_nav.dat. */
int  a380x_nav_load_global(A380XNavdata *nav);
/* Frees everything and re-reads on next use (for DATA/STATUS SWAP or a changed Custom Data). */
void a380x_nav_reload(A380XNavdata *nav);
const A380XNavCycle *a380x_nav_cycle(A380XNavdata *nav);
void a380x_nav_stats(A380XNavdata *nav, A380XNavStats *out);
/* AIRAC effective dates for a cycle "YYNN" (spec 03 §5.4). Returns 0 on success. */
int  a380x_nav_airac_dates(const char *yynn, int *eff_y, int *eff_m, int *eff_d,
                           int *exp_y, int *exp_m, int *exp_d);

/* ------------------------------------------------------------------------------------------ */
/* Fix / navaid / airport lookup                                                               */
/* ------------------------------------------------------------------------------------------ */

/* All records whose ident equals `ident` (case-sensitive, upper case data) and whose kind is in
 * `kind_mask`. Airports come from earth_aptmeta.dat (or the host when absent there). Returns the
 * total number of matches (may exceed max; only `max` are written). Order: file order. */
int  a380x_nav_find(A380XNavdata *nav, const char *ident, unsigned kind_mask, A380XNavFix *out, int max);
/* Same, sorted nearest-first from (ref_lat, ref_lon) and with dist_nm filled. This is the list the
 * DUPLICATE NAMES dialog shows (spec 05 §14). Returns the total number of matches. */
int  a380x_nav_find_near(A380XNavdata *nav, const char *ident, unsigned kind_mask,
                         double ref_lat, double ref_lon, A380XNavFix *out, int max);
/* Sorts an array nearest-first (stable) and fills dist_nm. */
void a380x_nav_sort_nearest(A380XNavFix *fixes, int count, double ref_lat, double ref_lon);
/* Best single match for a database reference: ident + optional region ("" = any) + kind
 * (A380X_NAV_NONE = any) + optional terminal airport ("" = prefer enroute). Ties and fallbacks
 * resolve to the candidate nearest (ref_lat, ref_lon) (NAN = no preference). Returns 1 if found. */
int  a380x_nav_resolve(A380XNavdata *nav, const char *ident, const char *region, A380XNavKind kind,
                       const char *airport, double ref_lat, double ref_lon, A380XNavFix *out);
/* Navaids within radius_nm of a position (any of kind_mask), nearest first. Returns total count. */
int  a380x_nav_nearby(A380XNavdata *nav, double lat, double lon, double radius_nm, unsigned kind_mask,
                      A380XNavFix *out, int max);
/* Airport reference data (earth_aptmeta + host). Returns 1 if the airport is known. */
int  a380x_nav_airport_info(A380XNavdata *nav, const char *icao, A380XNavAirportInfo *out);
/* Magnetic variation at a position (host, else estimate; 0 if nothing is known). East +. */
double a380x_nav_magvar(A380XNavdata *nav, double lat, double lon);

void a380x_nav_ref_from_fix(A380XNavRef *ref, const A380XNavFix *fix);
void a380x_nav_ref_clear(A380XNavRef *ref);
/* Pilot-defined waypoints (FBW getOrCreateWaypoint). `ident` is chosen by the caller (FBW: "LL01",
 * "PBD01", "PBX01"). Bearings are MAGNETIC at the reference place. Return 1 on success, 0 when no
 * intersection exists (PBX) or the input is invalid. */
int  a380x_nav_make_latlon(double lat, double lon, const char *ident, A380XNavFix *out);
int  a380x_nav_make_pbd(A380XNavdata *nav, const A380XNavFix *place, double brg_mag, double dist_nm,
                        const char *ident, A380XNavFix *out);
int  a380x_nav_make_pbx(A380XNavdata *nav, const A380XNavFix *place1, double brg1_mag,
                        const A380XNavFix *place2, double brg2_mag, const char *ident, A380XNavFix *out);

/* ------------------------------------------------------------------------------------------ */
/* Airways                                                                                     */
/* ------------------------------------------------------------------------------------------ */

/* 1 if an airway with this name exists anywhere. */
int  a380x_nav_airway_exists(A380XNavdata *nav, const char *name);
/* 1 if `fix` (ident + region, position used to separate duplicates) is on airway `name`. */
int  a380x_nav_airway_contains(A380XNavdata *nav, const char *name, const A380XNavRef *fix);
/* Fixes along airway `name` from `from` (excluded) to the first fix named `to_ident` (included),
 * following the airway in whichever direction reaches it (direction restrictions are reported in
 * spec 11 but not enforced, like the FMS). Returns the number of points written (<= max), or
 * -1 airway unknown, -2 `from` not on the airway, -3 `to_ident` not reachable on it. */
int  a380x_nav_airway_path(A380XNavdata *nav, const char *name, const A380XNavRef *from,
                           const char *to_ident, A380XNavRef *out, int max);
/* First fix on airway `awy1` (walking away from `from` in either direction, nearest along-airway
 * first) that is also on airway `awy2`. Returns 1 and the fix, or 0 (NO INTERSECTION FOUND). */
int  a380x_nav_airway_intersection(A380XNavdata *nav, const char *awy1, const A380XNavRef *from,
                                   const char *awy2, A380XNavRef *out);
/* Names of the airways through a fix (deduplicated, sorted). Returns the total count. */
int  a380x_nav_airways_at(A380XNavdata *nav, const A380XNavRef *fix,
                          char (*names)[A380X_NAV_IDENT_LEN], int max);

/* ------------------------------------------------------------------------------------------ */
/* Published holdings (earth_hold.dat)                                                         */
/* ------------------------------------------------------------------------------------------ */

typedef struct {
    A380XNavRef fix;
    char  airport[A380X_NAV_ICAO_LEN];  /* "" = enroute */
    float inbound_mag;                  /* inbound magnetic course */
    float leg_time_min, leg_dist_nm;    /* one of them is 0 */
    char  turn;                         /* 'L' / 'R' */
    float min_alt_ft, max_alt_ft;       /* 0 = none */
    float speed_kt;                     /* 0 = none */
} A380XNavHold;
/* Holds at a fix ident (region "" = any). Returns total count. */
int  a380x_nav_find_holds(A380XNavdata *nav, const char *ident, const char *region,
                          A380XNavHold *out, int max);

/* ------------------------------------------------------------------------------------------ */
/* Airport procedures (CIFP)                                                                   */
/* ------------------------------------------------------------------------------------------ */

typedef enum {                 /* ARINC 424 path terminators */
    A380X_LEG_NONE = 0,
    A380X_LEG_IF, A380X_LEG_TF, A380X_LEG_CF, A380X_LEG_DF, A380X_LEG_AF, A380X_LEG_RF,
    A380X_LEG_HF, A380X_LEG_HA, A380X_LEG_HM,
    A380X_LEG_CA, A380X_LEG_CD, A380X_LEG_CI, A380X_LEG_CR,
    A380X_LEG_FA, A380X_LEG_FC, A380X_LEG_FD, A380X_LEG_FM,
    A380X_LEG_PI,
    A380X_LEG_VA, A380X_LEG_VD, A380X_LEG_VI, A380X_LEG_VM, A380X_LEG_VR,
    A380X_LEG_COUNT
} A380XLegType;
const char  *a380x_leg_type_name(A380XLegType t);          /* "TF", "??" */
A380XLegType a380x_leg_type_parse(const char *two_chars);   /* A380X_LEG_NONE if unknown */
/* FlightPlanLeg.ts:229 sets: isXF = IF TF CF DF AF RF HF; isHX = HA HF HM; isXI = PI CI VI;
 * isVectors = FM VM; isFX = FA FC FD FM. "F-type" (fix is the START point) = FA FC FD FM. */
int a380x_leg_is_xf(A380XLegType t);
int a380x_leg_is_hx(A380XLegType t);
int a380x_leg_is_vectors(A380XLegType t);
int a380x_leg_starts_at_fix(A380XLegType t);   /* FA FC FD FM */
int a380x_leg_is_heading(A380XLegType t);      /* VA VD VI VM VR */

typedef struct {
    A380XLegType type;
    A380XNavRef  fix;          /* termination fix; for FA/FC/FD/FM the start fix; ident "" if none */
    A380XNavRef  navaid;       /* recommended navaid (theta/rho reference, DME, AF centre) */
    A380XNavRef  center;       /* RF arc centre fix */
    char   desc[5];            /* waypoint description code, 4 chars ("E  F", "G  M", "EY  ") */
    char   turn;               /* 'L', 'R', 'E' (either) or 0 */
    uint8_t turn_valid;        /* turn direction valid ('Y') */
    uint8_t overfly;           /* desc col 2 'Y' (flyover) or 'B' */
    uint8_t missed_start;      /* desc col 3 'M': first leg of the missed approach */
    uint8_t fix_unresolved;    /* fix ident present but not found in the database */
    char   alt_desc;           /* 0 none, '@' at (blank with an altitude), '+','-','B','C','G','H','I','J','V','X','Y' */
    char   spd_desc;           /* 0 none, '@', '+', '-' (blank with a speed = '@') */
    uint8_t course_true;       /* course coded true ('T' suffix) */
    float  alt1_ft, alt2_ft;   /* NAN if absent; FLxxx converted to feet */
    float  trans_alt_ft;       /* NAN if absent */
    float  speed_kt;           /* NAN if absent */
    float  course_deg;         /* magnetic unless course_true; NAN if absent */
    float  dist_nm;            /* route/holding distance, NAN if absent or time coded */
    float  time_min;           /* holding/leg time ('T' prefix), NAN if absent */
    float  theta_deg, rho_nm;  /* from the recommended navaid, NAN if absent */
    float  arc_radius_nm;      /* RF radius, NAN if absent */
    float  rnp_nm;             /* NAN if absent */
    float  vertical_angle_deg; /* negative descent angle, NAN if absent */
} A380XNavProcLeg;

typedef enum { A380X_PROC_SID = 0, A380X_PROC_STAR, A380X_PROC_APPROACH } A380XProcKind;

typedef enum {                 /* part of a procedure ("route type" classes) */
    A380X_TRANS_RUNWAY = 0,    /* SID runway transition / STAR runway transition */
    A380X_TRANS_COMMON,        /* SID/STAR common route; ident "", "ALL" or a runway "RW27R" */
    A380X_TRANS_ENROUTE,       /* SID/STAR enroute transition; ident = transition fix */
    A380X_TRANS_APPR_VIA,      /* approach transition (VIA), route type 'A'; ident = IAF */
    A380X_TRANS_APPR_FINAL,    /* final approach incl. missed approach legs */
    A380X_TRANS_ENGINE_OUT     /* SID route type '0' (EOSID; not offered by the FMS pages) */
} A380XProcTransRole;

typedef struct {
    char  ident[A380X_NAV_IDENT_LEN];   /* "RW27R", "RW09B", "ALL", "", "LAM" */
    A380XProcTransRole role;
    char  route_type;                   /* ARINC 424 route type character */
    int   first_leg, leg_count;         /* into A380XNavAirport.legs */
} A380XNavProcTrans;

typedef enum {                 /* FBW navdata ApproachType */
    A380X_APPR_UNKNOWN = 0, A380X_APPR_LOC_BACKCOURSE, A380X_APPR_VORDME, A380X_APPR_FMS,
    A380X_APPR_IGS, A380X_APPR_ILS, A380X_APPR_GLS, A380X_APPR_LOC, A380X_APPR_MLS, A380X_APPR_NDB,
    A380X_APPR_GPS, A380X_APPR_NDBDME, A380X_APPR_RNAV, A380X_APPR_VORTAC, A380X_APPR_TACAN,
    A380X_APPR_SDF, A380X_APPR_VOR, A380X_APPR_MLS_A, A380X_APPR_LDA, A380X_APPR_MLS_BC
} A380XApproachType;

typedef struct {
    char  ident[A380X_NAV_IDENT_LEN];   /* CIFP ident: "CPT3F", "LAM1Z", "I27R", "R22LX" */
    A380XProcKind kind;
    A380XApproachType appr_type;        /* approaches */
    char  appr_code;                    /* approach route type char ('I','R','L',...) */
    char  runway[A380X_NAV_IDENT_LEN];  /* approaches: "RW27R"; "" = circling / no runway */
    char  multiple;                     /* approach multiple indicator ('X','Y','Z') or 0 */
    uint8_t rnp_ar;                     /* RNP AR (authorisation required) - see spec 11 §6.6 */
    char  qualifiers[4];                /* approach: GNSS/FMS indicator, route qualifiers 1, 2 */
    int   first_trans, trans_count;     /* into A380XNavAirport.trans */
} A380XNavProcedure;

typedef struct {
    char   ident[A380X_NAV_IDENT_LEN];  /* "RW27R" */
    char   designator[4];               /* "27R" */
    int    number;                      /* 27 */
    char   side;                        /* 'L', 'R', 'C' or 0 */
    double thr_lat, thr_lon;            /* landing threshold */
    double start_lat, start_lon;        /* runway physical start (threshold moved back by the displacement) */
    float  thr_elev_ft;
    float  disp_thr_ft;                 /* displaced threshold distance */
    float  tch_ft;                      /* threshold crossing height, NAN */
    float  gradient_pct;                /* NAN */
    float  true_brg, mag_brg;           /* NAN when neither the opposite end nor a LOC is known */
    float  length_ft;                   /* physical length (both displacements added), NAN if unknown */
    char   ls_ident[A380X_NAV_IDENT_LEN]; /* "IRR", "" */
    int    ls_category;                 /* 0 none, 1..3 (CIFP), 4 = LOC only/other */
    float  ls_freq_mhz;                 /* 0 if none */
    float  ls_course_mag;               /* NAN */
    uint8_t ls_has_gs;
    float  gs_angle_deg;                /* NAN */
} A380XNavRunway;

/* Per-airport CIFP data. Owned by the LRU cache: the pointer stays valid until `cifp_cache_size`
 * other airports have been requested or the service is reloaded/destroyed. Do not keep it across
 * frames; re-request it (a cached lookup is a short string compare). */
typedef struct A380XNavAirport {
    A380XNavAirportInfo info;
    int runway_count;  A380XNavRunway    *runways;  /* sorted by ident */
    int proc_count;    A380XNavProcedure *procs;    /* SIDs, STARs, approaches; each kind sorted by ident */
    int trans_count;   A380XNavProcTrans *trans;
    int leg_count;     A380XNavProcLeg   *legs;
    int unresolved_fixes;
    int malformed_lines;
    uint32_t generation;                             /* bumps on every (re)load */
} A380XNavAirport;

/* CIFP for an airport (Custom Data first, then default data). Returns NULL when the airport is
 * unknown and has no CIFP file. An airport known from earth_aptmeta / host but without CIFP gets
 * an empty record (no runways/procedures). */
const A380XNavAirport *a380x_nav_airport(A380XNavdata *nav, const char *icao);
/* Runway by "27R" or "RW27R". */
const A380XNavRunway  *a380x_nav_runway(const A380XNavAirport *apt, const char *ident);
/* Procedure by kind + CIFP ident. */
const A380XNavProcedure *a380x_nav_procedure(const A380XNavAirport *apt, A380XProcKind kind, const char *ident);
/* Iteration helper: index-th procedure of a kind (sorted by ident), NULL past the end. */
const A380XNavProcedure *a380x_nav_procedure_at(const A380XNavAirport *apt, A380XProcKind kind, int index);
int  a380x_nav_procedure_count(const A380XNavAirport *apt, A380XProcKind kind);
/* Transitions of a role ("NONE"/common excluded), in database order. Returns count. */
int  a380x_nav_proc_transitions(const A380XNavAirport *apt, const A380XNavProcedure *p,
                                A380XProcTransRole role, const A380XNavProcTrans **out, int max);
/* 1 when a transition ident applies to a runway ident ("RW27R" vs "RW27R"/"RW27B"/"ALL"/""). */
int  a380x_nav_trans_matches_runway(const char *trans_ident, const char *runway_ident);
/* SID usable from / STAR usable to a runway ("RW27R"); runway NULL/"" = 1. */
int  a380x_nav_sid_serves_runway(const A380XNavAirport *apt, const A380XNavProcedure *sid, const char *rwy);
int  a380x_nav_star_serves_runway(const A380XNavAirport *apt, const A380XNavProcedure *star, const char *rwy);
/* Spec 05 §8.2 approach naming: "ILS27R", "RNV22L-X" (+ " (RNP)" when rnp_suffix and RNP AR). */
void a380x_nav_approach_name(const A380XNavProcedure *appr, int rnp_suffix, char *out, size_t out_size);
/* Approach sort key of spec 05 §8.2 (runway, then type order). */
int  a380x_nav_approach_type_order(A380XApproachType t);

/* ------------------------------------------------------------------------------------------ */
/* Geodesy helpers (spherical earth, NM)                                                       */
/* ------------------------------------------------------------------------------------------ */
double a380x_geo_distance_nm(double lat1, double lon1, double lat2, double lon2);
double a380x_geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);  /* initial true, 0..360 */
void   a380x_geo_destination(double lat, double lon, double brg_true, double dist_nm,
                             double *out_lat, double *out_lon);
double a380x_geo_norm360(double deg);
double a380x_geo_angle_diff(double from_deg, double to_deg);  /* signed -180..180, right turn positive */
/* Along-track (positive ahead of `lat1/lon1` towards `lat2/lon2`) and cross-track (right positive)
 * distance of point p relative to the great circle 1->2. */
void   a380x_geo_along_cross(double lat1, double lon1, double lat2, double lon2, double plat, double plon,
                             double *along_nm, double *cross_nm);
/* Intersection of two great-circle courses (true). Returns 1 and the intersection closest ahead of
 * point 1, else 0. */
int    a380x_geo_intersect(double lat1, double lon1, double crs1, double lat2, double lon2, double crs2,
                           double *out_lat, double *out_lon);

#ifdef __cplusplus
}
#endif

#endif
