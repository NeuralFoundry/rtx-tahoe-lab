import struct
import unittest
from collect import pci_ranges


class PCIRangesTest(unittest.TestCase):
    def test_64_bit_address_and_size(self):
        result = pci_ranges(struct.pack("<5I", 0xc2010018, 8, 0x20000000, 1, 0))[0]
        self.assertEqual(result["register"], 0x18)
        self.assertEqual(result["address"], 0x820000000)
        self.assertEqual(result["length"], 1 << 32)
        self.assertTrue(result["prefetchable"])

    def test_observed_bar0(self):
        blob = bytes.fromhex("1000018200000000000000fb0000000000000001")
        result = pci_ranges(blob)[0]
        self.assertEqual((result["register"], result["address"], result["length"]),
                         (0x10, 0xfb000000, 16 << 20))

    def test_truncated_cells_rejected(self):
        for length in (1, 4, 19, 21):
            with self.assertRaises(ValueError):
                pci_ranges(bytes(length))

    def test_empty_ranges(self):
        self.assertEqual(pci_ranges(b""), [])


if __name__ == "__main__":
    unittest.main()
