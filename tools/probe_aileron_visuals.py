#!/usr/bin/env python3
"""Read-only, bounded live animation probe using X-Plane's localhost REST API.

No commands, PATCH requests, control overrides or simulator writes.
https://developer.x-plane.com/article/x-plane-web-api/
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import time
from urllib.parse import urlencode
from urllib.request import urlopen

from aileron_visual_provider import DATAREFS

BASE = "http://127.0.0.1:8086/api/v3"
NAMES = (*DATAREFS, "sim/flightmodel2/controls/roll_ratio",
         "sim/joystick/yoke_roll_ratio", "sim/flightmodel/failures/onground_any",
         "sim/time/paused", "sim/operation/misc/frame_rate_period")


def get(path):
    with urlopen(BASE + path, timeout=3) as response:
        return json.load(response)["data"]


def resolve(name):
    try:
        rows = get("/datarefs?" + urlencode({"filter[name]": name}))
        return name, rows[0]
    except Exception as error:
        return name, {"error": str(error)}


def read(item):
    name, meta = item
    if "id" not in meta:
        return name, None
    try:
        return name, get(f"/datarefs/{meta['id']}/value")
    except Exception as error:
        return name, {"error": str(error)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if not 0 <= args.seconds <= 30:
        parser.error("seconds must be between 0 and 30")
    if args.report.exists():
        parser.error("report must be a new file")
    with ThreadPoolExecutor(max_workers=6) as pool:
        metadata = dict(pool.map(resolve, NAMES))
        samples, start = [], time.monotonic()
        while True:
            values = dict(pool.map(read, metadata.items()))
            samples.append({"elapsed": time.monotonic() - start, "values": values})
            if time.monotonic() - start >= args.seconds:
                break
            time.sleep(0.1)
    result = {"read_only": True, "metadata": metadata, "samples": samples}
    with args.report.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    for name in NAMES:
        values = [s["values"][name] for s in samples
                  if isinstance(s["values"][name], (float, int))]
        print(name, {"min": min(values), "max": max(values), "last": values[-1]}
              if values else metadata[name])
    print("Samples:", len(samples), "Report:", args.report)


if __name__ == "__main__":
    main()
