import copy
import struct
import unittest

import gsp_rpc
import gsp_startup as startup


def synthetic_facts():
    # Synthetic serializer fixtures, never a claim about a current allocation.
    return {"gpuPhysAddr": 0xfb000000, "gpuPhysFbAddr": 0x824000000,
            "gpuPhysInstAddr": 0xfc000000, "nvDomainBusDeviceFunc": 0x100,
            "PCIDeviceID": 0x252010de, "PCISubDeviceID": 0x104c1043,
            "PCIRevisionID": 0xa1, "pciConfigMirrorBase": 0x88000,
            "pciConfigMirrorSize": 4096, "maxUserVa": 0x7ffffffff000,
            "hostPageSize": 4096, "bIsPassthru": True}


def sequence(words, *, capacity=None, used=None, trailing=()):
    count = len(words) if used is None else used
    capacity = count + 1 if capacity is None else capacity
    return struct.pack("<10I", capacity, count, *range(8)) + struct.pack("<" + str(len(words) + len(trailing)) + "I", *words, *trailing)


class SystemInfoTests(unittest.TestCase):
    def test_full_570_144_abi_and_explicit_fields(self):
        facts = synthetic_facts()
        original = copy.deepcopy(facts)
        data = startup.encode_system_info(facts)
        self.assertEqual(len(data), 928)
        self.assertEqual(struct.unpack_from("<5Q", data), (0xfb000000, 0x824000000, 0xfc000000, 0, 0x100))
        self.assertEqual(struct.unpack_from("<5I", data, 80), (0x88000, 4096, 0x252010de, 0x104c1043, 0xa1))
        self.assertEqual(struct.unpack_from("<Q", data, 72)[0], 0x7ffffffff000)
        self.assertEqual(struct.unpack_from("<Q", data, 920)[0], 4096)
        self.assertEqual(data[840], 1)
        self.assertEqual(data[160:836], bytes(676))
        self.assertEqual(data[856:896], bytes(40))
        self.assertEqual(facts, original)

    def test_optional_platform_data_is_explicit_and_width_checked(self):
        facts = synthetic_facts()
        facts.update(acpiMethodData=b"A" * 676, FHBBusInfo=b"B" * 10,
                     chipsetIDInfo=b"C" * 10, pcieConfigReg=0x10203040,
                     bSystemHasMux=True, hostPageSize=4096)
        data = startup.encode_system_info(facts)
        self.assertEqual(data[138:158], b"B" * 10 + b"C" * 10)
        self.assertEqual(data[160:836], b"A" * 676)
        self.assertEqual(struct.unpack_from("<I", data, 900)[0], 0x10203040)
        self.assertEqual(data[136], 1)
        for name, value in (("gspVFInfo", b"\1" + bytes(39)), ("acpiMethodData", bytes(675)),
                            ("FHBBusInfo", bytearray(10)), ("bSystemHasMux", 2),
                            ("pcieConfigReg", 1 << 32), ("hostPageSize", True)):
            with self.subTest(field=name), self.assertRaises(ValueError):
                startup.encode_system_info(dict(facts, **{name: value}))

    def test_no_historical_defaults_for_required_facts(self):
        for name in startup.REQUIRED_SYSTEM_FIELDS:
            facts = synthetic_facts()
            del facts[name]
            with self.subTest(missing=name), self.assertRaises(ValueError):
                startup.encode_system_info(facts)
        for value in ({}, [], None, dict(synthetic_facts(), historical_bar3=0xfc000000)):
            with self.subTest(value=type(value).__name__), self.assertRaises(ValueError):
                startup.encode_system_info(value)

    def test_board_aperture_and_integer_boundaries(self):
        invalid = {
            "gpuPhysAddr": (0, 0xfb001000, 0x100000000, -1, True),
            "gpuPhysFbAddr": (0, 0x824001000, 1 << 40, 0xf8000000),
            "gpuPhysInstAddr": (0, 0xfc000001, 1 << 40, 0xfb001000, 0x824001000),
            "PCIDeviceID": (0x252110de, 1 << 32), "PCISubDeviceID": (0,),
            "nvDomainBusDeviceFunc": (0, 0x101, 0x10100), "PCIRevisionID": (256,),
            "pciConfigMirrorBase": (0x92000,), "pciConfigMirrorSize": (0,),
            "hostPageSize": (16384,), "maxUserVa": (0, 0x7ffffffff001, 1 << 48),
            "bIsPassthru": (2,), "notifyOpSharedSurfacePhysAddr": (1, 1 << 40),
        }
        for name, values in invalid.items():
            for value in values:
                with self.subTest(field=name, value=value), self.assertRaises(ValueError):
                    startup.encode_system_info(dict(synthetic_facts(), **{name: value}))
        facts = synthetic_facts()
        facts.update(gpuPhysInstAddr=(1 << 40) - 4096, maxUserVa=1 << 47,
                     notifyOpSharedSurfacePhysAddr=4096)
        self.assertEqual(len(startup.encode_system_info(facts)), 928)


class RegistryAndPrefillTests(unittest.TestCase):
    def setUp(self):
        self.system = startup.encode_system_info(synthetic_facts())

    def test_registry_golden_wire_layout(self):
        data = startup.encode_registry({"Alpha": 0x12345678, "Beta": 1})
        golden = (struct.pack("<II", 51, 2) + struct.pack("<IB3xII", 40, 1, 0x12345678, 4) +
                  struct.pack("<IB3xII", 46, 1, 1, 4) + b"Alpha\0Beta\0")
        self.assertEqual(data, golden)
        self.assertEqual(startup.encode_registry({}), struct.pack("<II", 8, 0))

    def test_registry_type_name_and_count_limits(self):
        for entries in (None, [], {"": 1}, {"A\0B": 1}, {"With space": 1}, {"Turk\u00e7e": 1},
                        {"x" * 128: 1}, {1: 1}, {"A": True}, {"A": -1}, {"A": 1 << 32},
                        {"A" + str(i): i for i in range(65)}):
            with self.subTest(entries=str(entries)[:30]), self.assertRaises(ValueError):
                startup.encode_registry(entries)
        self.assertGreater(len(startup.encode_registry({"x" * 127: 0xffffffff})), 127)

    def test_queue_prefills_only_two_async_records_in_vendor_order(self):
        pages = [0x40000000 + i * 4096 for i in range(129)]
        original_pages = list(pages)
        registry = startup.encode_registry({"RMForcePcieConfigSave": 1, "RMSecBusResetEnable": 1})
        result = startup.prefill_queue(pages, self.system, registry)
        data = result["shared_memory"]
        self.assertEqual(len(data), 0x81000)
        self.assertEqual(struct.unpack_from("<129Q", data), tuple(pages))
        self.assertEqual(struct.unpack_from("<I", data, 0x1000 + 16)[0], 2)
        first = gsp_rpc.decode_record(data[0x2000:0x3000], expected_sequence=0)
        second = gsp_rpc.decode_record(data[0x3000:0x4000], expected_sequence=1)
        self.assertEqual((first.rpc.function, second.rpc.function), (72, 73))
        self.assertEqual(first.rpc.payload, self.system)
        self.assertEqual(second.rpc.payload, registry)
        self.assertEqual((first.rpc.result, second.rpc.result), (0xffffffff, 0xffffffff))
        self.assertEqual(data[0x41000:], bytes(0x40000))
        self.assertEqual(data[0x1000 + 32:0x1000 + 36], bytes(4))
        self.assertEqual(result["metadata"]["next_transport_sequence"], 2)
        for key in ("allocation_performed", "dma_prepared", "execution_ready", "hardware_accessed", "firmware_executed", "facts_live_verified"):
            self.assertIs(result["metadata"][key], False)
        self.assertEqual(pages, original_pages)

    def test_multislot_registry_preserves_framing_and_unbound_state(self):
        registry = startup.encode_registry({"A" + str(i) + "z" * 120: i for i in range(64)})
        result = startup.prefill_queue(None, self.system, registry)
        data = result["shared_memory"]
        count = struct.unpack_from("<I", data, 0x3000 + 40)[0]
        self.assertEqual(count, 3)
        record = gsp_rpc.decode_record(data[0x3000:0x3000 + count * 4096], expected_sequence=1)
        self.assertEqual(record.rpc.payload, registry)
        self.assertEqual(result["metadata"]["command_write_index"], 4)
        self.assertEqual(data[0x41000:], bytes(0x40000))
        self.assertIsNone(result["gsp_arguments"])
        self.assertEqual(result["metadata"]["binding_state"], "unbound")

    def test_prefill_revalidates_payloads_and_page_aliases(self):
        registry = startup.encode_registry({"Alpha": 1})
        for system in (self.system[:-1], bytes(4), bytearray(self.system), self.system[:919] + b"\1" + self.system[920:]):
            with self.subTest(system_size=len(system)), self.assertRaises(ValueError):
                startup.prefill_queue(None, system, registry)
        mutations = []
        for offset, value in ((0, 1), (4, 65), (8, 8), (12, 2), (20, 8)):
            bad = bytearray(registry)
            struct.pack_into("<I", bad, offset, value)
            mutations.append(bytes(bad))
        bad = bytearray(registry); bad[13] = 1; mutations.append(bytes(bad))
        mutations.extend([registry[:-1], registry + b"\0", bytearray(registry)])
        for bad in mutations:
            with self.subTest(registry=bytes(bad).hex()), self.assertRaises(ValueError):
                startup.prefill_queue(None, self.system, bad)
        with self.assertRaises(ValueError):
            startup.prefill_queue([4096] * 129, self.system, registry)


class SequencerTests(unittest.TestCase):
    def test_every_opcode_and_vendor_modify_order(self):
        words = [0, 0x110040, 0x1234, 1, 0x110044, 0xffff0000, 0x12340000,
                 2, 0x110100, 0x10, 0x10, 5, 3, 3, 4, 4, 0x110040, 7, 5, 6, 7, 8]
        parsed = startup.parse_cpu_sequencer(sequence(words))
        self.assertEqual([row["opcode"] for row in parsed["operations"]], list(range(9)))
        modify = parsed["operations"][1]
        self.assertEqual((modify["mask"], modify["value"]), (0xffff0000, 0x12340000))
        self.assertEqual((0xabcdefff & ~modify["mask"]) | modify["value"], 0x1234efff)
        self.assertEqual(parsed["operations"][2]["timeout_us"], 5)
        self.assertEqual(parsed["maximum_declared_wait_us"], 4_000_009)
        self.assertEqual(parsed["register_save_area"], list(range(8)))
        for key in ("register_allowlist_validated", "operations_executed", "firmware_executed", "hardware_accessed"):
            self.assertIs(parsed[key], False)

    def test_capacity_and_transmitted_word_limits(self):
        self.assertEqual(startup.parse_cpu_sequencer(sequence([], capacity=1))["operations"], [])
        self.assertEqual(startup.parse_cpu_sequencer(sequence([5], capacity=4, trailing=(0xffffffff, 0xffffffff)))["wire_words"], 3)
        bad = [b"", bytes(39), bytes(41), sequence([], capacity=0), sequence([5], capacity=1),
               sequence([5], capacity=4097), sequence([5], used=2, capacity=3),
               sequence([5], capacity=2, trailing=(5, 5)), bytes(40 + 4097 * 4)]
        for payload in bad:
            with self.subTest(size=len(payload)), self.assertRaises(ValueError):
                startup.parse_cpu_sequencer(payload)
        with self.assertRaises(ValueError):
            startup.parse_cpu_sequencer(bytearray(sequence([5])))

    def test_truncated_operands_unknown_opcodes_and_count_bound(self):
        for opcode, arguments in ((0, [0, 0]), (1, [0, 1, 1]), (2, [0, 1, 1, 1, 0]), (3, [1]), (4, [0, 0])):
            for length in range(len(arguments)):
                with self.subTest(opcode=opcode, args=length), self.assertRaises(ValueError):
                    startup.parse_cpu_sequencer(sequence([opcode] + arguments[:length]))
        for opcode in (9, 0xffffffff):
            with self.subTest(opcode=opcode), self.assertRaises(ValueError):
                startup.parse_cpu_sequencer(sequence([opcode]))
        self.assertEqual(len(startup.parse_cpu_sequencer(sequence([5] * 256))["operations"]), 256)
        with self.assertRaises(ValueError):
            startup.parse_cpu_sequencer(sequence([5] * 257))

    def test_register_range_mask_and_saved_register_boundaries(self):
        for words in ([0, 1, 0], [0, 0x1000000, 0], [0, 0xffffffff, 0],
                      [1, 0, 0x12340000, 0xffff0000], [2, 0, 1, 2, 1, 0], [4, 0, 8]):
            with self.subTest(words=words), self.assertRaises(ValueError):
                startup.parse_cpu_sequencer(sequence(words))
        result = startup.parse_cpu_sequencer(sequence([0, 0xfffffc, 0xffffffff, 4, 0, 7]))
        self.assertEqual(result["operations"][0]["address"], 0xfffffc)
        self.assertFalse(result["register_allowlist_validated"])

    def test_finite_poll_delay_and_aggregate_time_limits(self):
        result = startup.parse_cpu_sequencer(sequence([2, 0, 1, 1, 0, 99]))
        self.assertEqual(result["operations"][0]["timeout_us"], 4_000_000)
        self.assertEqual(result["operations"][0]["error_code"], 99)
        for words in ([2, 0, 1, 1, 4_000_001, 0], [3, 1_000_001], [3, 1_000_000] * 11, [8] * 6):
            with self.subTest(words=words), self.assertRaises(ValueError):
                startup.parse_cpu_sequencer(sequence(words))
        self.assertEqual(startup.parse_cpu_sequencer(sequence([3, 1_000_000] * 10))["maximum_declared_wait_us"], 10_000_000)


class InitDoneTests(unittest.TestCase):
    def test_framed_success_is_not_a_claim_of_live_hardware(self):
        data = gsp_rpc.encode_record(0x1001, struct.pack("<I", 0x12345678), result=0, transport_sequence=7)
        result = startup.parse_init_done(data, expected_sequence=7)
        self.assertTrue(result["validated_init_done_record"])
        self.assertEqual(result["unused_payload_word"], 0x12345678)
        for key in ("live_execution_proven", "hardware_accessed", "compute_tested", "metal_supported"):
            self.assertIs(result[key], False)

    def test_wrong_function_result_length_and_sequence_rejected(self):
        for function, result, payload in ((72, 0, bytes(4)), (0x1002, 0, bytes(4)),
                                         (0x1001, 1, bytes(4)), (0x1001, 0xffffffff, bytes(4)),
                                         (0x1001, 0, b""), (0x1001, 0, bytes(3)), (0x1001, 0, bytes(5))):
            with self.subTest(function=function, result=result, size=len(payload)), self.assertRaises(ValueError):
                startup.parse_init_done(gsp_rpc.encode_record(function, payload, result=result), expected_sequence=0)
        data = gsp_rpc.encode_record(0x1001, bytes(4), result=0)
        for expected in (1, -1, 1 << 32, True):
            with self.subTest(expected=expected), self.assertRaises(ValueError):
                startup.parse_init_done(data, expected_sequence=expected)

    def test_transport_corruption_and_partial_publication_rejected(self):
        data = gsp_rpc.encode_record(0x1001, bytes(4), result=0)
        bad = bytearray(data); bad[80] ^= 1
        for record in (data[:-1], bytes(bad), bytearray(data)):
            with self.subTest(kind=type(record).__name__), self.assertRaises(ValueError):
                startup.parse_init_done(record, expected_sequence=0)


if __name__ == "__main__":
    unittest.main()
