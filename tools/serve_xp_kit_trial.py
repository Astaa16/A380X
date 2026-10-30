#!/usr/bin/env python3
"""Serve the original FlyByWire PFD / xp-kit trial for this A380X project."""
from __future__ import annotations

import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
from functools import partial
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
from pathlib import Path
import shutil
import threading
import time
from urllib.parse import parse_qs, urlencode, urlparse
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "native/avionics/xp-kit"
BUILD = ROOT / "build/xp-kit-pfd-test"
AIRCRAFT = ROOT / "output/A380X-XP12/A380X-XP12.acf"
ADIRS = "a380x/xp12/adirs/"
IR_FIELDS = ("attitude_valid", "heading_valid", "nav_valid", "pitch_deg", "roll_deg", "heading_deg",
             "track_deg", "vertical_speed_fpm", "ground_speed_knots", "flight_path_deg")
ADR_FIELDS = ("valid", "ias_knots", "altitude_ft", "mach")
RAW = ("sim/cockpit2/electrical/bus_volts", "sim/flightmodel/failures/onground_any",
       "sim/time/total_flight_time_sec", "sim/time/sim_speed_actual", "sim/time/paused",
       "sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_pilot",
       "sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_copilot",
       "sim/operation/failures/rel_pil_radalt", "sim/operation/failures/rel_cop_radalt",
       "a380x/xp12/du/captain_pfd", "a380x/xp12/du/first_officer_pfd")
NAMES = (*RAW, *(ADIRS + f"ir{n}_{field}" for n in (1, 2, 3) for field in IR_FIELDS),
         *(ADIRS + f"adr{n}_{field}" for n in (1, 2, 3) for field in ADR_FIELDS),
         *(ADIRS + f"{side}_{kind}_source" for side in ("captain", "first_officer") for kind in ("ir", "adr")))


def prepare(source: Path):
    """Copy only this trial's library, runtime and required font, with provenance."""
    copies = {"library/index.js": source / "xp-kit/dist/index.js",
              "library/LICENSE": source / "xp-kit/LICENSE",
              "runtime/LICENSE": source / "LICENSE",
              "Fonts/fbw-a380x/FBW-Display-EIS-A380.ttf": source / "fbw/xplane/fbw-a380x/src/fonts/FBW-Display-EIS-A380.ttf"}
    for name in ("msfs-runtime.js", "msfs/coherent.js", "msfs/simvar.js", "msfs/environment.js", "msfs/instrument.js"):
        copies["runtime/" + name] = source / "app/js" / name
    images = source / "fbw/xplane/fbw-a380x/src/base/flybywire-aircraft-a380-842/html_ui/Images/fbw-a380x"
    copies["Images/fbw-a380x/Common/thalesTest.svg"] = images / "Common/thalesTest.svg"
    copies["Images/fbw-a380x/TRIM_INDICATOR.png"] = images / "TRIM_INDICATOR.png"
    for path in (*copies.values(), BUILD / "PFD/bundle.js", BUILD / "PFD/bundle.css"):
        if not path.is_file():
            raise FileNotFoundError(f"Required built trial asset missing: {path}")
    manifest = {"format": 1, "aircraft": str(AIRCRAFT), "source": str(source), "files": {}}
    bundle = BUILD / "PFD/bundle.js"
    original = bundle.read_text()
    malformed = 'transform:`translate(0 ${offset}`'
    corrected = 'transform:`translate(0 ${offset})`'
    if malformed in original:
        if original.count(malformed) != 1:
            raise ValueError("VerticalTape SVG correction must match exactly once")
        shutil.copy2(bundle, bundle.with_name("bundle.original.js"))
        bundle.write_text(original.replace(malformed, corrected))
    if (BUILD / "PFD/bundle.original.js").exists():
        manifest["patches"] = [{"source": "fbw-a380x/src/systems/instruments/src/PFD/VerticalTape.tsx:88",
                                "reason": "Close the altitude graduation's SVG translate function",
                                "original_sha256": hashlib.sha256((BUILD / "PFD/bundle.original.js").read_bytes()).hexdigest(),
                                "matches": 1}]
    for relative, original in copies.items():
        destination = BUILD / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, destination)
        manifest["files"][relative] = {"source": str(original), "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}
    for relative in ("PFD/bundle.js", "PFD/bundle.css"):
        manifest["files"][relative] = {"sha256": hashlib.sha256((BUILD / relative).read_bytes()).hexdigest()}
    (BUILD / "trial-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def demo(scene="level"):
    if scene not in {"level", "climb", "turn", "failed", "dark"}:
        raise ValueError("Unknown demo scene")
    values = {"sim/cockpit2/electrical/bus_volts": [0 if scene == "dark" else 28],
              "sim/flightmodel/failures/onground_any": 0,
              "sim/time/total_flight_time_sec": 0, "sim/time/sim_speed_actual": 1, "sim/time/paused": 0,
              "a380x/xp12/du/captain_pfd": 1, "a380x/xp12/du/first_officer_pfd": 1,
              "sim/operation/failures/rel_pil_radalt": 0, "sim/operation/failures/rel_cop_radalt": 0,
              "sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_pilot": 5000,
              "sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_copilot": 5000}
    for n in (1, 2, 3):
        ir = {"attitude_valid": 1, "heading_valid": 1, "nav_valid": 1,
              "pitch_deg": 8 if scene == "climb" else 2, "roll_deg": 25 if scene == "turn" else 0,
              "heading_deg": 90, "track_deg": 90, "vertical_speed_fpm": 1800 if scene == "climb" else 0,
              "ground_speed_knots": 280, "flight_path_deg": 3 if scene == "climb" else 0}
        adr = {"valid": 1, "ias_knots": 250, "altitude_ft": 10000, "mach": .45}
        if scene == "failed" and n == 1:
            ir.update(attitude_valid=0, heading_valid=0, nav_valid=0)
            adr["valid"] = 0
        values.update({ADIRS + f"ir{n}_{key}": value for key, value in ir.items()})
        values.update({ADIRS + f"adr{n}_{key}": value for key, value in adr.items()})
    for side, index in (("captain", 0), ("first_officer", 1)):
        for kind in ("ir", "adr"):
            values[ADIRS + f"{side}_{kind}_source"] = index
    return {"ok": True, "mode": "demo", "scene": scene, "aircraft": "A380X-XP12", "values": values}


def decode_text(value):
    if isinstance(value, str):
        try:
            return base64.b64decode(value, validate=True).decode("utf-8").rstrip("\0")
        except (ValueError, UnicodeError):
            return value.rstrip("\0")
    if isinstance(value, list):
        return bytes(value).decode("utf-8").rstrip("\0")
    raise ValueError("Unexpected aircraft-path value")


class LiveSource:
    """GET requests only, gated to the project's exact .acf path."""
    def __init__(self, api):
        self.api = api.rstrip("/") + "/"
        self.ids = {}
        self.lock = threading.Lock()
        self.pool = ThreadPoolExecutor(max_workers=8)
        self.cached = None
        self.last = 0

    def get(self, suffix):
        with urlopen(self.api + suffix, timeout=1.5) as response:
            return json.load(response)["data"]

    def read(self, name):
        if name not in self.ids:
            return None
        return self.get(f"datarefs/{self.ids[name]}/value")

    def snapshot(self):
        with self.lock:
            if self.cached and time.monotonic() - self.last < .2:
                return self.cached
            try:
                if not self.ids:
                    names = (*NAMES, "sim/aircraft/view/acf_relative_path")
                    rows = self.get("datarefs?" + urlencode([("filter[name]", name) for name in names]))
                    self.ids = {row["name"]: row["id"] for row in rows if row["name"] in names}
                relative = decode_text(self.read("sim/aircraft/view/acf_relative_path"))
                xp_root = Path("/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12")
                loaded = (xp_root / relative.replace("\\", "/")).resolve()
                if loaded != AIRCRAFT.resolve():
                    raise ValueError(f"Load this project's A380X-XP12.acf; current aircraft: {relative}")
                results = list(self.pool.map(self.read, NAMES))
                missing = [name for name, value in zip(NAMES, results) if value is None]
                if missing:
                    raise ValueError(f"Required project datarefs missing ({len(missing)}): {missing[0]}")
                if decode_text(self.read("sim/aircraft/view/acf_relative_path")) != relative:
                    raise ValueError("Aircraft changed while reading the avionics sample")
                values = dict(zip(NAMES, results))
                json.dumps(values, allow_nan=False)
                result = {"ok": True, "mode": "live", "aircraft": relative,
                          "values": values, "sampled_unix": time.time()}
            except Exception as error:
                self.ids = {}
                result = {"ok": False, "mode": "live", "error": str(error), "values": {}}
            self.cached = result
            self.last = time.monotonic()
            return result


class Handler(BaseHTTPRequestHandler):
    def __init__(self, *args, live=None, **kwargs):
        self.live = live
        super().__init__(*args, **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/snapshot":
            try:
                result = self.live.snapshot() if self.live else demo(parse_qs(parsed.query).get("scene", ["level"])[0])
            except ValueError as error:
                self.send_error(400, str(error))
                return
            content = json.dumps(result, allow_nan=False).encode()
            mime = "application/json"
        else:
            relative = parsed.path.lstrip("/")
            if relative == "favicon.ico":
                self.send_response(204)
                self.end_headers()
                return
            source_files = {"": "trial.html", "trial.js": "trial.js", "pfd.html": "pfd.html", "bridge.js": "bridge.js"}
            generated = {"PFD/bundle.js", "PFD/bundle.css", "library/index.js", "trial-manifest.json",
                         "runtime/msfs-runtime.js", "runtime/msfs/coherent.js", "runtime/msfs/environment.js",
                         "runtime/msfs/simvar.js", "runtime/msfs/instrument.js",
                         "Fonts/fbw-a380x/FBW-Display-EIS-A380.ttf", "Images/fbw-a380x/Common/thalesTest.svg",
                         "Images/fbw-a380x/TRIM_INDICATOR.png"}
            filename = SOURCE / source_files[relative] if relative in source_files else BUILD / relative if relative in generated else None
            if filename is None or not filename.is_file():
                self.send_error(404)
                return
            content = filename.read_bytes()
            mime = mimetypes.guess_type(str(filename))[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(content)

    def log_message(self, fmt, *args):
        if len(args) < 2 or str(args[1]) not in {"200", "304"}:
            super().log_message(fmt, *args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", type=Path, metavar="FBW_SOURCE", help="Copy built library and supplied browser runtime")
    parser.add_argument("--live", action="store_true", help="Read only from this aircraft in X-Plane; no seeded data")
    parser.add_argument("--api", default="http://127.0.0.1:8086/api/v3")
    parser.add_argument("--port", type=int, default=8381)
    args = parser.parse_args()
    if args.prepare:
        prepare(args.prepare.resolve())
    if not (BUILD / "trial-manifest.json").is_file():
        parser.error("Prepare trial assets first with --prepare <FlyByWire source folder>")
    live = LiveSource(args.api) if args.live else None
    server = ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, live=live))
    print(f"FlyByWire xp-kit PFD: http://127.0.0.1:{server.server_port}/ ({'LIVE read-only' if live else 'DEMO DATA'})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if live:
            live.pool.shutdown(wait=False, cancel_futures=True)


if __name__ == "__main__":
    main()
