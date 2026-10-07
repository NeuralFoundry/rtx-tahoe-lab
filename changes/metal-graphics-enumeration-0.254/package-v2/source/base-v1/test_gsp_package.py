"""Offline package integration; every address in these tests is synthetic only."""
import copy
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from unittest.mock import patch

import prepare_gsp as package
from test_gsp_booter_decode import fixture as fuse_fixture


def digest(data):
    return hashlib.sha256(data).hexdigest()


class GSPPackageIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Assets are mandatory: an absent/corrupted local pin must fail, not skip.
        cls.directory = Path(__file__).parent / "firmware" / "570.144"
        cls.report, cls.files = package.prepare(cls.directory)
        resources = cls.report["host_resources"]
        # Deliberately explicit fake pages, confined to this offline test fixture.
        cls.synthetic_pages = {}
        names = ("radix3", "bootloader", "signature", "metadata", "queues", "rmargs",
                 "libos_args", "logs", "booter_load")
        for index, name in enumerate(names):
            base = 0x100000000 + index * 0x10000000
            stride = 0x3000 if name in ("radix3", "queues") else 4096
            cls.synthetic_pages[name] = [base + i * stride for i in range(resources[name]["pages"])]
        # Exercise the complete production binder, including re-verifying pins.
        cls.bound_report, cls.buffers = package.bind(cls.directory, cls.synthetic_pages)

    def test_unbound_package_contains_no_generated_dma_addresses_or_ready_claims(self):
        r = self.report
        self.assertTrue(r["assets_verified"])
        for key in ("bindings_present", "native_dma_prepared", "hardware_accessed", "gsp_firmware_executed",
                    "gsp_init_done_observed", "compute_tested", "metal_supported", "ready_for_hardware_boot"):
            self.assertIs(r[key], False)
        self.assertIsNone(r["booter_signature_selection"])
        self.assertNotIn("booter-payload-selected.bin", self.files)
        for name, resource in r["host_resources"].items():
            with self.subTest(resource=name):
                self.assertFalse(resource["allocated"])
                self.assertIsNone(resource["gpu_visible_pages"])
                self.assertEqual(resource["pages"], (resource["bytes"] + 4095) // 4096)
        self.assertEqual(r["queues"]["binding_state"], "unbound")
        self.assertEqual(r["libos"]["binding_state"], "unbound")
        self.assertFalse(r["layout"]["region_reserved"])
        self.assertEqual(self.files["queues-unbound.bin"][:4096], bytes(4096))
        self.assertEqual(r["host_bytes_required"], sum(v["pages"] * 4096 for v in r["host_resources"].values()))

    def test_all_bodies_match_manifest_hashes_and_pinned_source_ranges(self):
        rows = self.report["files"]
        self.assertEqual({row["path"] for row in rows}, set(self.files))
        for row in rows:
            with self.subTest(path=row["path"]):
                self.assertEqual(len(self.files[row["path"]]), row["bytes"])
                self.assertEqual(digest(self.files[row["path"]]), row["sha256"])
        assets = self.report["assets"]
        expected = {
            "gsp-fwimage.bin": assets["gsp"]["required_sections"][".fwimage"]["sha256"],
            "gsp-signature-ga10x.bin": assets["gsp"]["required_sections"][".fwsignature_ga10x"]["sha256"],
            "bootloader-payload.bin": assets["bootloader"]["envelope"]["payload_sha256"],
            "booter-payload-unpatched.bin": assets["booter_load"]["envelope"]["payload_sha256"],
        }
        for name, expected_hash in expected.items():
            self.assertEqual(digest(self.files[name]), expected_hash)
        self.assertEqual(json.loads(self.files["binding-requirements.json"]), self.report["host_resources"])
        self.assertEqual(json.loads(self.files["vram-layout.json"]), self.report["layout"])
        self.assertFalse(json.loads(self.files["firmware-inspection.json"])["payloads_modified"])

    def test_reference_vram_reservation_keeps_all_regions_disjoint(self):
        layout = self.report["layout"]
        self.assertEqual(layout["required_vram_bytes"], 193 << 20)
        self.assertEqual(layout["additional_vram_bytes_beyond_frts"], 192 << 20)
        ranges = layout["ranges"]
        for before, after in zip(ranges, ranges[1:]):
            self.assertLessEqual(before["end"], after["offset"])
        named = {r["name"]: r for r in ranges}
        self.assertEqual(named["frts"]["offset"], 0x17fe00000)
        self.assertEqual(named["frts"]["end"], named["bios_exclusion"]["offset"])
        self.assertEqual(named["bios_exclusion"]["end"], 6 << 30)

    def test_synthetic_bindings_are_explicit_and_globally_disjoint(self):
        all_pages = [p for pages in self.synthetic_pages.values() for p in pages]
        self.assertEqual(len(all_pages), len(set(all_pages)))
        self.assertTrue(all(p > 0xffffffff and p % 4096 == 0 and p < 1 << 40 for p in all_pages))
        bound = self.bound_report
        self.assertTrue(bound["bindings_present"])
        self.assertEqual(bound["bindings"], self.synthetic_pages)
        self.assertEqual(bound["status"], "offline-address-bindings-validated-not-dma-prepared")
        for key in ("native_dma_prepared", "hardware_accessed", "gsp_firmware_executed",
                    "gsp_init_done_observed", "compute_tested", "metal_supported", "ready_for_hardware_boot"):
            self.assertIs(bound[key], False)
        self.assertFalse(bound["queues"]["dma_prepared"])
        self.assertFalse(bound["queues"]["execution_ready"])
        self.assertEqual(bound["libos"]["binding_state"], "explicit-addresses-validated-offline")
        self.assertEqual(set(self.buffers), set(self.synthetic_pages))
        for name, data in self.buffers.items():
            with self.subTest(buffer=name):
                self.assertEqual(len(data), len(self.synthetic_pages[name]) * 4096)
                self.assertEqual(digest(data), bound["bound_buffer_hashes"][name])
        self.assertEqual(self.buffers["bootloader"], self.files["bootloader-payload.bin"])
        self.assertEqual(self.buffers["signature"], self.files["gsp-signature-ga10x.bin"])
        booter = self.files["booter-payload-unpatched.bin"]
        self.assertEqual(self.buffers["booter_load"][:len(booter)], booter)
        self.assertEqual(self.buffers["booter_load"][len(booter):], bytes(0xf000 - len(booter)))
        self.assertEqual(self.buffers["logs"], bytes(2 << 20))
        # Binding encoders must not mutate the unbound package or add addresses.
        self.assertIsNone(self.report["host_resources"]["radix3"]["gpu_visible_pages"])
        self.assertEqual(self.files["queues-unbound.bin"][:4096], bytes(4096))

    def test_full_real_image_radix_links_to_every_supplied_page(self):
        radix = self.bound_report["radix_geometry"]
        self.assertEqual(radix["level_page_counts"], [1, 1, 31, 15513])
        pages = self.synthetic_pages["radix3"]
        for level in range(3):
            offset = radix["level_offsets"][level]
            first_page = radix["level_offsets"][level + 1] // 4096
            count = radix["level_page_counts"][level + 1]
            entries = struct.unpack_from("<" + str(count) + "Q", self.buffers["radix3"], offset)
            self.assertEqual(list(entries), pages[first_page:first_page + count])
        size = len(self.files["gsp-fwimage.bin"])
        image = self.buffers["radix3"][radix["table_size"]:]
        self.assertEqual(digest(image[:size]), digest(self.files["gsp-fwimage.bin"]))
        self.assertEqual(image[size:], bytes(len(image) - size))

    def test_wpr_metadata_wire_offsets_match_parser_layout_and_bindings(self):
        data = self.buffers["metadata"]
        self.assertEqual(len(data), 4096)
        checks = {
            0: 0xdc3aae21371a60b3, 8: 1, 16: self.synthetic_pages["radix3"][0],
            24: len(self.files["gsp-fwimage.bin"]), 32: self.synthetic_pages["bootloader"][0],
            40: 0x6000, 48: 0x1800, 56: 0x800, 64: 0,
            72: self.synthetic_pages["signature"][0], 80: 4096,
            152: 0x17fe00000, 160: 0x100000, 168: 0x17ff00000, 176: 0x180000000, 200: 0,
        }
        for offset, value in checks.items():
            with self.subTest(offset=offset): self.assertEqual(struct.unpack_from("<Q", data, offset)[0], value)
        self.assertEqual(data[208:], bytes(4096 - 208))

    def test_queue_table_gsp_arguments_and_libos_rmargs_form_consistent_links(self):
        pages = self.synthetic_pages["queues"]
        shared, args = self.buffers["queues"], self.buffers["rmargs"]
        self.assertEqual(len(pages), 129)
        self.assertEqual(list(struct.unpack_from("<129Q", shared)), pages)
        self.assertEqual(struct.unpack_from("<QI4xQQ", args), (pages[0], 129, 4096, 0x41000))
        self.assertEqual(args[48], 1)
        self.assertEqual(struct.unpack_from("<8I", shared, 4096), (0, 0x40000, 4096, 63, 0, 1, 32, 4096))
        self.assertEqual(shared[0x41000:], bytes(0x40000))
        self.assertEqual(args[72:], bytes(4096 - 72))
        libos = self.buffers["libos_args"]
        self.assertEqual(libos[160:168], b"SGRAMR\0\0")
        self.assertEqual(struct.unpack_from("<QQ", libos, 168), (self.synthetic_pages["rmargs"][0], 4096))
        self.assertEqual(struct.unpack_from("<Q", args)[0], pages[0])
        self.assertEqual(self.bound_report["libos"]["address"], self.synthetic_pages["libos_args"][0])
        for index in range(5):
            self.assertEqual(struct.unpack_from("<QQ", libos, index * 32 + 8),
                             (self.synthetic_pages["logs"][index * 16], 65536))

    def test_global_binder_rejects_cross_resource_and_within_resource_aliases(self):
        # Successful integration above uses real prepare. Invalid address tests
        # reuse its verified bytes to avoid hashing 64 MiB for every bad page.
        with patch.object(package, "prepare", side_effect=lambda *a: (copy.deepcopy(self.report), self.files)):
            for destination, source in (("queues", "radix3"), ("logs", "booter_load"),
                                        ("metadata", "rmargs"), ("booter_load", "signature"),
                                        ("queues", "queues")):
                bindings = {name: list(pages) for name, pages in self.synthetic_pages.items()}
                bindings[destination][-1] = bindings[source][0]
                with self.subTest(destination=destination, source=source), self.assertRaisesRegex(ValueError, "alias"):
                    package.bind(self.directory, bindings)

    def test_global_binder_rejects_missing_extra_malformed_and_out_of_range_pages(self):
        cases = []
        missing = dict(self.synthetic_pages); del missing["logs"]; cases.append(("missing", missing))
        extra = dict(self.synthetic_pages, accidental=[0x800000000]); cases.append(("extra", extra))
        for bad in (None, "page", [], [True], [0], [4097], [1 << 40], [0x100000000000]):
            malformed = dict(self.synthetic_pages); malformed["metadata"] = bad
            cases.append((repr(bad), malformed))
        short = dict(self.synthetic_pages); short["radix3"] = short["radix3"][:-1]
        cases.append(("truncated-radix", short))
        with patch.object(package, "prepare", side_effect=lambda *a: (copy.deepcopy(self.report), self.files)):
            for label, bindings in cases:
                with self.subTest(case=label), self.assertRaises(ValueError):
                    package.bind(self.directory, bindings)

    def test_global_binder_rejects_fragmented_contiguous_loader_and_log_windows(self):
        with patch.object(package, "prepare", side_effect=lambda *a: (copy.deepcopy(self.report), self.files)):
            for name in ("bootloader", "logs"):
                bindings = {key: list(pages) for key, pages in self.synthetic_pages.items()}
                bindings[name][1] += 0x2000000  # Unique/aligned but physically noncontiguous.
                with self.subTest(resource=name), self.assertRaises(ValueError):
                    package.bind(self.directory, bindings)

    def test_optional_fuse_evidence_patches_exact_signature_region_only(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "synthetic-fuse.plist"
            for raw in (0, 1):
                path.write_bytes(plistlib.dumps([fuse_fixture(raw)]))
                report, files = package.prepare(self.directory, path)
                selected = report["booter_signature_selection"]
                self.assertEqual(selected["signature_index"], 1 - raw)
                expected_signature = self.report["assets"]["booter_load"]["signatures"][1 - raw]
                self.assertEqual(selected["signature_sha256"], expected_signature["sha256"])
                self.assertEqual(selected["snapshot_sha256"], digest(path.read_bytes()))
                self.assertEqual(selected["register"], "0x824148")
                self.assertTrue(selected["capture_is_historical_until_live_rechecked"])
                self.assertFalse(selected["gpu_acceptance_tested"])
                original, patched = files["booter-payload-unpatched.bin"], files["booter-payload-selected.bin"]
                off, size = selected["patch_offset"], selected["patch_size"]
                self.assertEqual((off, size), (0x8a10, 384))
                self.assertEqual(original[:off], patched[:off])
                self.assertEqual(original[off + size:], patched[off + size:])
                self.assertEqual(digest(patched[off:off + size]), selected["signature_sha256"])
                self.assertEqual(digest(original), digest(self.files["booter-payload-unpatched.bin"]))
                self.assertEqual(digest(files["gsp-fwimage.bin"]), digest(self.files["gsp-fwimage.bin"]))
                self.assertFalse(report["ready_for_hardware_boot"])
                self.assertFalse(report["bindings_present"])
                # Prepared evidence must also reach the final booter buffer;
                # other firmware buffers remain identical to unselected bind.
                with patch.object(package, "prepare", return_value=(copy.deepcopy(report), files)):
                    bound, buffers = package.bind(self.directory, self.synthetic_pages, path)
                self.assertEqual(buffers["booter_load"][:len(patched)], patched)
                self.assertEqual(buffers["booter_load"][len(patched):], bytes(0xf000 - len(patched)))
                for name in ("radix3", "bootloader", "signature"):
                    self.assertEqual(digest(buffers[name]), digest(self.buffers[name]))
                self.assertFalse(bound["ready_for_hardware_boot"])

    def test_old_fwsec_fuse_or_unsupported_version_cannot_select_booter_signature(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "wrong-fuse.plist"
            for changes in ({"BooterFuseOffset": 0x8241e0},
                            {"BooterFuseFirst": 2, "BooterFuseSecond": 2, "BooterSignatureIndex": 0xffffffff,
                             "ProbePassed": False, "Boot0Status": "unsupported-fuse"}):
                n = fuse_fixture(); n.update(changes); path.write_bytes(plistlib.dumps([n]))
                with self.subTest(changes=changes), self.assertRaises(ValueError): package.prepare(self.directory, path)

    def test_existing_output_is_rejected_before_preparation_and_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "existing"; output.mkdir()
            marker = output / "evidence.bin"; marker.write_bytes(b"preserve-this-evidence")
            before = marker.read_bytes()
            with patch.object(package, "prepare") as prepare:
                with self.assertRaisesRegex(ValueError, "already exists"):
                    package.write_package(self.directory, output)
                prepare.assert_not_called()
            self.assertEqual(marker.read_bytes(), before)
            self.assertEqual([p.name for p in output.iterdir()], ["evidence.bin"])


if __name__ == "__main__":
    unittest.main()
