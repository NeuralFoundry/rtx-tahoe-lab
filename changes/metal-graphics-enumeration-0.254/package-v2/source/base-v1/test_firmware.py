import struct
import tempfile
import unittest
from pathlib import Path
from prepare_firmware import inspect_elf, inspect_bin, obtain


def elf_fixture():
    names = b"\x00.shstrtab\x00.fwimage\x00.fwsignature_ga10x\x00"
    image, signature = b"test-image", b"test-signature"
    strings_off = 64 + 4 * 64
    image_off = strings_off + len(names)
    signature_off = image_off + len(image)
    header = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01" + bytes(9),
                         1, 243, 1, 0, 0, 64, 0, 64, 0, 0, 64, 4, 1)
    rows = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
            (1, 3, 0, 0, strings_off, len(names), 0, 0, 1, 0),
            (names.index(b".fwimage"), 1, 0, 0, image_off, len(image), 0, 0, 1, 0),
            (names.index(b".fwsignature_ga10x"), 1, 0, 0, signature_off, len(signature), 0, 0, 1, 0)]
    return header + b"".join(struct.pack("<IIQQQQIIQQ", *row) for row in rows) + names + image + signature


class FirmwareTest(unittest.TestCase):
    def test_required_sections_found_without_claiming_authentication(self):
        result = inspect_elf(elf_fixture())
        self.assertEqual(result["required_sections"][".fwimage"]["size"], 10)
        self.assertFalse(result["signature_cryptographically_verified"])

    def test_truncation_and_wrong_endianness(self):
        valid = elf_fixture()
        for broken in (valid[:63], valid[:-1], valid[:5] + b"\x02" + valid[6:]):
            with self.assertRaises((ValueError, struct.error)):
                inspect_elf(broken)

    def test_section_bounds_and_names(self):
        for offset, fmt, value in [(40, "Q", (1 << 64) - 1), (58, "H", 1), (62, "H", 9),
                                   (64 + 128, "I", 99999), (64 + 128 + 24, "Q", (1 << 64) - 1),
                                   (64 + 128 + 32, "Q", (1 << 64) - 1)]:
            broken = bytearray(elf_fixture())
            struct.pack_into("<" + fmt, broken, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                inspect_elf(broken)

    def test_missing_ampere_signature(self):
        broken = elf_fixture().replace(b".fwsignature_ga10x", b".fwsignature_ad10x")
        with self.assertRaises(ValueError):
            inspect_elf(broken)

    def test_nvidia_envelope(self):
        valid = struct.pack("<6I", 0x10de, 1, 32, 24, 28, 4) + bytes(8)
        self.assertEqual(inspect_bin(valid)["payload_size"], 4)
        nominal = bytearray(valid)
        struct.pack_into("<I", nominal, 8, 4096)
        self.assertEqual(inspect_bin(nominal)["actual_size"], 32)
        for offset, value in ((0, 0), (8, 16), (8, 0xffffffff), (12, 32), (16, 31), (20, 0xffffffff)):
            broken = bytearray(valid)
            struct.pack_into("<I", broken, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                inspect_bin(broken)

    def test_wrong_hash_rejected_without_download_or_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "gsp-570.144.bin"
            path.write_bytes(elf_fixture())
            before = path.read_bytes()
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                obtain(root, path.name, download=False)
            self.assertEqual(path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
