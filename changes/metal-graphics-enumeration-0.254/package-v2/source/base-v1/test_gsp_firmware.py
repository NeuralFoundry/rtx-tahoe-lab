from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import gsp_firmware as fw


def put(data, offset, *values):
    struct.pack_into("<" + str(len(values)) + "I", data, offset, *values)


def booter_fixture():
    data = bytearray(0x378 + 0xec00)
    put(data, 0, 0x10de, 1, 0xf000, 24, 0x378, 0xec00)
    put(data, 24, 0x3c, 0x300, 0x33c, 0x340, 0x344, 12, 0x350, 0x354, 36)
    data[0x3c:0x1bc] = bytes([0x5a]) * 384
    data[0x1bc:0x33c] = bytes([0xa5]) * 384
    put(data, 0x33c, 0x8a10, 0, 1, 1, 3, 2)
    put(data, 0x354, 0, 0x100, 0x8a00, 0x6200, 1, 0x100, 0x8900, 0x100, 0)
    return bytes(data)


def bootloader_fixture():
    data = bytearray(0x6c + 0x6000)
    put(data, 0, 0x10de, 1, 0x6100, 24, 0x6c, 0x6000)
    put(data, 24, 5, 0x5000, 0x880, 0x5880, 0x10, 0, 0, 0,
        0, 0x800, 0x800, 0x1000, 0x1800, 0x2900, 1, 0, 0, 0, 0, 0x6000, 0)
    return bytes(data)


def elf_fixture():
    names = b"\0.fwimage\0.fwsignature_ga10x\0.shstrtab\0"
    names_offset = 64 + 512 + 4096
    table = (names_offset + len(names) + 7) & ~7
    data = bytearray(table + 4 * 64)
    ident = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<16sHHIQQQIHHHHHH", data, 0, ident, 1, 243, 1, 0, 0, table,
                     0, 64, 0, 0, 64, 4, 3)
    data[names_offset:names_offset + len(names)] = names
    for index, (name, kind, offset, length) in enumerate((
            (0, 0, 0, 0), (1, 1, 64, 512), (10, 1, 576, 4096),
            (29, 3, names_offset, len(names)))):
        struct.pack_into("<IIQQQQIIQQ", data, table + index * 64, name, kind, 0, 0, offset, length, 0, 0, 1, 0)
    return bytes(data)


class GSPFirmwareTests(unittest.TestCase):
    def test_booter_full_descriptor_and_signature_indirection(self):
        r = fw.parse_booter(booter_fixture())
        self.assertTrue(r["structurally_valid"])
        self.assertFalse(r["sha256_verified"])
        self.assertEqual(r["segments"]["nonsecure_imem"]["offset"], 0)
        self.assertEqual(r["segments"]["secure_imem"]["offset"], 0x100)
        self.assertEqual(r["segments"]["secure_imem"]["size"], 0x8900)
        self.assertEqual(r["segments"]["dmem"]["offset"], 0x8a00)
        self.assertEqual(r["segments"]["dmem"]["size"], 0x6200)
        self.assertEqual(r["patch"]["destination_image_offset"], 0x8a10)
        self.assertEqual(r["patch"]["destination_dmem_offset"], 0x10)
        self.assertEqual([s["offset"] for s in r["signatures"]], [0x3c, 0x1bc])
        self.assertEqual([s["size"] for s in r["signatures"]], [384, 384])
        self.assertNotEqual(r["signatures"][0]["sha256"], r["signatures"][1]["sha256"])

    def test_booter_does_not_silently_choose_or_patch_first_signature(self):
        data = booter_fixture(); before = fw.sha256(data)
        r = fw.parse_booter(data)
        self.assertIsNone(r["selected_signature"])
        self.assertFalse(r["patch"]["applied"])
        self.assertFalse(r["ready_for_hardware_boot"])
        self.assertEqual(r["signature_selection"]["requires_register"], "0x824148")
        self.assertEqual(r["signature_selection"]["raw_zero_candidate_index"], 1)
        self.assertEqual(r["signature_selection"]["raw_one_candidate_index"], 0)
        self.assertEqual(before, fw.sha256(data))

    def test_nominal_envelope_size_never_extends_readable_file(self):
        for callback, fixture in ((fw.parse_booter, booter_fixture), (fw.parse_bootloader, bootloader_fixture)):
            data = fixture()
            self.assertGreater(callback(data)["envelope"]["declared_size"], len(data))
            for length in (0, 23, 59, len(data) - 1):
                with self.subTest(length=length), self.assertRaises(ValueError): callback(data[:length])
            altered = bytearray(data); put(altered, 20, len(data))
            with self.assertRaises(ValueError): callback(bytes(altered))

    def test_unknown_versions_and_envelope_geometry_fail(self):
        for callback, fixture in ((fw.parse_booter, booter_fixture), (fw.parse_bootloader, bootloader_fixture)):
            for offset, value in ((0, 0x10df), (4, 2), (8, 0xffffffff), (12, 20), (16, 24), (20, 0)):
                altered = bytearray(fixture()); put(altered, offset, value)
                with self.subTest(callback=callback.__name__, offset=offset), self.assertRaises(ValueError): callback(bytes(altered))
        for version in (0, 1, 4, 6, 0xffffffff):
            data = bytearray(bootloader_fixture()); put(data, 24, version)
            with self.subTest(version=version), self.assertRaises(ValueError): fw.parse_bootloader(bytes(data))

    def test_booster_metadata_pointer_ranges_and_overlaps_fail(self):
        for offset, value in ((24, 0x18), (32, 0x3c), (36, 0x33c), (40, 0x378),
                              (48, 0x10000000), (52, 0x360), (56, 28), (44, 8)):
            data = bytearray(booter_fixture()); put(data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError): fw.parse_booter(bytes(data))

    def test_booter_signature_count_metadata_and_patch_bounds_fail(self):
        for offset, value in ((0x350, 0), (0x350, 3), (28, 769), (0x340, 1),
                              (0x344, 2), (0x348, 0x400), (0x34c, 9),
                              (0x33c, 0x100), (0x33c, 0xeb00), (0x33c, 0x8a11)):
            data = bytearray(booter_fixture()); put(data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError): fw.parse_booter(bytes(data))

    def test_booter_segments_truncated_unaligned_or_overlapping_fail(self):
        for offset, value in ((0x354, 0x100), (0x358, 0), (0x35c, 0x8900),
                              (0x360, 0x6300), (0x364, 2), (0x368, 0x101),
                              (0x36c, 0x8a00), (0x370, 0xffffffff), (0x374, 0x100)):
            data = bytearray(booter_fixture()); put(data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError): fw.parse_booter(bytes(data))

    def test_bootloader_offsets_are_payload_relative(self):
        r = fw.parse_bootloader(bootloader_fixture())
        self.assertEqual(r["payload_size"], 0x6000)
        self.assertEqual(r["descriptor"]["monitorCodeOffset"], 0x1800)
        self.assertEqual(r["regions"]["monitorCode"]["file_offset"], 0x186c)
        self.assertEqual(r["regions"]["manifest"]["size"], 0x800)
        self.assertEqual(r["regions"]["monitorData"]["offset"], 0x800)
        self.assertFalse(r["manifest_contents_interpreted"])

    def test_bootloader_bounds_overlaps_and_unsupported_modes_fail(self):
        updates = (("monitorCodeOffset", 0x1000), ("monitorDataSize", 0x5900),
                   ("manifestSize", 0), ("bootloaderParamOffset", 0x5800),
                   ("riscvElfOffset", 4), ("fbReservedSize", 0x5000),
                   ("fbReservedSize", 0xffffffff), ("bIsMonitorEnabled", 0), ("bSignedAsCode", 1))
        for field, value in updates:
            data = bytearray(bootloader_fixture()); put(data, 24 + 4 * fw.RISCV_FIELDS.index(field), value)
            with self.subTest(field=field), self.assertRaises(ValueError): fw.parse_bootloader(bytes(data))

    def test_gsp_elf_image_and_family_signature_are_bounded(self):
        r = fw.parse_gsp_elf(elf_fixture())
        self.assertEqual(r["required_sections"][".fwimage"]["offset"], 64)
        self.assertEqual(r["required_sections"][".fwsignature_ga10x"]["size"], 4096)
        self.assertFalse(r["signature_cryptographically_verified"])
        self.assertFalse(r["sha256_verified"])

    def test_gsp_elf_header_version_section_size_and_truncation_fail(self):
        for offset, value in ((4, 1), (5, 2), (6, 2)):
            data = bytearray(elf_fixture()); data[offset] = value
            with self.assertRaises(ValueError): fw.parse_gsp_elf(bytes(data))
        for offset, value in ((18, 62), (58, 32), (60, 0), (62, 9)):
            data = bytearray(elf_fixture()); struct.pack_into("<H", data, offset, value)
            with self.assertRaises(ValueError): fw.parse_gsp_elf(bytes(data))
        with self.assertRaises(ValueError): fw.parse_gsp_elf(elf_fixture()[:-1])

    def test_gsp_elf_section_aliasing_names_and_signature_geometry_fail(self):
        data = elf_fixture(); table, = struct.unpack_from("<Q", data, 40)
        for offset, fmt, value in ((table + 128, "I", 1), (table + 128, "I", 999),
                                   (table + 128 + 4, "I", 8), (table + 128 + 24, "Q", 64),
                                   (table + 128 + 32, "Q", 4095), (table + 64 + 24, "Q", 0)):
            altered = bytearray(data); struct.pack_into("<" + fmt, altered, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError): fw.parse_gsp_elf(bytes(altered))

    def test_pinned_hash_is_checked_before_any_descriptor_is_interpreted(self):
        with tempfile.TemporaryDirectory() as folder:
            Path(folder, "gsp-570.144.bin").write_bytes(elf_fixture())
            with patch.object(fw, "parse_gsp_elf") as elf, patch.object(fw, "parse_booter") as booter:
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    fw.inspect_gsp_assets(Path(folder))
                elf.assert_not_called(); booter.assert_not_called()

    def test_missing_pinned_asset_is_an_error(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(ValueError, "Missing"):
                fw.inspect_gsp_assets(Path(folder))

    def test_actual_pinned_assets_validate_without_skipping(self):
        directory = Path(__file__).parent / "firmware" / "570.144"
        r = fw.inspect_gsp_assets(directory)
        self.assertTrue(r["all_pinned_assets_verified"])
        self.assertEqual(r["gsp"]["required_sections"][".fwimage"]["size"], 63541248)
        self.assertEqual(r["gsp"]["required_sections"][".fwsignature_ga10x"]["offset"], 63565932)
        self.assertEqual(r["booter_load"]["payload_size"], 0xec00)
        self.assertEqual(r["bootloader_info"]["payload_size"], 0x6000)
        self.assertEqual(r["bootloader_info"]["monitorCodeOffset"], 0x1800)
        self.assertEqual(r["bootloader_info"]["monitorCodeSize"], 0x2900)
        self.assertEqual(r["bootloader_info"]["monitorDataOffset"], 0x800)
        self.assertEqual(r["bootloader_info"]["monitorDataSize"], 0x1000)
        self.assertEqual(r["bootloader_info"]["manifestOffset"], 0)
        self.assertEqual(r["bootloader_info"]["manifestSize"], 0x800)
        self.assertIsNone(r["booter_load"]["selected_signature"])
        self.assertFalse(r["payloads_modified"])
        self.assertFalse(r["hardware_accessed"])
        self.assertFalse(r["firmware_executed"])


if __name__ == "__main__":
    unittest.main()
