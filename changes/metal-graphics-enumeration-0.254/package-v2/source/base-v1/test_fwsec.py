import struct
import unittest
from extract_fwsec import extract


def fixture():
    b = bytearray(1 << 20)
    def put(off, fmt, *values): struct.pack_into("<" + fmt, b, off, *values)
    for off, sig, pcisig, device, size, code in [(0, 0xaa55, b"PCIR", 0x2520, 0x1000, 0),
                                              (0x1000, 0x4e56, b"NPDS", 0x2200, 0x800, 0xe0)]:
        put(off, "H", sig); put(off + 0x18, "H", 0x40)
        b[off + 0x40:off + 0x44] = pcisig
        put(off + 0x44, "HH", 0x10de, device)
        put(off + 0x50, "H", size // 512)
        put(off + 0x54, "BB", code, 0x80 if off == 0 else 0)
    put(0x100, "HIH4B", 0xb8ff, 0x00544942, 0x100, 12, 6, 1, 0)
    b[0x10b] = (-sum(b[0x100:0x10c])) & 255
    put(0x10c, "BBHH", 0x70, 2, 4, 0x200)
    put(0x200, "I", 0x1800)
    put(0x1800, "6B", 1, 6, 6, 1, 1, 48)
    put(0x1806, "BBI", 0x85, 7, 0x2000)
    put(0x2000, "9IHBBHH", (1196 << 16) | 0x301, 0x1000, 0x200, 0x10,
        0, 0x800, 0, 0, 0x800, 0x400, 9, 3, 7, 0)
    for i in range(3): b[0x202c + i * 384:0x202c + (i + 1) * 384] = bytes([i + 1]) * 384
    dmem = 0x2000 + 1196 + 0x800
    put(dmem + 0x10, "4B", 1, 4, 8, 1)
    put(dmem + 0x14, "II", 4, 0x100)
    put(dmem + 0x100, "IHH14I", 0x50414d44, 3, 64, 0x700, 64, *([0] * 12))
    return b


class FWSECTest(unittest.TestCase):
    def test_extract_keeps_image_unmodified_and_signatures_unselected(self):
        data = bytes(fixture())
        report, assets = extract(data, 6144)
        self.assertTrue(report["extraction_complete"])
        self.assertEqual(report["fwsec"]["signature_count"], 3)
        self.assertEqual(assets["fwsec-signature-1.bin"], bytes([2]) * 384)
        self.assertEqual(assets["fwsec-image-unmodified.bin"], data[0x24ac:0x34ac])
        self.assertEqual(len(assets["frts-command-candidate.bin"]), 44)
        self.assertIsNone(report["signature_selected"])
        self.assertFalse(report["ready_for_hardware_boot"])

    def test_short_rom_and_wrong_identity(self):
        with self.assertRaises(ValueError): extract(fixture()[:-1], 6144)
        b = fixture(); struct.pack_into("<H", b, 0x46, 0x1740)
        with self.assertRaises(ValueError): extract(b, 6144)

    def test_zero_image_length_and_bad_pcir(self):
        for offset, fmt, value in [(0x50, "H", 0), (0x18, "H", 0), (0x40, "I", 0)]:
            b = fixture(); struct.pack_into("<" + fmt, b, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError): extract(b, 6144)

    def test_bit_checksum_and_duplicate_header(self):
        b = fixture(); b[0x10b] ^= 1
        with self.assertRaises(ValueError): extract(b, 6144)
        b = fixture(); b[0x300:0x306] = b"\xff\xb8BIT\x00"
        with self.assertRaises(ValueError): extract(b, 6144)

    def test_out_of_range_table_and_descriptor(self):
        for offset in (0x200, 0x1808):
            b = fixture(); struct.pack_into("<I", b, offset, 0xfffffff0)
            with self.subTest(offset=offset), self.assertRaises(ValueError): extract(b, 6144)

    def test_invalid_fwsec_descriptor(self):
        for offset, fmt, value in [(0, "I", (1196 << 16) | 0x201), (4, "I", 1), (8, "I", 0x780),
                                   (20, "I", 0xfffffff0), (39, "B", 0)]:
            b = fixture(); struct.pack_into("<" + fmt, b, 0x2000 + offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError): extract(b, 6144)

    def test_application_and_command_bounds(self):
        dmem = 0x2cac
        for offset, fmt, value in [(0x10, "B", 2), (0x13, "B", 0), (0x18, "I", 0x7f0),
                                   (0x104, "H", 2), (0x108, "I", 0x7f0), (0x10c, "I", 8)]:
            b = fixture(); struct.pack_into("<" + fmt, b, dmem + offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError): extract(b, 6144)

    def test_unreasonable_vram_refused(self):
        for size in (0, 65537):
            with self.assertRaises(ValueError): extract(fixture(), size)


if __name__ == "__main__": unittest.main()
