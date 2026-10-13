/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef A380X_EFIS_AIRPORT_H
#define A380X_EFIS_AIRPORT_H
#define EFIS_AIRPORT_RUNWAYS 32
#define EFIS_AIRPORT_RINGS 512
#define EFIS_AIRPORT_NODES 16384

typedef struct {
    double lat1, lon1, lat2, lon2;
    float width_m;
    char end1[8], end2[8];
} EfisAirportRunway;
typedef struct {
    double lat, lon, control_lat, control_lon;
    int bezier; /* control is outgoing; reflect about node for incoming. */
} EfisAirportNode;
typedef struct { int first, count, hole; } EfisAirportRing;
typedef struct {
    char id[16];
    double lat, lon;
    int loading, found, complete, truncated;
    int runway_count, ring_count, node_count;
    EfisAirportRunway runways[EFIS_AIRPORT_RUNWAYS];
    EfisAirportRing rings[EFIS_AIRPORT_RINGS];
    EfisAirportNode nodes[EFIS_AIRPORT_NODES];
} EfisAirport;
#endif
