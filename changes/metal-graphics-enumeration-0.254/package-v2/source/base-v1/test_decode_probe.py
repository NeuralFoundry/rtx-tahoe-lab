import struct
import unittest
from decode_probe import decode_header, decode_snapshot, decode_rebar


class HeaderTest(unittest.TestCase):
    def fixture(self, bar1_low=0x0000000c):
        words = [0] * 16
        words[0], words[11] = 0x252010de, 0x104c1043
        words[1] = 6
        words[4:10] = [0xfb000000, bar1_low, 8, 0x2200000c, 8, 0x1001]
        return words

    def test_upper_half_is_not_an_independent_bar(self):
        result = decode_header(struct.pack("<16I", *self.fixture()))
        self.assertEqual([b["index"] for b in result["bars"]], [0, 1, 3, 5])
        self.assertEqual(result["bars"][1]["address"], "0x800000000")
        self.assertEqual(result["bars"][2]["address"], "0x822000000")
        self.assertTrue(result["memory_decode_enabled"])

    def test_identity_mismatch_rejected(self):
        words = self.fixture()
        words[0] = 0xffffffff
        with self.assertRaises(ValueError):
            decode_header(struct.pack("<16I", *words))

    def test_trailing_wide_bar_rejected(self):
        words = self.fixture()
        words[9] = 4
        with self.assertRaises(ValueError):
            decode_header(struct.pack("<16I", *words))

    def test_missing_probe_is_not_success(self):
        with self.assertRaises(ValueError):
            decode_snapshot([])

    def test_rebar_current_size_and_supported_sizes(self):
        rows = decode_rebar(struct.pack("<IIII", 0x100, 0x440, 0x21000, 0x0d01))
        self.assertEqual(rows[0]["current_length"], 16 << 20)
        self.assertEqual(rows[1]["current_length"], 8 << 30)
        self.assertEqual(rows[1]["supported_lengths"], [256 << 20, 8 << 30])
        self.assertTrue(rows[1]["current_size_supported"])

    def test_rebar_duplicate_index_rejected(self):
        with self.assertRaises(ValueError):
            decode_rebar(struct.pack("<IIII", 0x100, 0x440, 0x100, 0x400))

    def mmio_snapshot(self):
        words = self.fixture()
        words[1] = 0
        return {"ProbeVersion": "0.2.0", "ProbeComplete": True, "ReadOnly": False,
                "Mode": "bounded-boot0", "MMIOReadOnly": True, "PCIHeader": struct.pack("<16I", *words),
                "MMIOReadCount": 2, "MMIOBoot0First": 0x176000a1, "MMIOBoot0Second": 0x176000a1,
                "MMIOCommandBefore": 0, "MMIOCommandDuring": 2, "MMIOCommandAfter": 0,
                "MMIORestoreVerified": True, "MMIOPassed": True}

    def test_boot0_success_is_not_compute_success(self):
        result = decode_snapshot([self.mmio_snapshot()])
        self.assertTrue(result['mmio']['passed'])
        self.assertTrue(result['mmio_tested'])
        self.assertFalse(result['compute_tested'])

    def test_mmio_forged_success_rejected(self):
        for key, value in [('MMIOCommandAfter', 2), ('MMIORestoreVerified', False),
                           ('MMIOBoot0Second', 0xffffffff), ('MMIOReadOnly', False)]:
            node = self.mmio_snapshot()
            node[key] = value
            with self.assertRaises(ValueError):
                decode_snapshot([node])

    def test_failed_mmio_remains_failure(self):
        node = self.mmio_snapshot()
        node.update(MMIOPassed=False, MMIOReadCount=0, MMIOStatus='device-busy')
        result = decode_snapshot([node])
        self.assertFalse(result['mmio']['passed'])
        self.assertFalse(result['mmio_tested'])

    def test_unknown_write_mode_rejected(self):
        node = self.mmio_snapshot()
        node['Mode'] = 'unknown'
        with self.assertRaises(ValueError):
            decode_snapshot([node])


if __name__ == "__main__":
    unittest.main()
