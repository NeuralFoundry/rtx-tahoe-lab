"""Pure fake IOKit transport tests; no device, firmware, SSH or DMA allocation."""
import copy
import ctypes
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import gsp_dma_client as client

NOT_READY = 0xe00002d8


class FakeBackend(client.RestrictedBackend):
    def __init__(self, resources, fault=None):
        self.closed = False
        self.close_calls = 0
        self.begin_calls = 0
        self.abort_calls = 0
        self.calls = []
        self.fault = fault
        self.fault_fired = False
        self.header = dict(zip(client.HEADER, (client.MAGIC, 1, 0, 9, 4096, 0x824148,
                      1, 1, 2, 0, sum(v["bytes"] for v in resources.values()), 77, 0, 0, 0, 0)))
        self.rows, self.storage, self.addresses, self.exported_pages = {}, {}, {}, {}
        for index, name in enumerate(client.NAMES):
            size = resources[name]["bytes"]
            row = dict.fromkeys(client.ROW, 0)
            row.update(id=index, bytes=size, page_count=size // 4096)
            for key in client.PREPARE_RETURNS + ("sync_return",) + client.CLEANUP_RETURNS:
                row[key] = NOT_READY
            self.rows[name] = row
            self.exported_pages[name] = 0
            self.storage[name] = bytearray(size)
            self.addresses[name] = [0x100000000 + index * 0x10000000 + page * 0x3000
                                    for page in range(size // 4096)]

    def _wire_info(self):
        words = [self.header[key] for key in client.HEADER]
        for name in client.NAMES: words.extend(self.rows[name][key] for key in client.ROW)
        return struct.pack("<160Q", *words)

    def _invoke(self, selector, scalars, data, output_size):
        self.calls.append((selector, scalars, len(data), output_size))
        if self.fault == "begin" and selector == 0:
            self.begin_calls += 1
            raise OSError("ambiguous Begin failure")
        if selector == 0:
            self.begin_calls += 1
            if self.begin_calls != 1: raise OSError("one-shot Begin")
            self.header.update(state=1, allocated=1, published=0, cleanup_verified=0)
            for row in self.rows.values():
                row["mapping_was_prepared"] = 1
                row["enumerated_pages"] = row["page_count"]
                for key in client.PREPARE_RETURNS: row[key] = 0
            if self.fault == "prepare": self.rows["metadata"]["dma_prepare_return"] = NOT_READY
            if self.fault == "fuse": self.header["fuse_second"] = 0
            if self.fault == "pci": self.header["pci_command"] = 2
            if self.fault == "geometry": self.rows["metadata"]["page_count"] += 1
            return b""
        if selector == 1:
            if self.fault == "generation" and self.header["state"] == 2:
                self.header["generation"] = 78
            data = self._wire_info()
            if self.fault == "info_partial" and not self.fault_fired:
                self.fault_fired = True
                return data[:-8]
            return data
        if selector == 2:
            resource, start, count = scalars
            name = client.NAMES[resource]
            row = self.rows[name]
            if self.header["state"] != 1 or start != self.exported_pages[name]:
                raise OSError("nonsequential page enumeration")
            self.exported_pages[name] += count
            pages = self.addresses[name][start:start + count]
            if self.fault == "alias" and resource == 1:
                pages[0] = self.addresses["radix3"][0]
            if self.fault == "unaligned" and resource == 0: pages[0] += 1
            if self.fault == "page_overflow" and resource == 0: pages[0] = 1 << 40
            if self.fault == "pages_partial" and resource == 0: pages = pages[:-1]
            return struct.pack("<" + str(len(pages)) + "Q", *pages)
        if selector == 3:
            resource, offset = scalars
            name = client.NAMES[resource]
            row = self.rows[name]
            if self.header["state"] != 1 or offset != row["written_bytes"]:
                raise OSError("nonsequential write")
            self.storage[name][offset:offset + len(data)] = data
            row["written_bytes"] += len(data)
            if self.fault == "write" and not self.fault_fired:
                self.fault_fired = True
                raise OSError("write completed but reply failed")
            return b""
        if selector == 4:
            if any(r["written_bytes"] != r["bytes"] for r in self.rows.values()):
                raise OSError("incomplete Publish")
            self.header.update(state=2, published=1)
            for row in self.rows.values(): row.update(synchronized=1, sync_return=0)
            if self.fault == "publish": raise OSError("Publish reply failed")
            if self.fault == "sync": self.rows["logs"]["sync_return"] = NOT_READY
            return b""
        if selector == 5:
            resource, offset, size = scalars
            name = client.NAMES[resource]
            row = self.rows[name]
            if self.header["state"] != 2 or offset != row["read_bytes"]:
                raise OSError("nonsequential read")
            row["read_bytes"] += size
            block = bytes(self.storage[name][offset:offset + size])
            if self.fault == "read_partial" and not self.fault_fired:
                self.fault_fired = True
                return block[:-1]
            if self.fault == "read_corrupt" and resource == 0 and offset == 0:
                return bytes([block[0] ^ 0xff]) + block[1:]
            return block
        if selector == 6:
            if any(r["read_bytes"] != r["bytes"] for r in self.rows.values()):
                raise OSError("Finish before full readback")
            if self.fault == "finish": raise OSError("Finish failed")
            self.header.update(state=3, allocated=0, published=0, cleanup_verified=1)
            for row in self.rows.values():
                row["synchronized"] = 0
                for key in client.CLEANUP_RETURNS: row[key] = 0
            if self.fault == "cleanup_return": self.rows["logs"]["dma_complete_return"] = NOT_READY
            return b""
        if selector == 7:
            self.abort_calls += 1
            if self.fault == "abort_once" and self.abort_calls == 1:
                raise OSError("Abort reply failed")
            if self.header["state"] != 3:
                self.header.update(state=4, allocated=0, published=0, cleanup_verified=1, pci_command=0)
                # Fuse identity itself must remain readable even after failure.
                self.header.update(fuse_first=1, fuse_second=1)
                for row in self.rows.values():
                    row["page_count"] = row["bytes"] // 4096
                    row["resources_retained"] = 0
                    for key in client.CLEANUP_RETURNS: row[key] = 0
            if self.fault == "retain":
                self.header.update(state=5, allocated=1, cleanup_verified=0)
                self.rows["radix3"]["resources_retained"] = 1
            return b""
        raise AssertionError("Unexpected selector")

    def _close(self):
        self.close_calls += 1
        if self.fault == "close": raise OSError("Close reply failed")


class GSPDMAClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Sizes are intentionally small fake transport fixtures, not a firmware
        # package. Real package/ABI encoders are tested in test_gsp_package.py.
        counts = (258, 2, 1, 1, 2, 1, 1, 32, 2)
        cls.resources = {name: {"bytes": count * 4096, "pages": count}
                         for name, count in zip(client.NAMES, counts)}
        cls.buffers = {name: bytes([index + 1]) * cls.resources[name]["bytes"]
                       for index, name in enumerate(client.NAMES)}
        cls.selection = {"fuse_raw": 1, "signature_index": 0, "register": "0x824148",
                         "snapshot_sha256": "a" * 64, "signature_sha256": "b" * 64}
        cls.plan = {"host_resources": cls.resources, "booter_signature_selection": cls.selection}

    def setUp(self):
        self.prepare_patch = patch.object(client.prepare_gsp, "prepare",
                                         side_effect=lambda *a: (copy.deepcopy(self.plan), {}))
        self.bind_patch = patch.object(client.prepare_gsp, "bind", side_effect=self.bind)
        self.prepare_mock = self.prepare_patch.start()
        self.bind_mock = self.bind_patch.start()
        self.addCleanup(self.prepare_patch.stop)
        self.addCleanup(self.bind_patch.stop)

    def bind(self, firmware, bindings, snapshot):
        self.assertEqual(set(bindings), set(client.NAMES))
        result = copy.deepcopy(self.plan)
        result["bound_buffer_hashes"] = {name: hashlib.sha256(data).hexdigest() for name, data in self.buffers.items()}
        return result, self.buffers

    def run_fake(self, fault=None):
        backend = FakeBackend(self.resources, fault)
        result = client.run_binding(backend, "fake-firmware", "fake-snapshot")
        return backend, result

    def failure(self, fault):
        backend = FakeBackend(self.resources, fault)
        with self.assertRaises(client.BindingError) as caught:
            client.run_binding(backend, "fake-firmware", "fake-snapshot")
        self.assertFalse(caught.exception.report["passed"])
        self.assertTrue(backend.closed)
        self.assertEqual(backend.close_calls, 1)
        self.assertGreaterEqual(backend.abort_calls, 1)
        self.assertEqual(backend.begin_calls, 1)
        return backend, caught.exception.report

    def test_complete_binding_uses_native_pages_bounded_calls_hashes_and_releases(self):
        backend, report = self.run_fake()
        self.assertTrue(report["passed"])
        self.assertTrue(report["cleanup_verified"])
        self.assertFalse(report["native_allocations_live"])
        self.assertTrue(report["bindings_are_historical"])
        self.assertTrue(report["native_dma_prepared_during_transaction"])
        self.assertEqual(report["bindings"], backend.addresses)
        self.assertEqual(report["snapshots"]["finished"]["state"], 3)
        self.assertEqual(backend.abort_calls, 1)
        self.assertEqual(backend.close_calls, 1)
        for key in ("firmware_executed", "dma_transfer_executed", "reset_executed",
                    "gpu_commands_submitted", "ready_for_hardware_boot", "metal_supported"):
            self.assertIs(report[key], False)
        for selector, scalars, input_size, output_size in backend.calls:
            self.assertLessEqual(input_size, 4096)
            self.assertLessEqual(output_size, 4096)
            if selector == 2: self.assertLessEqual(scalars[2], 256)
        radix_batches = [call[1] for call in backend.calls if call[0] == 2 and call[1][0] == 0]
        self.assertEqual(radix_batches, [(0, 0, 256), (0, 256, 2)])
        self.assertEqual(set(report["buffer_checks"]), set(client.NAMES))
        self.assertTrue(all(v["passed"] for v in report["buffer_checks"].values()))

    def test_alias_alignment_and_address_width_fail_before_any_upload(self):
        for fault in ("alias", "unaligned", "page_overflow", "pages_partial"):
            with self.subTest(fault=fault):
                backend, report = self.failure(fault)
                self.assertNotIn(3, [c[0] for c in backend.calls])
                self.assertTrue(report["cleanup_verified"])

    def test_initial_abi_fuse_pci_geometry_and_prepare_fail_closed(self):
        for fault in ("fuse", "pci", "geometry", "prepare", "info_partial", "begin"):
            with self.subTest(fault=fault):
                backend, report = self.failure(fault)
                self.assertNotIn(3, [c[0] for c in backend.calls])
                self.assertFalse(report["ready_for_hardware_boot"])

    def test_mutation_ipc_failures_are_not_retried(self):
        for fault, selector in (("write", 3), ("publish", 4), ("finish", 6)):
            with self.subTest(fault=fault):
                backend, report = self.failure(fault)
                self.assertEqual(sum(c[0] == selector for c in backend.calls), 1)
                self.assertTrue(report["cleanup_verified"])

    def test_sync_generation_and_partial_or_corrupt_reads_cannot_finish(self):
        for fault in ("sync", "generation", "read_partial", "read_corrupt"):
            with self.subTest(fault=fault):
                backend, report = self.failure(fault)
                self.assertNotIn(6, [c[0] for c in backend.calls])
                self.assertTrue(report["cleanup_verified"])

    def test_cleanup_return_failure_cannot_claim_success(self):
        backend, report = self.failure("cleanup_return")
        self.assertIn("cleanup IOReturn", report["error"])
        self.assertFalse(report["cleanup_verified"])
        self.assertEqual(backend.abort_calls, 2)

    def test_close_error_is_reported_without_claiming_cleanup_or_closed_connection(self):
        backend, report = self.failure("close")
        self.assertFalse(report["connection_closed"])
        self.assertFalse(report["cleanup_verified"])
        self.assertIsNone(report["native_allocations_live"])
        self.assertIn("Close", report["cleanup_errors"][0])

    def test_idempotent_abort_retried_only_during_close_after_first_abort_error(self):
        backend = FakeBackend(self.resources, "abort_once")
        self.prepare_mock.side_effect = ValueError("bad pin")
        with self.assertRaises(client.BindingError) as caught:
            client.run_binding(backend, "fake-firmware", "fake-snapshot")
        self.assertEqual(backend.begin_calls, 0)
        self.assertEqual(backend.abort_calls, 2)
        self.assertEqual(backend.close_calls, 1)
        self.assertFalse(caught.exception.report["cleanup_verified"])

    def test_retained_resource_state_never_becomes_verified_cleanup(self):
        backend = FakeBackend(self.resources, "retain")
        self.prepare_mock.side_effect = ValueError("bad pin")
        with self.assertRaises(client.BindingError) as caught:
            client.run_binding(backend, "fake-firmware", "fake-snapshot")
        report = caught.exception.report
        self.assertFalse(report["cleanup_verified"])
        self.assertIsNone(report["native_allocations_live"])
        self.assertTrue(report["connection_closed"])

    def test_changed_snapshot_between_prepare_and_bind_is_rejected(self):
        def changed(*args):
            report, buffers = self.bind(*args)
            report["booter_signature_selection"]["snapshot_sha256"] = "c" * 64
            return report, buffers
        self.bind_mock.side_effect = changed
        backend = FakeBackend(self.resources)
        with self.assertRaises(client.BindingError) as caught:
            client.run_binding(backend, "fake-firmware", "fake-snapshot")
        self.assertIn("evidence changed", str(caught.exception))
        self.assertNotIn(3, [c[0] for c in backend.calls])

    def test_fixed_transport_rejects_invalid_chunk_page_and_resource_arguments(self):
        backend = FakeBackend(self.resources)
        for action in (lambda: backend.pages(0, 0, 257), lambda: backend.pages(0, 0, 0),
                       lambda: backend.pages(True, 0, 1), lambda: backend.pages(9, 0, 1),
                       lambda: backend.write(0, 0, b""), lambda: backend.write(0, 0, bytes(4097)),
                       lambda: backend.write(0, 0, bytearray(1)), lambda: backend.read(0, 0, 4097),
                       lambda: backend.read(0, -1, 1), lambda: backend.read(0, 0, False)):
            with self.assertRaises(ValueError): action()
        self.assertEqual(backend.calls, [])

    def test_info_wire_parser_rejects_forged_headers_counts_and_non_u32_return_values(self):
        backend = FakeBackend(self.resources)
        words = list(struct.unpack("<160Q", backend.info()))
        for index, value in ((0, 0), (1, 2), (2, 6), (3, 10), (4, 8192), (5, 0x8241e0),
                             (9, 1), (10, 0), (11, 0), (12, 2), (15, 2), (16, 8),
                             (20, 2), (23, 1 << 32), (30, 2), (31, 2)):
            with self.subTest(word=index):
                mutated = words[:]; mutated[index] = value
                with self.assertRaises(ValueError): client.decode_info(struct.pack("<160Q", *mutated))
        with self.assertRaises(ValueError): client.decode_info(bytes(client.INFO_SIZE - 1))

    def test_live_backend_never_loads_ctypes_on_other_platforms(self):
        with patch.object(client.sys, "platform", "win32"), patch.object(client.ctypes, "CDLL") as library:
            with self.assertRaises(OSError): client.MacIOKitBackend()
            library.assert_not_called()

    def test_ctypes_call_uses_u64_scalars_exact_structure_counts_and_no_scalar_output(self):
        backend = object.__new__(client.MacIOKitBackend)
        backend.closed, backend.connection = False, 123
        captured = []
        class FakeIOKit:
            def IOConnectCallMethod(self, connection, selector, scalars, count, data, size,
                                    scalar_output, scalar_count, output, output_count):
                captured.append((connection, selector, list(scalars) if scalars else [],
                                 count, ctypes.string_at(data, size) if size else b"", scalar_output))
                if output is not None: ctypes.memset(output, 0x5a, output_count._obj.value)
                return 0
        backend.io = FakeIOKit()
        backend.write(2, 4096, b"binary\0payload")
        self.assertEqual(captured[-1], (123, 3, [2, 4096], 2, b"binary\0payload", None))
        self.assertEqual(backend.read(2, 0, 4096), b"Z" * 4096)
        def partial(*args):
            args[-1]._obj.value -= 1
            return 0
        backend.io.IOConnectCallMethod = partial
        with self.assertRaises(client.BindingError): backend.read(2, 0, 4096)

    def test_cli_existing_evidence_rejected_before_live_backend_open(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            path.write_bytes(b"preserve")
            with patch.object(client, "MacIOKitBackend") as backend:
                with self.assertRaises(FileExistsError):
                    client.main(["--fuse-snapshot", "fake", "--output", str(path)])
                backend.assert_not_called()
            self.assertEqual(path.read_bytes(), b"preserve")

    def test_close_is_idempotent_and_later_calls_fail(self):
        backend = FakeBackend(self.resources)
        backend.close(); backend.close()
        self.assertEqual(backend.abort_calls, 1)
        self.assertEqual(backend.close_calls, 1)
        with self.assertRaises(client.BindingError): backend.info()


if __name__ == "__main__":
    unittest.main()
