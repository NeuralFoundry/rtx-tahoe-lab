"""Synthetic offline fixtures; no page below is claimed to be live DMA memory."""
import copy
import struct
import unittest

import gsp_memory as g


def descriptor():
    return dict(payload_size=0x6000, monitorCodeOffset=0x1800, monitorCodeSize=0x2900,
                monitorDataOffset=0x800, monitorDataSize=0x1000, manifestOffset=0, manifestSize=0x800)


def bound_fixture(image=b"test image"):
    counts = g.radix_geometry(len(image))
    # Explicit synthetic, deliberately discontiguous >4GiB addresses.
    pages = [0x100000000 + i * 0x3000 for i in range(counts["total_pages"])]
    binding = dict(radix3=g.build_radix3(image, pages),
                   bootloader_pages=[0x200000000 + i * g.PAGE for i in range(6)],
                   signature_pages=[0x300000000], signature_size=g.PAGE,
                   metadata_pages=[0x400000000])
    return g.plan_vram(g.VRAM_BYTES, len(image), 0x6000), descriptor(), binding


class LayoutTests(unittest.TestCase):
    def test_pinned_layout_and_incremental_reservation(self):
        p = g.plan_vram(6 << 30, 63541248, 24576)
        self.assertEqual(p["required_vram_bytes"], 193 << 20)
        self.assertEqual(p["additional_vram_bytes_beyond_frts"], 192 << 20)
        self.assertEqual(p["top_vram_bytes_including_bios"], 194 << 20)
        expected = dict(bootBinOffset=0x17FDFA000, gspFwOffset=0x17C160000,
                        gspFwHeapOffset=0x174000000, gspFwWprStart=0x173F00000,
                        nonWprHeapOffset=0x173E00000, frtsOffset=0x17FE00000,
                        gspFwWprEnd=0x17FF00000)
        for key, value in expected.items():
            self.assertEqual(p["fields"][key], value)
        self.assertFalse(p["region_reserved"])
        self.assertFalse(p["live_dma_verified"])
        self.assertIn("not_live_validated", p["heap_size_provenance"])

    def test_alignment_disjointness_and_untouched_frts(self):
        for image_size in (1, 4095, 4096, 4097, 65535, 65536, 65537, 63541248):
            for boot_size in (1, 4095, 4096, 4097, 24576):
                with self.subTest(image=image_size, boot=boot_size):
                    p = g.plan_vram(g.VRAM_BYTES, image_size, boot_size)
                    f = p["fields"]
                    self.assertEqual(f["bootBinOffset"] % 4096, 0)
                    self.assertEqual(f["gspFwOffset"] % 65536, 0)
                    self.assertEqual(f["gspFwHeapOffset"] % g.MIB, 0)
                    self.assertEqual(f["frtsOffset"], g.FRTS_OFFSET)
                    self.assertEqual(f["frtsSize"], g.MIB)
                    for before, after in zip(p["ranges"], p["ranges"][1:]):
                        self.assertLessEqual(before["end"], after["offset"])
                    self.assertGreaterEqual(p["reserved_start"], p["prescrubbed_window_start"])

    def test_reject_wrong_board_and_integer_inputs(self):
        for kwargs in (dict(vram_bytes=4 << 30), dict(workspace_raw=0), dict(workspace_raw=9),
                       dict(display_fuse_raw=1), dict(heap_size=87 << 20),
                       dict(heap_size=(129 << 20) + 1), dict(non_wpr_heap_size=0)):
            args = dict(vram_bytes=g.VRAM_BYTES, image_size=4096, bootloader_size=4096)
            args.update(kwargs)
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                g.plan_vram(**args)
        for value in (-1, 0, True, 1.0, "4096", 1 << 64):
            with self.subTest(value=value), self.assertRaises(ValueError):
                g.plan_vram(g.VRAM_BYTES, value, 4096)

    def test_prescrubbed_window_limit(self):
        with self.assertRaisesRegex(ValueError, "pre-scrubbed"):
            g.plan_vram(g.VRAM_BYTES, 128 << 20, 24576)
        with self.assertRaises(ValueError):
            g.plan_vram(g.VRAM_BYTES, 1, 24576, heap_size=256 << 20)
        p = g.plan_vram(g.VRAM_BYTES, 4096, 24576, heap_size=88 << 20)
        self.assertEqual(p["heap_size_provenance"], "explicit_offline_heap_choice")


class RadixTests(unittest.TestCase):
    def test_geometry_boundaries(self):
        for size, expected in ((1, [1, 1, 1, 1]), (4096, [1, 1, 1, 1]),
                               (4097, [1, 1, 1, 2]), (512 * 4096, [1, 1, 1, 512]),
                               (512 * 4096 + 1, [1, 1, 2, 513]),
                               (63541248, [1, 1, 31, 15513])):
            with self.subTest(size=size):
                geometry = g.radix_geometry(size)
                self.assertEqual(geometry["level_page_counts"], expected)
                self.assertEqual(geometry["total_size"], sum(expected) * 4096)
        for size in (0, -1, True, (128 << 20) + 1, 1 << 64):
            with self.assertRaises(ValueError):
                g.radix_geometry(size)

    def test_walk_discontiguous_tree_across_leaf_table_boundary(self):
        image = bytes((i * 29 + 7) & 255 for i in range(512 * 4096 + 19))
        geometry = g.radix_geometry(len(image))
        pages = [0x400000000 + i * 0x5000 for i in range(geometry["total_pages"])]
        r = g.build_radix3(image, pages)
        storage = r["table_bytes"] + r["image_bytes"]
        address_to_page = {address: storage[i * 4096:(i + 1) * 4096] for i, address in enumerate(pages)}
        # Follow each512-way level, independently of the builder's flat offsets.
        recovered = bytearray()
        for image_page in range(geometry["level_page_counts"][3]):
            root = address_to_page[r["root_address"]]
            l1 = struct.unpack_from("<Q", root, (image_page // (512 * 512)) * 8)[0]
            l2 = struct.unpack_from("<Q", address_to_page[l1], ((image_page // 512) % 512) * 8)[0]
            leaf = struct.unpack_from("<Q", address_to_page[l2], (image_page % 512) * 8)[0]
            recovered.extend(address_to_page[leaf])
        self.assertEqual(bytes(recovered[:len(image)]), image)
        self.assertFalse(any(recovered[len(image):]))
        self.assertEqual(r["level_page_counts"], [1, 1, 2, 513])
        self.assertFalse(r["live_dma_verified"])

    def test_unused_entries_and_image_tail_zero(self):
        r = g.build_radix3(b"abc", [0x1000, 0x9000, 0x5000, 0xD000])
        for offset, next_address in ((0, 0x9000), (4096, 0x5000), (8192, 0xD000)):
            self.assertEqual(struct.unpack_from("<Q", r["table_bytes"], offset)[0], next_address)
            self.assertFalse(any(r["table_bytes"][offset + 8:offset + 4096]))
        self.assertEqual(r["image_bytes"][:3], b"abc")
        self.assertFalse(any(r["image_bytes"][3:]))

    def test_address_and_page_count_rejections(self):
        for pages in ([], [4096] * 4, [4096, 8192, 12288],
                      [4096, 8192, 12288, 16384, 20480],
                      [0, 8192, 12288, 16384], [4097, 8192, 12288, 16384],
                      [True, 8192, 12288, 16384], [1 << 40, 8192, 12288, 16384],
                      [(1 << 40) - 1, 8192, 12288, 16384]):
            with self.subTest(pages=pages), self.assertRaises(ValueError):
                g.build_radix3(b"a", pages)
        g.build_radix3(b"a", [(1 << 40) - 4096, 8192, 12288, 16384])
        for image in (b"", bytearray(b"a"), "a", None):
            with self.assertRaises(ValueError):
                g.build_radix3(image, [4096, 8192, 12288, 16384])


class MetadataTests(unittest.TestCase):
    def test_exact_wire_offsets_and_zero_verified(self):
        layout, info, bindings = bound_fixture()
        encoded = g.encode_wpr_meta(layout, info, bindings)
        self.assertEqual(len(encoded), 256)
        expected = {0: g.META_MAGIC, 8: 1, 16: 0x100000000, 24: 10,
                    32: 0x200000000, 40: 0x6000, 48: 0x1800, 56: 0x800,
                    64: 0, 72: 0x300000000, 80: 4096, 152: g.FRTS_OFFSET,
                    160: g.MIB, 168: g.BIOS_OFFSET, 176: g.VRAM_BYTES,
                    184: g.BIOS_OFFSET, 192: g.MIB, 200: 0, 248: 0}
        for offset, value in expected.items():
            self.assertEqual(struct.unpack_from("<Q", encoded, offset)[0], value)
        self.assertEqual(encoded[208:], bytes(48))

    def test_missing_bindings_never_produce_unbound_metadata(self):
        layout, info, bindings = bound_fixture()
        for key in bindings:
            changed = dict(bindings); del changed[key]
            with self.subTest(key=key), self.assertRaises(ValueError):
                g.encode_wpr_meta(layout, info, changed)

    def test_binding_aliases_and_contiguous_direct_pointer(self):
        layout, info, bindings = bound_fixture()
        for key in ("bootloader_pages", "signature_pages", "metadata_pages"):
            bad = copy.deepcopy(bindings)
            bad[key][0] = bindings["radix3"]["physical_pages"][0]
            with self.subTest(key=key), self.assertRaises(ValueError):
                g.encode_wpr_meta(layout, info, bad)
        bad = copy.deepcopy(bindings); bad["bootloader_pages"][3] += 4096
        with self.assertRaises(ValueError):
            g.encode_wpr_meta(layout, info, bad)
        bad = copy.deepcopy(bindings); bad["metadata_pages"] = bad["signature_pages"]
        with self.assertRaises(ValueError):
            g.encode_wpr_meta(layout, info, bad)

    def test_reject_tampered_layout(self):
        layout, info, bindings = bound_fixture()
        for key in layout["fields"]:
            bad = copy.deepcopy(layout); bad["fields"][key] += 1
            with self.subTest(key=key), self.assertRaises(ValueError):
                g.encode_wpr_meta(bad, info, bindings)
        bad = copy.deepcopy(layout); bad["region_reserved"] = True
        with self.assertRaises(ValueError):
            g.encode_wpr_meta(bad, info, bindings)

    def test_reject_tampered_radix_and_truncation(self):
        layout, info, bindings = bound_fixture()
        for field, value in (("root_address", 8192), ("total_pages", 99),
                             ("table_bytes", b""), ("image_bytes", b""),
                             ("image_sha256", "0" * 64)):
            bad = copy.deepcopy(bindings); bad["radix3"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                g.encode_wpr_meta(layout, info, bad)
        bad = copy.deepcopy(bindings)
        tail = bytearray(bad["radix3"]["image_bytes"]); tail[-1] = 1
        bad["radix3"]["image_bytes"] = bytes(tail)
        with self.assertRaises(ValueError):
            g.encode_wpr_meta(layout, info, bad)

    def test_bootloader_range_overflow_overlap_and_size(self):
        layout, info, bindings = bound_fixture()
        for field, value in (("payload_size", 0x5000), ("monitorCodeOffset", 0x6000),
                             ("monitorCodeSize", 0x5000), ("manifestSize", 0),
                             ("monitorDataOffset", 0), ("manifestOffset", 1 << 64),
                             ("manifestSize", True)):
            bad = dict(info); bad[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                g.encode_wpr_meta(layout, bad, bindings)
        bad = dict(bindings); bad["signature_size"] = 384
        with self.assertRaises(ValueError):
            g.encode_wpr_meta(layout, info, bad)


if __name__ == "__main__":
    unittest.main()
