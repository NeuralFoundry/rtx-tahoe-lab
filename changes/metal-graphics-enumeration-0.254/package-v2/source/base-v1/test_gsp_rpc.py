"""Pinned ABI fixtures and adversarial offline queue tests; no hardware imports."""
import ctypes
import struct
import unittest

import gsp_rpc as rpc


def raw_record(function=72, payload=b"", sequence=0, result=0):
    """Independent fixture constructor; intentionally permits fragmentation."""
    header = struct.pack("<8I", 0x03000000, 0x43505256, 32 + len(payload),
                         function, result, result, 0, 0)
    used = 48 + len(header) + len(payload)
    elements = (used + 4095) // 4096
    frame = bytearray(bytes(32) + struct.pack("<4I", 0, sequence, elements, 0) + header + payload)
    frame.extend(bytes(elements * 4096 - len(frame)))
    rechecksum(frame)
    return bytes(frame)


def rechecksum(frame):
    used = 48 + struct.unpack_from("<I", frame, 56)[0]
    struct.pack_into("<I", frame, 32, 0)
    # An independent u32 XOR equals NVIDIA's u64 XOR then high/low fold.
    padded = bytes(frame[:used]) + bytes((-used) % 8)
    checksum = 0
    for (value,) in struct.iter_unpack("<I", padded):
        checksum ^= value
    struct.pack_into("<I", frame, 32, checksum)


class AbiTests(unittest.TestCase):
    def test_struct_offsets_against_pinned_c_declarations(self):
        # Handwritten C-equivalent structures from NVIDIA570.144 source.
        # No generated tinygrad classes are imported or trusted.
        class Tx(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint32) for name in rpc.ABI_OFFSETS["tx"]]
        class Rpc(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint32) for name in
                        ("version", "signature", "length", "function", "result", "privateResult", "sequence", "cpuRmGfid")]
        class Element(ctypes.Structure):
            # NV_DECLARE_ALIGNED(rpc,8) adds four bytes after elemCount.
            _fields_ = [("authTag", ctypes.c_ubyte * 16), ("aad", ctypes.c_ubyte * 16),
                        ("checksum", ctypes.c_uint32), ("sequence", ctypes.c_uint32),
                        ("elementCount", ctypes.c_uint32), ("alignmentPadding", ctypes.c_uint32), ("rpc", Rpc)]
        self.assertEqual(ctypes.sizeof(Tx), 32)
        self.assertEqual(ctypes.sizeof(Rpc), 32)
        self.assertEqual(ctypes.sizeof(Element), 80)
        for kind, cls in (("tx", Tx), ("transport", Element), ("rpc", Rpc)):
            for name, offset in rpc.ABI_OFFSETS[kind].items():
                if name != "payload":
                    self.assertEqual(getattr(cls, name).offset, offset, (kind, name))

    def test_boot_argument_native_alignment(self):
        class QueueArgs(ctypes.Structure):
            _fields_ = [("shared", ctypes.c_uint64), ("count", ctypes.c_uint32),
                        ("command", ctypes.c_uint64), ("status", ctypes.c_uint64)]
        class SrArgs(ctypes.Structure):
            _fields_ = [("old", ctypes.c_uint32), ("flags", ctypes.c_uint32), ("transition", ctypes.c_ubyte)]
        class Profiler(ctypes.Structure):
            _fields_ = [("pa", ctypes.c_uint64), ("size", ctypes.c_uint64)]
        class Args(ctypes.Structure):
            _fields_ = [("queue", QueueArgs), ("sr", SrArgs), ("instance", ctypes.c_uint32),
                        ("stack", ctypes.c_ubyte), ("profiler", Profiler)]
        self.assertEqual((ctypes.sizeof(QueueArgs), ctypes.sizeof(SrArgs), ctypes.sizeof(Args)), (32, 12, 72))
        self.assertEqual((QueueArgs.count.offset, QueueArgs.command.offset, QueueArgs.status.offset), (8, 16, 24))
        self.assertEqual((Args.sr.offset, Args.instance.offset, Args.stack.offset, Args.profiler.offset), (32, 44, 48, 56))

    def test_known_queue_header(self):
        layout = rpc.QueueLayout()
        golden = bytes.fromhex("00000000 00000400 00100000 3f000000 00000000 01000000 20000000 00100000")
        self.assertEqual(layout.tx_header(), golden)
        self.assertEqual((layout.slot_count, layout.usable_slots), (63, 62))
        decoded = rpc.decode_queue_header(golden)
        self.assertEqual(decoded["entryOff"], 4096)

    def test_queue_header_corruption(self):
        base = rpc.QueueLayout().tx_header()
        for offset in range(0, 32, 4):
            damaged = bytearray(base)
            struct.pack_into("<I", damaged, offset, 0xffffffff)
            with self.subTest(offset=offset), self.assertRaises(rpc.PacketError):
                rpc.decode_queue_header(bytes(damaged))
        for length in range(32):
            with self.subTest(length=length), self.assertRaises(rpc.PacketError):
                rpc.decode_queue_header(base[:length])

    def test_layout_invalid_sizes(self):
        for value in (0, 4096, 8192, 12287, 12289, 0x41000, -1, True, 1.0, "262144"):
            with self.subTest(value=value), self.assertRaises(rpc.PacketError):
                rpc.QueueLayout(value)


class PacketTests(unittest.TestCase):
    def test_known_rpc_header_and_checksum(self):
        encoded = rpc.encode_record(72, transport_sequence=7)
        golden = bytes.fromhex("00000003 56525043 20000000 48000000 ffffffff ffffffff 00000000 00000000")
        self.assertEqual(encoded[48:80], golden)
        self.assertEqual(struct.unpack_from("<I", encoded, 32)[0], 0x40505238)
        self.assertEqual(rpc.checksum32(encoded[:80]), 0)
        record = rpc.decode_record(encoded, expected_sequence=7)
        self.assertEqual((record.element_count, record.rpc.function, record.rpc.payload), (1, 72, b""))

    def test_independent_packet_fixture(self):
        frame = raw_record(0x1001, b"init", sequence=23)
        record = rpc.decode_record(frame, expected_sequence=23)
        self.assertEqual(record.rpc.bootstrap_event, "gsp-init-done")
        self.assertEqual(record.rpc.payload, b"init")
        self.assertEqual(record.rpc.result, 0)

    def test_payload_slot_boundaries(self):
        for length in (0, 1, 7, 8, 4015, 4016, 4017, 8112, 8113, rpc.MAX_PAYLOAD_SIZE):
            with self.subTest(length=length):
                body = bytes((i * 37 + 9) & 255 for i in range(length))
                frame = rpc.encode_record(76, body, transport_sequence=0xffffffff, sequence=123)
                record = rpc.decode_record(frame, expected_sequence=0xffffffff)
                self.assertEqual(record.rpc.payload, body)
                self.assertEqual(record.rpc.sequence, 123)
                self.assertEqual(record.element_count, (80 + length + 4095) // 4096)

    def test_fields_roundtrip_and_error_results_preserved(self):
        data = rpc.encode_rpc(0x1006, b"failure", result=0x1234, private_result=5, sequence=0xffffffff)
        parsed = rpc.decode_rpc(data)
        self.assertEqual((parsed.result, parsed.private_result, parsed.sequence), (0x1234, 5, 0xffffffff))
        self.assertIsNone(parsed.bootstrap_event)

    def test_cpu_sequencer_is_only_classified(self):
        parsed = rpc.decode_rpc(rpc.encode_rpc(0x1002, bytes.fromhex("ffffffff"), result=0))
        self.assertEqual(parsed.bootstrap_event, "cpu-sequencer-handler-required")
        self.assertEqual(parsed.payload, b"\xff" * 4)

    def test_integer_and_buffer_types(self):
        for field in ("function", "result", "private_result", "sequence", "cpu_rm_gfid"):
            for value in (-1, 1 << 32, True, 1.0, None):
                args = {field: value} if field != "function" else {}
                with self.subTest(field=field, value=value), self.assertRaises(rpc.PacketError):
                    rpc.encode_rpc(value if field == "function" else 72, b"", **args)
        for payload in (bytearray(b"x"), "x", None):
            with self.subTest(payload=payload), self.assertRaises(rpc.PacketError):
                rpc.encode_rpc(72, payload)

    def test_rpc_length_signature_version_validation(self):
        base = rpc.encode_rpc(72, b"abc")
        for offset, value in ((0, 0), (0, 0x03010000), (4, 0), (8, 0), (8, 31), (8, 36), (8, 0xffffffff)):
            malformed = bytearray(base)
            struct.pack_into("<I", malformed, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(rpc.PacketError):
                rpc.decode_rpc(bytes(malformed))
        with self.assertRaises(rpc.PacketError):
            rpc.decode_rpc(base + b"extra")
        with self.assertRaises(rpc.PacketError):
            rpc.decode_rpc(base[:31])

    def test_fragmentation_rejected_both_directions(self):
        for function, body in ((71, b"x"), (72, bytes(rpc.MAX_PAYLOAD_SIZE + 1)),
                               (72, bytes(rpc.MAX_PAYLOAD_SIZE + 2))):
            with self.subTest(function=function, length=len(body)), self.assertRaises(rpc.UnsupportedFragmentation):
                rpc.encode_record(function, body)
        for frame in (raw_record(71, b"fragment"), raw_record(72, bytes(rpc.MAX_PAYLOAD_SIZE + 1))):
            with self.assertRaises(rpc.UnsupportedFragmentation):
                rpc.decode_record(frame)

    def test_confidential_mode_and_gfid_rejected(self):
        for offset in (0, 16):
            damaged = bytearray(raw_record())
            damaged[offset] = 1
            rechecksum(damaged)
            with self.assertRaises(rpc.UnsupportedMode):
                rpc.decode_record(bytes(damaged))
        with self.assertRaises(rpc.UnsupportedMode):
            rpc.encode_rpc(72, cpu_rm_gfid=1)
        damaged = bytearray(raw_record())
        struct.pack_into("<I", damaged, 76, 1)
        rechecksum(damaged)
        with self.assertRaises(rpc.UnsupportedMode):
            rpc.decode_record(bytes(damaged))

    def test_count_and_length_disagreement(self):
        for count in (0, 2, 17, 0xffffffff):
            malformed = bytearray(raw_record())
            struct.pack_into("<I", malformed, 40, count)
            with self.subTest(count=count), self.assertRaises(rpc.PacketError):
                rpc.decode_record(bytes(malformed))
        frame = bytearray(raw_record(72, bytes(4017)))
        struct.pack_into("<I", frame, 56, 32)
        rechecksum(frame)
        with self.assertRaises(rpc.PacketError):
            rpc.decode_record(bytes(frame))

    def test_checksum_and_only_meaningful_padding(self):
        original = raw_record(72, b"x")
        for offset in (32, 36, 48, 79, 80):
            frame = bytearray(original)
            frame[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(rpc.PacketError):
                rpc.decode_record(bytes(frame))
        frame = bytearray(original)
        frame[81] = 1
        rechecksum(frame)
        with self.assertRaises(rpc.PacketError):
            rpc.decode_record(bytes(frame))
        frame = bytearray(original)
        frame[88:] = b"\xa5" * (4096 - 88)
        self.assertEqual(rpc.decode_record(bytes(frame)).rpc.payload, b"x")

    def test_sequence_checked(self):
        frame = raw_record(sequence=0xffffffff)
        with self.assertRaises(rpc.PacketError):
            rpc.decode_record(frame, expected_sequence=0)
        self.assertEqual(rpc.decode_record(frame, expected_sequence=0xffffffff).sequence, 0xffffffff)

    def test_record_size_limits(self):
        for frame in (b"", bytes(80), bytes(4095), bytes(4097), bytes(17 * 4096)):
            with self.subTest(length=len(frame)), self.assertRaises(rpc.PacketError):
                rpc.decode_record(frame)


class RingTests(unittest.TestCase):
    def test_exhaustive_small_ring_arithmetic(self):
        for count in range(2, 20):
            for read in range(count):
                for write in range(count):
                    used = rpc.ring_used(read, write, count)
                    free = rpc.ring_free(read, write, count)
                    self.assertEqual(used + free, count - 1)
                    self.assertEqual(used == 0, read == write)

    def test_invalid_indices(self):
        for read, write, count in ((0, 0, 0), (0, 0, 1), (-1, 0, 4), (0, 4, 4),
                                    (4, 0, 4), (True, 0, 4), (0, 0, True)):
            with self.subTest(values=(read, write, count)), self.assertRaises(rpc.PacketError):
                rpc.ring_used(read, write, count)

    def test_reserved_slot_and_full_send_atomicity(self):
        ring = rpc.OfflineRing(rpc.QueueLayout(4 * 4096))
        ring.push(72, b"a")
        ring.push(73, b"b")
        self.assertEqual((ring.available, ring.free), (2, 0))
        before = (ring.write_index, ring.next_tx_sequence, bytes(ring.data))
        with self.assertRaises(rpc.RingFull):
            ring.push(72, b"c")
        self.assertEqual(before, (ring.write_index, ring.next_tx_sequence, bytes(ring.data)))
        self.assertEqual(ring.pop().rpc.payload, b"a")
        ring.push(72, b"c")
        self.assertEqual([ring.pop().rpc.payload, ring.pop().rpc.payload], [b"b", b"c"])
        self.assertIsNone(ring.pop())

    def test_multislot_wrap_and_sequence_wrap(self):
        ring = rpc.OfflineRing(initial_index=62, initial_sequence=0xffffffff)
        body = bytes((i * 11) & 255 for i in range(7000))
        self.assertEqual(ring.push(76, body), 0xffffffff)
        self.assertEqual(ring.write_index, 1)
        self.assertEqual(ring.peek().rpc.payload, body)
        self.assertEqual(ring.read_index, 62)
        self.assertEqual(ring.pop().rpc.payload, body)
        self.assertEqual((ring.read_index, ring.next_rx_sequence, ring.next_tx_sequence), (1, 0, 0))
        self.assertEqual(ring.push(72), 0)
        self.assertEqual(ring.pop().sequence, 0)

    def test_max_record_wrap(self):
        ring = rpc.OfflineRing(initial_index=60)
        body = b"\x4b" * rpc.MAX_PAYLOAD_SIZE
        ring.push(103, body)
        self.assertEqual(ring.available, 16)
        self.assertEqual(ring.pop().rpc.payload, body)
        self.assertEqual(ring.free, 62)

    def test_invalid_input_does_not_publish(self):
        ring = rpc.OfflineRing()
        before = (ring.write_index, ring.next_tx_sequence, bytes(ring.data))
        with self.assertRaises(rpc.UnsupportedFragmentation):
            ring.push(71, b"fragment")
        self.assertEqual(before, (ring.write_index, ring.next_tx_sequence, bytes(ring.data)))

    def test_malformed_inbound_never_advances(self):
        for offset, value in ((0, 1), (32, 1), (36, 8), (40, 0), (40, 17),
                              (40, 0xffffffff), (44, 1), (48, 0), (52, 0), (56, 0xffffffff)):
            with self.subTest(offset=offset, value=value):
                ring = rpc.OfflineRing()
                ring.push(72, b"x", result=0)
                struct.pack_into("<I", ring.data, offset, value)
                before = (ring.read_index, ring.next_rx_sequence, ring.write_index, bytes(ring.data))
                with self.assertRaises(rpc.PacketError):
                    ring.pop()
                self.assertEqual(before, (ring.read_index, ring.next_rx_sequence, ring.write_index, bytes(ring.data)))

    def test_incomplete_multislot_does_not_advance(self):
        ring = rpc.OfflineRing()
        ring.push(76, bytes(5000))
        ring.write_index = 1
        with self.assertRaises(rpc.PacketError):
            ring.pop()
        self.assertEqual((ring.read_index, ring.next_rx_sequence), (0, 0))

    def test_unsupported_inbound_fragment_not_consumed(self):
        for frame in (raw_record(71), raw_record(72, bytes(rpc.MAX_PAYLOAD_SIZE + 1))):
            ring = rpc.OfflineRing()
            ring.data[:len(frame)] = frame
            ring.write_index = len(frame) // 4096
            with self.assertRaises(rpc.UnsupportedFragmentation):
                ring.pop()
            self.assertEqual((ring.read_index, ring.next_rx_sequence), (0, 0))

    def test_backing_store_shape_corruption(self):
        ring = rpc.OfflineRing()
        ring.push(72)
        del ring.data[-1]
        with self.assertRaises(rpc.PacketError):
            ring.pop()
        self.assertEqual((ring.read_index, ring.next_rx_sequence), (0, 0))


class TemplateTests(unittest.TestCase):
    @staticmethod
    def addresses():
        return [0x400000000 + i * 8192 for i in range(129)]

    def test_unbound_has_no_fabricated_boot_pointer(self):
        output = rpc.build_queue_template()
        meta, shared = output["metadata"], output["shared_memory"]
        self.assertEqual(meta["binding_state"], "unbound")
        self.assertFalse(meta["execution_ready"])
        self.assertIsNone(output["gsp_arguments"])
        self.assertEqual((meta["page_count"], meta["page_table_bytes"], len(shared)), (129, 4096, 0x81000))
        self.assertEqual(shared[:4096], bytes(4096))
        self.assertEqual(shared[0x41000:], bytes(0x40000))
        self.assertEqual(rpc.decode_queue_header(shared[0x1000:])["msgCount"], 63)
        self.assertFalse(meta["status_header_initialized"])

    def test_bound_noncontiguous_pte_self_mapping(self):
        addresses = self.addresses()
        output = rpc.build_queue_template(addresses)
        shared, args, meta = output["shared_memory"], output["gsp_arguments"], output["metadata"]
        self.assertEqual(meta["binding_state"], "bound")
        self.assertFalse(meta["allocation_performed"])
        self.assertFalse(meta["dma_prepared"])
        self.assertFalse(meta["execution_ready"])
        self.assertEqual(list(struct.unpack_from("<129Q", shared)), addresses)
        self.assertEqual(shared[129 * 8:4096], bytes(4096 - 129 * 8))
        self.assertEqual(len(args), 72)
        self.assertEqual(struct.unpack_from("<QI4xQQ", args), (addresses[0], 129, 0x1000, 0x41000))
        self.assertEqual(args[48], 1)
        expected = bytearray(72)
        struct.pack_into("<QI4xQQ", expected, 0, addresses[0], 129, 0x1000, 0x41000)
        expected[48] = 1
        self.assertEqual(args, bytes(expected))

    def test_swapped_pointer_locations(self):
        pointers = rpc.build_queue_template()["metadata"]["pointer_locations"]
        self.assertEqual(pointers, {"host_command_write": 0x1010, "gsp_command_read": 0x41020,
                                    "gsp_status_write": 0x41010, "host_status_read": 0x1020})

    def test_binding_count_and_alias_rejection(self):
        for values in ([], self.addresses()[:-1], self.addresses() + [0x500000000], iter(self.addresses())):
            with self.assertRaises(rpc.PacketError):
                rpc.build_queue_template(values)
        values = self.addresses()
        values[128] = values[0]
        with self.assertRaises(rpc.PacketError):
            rpc.build_queue_template(values)

    def test_binding_address_validation(self):
        for value in (0, -4096, 1, 0x400000001, 1 << 40, True, 0.0, None):
            values = self.addresses()
            values[4] = value
            with self.subTest(value=value), self.assertRaises(rpc.PacketError):
                rpc.build_queue_template(values)
        values = self.addresses()
        values[4] = (1 << 40) - 4096
        self.assertIsNotNone(rpc.build_queue_template(values)["gsp_arguments"])

    def test_small_template_geometry(self):
        for pages in (3, 4, 17, 64):
            output = rpc.build_queue_template(queue_size=pages * 4096)
            meta = output["metadata"]
            self.assertEqual(meta["page_count"], 2 * pages + 1)
            self.assertEqual(meta["total_shared_bytes"], (2 * pages + 1) * 4096)


if __name__ == "__main__":
    unittest.main()
