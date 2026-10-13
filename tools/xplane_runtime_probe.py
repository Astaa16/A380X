#!/usr/bin/env python3
"""Record a small X-Plane flight-model snapshot through the local RREF API.

This is a development test tool. It does not install a plugin or modify the
simulator. Output is JSON Lines so a failed ground start can be inspected after
X-Plane has already entered its crash state.
"""

from __future__ import annotations

import argparse
import json
import select
import socket
import struct
import time


DEFAULT_DATAREFS = [
    "sim/time/total_running_time_sec",
    "sim/time/total_flight_time_sec",
    "sim/time/paused",
    "sim/operation/failures/enable_random_failures",
    "sim/operation/failures/mean_time_between_failure_hrs",
    "sim/aircraft/weight/acf_cgY_original",
    "sim/aircraft/weight/acf_cgZ_original",
    "sim/aircraft/gear/acf_h_eqlbm",
    "sim/aircraft/gear/acf_the_eqlbm",
    "sim/flightmodel/weight/m_total",
    "sim/flightmodel2/misc/cg_offset_z",
    *(f"sim/aircraft/parts/acf_gear_ynodef[{index}]" for index in range(5)),
    *(f"sim/aircraft/parts/acf_gear_znodef[{index}]" for index in range(5)),
    "sim/flightmodel2/misc/has_crashed",
    "sim/flightmodel/forces/g_nrml",
    "sim/flightmodel/position/groundspeed",
    "sim/flightmodel/position/y_agl",
    "sim/flightmodel/position/local_vy",
    "sim/flightmodel/position/phi",
    "sim/flightmodel/position/theta",
    "sim/flightmodel/failures/onground_any",
    "sim/cockpit2/controls/gear_handle_down",
    *(f"sim/flightmodel2/gear/deploy_ratio[{index}]" for index in range(5)),
    *(f"sim/flightmodel2/gear/on_ground[{index}]" for index in range(5)),
    *(f"sim/flightmodel2/gear/tire_vertical_deflection_mtr[{index}]" for index in range(5)),
    *(f"sim/flightmodel2/gear/tire_vertical_force_n_mtr[{index}]" for index in range(5)),
    *(f"sim/operation/failures/rel_collapse{index + 1}" for index in range(5)),
    "sim/operation/failures/rel_wing1L",
    "sim/operation/failures/rel_wing1R",
    "sim/operation/failures/rel_wing2L",
    "sim/operation/failures/rel_wing2R",
    "sim/operation/failures/rel_wing3L",
    "sim/operation/failures/rel_wing3R",
    "sim/operation/failures/rel_wing4L",
    "sim/operation/failures/rel_wing4R",
]


def rref_packet(frequency: int, index: int, dataref: str) -> bytes:
    encoded = dataref.encode("utf-8")
    if len(encoded) >= 400:
        raise ValueError(f"Dataref name is too long: {dataref}")
    return struct.pack("<4sxii400s", b"RREF", frequency, index, encoded)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=49000)
    parser.add_argument("--duration", type=float, default=20.0)
    parser.add_argument("--frequency", type=int, default=20)
    parser.add_argument("--interval", type=float, default=0.1)
    parser.add_argument("--resubscribe", type=float, default=2.0,
                        help="Retry subscriptions during loading, seconds (0 disables)")
    parser.add_argument("dataref", nargs="*")
    arguments = parser.parse_args()

    datarefs = arguments.dataref or DEFAULT_DATAREFS
    destination = (arguments.host, arguments.port)
    values: dict[int, float] = {}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    sock.setblocking(False)

    for index, dataref in enumerate(datarefs):
        sock.sendto(rref_packet(arguments.frequency, index, dataref), destination)

    started = time.monotonic()
    next_emit = started
    next_subscribe = started + arguments.resubscribe
    last_packet = None
    try:
        while time.monotonic() - started < arguments.duration:
            now = time.monotonic()
            if arguments.resubscribe > 0 and now >= next_subscribe:
                for index, dataref in enumerate(datarefs):
                    sock.sendto(rref_packet(arguments.frequency, index, dataref), destination)
                next_subscribe = now + arguments.resubscribe
            timeout = max(0.0, min(0.1, next_emit - now))
            readable, _, _ = select.select([sock], [], [], timeout)
            if readable:
                packet, _address = sock.recvfrom(65535)
                if packet[:4] != b"RREF" or len(packet) < 13:
                    continue
                last_packet = time.monotonic()
                for offset in range(5, len(packet) - 7, 8):
                    index, value = struct.unpack_from("<if", packet, offset)
                    if 0 <= index < len(datarefs):
                        values[index] = value

            now = time.monotonic()
            if now >= next_emit:
                snapshot = {"t": round(now - started, 3),
                            "packet_age": None if last_packet is None else round(now - last_packet, 3)}
                snapshot.update(
                    (dataref, values.get(index))
                    for index, dataref in enumerate(datarefs)
                )
                print(json.dumps(snapshot, separators=(",", ":")), flush=True)
                next_emit += arguments.interval
    finally:
        for index, dataref in enumerate(datarefs):
            sock.sendto(rref_packet(0, index, dataref), destination)
        sock.close()


if __name__ == "__main__":
    main()
