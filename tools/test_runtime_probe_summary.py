import json
from pathlib import Path
import tempfile
import unittest

from summarize_runtime_probe import summarize


class RuntimeSummaryTests(unittest.TestCase):
    def result(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'probe.jsonl'
            path.write_text('\n'.join(json.dumps(row) for row in rows))
            return summarize(path)

    def rows(self):
        return [{'t': i, 'packet_age': 0, 'sim/time/paused': 0,
                 'sim/time/total_flight_time_sec': i,
                 'sim/operation/failures/enable_random_failures': 0,
                 'sim/flightmodel2/misc/has_crashed': 0} for i in range(65)]

    def test_fresh_continuous_ground_interval(self):
        self.assertTrue(self.result(self.rows())['sustained_ground_check'])

    def test_stale_packets_are_not_evidence(self):
        rows = self.rows()
        for row in rows:
            row['packet_age'] = 3
        self.assertFalse(self.result(rows)['sustained_ground_check'])

    def test_missing_random_failure_state_is_not_evidence(self):
        rows = self.rows()
        for row in rows:
            del row['sim/operation/failures/enable_random_failures']
        self.assertFalse(self.result(rows)['sustained_ground_check'])

    def test_crash_and_missing_middle_interval_fail(self):
        rows = self.rows()
        rows[30]['sim/flightmodel2/misc/has_crashed'] = 1
        self.assertFalse(self.result(rows)['sustained_ground_check'])
        rows = self.rows()
        del rows[10:15]
        self.assertFalse(self.result(rows)['sustained_ground_check'])


if __name__ == '__main__':
    unittest.main()
