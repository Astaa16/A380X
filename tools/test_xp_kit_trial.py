"""Check the live-data boundary without changing or launching X-Plane."""
import base64
import unittest

from serve_xp_kit_trial import ADIRS, AIRCRAFT, LiveSource, NAMES, decode_text, demo


class FakeSimulator(LiveSource):
    def __init__(self):
        super().__init__("http://127.0.0.1:8086/api/v3")
        self.values = demo()["values"]
        self.relative = "Aircraft/A380X-XP12/A380X-XP12.acf"
        self.paths = []
        self.queries = 0

    def get(self, suffix):
        self.paths.append(suffix)
        if suffix.startswith("datarefs?"):
            self.queries += 1
            names = [*NAMES, "sim/aircraft/view/acf_relative_path"]
            self.fake_ids = {i: name for i, name in enumerate(names)}
            return [{"id": i, "name": name} for i, name in self.fake_ids.items()]
        identifier = int(suffix.split("/")[1])
        name = self.fake_ids[identifier]
        if name == "sim/aircraft/view/acf_relative_path":
            return base64.b64encode(self.relative.encode()).decode()
        return self.values.get(name)


class LiveTests(unittest.TestCase):
    def setUp(self):
        self.sim = FakeSimulator()
        self.addCleanup(self.sim.pool.shutdown)

    def test_exact_project_aircraft_and_cached_reads(self):
        self.assertTrue(AIRCRAFT.exists())
        state = self.sim.snapshot()
        self.assertTrue(state["ok"], state)
        self.assertEqual(state["values"][ADIRS + "adr1_ias_knots"], 250)
        requests = len(self.sim.paths)
        self.assertIs(self.sim.snapshot(), state)
        self.assertEqual(len(self.sim.paths), requests)
        self.assertEqual(self.sim.queries, 1)
        self.assertTrue(all(p.startswith("datarefs") for p in self.sim.paths))

    def test_other_aircraft_never_returns_seeded_or_live_values(self):
        self.sim.relative = "Aircraft/FlyByWire A380X/FlyByWire A380X.acf"
        state = self.sim.snapshot()
        self.assertFalse(state["ok"])
        self.assertEqual(state["mode"], "live")
        self.assertEqual(state["values"], {})
        self.assertIn("current aircraft", state["error"])

    def test_missing_project_sensor_is_reported(self):
        del self.sim.values[ADIRS + "adr1_valid"]
        state = self.sim.snapshot()
        self.assertFalse(state["ok"])
        self.assertIn("datarefs missing", state["error"])

    def test_simulator_disconnect_is_invalid(self):
        def unavailable(_suffix):
            raise ConnectionError("simulator unavailable")
        self.sim.get = unavailable
        state = self.sim.snapshot()
        self.assertFalse(state["ok"])
        self.assertEqual(state["values"], {})
        self.assertIn("unavailable", state["error"])

    def test_nonfinite_sample_never_becomes_valid_json(self):
        self.sim.values[ADIRS + "adr1_ias_knots"] = float("nan")
        state = self.sim.snapshot()
        self.assertFalse(state["ok"])
        self.assertEqual(state["values"], {})

    def test_demo_failure_affects_one_unit_only(self):
        values = demo("failed")["values"]
        self.assertEqual(values[ADIRS + "adr1_valid"], 0)
        self.assertEqual(values[ADIRS + "adr2_valid"], 1)
        self.assertEqual(values[ADIRS + "ir1_attitude_valid"], 0)
        self.assertEqual(values[ADIRS + "ir2_attitude_valid"], 1)
        self.assertEqual(demo("dark")["values"]["sim/cockpit2/electrical/bus_volts"], [0])

    def test_path_decoding(self):
        expected = "Aircraft/A380X-XP12/A380X-XP12.acf"
        self.assertEqual(decode_text(base64.b64encode(expected.encode()).decode()), expected)
        self.assertEqual(decode_text(list(expected.encode()) + [0]), expected)
        self.assertEqual(decode_text(expected), expected)


if __name__ == "__main__":
    unittest.main()
