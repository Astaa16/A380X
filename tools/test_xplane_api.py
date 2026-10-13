"""Offline regression checks for the local X-Plane Web API transport.

No simulator is contacted. Run with:
    python3 -m unittest discover -s tools -p 'test_xplane_api.py' -v
"""
from __future__ import annotations

import importlib.util
from http.client import RemoteDisconnected
from io import StringIO
import json
from pathlib import Path
import unittest
from unittest.mock import MagicMock, call, patch
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlsplit


SPEC = importlib.util.spec_from_file_location(
    "a380x_api_under_test", Path(__file__).with_name("xplane_api.py")
)
api = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(api)


class RequestTests(unittest.TestCase):
    def response(self, content):
        response = MagicMock()
        response.__enter__.return_value = response
        response.read.return_value = content
        return response

    def test_get_decodes_json_and_uses_local_versioned_endpoint(self):
        response = self.response(b'{"data": 42}')
        with patch.object(api, "urlopen", return_value=response) as opened:
            self.assertEqual(api.request("datarefs/7/value"), {"data": 42})
        req = opened.call_args.args[0]
        self.assertEqual(req.full_url, "http://127.0.0.1:8086/api/v3/datarefs/7/value")
        self.assertEqual(req.get_method(), "GET")
        self.assertIsNone(req.data)
        self.assertEqual(req.get_header("Content-type"), "application/json")
        self.assertEqual(opened.call_args.kwargs, {"timeout": 10})
        response.__exit__.assert_called_once()

    def test_patch_encodes_nested_json_without_changing_types(self):
        payload = {"data": [0, False, None, {"label": "pilot \u00e9"}]}
        with patch.object(api, "urlopen", return_value=self.response(b"")) as opened:
            self.assertIsNone(api.request("datarefs/9/value", payload, "PATCH", timeout=17))
        req = opened.call_args.args[0]
        self.assertEqual(req.get_method(), "PATCH")
        self.assertEqual(json.loads(req.data.decode("utf-8")), payload)
        self.assertEqual(opened.call_args.kwargs, {"timeout": 17})

    def test_body_defaults_to_post_and_preserves_falsey_values(self):
        for payload in ({}, False, 0):
            with self.subTest(payload=payload), patch.object(
                api, "urlopen", return_value=self.response(b"null")
            ) as opened:
                self.assertIsNone(api.request("flight", payload))
                req = opened.call_args.args[0]
                self.assertEqual(req.get_method(), "POST")
                self.assertEqual(json.loads(req.data), payload)

    def test_transport_errors_are_not_reported_as_success(self):
        failures = [
            URLError("connection refused"),
            HTTPError("http://127.0.0.1:8086/api/v3/flight", 404, "Not Found", {}, None),
            TimeoutError("timed out"),
        ]
        for failure in failures:
            with self.subTest(error=type(failure).__name__), patch.object(
                api, "urlopen", side_effect=failure
            ):
                with self.assertRaises(type(failure)):
                    api.request("flight")

    def test_nonempty_invalid_json_is_rejected(self):
        with patch.object(api, "urlopen", return_value=self.response(b"not JSON")):
            with self.assertRaises(json.JSONDecodeError):
                api.request("datarefs/7/value")


class IdentifierTests(unittest.TestCase):
    def test_uses_exact_match_when_server_also_returns_similar_names(self):
        name = "sim/operation/failures/enable_random_failures"
        result = {"data": [
            {"name": name + "_old", "id": 1},
            {"name": name, "id": 23},
            {"name": name.upper(), "id": 3},
        ]}
        with patch.object(api, "request", return_value=result):
            self.assertEqual(api.identifier("datarefs", name), 23)

    def test_query_encodes_name_as_one_filter_value(self):
        name = "custom/array[2]?x=1&label=pilot \u00e9"
        with patch.object(api, "request", return_value={"data": [{"name": name, "id": 0}]}) as request:
            self.assertEqual(api.identifier("commands", name), 0)
        sent = urlsplit(request.call_args.args[0])
        self.assertEqual(sent.path, "commands")
        self.assertEqual(parse_qs(sent.query), {"filter[name]": [name]})

    def test_missing_or_duplicate_exact_names_fail_closed(self):
        name = "sim/test/value"
        for items in (
            [],
            [{"name": name + "_other", "id": 2}],
            [{"name": name, "id": 2}, {"name": name, "id": 3}],
        ):
            with self.subTest(items=items), patch.object(api, "request", return_value={"data": items}):
                with self.assertRaisesRegex(ValueError, "Expected one datarefs named sim/test/value"):
                    api.identifier("datarefs", name)


class RandomFailureSwitchTests(unittest.TestCase):
    def test_disables_native_switch_and_verifies_readback(self):
        with patch.object(api, "identifier", return_value=314) as identifier, patch.object(
            api, "request", side_effect=[None, {"data": 0}]
        ) as request:
            self.assertEqual(api.disable_random_failures(), {"data": 0})
        identifier.assert_called_once_with("datarefs", "sim/operation/failures/enable_random_failures")
        self.assertEqual(request.call_args_list, [
            call("datarefs/314/value", {"data": 0}, "PATCH"),
            call("datarefs/314/value"),
        ])
        self.assertIs(type(request.call_args_list[0].args[1]["data"]), int)

    def test_nonzero_or_missing_readback_cannot_pass(self):
        for state in ({"data": 1}, {"data": -1}, {"data": "0"}, {}):
            with self.subTest(state=state), patch.object(api, "identifier", return_value=314), patch.object(
                api, "request", side_effect=[None, state]
            ):
                with self.assertRaisesRegex(RuntimeError, "Random failures remain enabled"):
                    api.disable_random_failures()

    def test_failed_write_does_not_continue_to_verification(self):
        with patch.object(api, "identifier", return_value=314), patch.object(
            api, "request", side_effect=URLError("connection refused")
        ) as request:
            with self.assertRaises(URLError):
                api.disable_random_failures()
        request.assert_called_once_with("datarefs/314/value", {"data": 0}, "PATCH")


class GroundStartMainTests(unittest.TestCase):
    def assert_one_ground_start(self, request):
        request.assert_called_once()
        invocation = request.call_args
        self.assertEqual(invocation.args[0], "flight")
        self.assertEqual(invocation.args[2], "POST")
        self.assertEqual(invocation.kwargs, {"timeout": 240})
        data = invocation.args[1]["data"]
        self.assertEqual(data["aircraft"]["path"], "Aircraft/A380X-XP12/A380X-XP12.acf")
        self.assertEqual(data["failures"]["mean_time_between_failures_in_hours"], 5000)

    def test_normal_initialization_disables_failures_after_post(self):
        events = []

        def initialize(*args, **kwargs):
            events.append("post")
            return {"data": {"status": "accepted"}}

        def disable():
            events.append("disable")
            return {"data": 0}

        with patch.object(api.sys, "argv", ["xplane_api.py", "ground-start"]), patch.object(
            api, "request", side_effect=initialize
        ) as request, patch.object(api, "disable_random_failures", side_effect=disable) as disable_switch, patch.object(
            api.sys, "stdout", new_callable=StringIO
        ) as stdout, patch.object(api.sys, "stderr", new_callable=StringIO) as stderr:
            api.main()
        self.assert_one_ground_start(request)
        disable_switch.assert_called_once_with()
        self.assertEqual(events, ["post", "disable"])
        self.assertEqual(json.loads(stdout.getvalue()), {
            "flight": {"data": {"status": "accepted"}}, "random_failures": {"data": 0}
        })
        self.assertEqual(stderr.getvalue(), "")

    def test_remote_disconnect_remains_unconfirmed_but_disables_failures_without_retry(self):
        with patch.object(api.sys, "argv", ["xplane_api.py", "ground-start"]), patch.object(
            api, "request", side_effect=RemoteDisconnected("no response")
        ) as request, patch.object(api, "disable_random_failures", return_value={"data": 0}) as disable_switch, patch.object(
            api.sys, "stdout", new_callable=StringIO
        ) as stdout, patch.object(api.sys, "stderr", new_callable=StringIO) as stderr:
            api.main()
        self.assert_one_ground_start(request)
        disable_switch.assert_called_once_with()
        self.assertEqual(json.loads(stdout.getvalue()), {
            "flight": {
                "status": "initialization_unconfirmed", "aircraft_confirmed": False,
                "error": "RemoteDisconnected", "detail": "no response", "post_retried": False,
            },
            "random_failures": {"data": 0},
        })
        self.assertIn("Flight initialization is unconfirmed", stderr.getvalue())
        self.assertIn("No POST retry", stderr.getvalue())

    def test_other_initialization_errors_propagate_without_disable_or_retry(self):
        failures = [
            URLError("connection refused"),
            HTTPError("http://127.0.0.1:8086/api/v3/flight", 500, "Server error", {}, None),
            TimeoutError("timed out"),
            ConnectionResetError("reset"),
        ]
        for failure in failures:
            with self.subTest(error=type(failure).__name__), patch.object(
                api.sys, "argv", ["xplane_api.py", "ground-start"]
            ), patch.object(api, "request", side_effect=failure) as request, patch.object(
                api, "disable_random_failures"
            ) as disable_switch, patch.object(api.sys, "stdout", new_callable=StringIO) as stdout:
                with self.assertRaises(type(failure)):
                    api.main()
            self.assert_one_ground_start(request)
            disable_switch.assert_not_called()
            self.assertEqual(stdout.getvalue(), "")

    def test_post_disconnect_does_not_swallow_disable_failure_or_emit_success(self):
        with patch.object(api.sys, "argv", ["xplane_api.py", "ground-start"]), patch.object(
            api, "request", side_effect=RemoteDisconnected("no response")
        ) as request, patch.object(
            api, "disable_random_failures", side_effect=RemoteDisconnected("verification disconnected")
        ) as disable_switch, patch.object(api.sys, "stdout", new_callable=StringIO) as stdout, patch.object(
            api.sys, "stderr", new_callable=StringIO
        ) as stderr:
            with self.assertRaisesRegex(RemoteDisconnected, "verification disconnected"):
                api.main()
        self.assert_one_ground_start(request)
        disable_switch.assert_called_once_with()
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("Flight initialization is unconfirmed", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
