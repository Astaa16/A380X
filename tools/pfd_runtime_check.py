#!/usr/bin/env python3
"""Record repeatable native-PFD ground checks, restoring controls on exit.

Requires an already loaded, stationary A380X-XP12 with avionics powered.
Uses the local X-Plane API; never loads a flight, moves the aircraft, changes
thrust, or overrides a flight-model state. Run only when ready to move the
flap and speedbrake controls. Proof is saved after every observation, including
failures and restoration attempts.
"""
from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import signal
import sys
import time
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from xplane_api import request


PFD = "a380x/xp12/pfd/"
PFD_FIELDS = (
    "on_ground", "flap_handle_index", "flap_config", "flap_valid", "slat_valid",
    "speedbrake_valid", "spoilers_armed", "gear_down", "flap_fault", "slat_fault",
    "stall_warning", "envelope_valid", "clean_performance_valid", "flap_ratio", "slat_ratio", "spoiler_ratio",
    "flap_target_ratio", "slat_target_ratio", "vmax_knots", "vfe_next_knots",
    "vls_knots", "alpha_prot_knots", "alpha_limit_knots", "stall_warning_knots",
    "f_speed_knots", "s_speed_knots", "green_dot_knots", "v1_knots", "vr_knots",
)
OPTIONAL_ENVELOPE_FIELDS = (
    "vls_knots", "alpha_prot_knots", "alpha_limit_knots", "stall_warning_knots",
    "f_speed_knots", "s_speed_knots", "v1_knots", "vr_knots",
)
CONTROLS = {
    "flap_handle": "sim/cockpit2/controls/flap_handle_request_ratio",
    "spoiler_handle": "sim/cockpit2/controls/speedbrake_ratio",
    "parking_brake": "sim/cockpit2/controls/parking_brake_ratio",
    "parking_valve": "sim/cockpit2/controls/park_brake_valve",
    **{f"baro_{side}": f"sim/cockpit2/gauges/actuators/barometer_setting_in_hg_{side}"
       for side in ("pilot", "copilot")},
    **{f"baro_std_{side}": f"sim/cockpit2/gauges/actuators/barometer_setting_is_std_{side}"
       for side in ("pilot", "copilot")},
    **{f"dh_{side}": f"sim/cockpit2/gauges/actuators/radio_altimeter_bug_ft_{side}"
       for side in ("pilot", "copilot")},
}
RAW = {
    "on_ground": "sim/flightmodel/failures/onground_any",
    "groundspeed_m_s": "sim/flightmodel/position/groundspeed",
    "paused": "sim/time/paused",
    "flap_actual": "sim/flightmodel2/controls/flap1_deploy_ratio",
    "slat_actual": "sim/flightmodel2/controls/slat1_deploy_ratio",
    "spoiler_actual": "sim/flightmodel2/controls/speedbrake_ratio",
    "gear_actual": "sim/flightmodel2/gear/deploy_ratio",
    "flap_detents": "sim/aircraft/controls/acf_flap_detents",
    "flap_angles_deg": "sim/aircraft/controls/acf_flap_dn",
    "mass_kg": "sim/flightmodel/weight/m_total",
    "pressure_altitude_ft": "sim/flightmodel2/position/pressure_altitude",
    "captain_valid": "a380x/xp12/avionics/pfd_captain_valid",
    "first_officer_valid": "a380x/xp12/avionics/pfd_first_officer_valid",
}
AIRCRAFT = {
    "description": "sim/aircraft/view/acf_descrip",
    "relative_path": "sim/aircraft/view/acf_relative_path",
    "icao": "sim/aircraft/view/acf_ICAO",
}


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def decode_text(value):
    if isinstance(value, str):
        try:
            return base64.b64decode(value, validate=True).decode("utf-8", "replace").rstrip("\0")
        except (ValueError, UnicodeError):
            return value.rstrip("\0")
    if isinstance(value, list):
        return bytes(value).decode("utf-8", "replace").rstrip("\0")
    return str(value)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def close(actual, expected, tolerance=.01):
    return isinstance(actual, (int, float)) and math.isfinite(actual) and abs(actual - expected) <= tolerance


class Check:
    def __init__(self, destination):
        self.destination = destination
        self.ids = {}
        self.original = {}
        self.modified = set()
        self.deadline = None
        self.proof = {
            "format": 1, "started_utc": utc_now(), "status": "running",
            "purpose": "Native A380X PFD ground-control integration checks",
            "checks": [], "errors": [], "restoration": {},
            "limits": {"move_timeout_seconds": 60, "poll_seconds": 1,
                       "aircraft_model_changed": False, "flight_state_overrides": False},
        }
        self.save()

    def save(self):
        self.destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.destination.with_suffix(self.destination.suffix + ".tmp")
        temporary.write_text(json.dumps(self.proof, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(self.destination)

    def discover(self):
        names = [*CONTROLS.values(), *RAW.values(), *AIRCRAFT.values(), *(PFD + f for f in PFD_FIELDS)]
        query = urlencode([("filter[name]", name) for name in names])
        rows = request("datarefs?" + query, timeout=10)["data"]
        self.ids = {row["name"]: row["id"] for row in rows}
        missing = [name for name in names if name not in self.ids]
        self.proof["missing_datarefs"] = missing
        self.save()
        require(not missing, "Required datarefs missing: " + ", ".join(missing))

    def read(self, name, timeout=3):
        if self.deadline is not None:
            remaining = self.deadline - time.monotonic()
            require(remaining > 0, "The 60-second move deadline expired")
            timeout = min(timeout, remaining)
        value = request(f"datarefs/{self.ids[name]}/value", timeout=timeout)["data"]
        if isinstance(value, float):
            require(math.isfinite(value), f"Non-finite value from {name}")
        return value

    def values(self, mapping):
        return {key: self.read(name) for key, name in mapping.items()}

    def snapshot(self):
        return {"utc": utc_now(), "raw": self.values(RAW), "controls": self.values(CONTROLS),
                "pfd": self.values({f: PFD + f for f in PFD_FIELDS})}

    def ensure_stationary(self):
        require(self.read(RAW["on_ground"]) == 1, "Ground checks require weight on wheels")
        speed = self.read(RAW["groundspeed_m_s"])
        require(0 <= speed < 1, f"Aircraft is moving ({speed} m/s)")

    def write_control(self, key, value, *, restoring=False):
        if not restoring:
            self.ensure_stationary()
            require(key in self.original, f"Original value was not saved for {key}")
        # Record before sending: a timeout can follow a successfully applied PATCH.
        self.modified.add(key)
        request(f"datarefs/{self.ids[CONTROLS[key]]}/value", {"data": value}, "PATCH", timeout=3)

    def wait_for(self, label, read_values, predicate):
        result = {"name": label, "started_utc": utc_now(), "status": "running", "samples": []}
        self.proof["checks"].append(result)
        self.save()
        started = time.monotonic()
        next_progress = 0
        previous_deadline = self.deadline
        self.deadline = started + 60
        try:
            while True:
                self.ensure_stationary()
                elapsed = time.monotonic() - started
                require(elapsed < 60, f"{label}: did not converge within 60 seconds")
                sample = {"elapsed_seconds": round(elapsed, 3), **read_values()}
                result["samples"].append(sample)
                self.save()
                if elapsed >= next_progress:
                    print(f"{label}: {elapsed:.0f}s {json.dumps(sample, sort_keys=True)}", flush=True)
                    next_progress += 5
                if predicate(sample):
                    result.update(status="passed", ended_utc=utc_now())
                    self.save()
                    return sample
                remaining = 60 - (time.monotonic() - started)
                require(remaining > 0, f"{label}: did not converge within 60 seconds")
                time.sleep(min(1, remaining))
        except BaseException as error:
            result.update(status="failed", ended_utc=utc_now(), error=f"{type(error).__name__}: {error}")
            self.save()
            raise
        finally:
            self.deadline = previous_deadline

    def flap_values(self):
        return {
            "handle": self.read(CONTROLS["flap_handle"]),
            "actual": self.read(RAW["flap_actual"]), "slat_actual": self.read(RAW["slat_actual"]),
            **{f: self.read(PFD + f) for f in (
                "flap_valid", "slat_valid", "flap_config", "flap_ratio", "slat_ratio",
                "flap_target_ratio", "slat_target_ratio", "vmax_knots", "vfe_next_knots")},
        }

    def envelope_check(self):
        state = self.values({f: PFD + f for f in ("envelope_valid", "clean_performance_valid", *OPTIONAL_ENVELOPE_FIELDS)})
        record = {"name": "No fabricated envelope speeds", "utc": utc_now(), "pfd": state}
        self.proof["checks"].append(record)
        try:
            if state["envelope_valid"] == 0:
                require(all(close(state[f], 0, .0001) for f in OPTIONAL_ENVELOPE_FIELDS if f != "vls_knots"),
                        "An unavailable envelope produced computed protection/takeoff speeds")
                if state["clean_performance_valid"] == 1:
                    clean = {"mass_kg": self.read(RAW["mass_kg"]),
                             "pressure_altitude_ft": self.read(RAW["pressure_altitude_ft"]),
                             "flap_handle": self.read(CONTROLS["flap_handle"]),
                             "flap_actual": self.read(RAW["flap_actual"]),
                             "slat_actual": self.read(RAW["slat_actual"]),
                             "spoiler_handle": self.read(CONTROLS["spoiler_handle"]),
                             "spoiler_actual": self.read(RAW["spoiler_actual"])}
                    record["clean_performance_inputs"] = clean
                    require(250000 <= clean["mass_kg"] <= 600000 and
                            0 <= clean["pressure_altitude_ft"] <= 43000,
                            "Clean VLS enabled outside the pinned PRIM table domain")
                    require(all(close(clean[key], 0, .0001) for key in
                                ("flap_handle", "flap_actual", "slat_actual", "spoiler_actual"))
                            and clean["spoiler_handle"] <= 0,
                            "Clean VLS enabled with extended/requested surfaces")
                    require(40 < state["vls_knots"] < 450, "Valid clean lookup produced no VLS")
                    record["mode"] = "clean_vls_only_other_envelope_speeds_zero"
                else:
                    require(close(state["vls_knots"], 0, .0001),
                            "VLS present without a clean lookup or aircraft-systems provider")
                    record["mode"] = "unavailable_speeds_are_zero"
            else:
                require(state["clean_performance_valid"] == 0,
                        "Complete provider and fallback are both marked as VLS source")
                name = "a380x/xp12/systems/pfd/envelope_valid"
                rows = request("datarefs?" + urlencode({"filter[name]": name}))["data"]
                exact = [r for r in rows if r["name"] == name]
                require(len(exact) == 1, "Envelope valid without its aircraft-systems provider")
                provider = request(f'datarefs/{exact[0]["id"]}/value')["data"]
                require(provider == 1, "Aircraft-systems envelope provider is not valid")
                record.update(mode="provider_present", provider_valid=provider)
            record["status"] = "passed"
        except BaseException as error:
            record.update(status="failed", error=f"{type(error).__name__}: {error}")
            raise
        finally:
            self.save()

    def flap_check(self, label, handle, target, config, vmax, vfe_next):
        self.write_control("flap_handle", handle)
        state = self.wait_for(label, self.flap_values, lambda s:
            close(s["handle"], handle, .001) and close(s["actual"], target, .005)
            and close(s["flap_target_ratio"], target, .005)
            and close(s["flap_ratio"], s["actual"], .005)
            and close(s["slat_actual"], 0 if handle == 0 else 1, .01)
            and close(s["slat_ratio"], s["slat_actual"], .01)
            and close(s["slat_target_ratio"], 0 if handle == 0 else 1, .01)
            and s["flap_valid"] == 1 and s["slat_valid"] == 1
            and s["flap_config"] == config and 40 < s["vmax_knots"] <= vmax + .1
            and close(s["vfe_next_knots"], vfe_next, .1))
        self.proof["checks"][-1]["confirmed_snapshot"] = self.snapshot()
        self.save()
        self.envelope_check()

    def spoiler_values(self):
        return {"handle": self.read(CONTROLS["spoiler_handle"]),
                "actual": self.read(RAW["spoiler_actual"]),
                **{f: self.read(PFD + f) for f in ("speedbrake_valid", "spoilers_armed", "spoiler_ratio")}}

    def run(self):
        try:
            with urlopen(Request("http://127.0.0.1:8086/api/capabilities",
                                 headers={"Accept": "application/json"}), timeout=3) as response:
                self.proof["capabilities"] = json.load(response)
        except Exception as error:
            self.proof["capabilities_error"] = f"{type(error).__name__}: {error}"
        self.discover()
        self.proof["aircraft"] = {key: decode_text(self.read(name)) for key, name in AIRCRAFT.items()}
        require("A380X-XP12" in self.proof["aircraft"]["relative_path"],
                "The loaded aircraft is not A380X-XP12")
        self.original = self.values(CONTROLS)
        self.proof["original_controls"] = self.original
        self.proof["initial_snapshot"] = self.snapshot()
        self.save()
        self.ensure_stationary()
        initial = self.proof["initial_snapshot"]
        require(initial["raw"]["paused"] == 0, "Unpause X-Plane before moving the controls")
        require(initial["raw"]["captain_valid"] == 1 and initial["raw"]["first_officer_valid"] == 1,
                "Both PFDs must be powered and valid")
        require(initial["raw"]["flap_detents"] == 6 and
                initial["raw"]["flap_angles_deg"][:7] == [0, 1, 5, 10, 20, 25, 40],
                "This check expects the verified native six-detent flap schedule")
        self.write_control("parking_brake", 1)
        self.write_control("parking_valve", 1)
        self.wait_for("Parking brake secured", lambda: {
            "ratio": self.read(CONTROLS["parking_brake"]),
            "valve": self.read(CONTROLS["parking_valve"])},
            lambda s: close(s["ratio"], 1, .01) and s["valve"] == 1)
        self.flap_check("Flaps 2: handle 0.5 / surface 0.25", .5, .25, 3, 220, 196)
        self.flap_check("Flaps FULL", 1, 1, 5, 182, 0)
        self.flap_check("Flaps clean", 0, 0, 0, 340, 263)
        self.write_control("spoiler_handle", -.5)
        self.wait_for("Spoilers armed", self.spoiler_values, lambda s:
            close(s["handle"], -.5, .01) and s["speedbrake_valid"] == 1 and s["spoilers_armed"] == 1)
        self.write_control("spoiler_handle", .5)
        self.wait_for("Spoilers deployed 0.5", self.spoiler_values, lambda s:
            close(s["handle"], .5, .01) and close(s["actual"], .5, .025)
            and close(s["spoiler_ratio"], s["actual"], .01)
            and s["speedbrake_valid"] == 1 and s["spoilers_armed"] == 0)
        self.envelope_check()
        self.proof["final_test_snapshot"] = self.snapshot()
        self.save()

    def restore(self):
        if not self.original or not self.modified:
            self.proof["restoration"] = {"status": "not_needed"}
            self.save()
            return True
        results = self.proof["restoration"]
        results.update(status="running", started_utc=utc_now(), controls={})
        # Restore flap/spoiler commands with the brake still applied. Barometer
        # and DH were observed only, and are checked for unchanged readback.
        order = ["flap_handle", "spoiler_handle", "parking_brake", "parking_valve"]
        for key in order:
            if key not in self.modified:
                continue
            try:
                self.write_control(key, self.original[key], restoring=True)
                actual = self.read(CONTROLS[key])
                require(close(actual, self.original[key], .01), f"{key} restore readback differs")
                results["controls"][key] = {"status": "restored", "requested": self.original[key], "actual": actual}
            except BaseException as error:
                results["controls"][key] = {"status": "failed", "error": f"{type(error).__name__}: {error}"}
            self.save()
        for key in self.original:
            if key in self.modified:
                continue
            try:
                actual = self.read(CONTROLS[key])
                require(close(actual, self.original[key], .01), f"Observed control {key} changed during checks")
                results["controls"][key] = {"status": "unchanged", "actual": actual}
            except BaseException as error:
                results["controls"][key] = {"status": "failed", "error": f"{type(error).__name__}: {error}"}
        success = all(item["status"] != "failed" for item in results["controls"].values())
        results.update(status="passed" if success else "failed", ended_utc=utc_now())
        self.save()
        return success


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("proof", nargs="?", type=Path,
                        default=Path.home() / "Desktop/A380/proof/PFD-live-control-checks.json")
    args = parser.parse_args(argv)
    check = Check(args.proof.expanduser().resolve())
    def interrupted(*_):
        raise KeyboardInterrupt("SIGTERM")
    signal.signal(signal.SIGTERM, interrupted)
    success = False
    try:
        check.run()
        success = True
    except BaseException as error:
        check.proof["errors"].append({"utc": utc_now(), "type": type(error).__name__, "message": str(error)})
        print(f"PFD check failed: {type(error).__name__}: {error}", file=sys.stderr, flush=True)
    finally:
        restored = check.restore()
        check.proof.update(status="passed" if success and restored else "failed", ended_utc=utc_now())
        check.save()
    print(f'PFD check {check.proof["status"]}: {check.destination}', flush=True)
    return 0 if check.proof["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
