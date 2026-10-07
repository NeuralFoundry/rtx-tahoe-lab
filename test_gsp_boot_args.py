import struct
import unittest

from gsp_boot_args import DMA_LIMIT, LOG_ALLOCATION_SIZE, PAGE, encode_libos_args, plan_libos


def bindings():
    return list(range(0x200000, 0x200000 + LOG_ALLOCATION_SIZE, PAGE)), [0x500000], [0x501000]


class LibosArgsTest(unittest.TestCase):
    def test_unbound_plan_has_no_fake_address(self):
        plan = plan_libos()
        self.assertEqual(plan["binding_state"], "unbound")
        self.assertFalse(plan["live_dma_verified"])
        self.assertEqual(plan["descriptor_bytes"], 32)
        self.assertEqual(plan["descriptor_count"], 6)

    def test_published_offsets_and_big_endian_identifier(self):
        result = encode_libos_args(*bindings())
        data = result["data"]
        self.assertEqual(len(data), 4096)
        self.assertEqual(data[:8], b"TINIGOL\x00")
        self.assertEqual(struct.unpack_from("<QQ", data, 8), (0x200000, 0x10000))
        self.assertEqual(data[24:32], b"\x01\x01" + bytes(6))
        self.assertEqual(data[160:168], b"SGRAMR\x00\x00")
        self.assertEqual(struct.unpack_from("<QQ", data, 168), (0x500000, 4096))
        self.assertEqual(data[192:], bytes(4096 - 192))
        self.assertFalse(result["live_dma_verified"])

    def test_log_windows_may_be_disjoint_but_each_is_contiguous(self):
        logs, rmargs, args = bindings()
        for index in range(5):
            logs[index * 16:(index + 1) * 16] = list(range(0x1000000 + index * 0x20000,
                                                         0x1010000 + index * 0x20000, PAGE))
        result = encode_libos_args(logs, rmargs, args)
        self.assertEqual([r["address"] for r in result["regions"][:5]],
                         [0x1000000 + i * 0x20000 for i in range(5)])

    def test_inner_log_fragmentation_rejected(self):
        for index in (1, 15, 16, 31, 64, 79):
            logs, rmargs, args = bindings()
            logs[index] = 0x900000
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "Noncontiguous"):
                encode_libos_args(logs, rmargs, args)

    def test_allocation_aliases_rejected(self):
        for which in range(4):
            logs, rmargs, args = bindings()
            if which == 0:
                logs[-1] = logs[0]
            elif which == 1:
                rmargs[0] = logs[0]
            elif which == 2:
                args[0] = rmargs[0]
            else:
                args[0] = logs[-1]
            with self.subTest(which=which), self.assertRaises(ValueError):
                encode_libos_args(logs, rmargs, args)

    def test_page_bounds_types_alignment_and_counts(self):
        for bad in (True, False, 0, -PAGE, 0x200001, DMA_LIMIT, 2097152.0, "0x200000"):
            logs, rmargs, args = bindings()
            logs[0] = bad
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                encode_libos_args(logs, rmargs, args)
        for index in range(3):
            values = list(bindings())
            values[index] = values[index][:-1]
            with self.subTest(index=index), self.assertRaises(ValueError):
                encode_libos_args(*values)

    def test_highest_valid_page_and_input_immutability(self):
        logs, rmargs, args = bindings()
        args[0] = DMA_LIMIT - PAGE
        before = (logs[:], rmargs[:], args[:])
        self.assertEqual(encode_libos_args(logs, rmargs, args)["address"], DMA_LIMIT - PAGE)
        self.assertEqual((logs, rmargs, args), before)


if __name__ == "__main__":
    unittest.main()
