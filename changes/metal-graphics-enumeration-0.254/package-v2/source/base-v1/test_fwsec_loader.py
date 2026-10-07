import copy
import hashlib
import json
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import sys
import tempfile
import unittest

import test_falcon_decode
import test_fwsec
import test_host_read_decode
from prepare_fwsec_loader import BOARD_ROM_SHA256, prepare


DESC = 0x2000
IMAGE = DESC + 1196
DMEM = IMAGE + 57600
MAPPER = DMEM + 1376


def fixture():
    n = test_host_read_decode.fixture()
    n.update(test_falcon_decode.fixture())
    n.update(Mode="bounded-falcon-dma", ProbeVersion="0.6.0", PCIDState=0)
    for snapshot in ("Initial", "Final"):
        test_falcon_decode.set_register(n, snapshot, 2, 0x80420100)
    rom = test_fwsec.fixture()
    struct.pack_into("<9IHBBHH", rom, DESC, (1196 << 16) | 0x301,
                     59648, 1444, 28, 0, 57600, 0, 0, 2048, 0x400, 9, 3, 7, 0x9249)
    # Nonzero code/tail catches accidental zero-fill and modification of IMEM.
    rom[IMAGE:IMAGE + 59648] = bytes((i * 17 + 91) & 255 for i in range(59648))
    struct.pack_into("<4B4I", rom, DMEM + 28, 1, 4, 8, 2, 4, 1376, 5, 1200)
    struct.pack_into("<IHH14I", rom, MAPPER, 0x50414d44, 3, 64,
                     1984, 64, 0x01000000, 256, 0x900, 0x2600,
                     0, 0x4c8, 0x4d0, 0, 4, 0x44000, 0, 0x7ac)
    n["VBIOSShadow"] = bytes(rom)
    return n


def prepared(node):
    return prepare([node], hashlib.sha256(node["VBIOSShadow"]).hexdigest())


def changed(node, offset, fmt, value):
    rom = bytearray(node["VBIOSShadow"])
    struct.pack_into("<" + fmt, rom, offset, value)
    node["VBIOSShadow"] = bytes(rom)
    return node


class FWSECLoaderTest(unittest.TestCase):
    def test_only_three_dmem_ranges_change_and_original_is_preserved(self):
        node = fixture()
        before = copy.deepcopy(node)
        report, assets = prepared(node)
        self.assertEqual(node, before)
        image = assets["fwsec-image-prepared.bin"]
        original = node["VBIOSShadow"][IMAGE:IMAGE + 59648]
        self.assertEqual(assets["fwsec-image-unmodified.bin"], original)
        self.assertEqual(image[:57600], original[:57600])
        self.assertEqual(image[57600 + 1444:57600 + 1444 + 384], b"\3" * 384)
        self.assertEqual(struct.unpack_from("<I", image, 57600 + 1376 + 44)[0], 0x15)
        command = struct.pack("<IIQIIIIIII", 1, 24, 0, 0, 2, 1, 20, 0x17fe00, 0x100, 2)
        self.assertEqual(image[57600 + 1984:57600 + 1984 + 44], command)
        self.assertEqual(image[-20:], original[-20:])
        allowed = set()
        for start, size in ((57600 + 1444, 384), (57600 + 1376 + 44, 4), (57600 + 1984, 44)):
            allowed.update(range(start, start + size))
        self.assertTrue(all(i in allowed for i, (a, b) in enumerate(zip(image, original)) if a != b))
        self.assertEqual(report["allowed_patch_bytes"], 432)
        self.assertEqual(report["changed_bytes"], sum(a != b for a, b in zip(image, original)))
        self.assertEqual(report["signature_selected"], 2)
        self.assertEqual(report["descriptor_header"], {"vdesc": "0x4ac0301", "flags_version": "0x0301",
                                                      "reserved_opaque": "0x9249", "reserved_interpreted": False})
        self.assertTrue(report["falcon_dma_passed"])
        for key in ("firmware_executed", "ready_for_hardware_boot", "signature_verified_by_hardware",
                    "mapper_masks_interpreted_as_command_support"):
            self.assertFalse(report[key])
        self.assertFalse(report["frts_candidate"]["reserved"])
        self.assertFalse(report["frts_candidate"]["allocation_verified"])
        json.dumps(report)

    def test_every_fuse_selector_uses_its_own_descriptor_signature(self):
        for fuse, index in ((0, 0), (1, 1), (2, 2), (3, 2)):
            n = fixture()
            n.update(FuseFirst=fuse, FuseSecond=fuse, FuseSignatureIndex=index)
            report, assets = prepared(n)
            self.assertEqual(report["signature_selected"], index)
            self.assertEqual(assets["fwsec-selected-signature.bin"], bytes([index + 1]) * 384)
            self.assertEqual(assets["fwsec-image-prepared.bin"][59044:59428], bytes([index + 1]) * 384)

    def test_header_arrays_and_public_geometry_match_binary_assets(self):
        report, assets = prepared(fixture())
        header = assets["fwsec-payload.hpp"].decode("ascii")
        self.assertIn("namespace FWSECPayload", header)
        constants = {"ImageSize": 59648, "ImemSize": 57600, "DmemSize": 2048,
                     "ImemBase": 0, "ImemVirtualBase": 0, "DmemBase": 0, "PkcOffset": 1444,
                     "MapperOffset": 1376, "CommandOffset": 1984, "DescriptorOffset": DESC,
                     "DescriptorSize": 1196, "ImageRomOffset": IMAGE, "SignatureIndex": 2, "FuseValue": 3}
        for name, value in constants.items():
            self.assertIn(f"constexpr unsigned {name} = {value}U;", header)
        for name, asset in (("Image", "fwsec-image-prepared.bin"),
                            ("OriginalImage", "fwsec-image-unmodified.bin"), ("Descriptor", "fwsec-descriptor.bin")):
            match = re.search(r"alignas\(256\) constexpr unsigned char " + name + r"\[\w+\] = \{(.*?)\};", header, re.S)
            self.assertIsNotNone(match)
            self.assertEqual(bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", match.group(1))), assets[asset])
        for name, blob in assets.items():
            self.assertEqual(report["assets"][name], {"bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest()})

    def test_default_pin_rejects_synthetic_or_changed_rom(self):
        n = fixture()
        with self.assertRaises(ValueError): prepare([n])
        correct_hash = hashlib.sha256(n["VBIOSShadow"]).hexdigest()
        changed(n, IMAGE + 100, "B", 0)
        with self.assertRaises(ValueError): prepare([n], correct_hash)
        for value in (None, "a" * 63, "X" * 64, "g" * 64):
            with self.subTest(value=value), self.assertRaises(ValueError): prepare([fixture()], value)

    def test_requires_successful_06_measurement_and_cleanup(self):
        mutations = (("ProbeVersion", "0.5.0"), ("ProbeComplete", False), ("FalconPassed", False),
                     ("FalconResourcesRetained", True), ("FalconCleanupVerified", False),
                     ("FalconCommandAfter", 4), ("FusePassed", False), ("ROMPassed", False),
                     ("ROMStable", False), ("FirmwareExecuted", True), ("PCIDState", 3))
        for key, value in mutations:
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): prepared(n)
        for nodes in (None, {}, [1], [], [fixture(), fixture()]):
            with self.subTest(nodes_type=type(nodes)), self.assertRaises(ValueError): prepare(nodes)

    def test_descriptor_identity_and_reserved_bits_fail_closed(self):
        mutations = ((36, "H", 1), (38, "B", 8), (39, "B", 2), (40, "H", 3),
                     (42, "H", 0), (42, "H", 1), (42, "H", 0x9248),
                     (0, "I", (1196 << 16) | 0x303))
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                prepared(changed(fixture(), DESC + offset, fmt, value))
        with self.assertRaises(ValueError): prepared(changed(fixture(), 0x1807, "B", 6))

    def test_geometry_alignment_trailing_padding_and_measured_capacity(self):
        for offset, value in ((4, 59904), (16, 1), (20, 57599), (24, 256), (28, 1), (32, 2047)):
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                prepared(changed(fixture(), DESC + offset, "I", value))
        for value in (0x202, 0x80400100):
            n = fixture(); test_falcon_decode.set_register(n, "Final", 2, value)
            with self.subTest(hwcfg=value), self.assertRaises(ValueError): prepared(n)

    def test_mapper_headers_masks_and_existing_init_command_are_pinned(self):
        mutations = ((0, "I", 0), (4, "H", 4), (6, "H", 60), (44, "I", 0x15),
                     (48, "I", 0), (52, "I", 0x44000 | (1 << 21)), (56, "I", 1))
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                prepared(changed(fixture(), MAPPER + offset, fmt, value))

    def test_overlapping_or_unaligned_patch_and_interface_spans_are_refused(self):
        mutations = ((DESC + 8, 1376), (DESC + 8, 1445), (DESC + 8, 1800),
                     (MAPPER + 8, 1444), (MAPPER + 8, 1376), (MAPPER + 8, 28),
                     (MAPPER + 8, 1985), (MAPPER + 8, 0xfffffffc), (MAPPER + 12, 43),
                     (DESC + 12, 2040))
        for offset, value in mutations:
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                prepared(changed(fixture(), offset, "I", value))

    def test_interface_layout_and_duplicate_mapper_rejected(self):
        for offset, fmt, value in ((DMEM + 29, "B", 8), (DMEM + 30, "B", 12),
                                   (DMEM + 31, "B", 1), (DMEM + 40, "I", 4)):
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                prepared(changed(fixture(), offset, fmt, value))

    def test_cli_invalid_snapshot_creates_no_output_and_exposes_no_pin_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            snapshot, output = Path(tmp) / "input.plist", Path(tmp) / "package"
            snapshot.write_bytes(plistlib.dumps([fixture()]))
            run = subprocess.run([sys.executable, str(Path(__file__).with_name("prepare_fwsec_loader.py")),
                                  str(snapshot), "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 2)
            self.assertIn("pinned board ROM", run.stderr)
            self.assertFalse(output.exists())

    def test_live_board_package_when_capture_is_available(self):
        snapshot = Path(__file__).parent / "results/probe-20260906T092946Z/snapshot.plist"
        if not snapshot.exists(): self.skipTest("Real board capture is not in this checkout")
        report, assets = prepare(plistlib.loads(snapshot.read_bytes()))
        self.assertEqual(report["rom_sha256"], BOARD_ROM_SHA256)
        self.assertTrue(report["board_hash_matches_production_pin"])
        self.assertEqual(report["descriptor_header"]["reserved_opaque"], "0x9249")
        self.assertEqual(report["original_image_sha256"], "6baa5fd46584aa2d573a5f0b67f4cacf8aefad19d95cc17e24e24d57b371a29a")
        self.assertEqual(report["signature_sha256"], "a5dbb6989fb4b0fa0309fe04d73b2f7e823e5ba00b2559765b1d69c189358918")
        # Independently reproduced from the captured ROM using PowerShell byte copies.
        self.assertEqual(report["prepared_image_sha256"], "8c0785316a8506f192692d036c3b098e31cc4d3e4673f9760b6f3862d8757ca8")
        self.assertEqual(report["changed_bytes"], 393)
        self.assertEqual(len(assets["fwsec-image-prepared.bin"]), 59648)


if __name__ == "__main__":
    unittest.main()
