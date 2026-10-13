/* SPDX-License-Identifier: GPL-3.0-or-later
 * Parser fixtures follow apt.dat 1200 row definitions. No invented airport
 * geometry enters the aircraft: these small fixtures are test-only inputs.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EFIS_AIRPORT_PARSER_ONLY 1
#include "a380x_efis_airport.inc"
static void feed(const char *line) { assert(!efis_airport_parse_line(line)); }
static void start(void)
{
    efis_airport_parser_begin("TEST");
    feed("I\n"); feed("1200 Version\n");
    feed("1 100 0 0 OTHER Another Airport\n");
    feed("100 45 1 0 0.25 1 2 1 09 40 -70 0 0 2 0 0 0 27 40 -69.97 0 0 2 0 0 0\n");
    assert(!efis_airport.runway_count);
    feed("1 100 0 0 TEST Target Airport\n");
}
static void test_runways(void)
{
    start();
    feed("100 45 1 0 0.25 1 2 1 09 40 -70 0 0 2 0 0 0 27 40 -69.97 0 0 2 0 0 0\n");
    assert(efis_airport.runway_count == 1);
    assert(!strcmp(efis_airport.runways[0].end1, "09"));
    assert(!strcmp(efis_airport.runways[0].end2, "27"));
    assert(fabs(efis_airport.runways[0].lon2 + 69.97) < .000001);
    assert(efis_airport.runways[0].width_m == 45);
    feed("100 0 1 0 0.25 1 2 1 09 40 -70 0 0 2 0 0 0 27 40 -69.97 0 0 2 0 0 0\n");
    feed("100 45 1 0 0.25 1 2 1 09 nan -70 0 0 2 0 0 0 27 40 -69.97 0 0 2 0 0 0\n");
    assert(efis_airport.runway_count == 1);
    assert(efis_airport_parse_line("1 0 0 0 NEXT Next Airport\n"));
    assert(efis_airport.complete && efis_airport.found && !efis_airport.loading);
}
static void test_rings(void)
{
    start();
    feed("110 1 .25 0 Pavement\n");
    feed("111 40 -70\n");
    feed("112 40.001 -70 40.002 -70.001 3\n");
    feed("114 40 -70.001 40 -70.002\n");
    assert(efis_airport.ring_count == 1 && efis_airport.node_count == 3);
    assert(!efis_airport.rings[0].hole && efis_airport.nodes[1].bezier);
    assert(fabs(efis_airport.nodes[1].control_lat - 40.002) < .000001);
    feed("111 40.0001 -70.0001\n"); feed("111 40.0002 -70.0001\n"); feed("113 40.0001 -70.0002\n");
    assert(efis_airport.ring_count == 2 && efis_airport.rings[1].hole);
    assert(efis_airport.rings[1].first == 3 && efis_airport.rings[1].count == 3);
    feed("120 Painted Line\n");
    feed("111 40 -70\n"); feed("111 40.001 -70\n"); feed("113 40 -70.001\n");
    feed("130 Airport Boundary\n");
    feed("111 40 -70\n"); feed("111 40.001 -70\n"); feed("113 40 -70.001\n");
    assert(efis_airport.ring_count == 2 && efis_airport.node_count == 6);
    feed("110 2 .25 0 Broken Pavement\n");
    feed("111 40 -70\n"); feed("111 40.001 -70\n");
    feed("110 1 .25 0 New Pavement\n");
    assert(efis_airport.node_count == 6);
    feed("111 40 -70\n"); feed("111 400 -70\n"); feed("113 40 -70.001\n");
    assert(efis_airport.node_count == 6 && efis_airport.ring_count == 2);
    assert(efis_airport_parse_line("99\n"));
}
static void test_capacity_and_alias(void)
{
    int i;
    start();
    feed("110 1 .25 0 Oversized ring\n");
    for (i=0; i<EFIS_AIRPORT_NODES+4; ++i) feed("111 40 -70\n");
    feed("113 40 -70\n");
    assert(efis_airport.truncated && efis_airport.node_count == 0 && efis_airport.ring_count == 0);
    efis_airport_parser_begin("KABC");
    feed("1 0 0 0 XINTERN Internal ID\n");
    feed("1302 icao_code KABC\n");
    assert(airport_parser.rewind_requested && !strcmp(airport_parser.accepted,"XINTERN"));
    airport_parser.rewind_requested = 0;
    feed("1 0 0 0 XINTERN Internal ID\n");
    assert(efis_airport.found && !strcmp(efis_airport.id,"KABC"));
}
int main(int argc, char **argv)
{
    test_runways(); test_rings(); test_capacity_and_alias();
    if (argc == 4 && !strcmp(argv[1], "--file")) {
        char line[2048];
        FILE *f = fopen(argv[2], "rb");
        assert(f);
        efis_airport_parser_begin(argv[3]);
        while (fgets(line,sizeof(line),f)) if (efis_airport_parse_line(line)) break;
        fclose(f);
        assert(efis_airport.found && efis_airport.complete && efis_airport.runway_count);
        printf("Airport %s: %d runways, %d pavement rings, %d nodes, truncated=%d\n", efis_airport.id,
            efis_airport.runway_count,efis_airport.ring_count,efis_airport.node_count,efis_airport.truncated);
    }
    puts("Airport: apt1200 runways, closed pavement rings/holes/Bezier handles, separate painted lines/boundaries, malformed records, bounds and ICAO aliases passed");
    return 0;
}
