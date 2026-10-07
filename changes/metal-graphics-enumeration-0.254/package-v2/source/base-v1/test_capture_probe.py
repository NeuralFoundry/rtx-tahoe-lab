import plistlib
import unittest
from capture_probe import wait_complete


class CaptureTest(unittest.TestCase):
    def test_async_start_waits_for_final_record(self):
        states = [[], [{"ProbeVersion": "0.4.0"}], [{"ProbeVersion": "0.4.0", "ProbeComplete": True}]]
        calls = []
        def read():
            calls.append(1)
            return plistlib.dumps(states.pop(0))
        raw = wait_complete(read, "0.4.0", now=lambda: 0, sleep=lambda _: None)
        self.assertTrue(plistlib.loads(raw)[0]["ProbeComplete"])
        self.assertEqual(len(calls), 3)

    def test_timeout_is_not_success(self):
        times = iter([0, 46])
        with self.assertRaises(TimeoutError):
            wait_complete(lambda: plistlib.dumps([{"ProbeVersion": "0.4.0"}]), "0.4.0", now=lambda: next(times))

    def test_wrong_cached_version_rejected(self):
        with self.assertRaises(ValueError):
            wait_complete(lambda: plistlib.dumps([{"ProbeVersion": "0.3.0", "ProbeComplete": True}]), "0.4.0")


if __name__ == "__main__": unittest.main()
