/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real airport geometry from active X-Plane scenery, read incrementally outside
 * the draw callback. Format: developer.x-plane.com/article/
 * airport-data-apt-dat-12-00-file-format-specification/ . Taxiway rings preserve
 * holes and Bezier handles; runway coordinates and width are not synthesized.
 */
#include <ctype.h>
#include "a380x_efis_airport.h"
static EfisAirport efis_airport;
typedef struct {
    char target[16], accepted[16], current[16];
    int in_airport, pavement, hole, ring_start, ring_bad, rewind_requested;
} EfisAirportParser;
static EfisAirportParser airport_parser;

static int efis_airport_coordinate(double lat, double lon)
{
    return isfinite(lat) && isfinite(lon) && fabs(lat) <= 90 && fabs(lon) <= 180;
}
static void efis_airport_discard_ring(void)
{
    if (airport_parser.ring_start >= 0) efis_airport.node_count = airport_parser.ring_start;
    airport_parser.ring_start = -1;
    airport_parser.ring_bad = 0;
}
static void efis_airport_parser_begin(const char *id)
{
    memset(&efis_airport, 0, sizeof(efis_airport));
    memset(&airport_parser, 0, sizeof(airport_parser));
    snprintf(efis_airport.id, sizeof(efis_airport.id), "%.15s", id);
    snprintf(airport_parser.target, sizeof(airport_parser.target), "%.15s", id);
    snprintf(airport_parser.accepted, sizeof(airport_parser.accepted), "%.15s", id);
    airport_parser.ring_start = -1;
    efis_airport.loading = 1;
}
/* Return one after the target's complete record has been consumed. */
static int efis_airport_parse_line(const char *line)
{
    int code;
    char *end;
    while (isspace((unsigned char)*line)) ++line;
    code = (int)strtol(line, &end, 10);
    if (end == line) return 0;
    if (code == 1 || code == 16 || code == 17 || code == 99) {
        if (airport_parser.in_airport) {
            efis_airport_discard_ring();
            efis_airport.complete = 1; efis_airport.loading = 0;
            return 1;
        }
        if (code == 99) return 0;
        airport_parser.current[0] = 0;
        if (sscanf(line, "%*d %*f %*d %*d %15s", airport_parser.current) != 1) return 0;
        airport_parser.in_airport = !strcmp(airport_parser.current, airport_parser.accepted);
        if (airport_parser.in_airport) efis_airport.found = 1;
        return 0;
    }
    if (!airport_parser.in_airport) {
        /* apt internal IDs need not equal the ICAO identifier used by GPS.
         * The loader rewinds this airport when its metadata supplies a match. */
        char key[32], id[16];
        if (code == 1302 && sscanf(line, "%*d %31s %15s", key, id) == 2
            && (!strcmp(key, "icao_code") || !strcmp(key, "local_code"))
            && !strcmp(id, airport_parser.target) && airport_parser.current[0]) {
            snprintf(airport_parser.accepted, sizeof(airport_parser.accepted), "%s", airport_parser.current);
            airport_parser.rewind_requested = 1;
        }
        return 0;
    }
    if (code == 100) {
        EfisAirportRunway r;
        memset(&r, 0, sizeof(r));
        if (sscanf(line, "%*d %f %*d %*d %*f %*d %*d %*d %7s %lf %lf %*f %*f %*d %*d %*d %*d %7s %lf %lf",
                &r.width_m, r.end1, &r.lat1, &r.lon1, r.end2, &r.lat2, &r.lon2) == 7
            && isfinite(r.width_m) && r.width_m >= 1 && r.width_m <= 1000
            && efis_airport_coordinate(r.lat1, r.lon1) && efis_airport_coordinate(r.lat2, r.lon2)) {
            if (efis_airport.runway_count < EFIS_AIRPORT_RUNWAYS)
                efis_airport.runways[efis_airport.runway_count++] = r;
            else efis_airport.truncated = 1;
        }
        return 0;
    }
    if (code == 110) {
        efis_airport_discard_ring();
        airport_parser.pavement = 1; airport_parser.hole = 0;
        return 0;
    }
    if (code >= 111 && code <= 116 && airport_parser.pavement) {
        EfisAirportNode p;
        int n, curved = code == 112 || code == 114 || code == 116;
        memset(&p, 0, sizeof(p));
        n = sscanf(line, "%*d %lf %lf %lf %lf", &p.lat, &p.lon, &p.control_lat, &p.control_lon);
        p.bezier = curved;
        if (airport_parser.ring_start < 0) airport_parser.ring_start = efis_airport.node_count;
        if (n < (curved ? 4 : 2) || !efis_airport_coordinate(p.lat, p.lon)
            || (curved && !efis_airport_coordinate(p.control_lat, p.control_lon)))
            airport_parser.ring_bad = 1;
        if (efis_airport.node_count >= EFIS_AIRPORT_NODES) {
            airport_parser.ring_bad = 1; efis_airport.truncated = 1;
        }
        if (!airport_parser.ring_bad) efis_airport.nodes[efis_airport.node_count++] = p;
        if (code == 113 || code == 114) {
            int count = efis_airport.node_count - airport_parser.ring_start;
            if (!airport_parser.ring_bad && count >= 3 && efis_airport.ring_count < EFIS_AIRPORT_RINGS) {
                EfisAirportRing *r = &efis_airport.rings[efis_airport.ring_count++];
                r->first = airport_parser.ring_start; r->count = count; r->hole = airport_parser.hole;
                airport_parser.ring_start = -1;
            } else {
                if (efis_airport.ring_count >= EFIS_AIRPORT_RINGS) efis_airport.truncated = 1;
                efis_airport_discard_ring();
            }
            airport_parser.hole = 1;
        } else if (code == 115 || code == 116) {
            efis_airport_discard_ring(); airport_parser.pavement = 0;
        }
        return 0;
    }
    /* Airport boundaries, painted lines, objects and traffic networks are not
     * pavement. Incomplete rings must never be joined to the next feature. */
    if (code != 0) { efis_airport_discard_ring(); airport_parser.pavement = 0; }
    return 0;
}

#ifndef EFIS_AIRPORT_PARSER_ONLY
#define EFIS_AIRPORT_PATHS 128
#define EFIS_AIRPORT_PATH_SIZE 1024
static struct {
    char paths[EFIS_AIRPORT_PATHS][EFIS_AIRPORT_PATH_SIZE];
    int path_count, path_index, long_line;
    FILE *file;
    long offset, airport_offset;
    float timer;
} airport_source;

static void efis_airport_add_path(const char *root, const char *pack)
{
    char path[EFIS_AIRPORT_PATH_SIZE];
    const char *suffix = "Earth nav data/apt.dat";
    size_t n;
    int i, written;
    if (airport_source.path_count >= EFIS_AIRPORT_PATHS) return;
    if (!strcmp(pack, "*GLOBAL_AIRPORTS*")) pack = "Global Scenery/Global Airports/";
    n = strlen(pack);
    if (!n) return;
    written = snprintf(path, sizeof(path), "%s%s%s%s", (pack[0]=='/' || (n>1 && pack[1]==':')) ? "" : root,
        pack, (pack[n-1]=='/' || pack[n-1]=='\\') ? "" : "/", suffix);
    if (written < 0 || written >= (int)sizeof(path)) return;
    for (i = 0; i < airport_source.path_count; ++i)
        if (!strcmp(path, airport_source.paths[i])) return;
    snprintf(airport_source.paths[airport_source.path_count++], EFIS_AIRPORT_PATH_SIZE, "%s", path);
}
static void efis_airport_shutdown(void)
{
    if (airport_source.file) fclose(airport_source.file);
    airport_source.file = NULL;
    efis_airport.loading = 0;
}
static void efis_airport_init(void)
{
    char root[512] = {0}, ini[1024], line[2048];
    FILE *file;
    int global_seen = 0, count = 0;
    memset(&airport_source, 0, sizeof(airport_source));
    memset(&efis_airport, 0, sizeof(efis_airport));
#if APL
    /* The SDK otherwise returns legacy HFS colon paths on macOS. */
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
#endif
    XPLMGetSystemPath(root);
    snprintf(ini, sizeof(ini), "%sCustom Scenery/scenery_packs.ini", root);
    file = fopen(ini, "rb");
    while (file && count++ < 2048 && fgets(line, sizeof(line), file)) {
        char *pack, *tail;
        if (strstr(line, "*GLOBAL_AIRPORTS*")) global_seen = 1;
        if (strncmp(line, "SCENERY_PACK ", 13)) continue;
        pack = line + 13;
        while (isspace((unsigned char)*pack)) ++pack;
        tail = pack + strlen(pack);
        while (tail > pack && isspace((unsigned char)tail[-1])) *--tail = 0;
        efis_airport_add_path(root, pack);
    }
    if (file) fclose(file);
    if (!global_seen) efis_airport_add_path(root, "*GLOBAL_AIRPORTS*");
    airport_source.timer = 2.0f;
}
static void efis_airport_read_slice(void)
{
    char line[2048];
    size_t bytes = 0;
    int rows = 0;
    float start = XPLMGetElapsedTime();
    while (efis_airport.loading && bytes < 1048576 && rows < 16000) {
        if (!airport_source.file) {
            if (airport_source.path_index >= airport_source.path_count) {
                efis_airport.complete = 1; efis_airport.loading = 0; break;
            }
            airport_source.file = fopen(airport_source.paths[airport_source.path_index++], "rb");
            airport_source.offset = airport_source.airport_offset = 0;
            airport_source.long_line = 0;
            if (!airport_source.file) {
                if (XPLMGetElapsedTime() - start > 0.002f) break;
                continue;
            }
            airport_parser.current[0] = 0;
        }
        if (!fgets(line, sizeof(line), airport_source.file)) {
            if (airport_parser.in_airport) {
                efis_airport_discard_ring(); efis_airport.complete = 1; efis_airport.loading = 0;
            }
            fclose(airport_source.file); airport_source.file = NULL;
            continue;
        }
        {
            size_t length = strlen(line);
            char *p = line;
            if (!length) {
                /* apt.dat is text. Reject an embedded-NUL record without
                 * indexing before the buffer or joining another source into
                 * a partly read definition of the selected airport. */
                if (airport_parser.in_airport) {
                    efis_airport_discard_ring(); efis_airport.truncated = 1;
                    efis_airport.complete = 1; efis_airport.loading = 0;
                }
                fclose(airport_source.file); airport_source.file = NULL;
                continue;
            }
            while (isspace((unsigned char)*p)) ++p;
            if ((p[0]=='1' && isspace((unsigned char)p[1])) ||
                (p[0]=='1' && (p[1]=='6' || p[1]=='7') && isspace((unsigned char)p[2])))
                airport_source.airport_offset = airport_source.offset;
            airport_source.offset += (long)length;
            bytes += length; ++rows;
            if (airport_source.long_line) {
                airport_source.long_line = line[length-1] != '\n';
            } else if (length == sizeof(line)-1 && line[length-1] != '\n') {
                airport_source.long_line = 1;
                /* Overlong records are invalid input; drain their remainder
                 * in subsequent slices without interpreting partial geometry. */
                if (airport_parser.in_airport) efis_airport.truncated = 1;
            } else if (efis_airport_parse_line(line)) {
                fclose(airport_source.file); airport_source.file = NULL;
            }
            if (airport_parser.rewind_requested && airport_source.file) {
                airport_parser.rewind_requested = 0;
                if (!fseek(airport_source.file, airport_source.airport_offset, SEEK_SET))
                    airport_source.offset = airport_source.airport_offset;
                else { fclose(airport_source.file); airport_source.file = NULL; }
            }
        }
        if ((rows & 127) == 0 && XPLMGetElapsedTime() - start > 0.002f) break;
    }
}
static void efis_airport_update(float dt)
{
    if (!(efis[0].zoom || efis[1].zoom) || !nd_data.position_valid) return;
    airport_source.timer += fmaxf(0, dt);
    if (airport_source.timer >= 2.0f) {
        float lat = (float)nd_data.lat, lon = (float)nd_data.lon;
        float airport_lat = NAN, airport_lon = NAN, x, y;
        char id[256] = {0};
        XPLMNavRef ref = XPLMFindNavAid(NULL, NULL, &lat, &lon, NULL, xplm_Nav_Airport);
        airport_source.timer = 0;
        if (ref != XPLM_NAV_NOT_FOUND) XPLMGetNavAidInfo(ref, NULL, &airport_lat, &airport_lon, NULL, NULL, NULL, id, NULL, NULL);
        if (!id[0] || !efis_nd_project(nd_data.lat, nd_data.lon, airport_lat, airport_lon, 0, &x, &y) || hypotf(x,y) > 5.0f) {
            efis_airport_shutdown(); memset(&efis_airport, 0, sizeof(efis_airport)); return;
        }
        if (strcmp(efis_airport.id, id)) {
            efis_airport_shutdown(); efis_airport_parser_begin(id);
            efis_airport.lat = airport_lat; efis_airport.lon = airport_lon;
            airport_source.path_index = 0;
        }
    }
    if (efis_airport.loading) efis_airport_read_slice();
}
#endif
