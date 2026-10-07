import struct
import unittest
from decode_probe import decode_snapshot, STATE_OFFSETS
import test_decode_probe


class StateDecodeTest(unittest.TestCase):
    def fixture(self, values=(0x176000a1, 0, 1, 0xff, 6144)):
        node = test_decode_probe.HeaderTest().mmio_snapshot()
        rows = [(off, v, v, 2) for off, v in zip(STATE_OFFSETS, values)]
        if not values[2] & 1:
            rows[3] = (STATE_OFFSETS[3], 0, 0, 0)
        node.update(Mode="bounded-state", ProbeVersion="0.3.0", StateComplete=True, StatePassed=True,
                    StateRegisters=b"".join(struct.pack("<4I", *r) for r in rows))
        return node

    def test_valid_snapshot_is_not_boot_readiness(self):
        result = decode_snapshot([self.fixture()])
        state = result["gpu_state"]
        self.assertTrue(state["passed"])
        self.assertEqual(state["reported_vram_mib"], 6144)
        self.assertTrue(state["wpr2_hi_is_zero"])
        self.assertFalse(state["ready_to_boot"])
        self.assertFalse(state["firmware_executed"])
        self.assertFalse(result["compute_tested"])

    def test_protected_read_skipped(self):
        state = decode_snapshot([self.fixture(values=(0x176000a1, 0, 0, 0, 6144))])["gpu_state"]
        self.assertTrue(state["passed"])
        self.assertIsNone(state["reset_progress_complete"])

    def test_nonzero_wpr2_is_measured_without_reset(self):
        state = decode_snapshot([self.fixture(values=(0x176000a1, 0x600000, 1, 0x12, 6144))])["gpu_state"]
        self.assertTrue(state["passed"])
        self.assertFalse(state["wpr2_hi_is_zero"])
        self.assertFalse(state["reset_progress_complete"])

    def test_false_success_flags(self):
        for key, value in [("StateComplete", False), ("StatePassed", False), ("MMIOPassed", False),
                           ("MMIOCommandAfter", 2), ("MMIORestoreVerified", False)]:
            node = self.fixture()
            node[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                decode_snapshot([node])

    def test_corrupt_or_failed_registers_cannot_claim_success(self):
        for index in range(5):
            for column, value in [(0, 0x100), (1, 0xbadf1000), (2, 0xffffffff), (3, 0), (3, 3)]:
                node = self.fixture()
                rows = bytearray(node["StateRegisters"])
                struct.pack_into("<I", rows, index * 16 + column * 4, value)
                node["StateRegisters"] = bytes(rows)
                with self.subTest(index=index, column=column, value=value), self.assertRaises(ValueError):
                    decode_snapshot([node])

    def test_partial_failure_stays_available_for_diagnosis(self):
        node = self.fixture()
        rows = [(STATE_OFFSETS[0], 0xbadf0000, 0, 1)] + [(off, 0, 0, 0) for off in STATE_OFFSETS[1:]]
        node.update(StateRegisters=b"".join(struct.pack("<4I", *r) for r in rows),
                    StateComplete=False, StatePassed=False, MMIOPassed=False)
        state = decode_snapshot([node])["gpu_state"]
        self.assertFalse(state["passed"])
        self.assertIsNone(state["reported_vram_mib"])

    def test_wrong_vram_size(self):
        for size in (0, 65537):
            with self.assertRaises(ValueError):
                decode_snapshot([self.fixture(values=(0x176000a1, 0, 1, 0xff, size))])


if __name__ == "__main__":
    unittest.main()
