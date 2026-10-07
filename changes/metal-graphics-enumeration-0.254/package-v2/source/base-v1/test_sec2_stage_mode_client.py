"""SEC2 staging fault tests with a fake backend; no IOKit or device calls."""
import copy
import ctypes
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import gsp_dma_client as dma
import sec2_stage_mode_client as client
from test_gsp_dma_client import FakeBackend


def success_status(riscv=False):
    result = dict.fromkeys(client.STAGE_FIELDS, 0)
    result.update(magic=client.STAGE_MAGIC, abi=2, snapshot_count=14)
    for key in client.STAGE_BOOL_FIELDS:
        if key not in ("has_riscv", "unsafe"): result[key] = 1
    result.update(command_before=0, command_enabled=6, command_after=0, current_command=0,
                  initial_mask=0x3F3F, final_mask=0x3F3F, initial_reads=12, final_reads=12,
                  hwcfg=0xC500, reset_count=2, reset_polls=4, drain_polls=2, dma_polls=705,
                  synchronize_count=1, canary_matched=client.DMEM_WORDS,
                  imem_submitted=client.IMEM_BLOCKS, imem_completed=client.IMEM_BLOCKS,
                  dmem_submitted=client.DMEM_BLOCKS, dmem_completed=client.DMEM_BLOCKS,
                  dmem_reads=client.DMEM_WORDS, dmem_matched=client.DMEM_WORDS,
                  mismatch_word=0xFFFFFFFF, session_state=2, generation=77)
    registers = dict.fromkeys(client.REGISTERS, 0)
    registers.update(hwcfg=0xC500, cpuctl=0x10, dmactl=1, dmacmd=2, transcfg=5)
    result["initial"] = registers.copy()
    result["final"] = registers.copy()
    result["baseline"] = registers.copy()
    result.update(baseline_mask=0x3F3F, baseline_reads=12, baseline_verified=1,
                  mode_init_attempted=0, mode_init_verified=0, mode_check_reads=0)
    result["mode_check"] = dict.fromkeys(("engine", "hwcfg2", "hwcfg", "bcr", "riscvcpu"), 0)
    if riscv:
        bootstrap = {"engine": 0, "hwcfg2": 0x67F7, "hwcfg": 0x80420100, "bcr": 0x110, "riscvcpu": 0x10}
        result["initial"] = dict.fromkeys(client.REGISTERS, 0xBADF5620)
        result["initial"].update(bootstrap)
        result.update(has_riscv=1, initial_mask=0xC7, initial_reads=14, final_mask=0x3FFF, final_reads=14,
                      baseline_mask=0x3FFF, baseline_reads=14, mode_init_attempted=1, mode_init_verified=1,
                      mode_check_reads=5, mode_check=bootstrap)
        for phase in ("baseline", "final"):
            result[phase].update(hwcfg2=0x67F7, bcr=1, riscvcpu=0x10, cpuctl=0)
    return result


def pack_status(status):
    words = [status[key] for key in client.STAGE_FIELDS]
    words += [status["initial"][key] for key in client.REGISTERS]
    words += [status["final"][key] for key in client.REGISTERS]
    words += [status["generation"]]
    words += [status.get("baseline", {}).get(key, 0) for key in client.REGISTERS]
    words += [status.get(key, 0) for key in ("baseline_mask", "baseline_reads", "mode_init_attempted",
                                            "mode_init_verified", "baseline_verified")]
    words += [status.get("mode_check", {}).get(key, 0) for key in ("engine", "hwcfg2", "hwcfg", "bcr", "riscvcpu")]
    words += [status.get("mode_check_reads", 0)] + [0] * 58
    assert len(words) == 160
    return struct.pack("<160Q", *words)


class StageFake(FakeBackend):
    kind = "injected-SEC2-stage-test-backend"

    def __init__(self, resources, image, fault=None, host_fault=None):
        super().__init__(resources, host_fault)
        self.stage_fault = fault
        self.stage_calls = 0
        self.stage_status = None
        self.status_reads_after_stage = 0
        self.dmem = list(struct.unpack("<%dI" % client.DMEM_WORDS, image[client.DMEM_OFFSET:]))
        self.completions = [0x616] * client.IMEM_BLOCKS + [0x602] * client.DMEM_BLOCKS

    def _invoke(self, selector, scalars, data, output_size):
        if selector < 8:
            result = super()._invoke(selector, scalars, data, output_size)
            if selector == 7 and self.stage_fault == "retain" and self.stage_calls:
                self.header.update(state=5, allocated=1, published=1, cleanup_verified=0)
                self.rows["booter_load"]["resources_retained"] = 1
            return result
        self.calls.append((selector, scalars, len(data), output_size))
        if selector == 8:
            self.stage_calls += 1
            if self.stage_calls != 1: raise OSError("Stage was retried")
            if self.header["state"] != 2 or any(row["read_bytes"] != row["bytes"] for row in self.rows.values()):
                raise OSError("Stage before full host readback")
            self.stage_status = success_status(bool(self.stage_fault and self.stage_fault.startswith("riscv")))
            if self.stage_fault == "prefix":
                self.stage_status.update(passed=0, staged=0, dmem_reads=12, dmem_matched=11,
                                         mismatch_word=11, mismatch_value=self.dmem[11] ^ 1)
            if self.stage_fault == "retain":
                self.stage_status.update(passed=0, release_safe=0, unsafe=1)
            if self.stage_fault in ("stage_error", "prefix", "retain"):
                raise OSError("ambiguous or failed native Stage reply")
            return b""
        if selector == 9:
            if self.stage_status is None:
                result = dict.fromkeys(client.STAGE_FIELDS, 0)
                result.update(magic=client.STAGE_MAGIC, abi=2, snapshot_count=14,
                              provider_open=1, session_state=self.header["state"], generation=77)
                result["initial"] = result["final"] = dict.fromkeys(client.REGISTERS, 0)
                if self.stage_fault == "stale": result.update(attempted=1, completed=1)
                if self.stage_fault == "stale_hidden": result["canary_matched"] = 1
            else:
                self.status_reads_after_stage += 1
                result = copy.deepcopy(self.stage_status)
                if self.stage_fault == "status_magic": result["magic"] = 0
                if self.stage_fault == "generation": result["generation"] += 1
                if self.stage_fault == "status_drift" and self.status_reads_after_stage > 1:
                    result["dma_polls"] += 1
                if self.stage_fault == "raw_cleanup": result["final"]["dmabase"] = 0x1000000
                if self.stage_fault == "incomplete": result["completed"] = 0
                if self.stage_fault == "initial_not_halted": result["initial"]["cpuctl"] = 0
                if self.stage_fault == "initial_halted_start_latched": result["initial"]["cpuctl"] = 0x12
                if self.stage_fault == "riscv_unknown_bcr": result["initial"]["bcr"] = 0x111
                if self.stage_fault == "riscv_unknown_badf": result["initial"]["cpuctl"] = 0xBADF0000
                if self.stage_fault == "riscv_second_mismatch": result["mode_check"]["hwcfg2"] ^= 1
                if self.stage_fault == "riscv_second_missing": result["mode_check_reads"] = 4
                if self.stage_fault == "riscv_invalid_baseline": result["baseline"]["cpuctl"] = 2
                if self.stage_fault == "riscv_restore_initial": result["final"]["transcfg"] = result["initial"]["transcfg"]
                if self.stage_fault == "riscv_missing_baseline_flag": result["baseline_verified"] = 0
                if self.stage_fault == "riscv_mode_unverified": result["mode_init_verified"] = 0
                if self.stage_fault == "riscv_baseline_mask_missing": result["baseline_mask"] &= ~(1 << 8)
                if self.stage_fault == "readable_riscv_selected":
                    result.update(has_riscv=1, initial_mask=0x3FFF, initial_reads=14,
                                  baseline_mask=0x3FFF, baseline_reads=14, final_mask=0x3FFF, final_reads=14)
                    for phase in ("initial", "baseline", "final"):
                        result[phase].update(hwcfg2=0x400, bcr=1, riscvcpu=0x10)
                    result["initial"]["bcr"] = 0x11
            return pack_status(result)
        if selector == 10:
            kind, first, count = scalars
            if not self.stage_status: raise OSError("No stage result")
            values = self.dmem[:self.stage_status["dmem_reads"]] if kind == 0 else self.completions
            if count > 1024 or not count or first + count > len(values):
                raise OSError("Native array bounds")
            output = list(values[first:first + count])
            if self.stage_fault == "dmem_corrupt" and kind == 0 and first + count == client.DMEM_WORDS:
                output[-1] ^= 1
            if self.stage_fault == "completion_corrupt" and kind == 1:
                output[-1] = 0
            raw = struct.pack("<%dI" % count, *output)
            if self.stage_fault == "array_partial" and kind == 0: raw = raw[:-1]
            return raw
        raise AssertionError("Unexpected stage selector")


class SEC2StageClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        counts = (258, 2, 1, 1, 2, 1, 1, 32, 15)
        cls.resources = {name: {"bytes": count * 4096, "pages": count}
                         for name, count in zip(dma.NAMES, counts)}
        cls.image = bytes((i * 17 + 3) & 255 for i in range(client.IMAGE_BYTES))
        cls.buffers = {name: bytes([index + 1]) * cls.resources[name]["bytes"]
                       for index, name in enumerate(dma.NAMES)}
        cls.buffers["booter_load"] = cls.image + bytes(61440 - len(cls.image))
        cls.selection = {"fuse_raw": 1, "signature_index": 0, "register": "0x824148",
                         "snapshot_sha256": "a" * 64, "signature_sha256": "b" * 64}
        cls.plan = {"host_resources": cls.resources, "booter_signature_selection": cls.selection}

    def setUp(self):
        self.prepare_patch = patch.object(dma.prepare_gsp, "prepare", side_effect=lambda *args:
            (copy.deepcopy(self.plan), {"booter-payload-selected.bin": self.image}))
        self.bind_patch = patch.object(dma.prepare_gsp, "bind", side_effect=self.bind)
        self.prepare_mock = self.prepare_patch.start(); self.bind_mock = self.bind_patch.start()
        self.addCleanup(self.prepare_patch.stop); self.addCleanup(self.bind_patch.stop)

    def bind(self, *args):
        report = copy.deepcopy(self.plan)
        report["bound_buffer_hashes"] = {name: hashlib.sha256(data).hexdigest() for name, data in self.buffers.items()}
        return report, self.buffers

    def run_fake(self, fault=None, host_fault=None):
        backend = StageFake(self.resources, self.image, fault, host_fault)
        return backend, client.run_staging(backend, "fixture", "fuse")

    def failure(self, fault=None, host_fault=None):
        backend = StageFake(self.resources, self.image, fault, host_fault)
        with self.assertRaises(client.BindingError) as caught:
            client.run_staging(backend, "fixture", "fuse")
        self.assertEqual(backend.close_calls, 1)
        self.assertTrue(backend.closed)
        self.assertFalse(caught.exception.report["passed"])
        self.assertEqual(caught.exception.report["probe_version"], "0.12.1")
        return backend, caught.exception.report

    def test_success_stages_once_after_all_reads_and_finishes_after_all_diagnostics(self):
        backend, report = self.run_fake()
        self.assertTrue(report["passed"])
        self.assertTrue(report["sec2_stage"]["verification"]["passed"])
        self.assertEqual(report["schema"], "ga106-sec2-mode-stage-v1")
        self.assertEqual(report["probe_version"], "0.12.1")
        self.assertEqual(backend.stage_calls, 1)
        self.assertEqual(report["sec2_stage"]["dmem_words"], backend.dmem)
        self.assertEqual(report["sec2_stage"]["dma_completions"], backend.completions)
        selectors = [call[0] for call in backend.calls]
        self.assertLess(max(i for i, selector in enumerate(selectors) if selector == 5), selectors.index(8))
        self.assertLess(max(i for i, selector in enumerate(selectors) if selector == 10), selectors.index(6))
        self.assertEqual(selectors.count(6), 1)
        for key in ("dma_transfer_executed", "reset_executed", "bus_master_enabled", "gpu_commands_submitted"):
            self.assertIs(report[key], True)
        for key in ("firmware_executed", "gsp_init_done_observed", "ready_for_hardware_boot"):
            self.assertIs(report[key], False)
        self.assertTrue(report["cleanup_verified"])
        self.assertFalse(report["native_allocations_live"])
        for selector, scalars, _, output_size in backend.calls:
            self.assertLessEqual(output_size, 4096)
            if selector == 10: self.assertLessEqual(scalars[2], 1024)

    def test_host_failure_never_reaches_stage(self):
        backend, report = self.failure(host_fault="read_corrupt")
        self.assertEqual(backend.stage_calls, 0)
        self.assertFalse(report["sec2_stage"]["stage_selector_attempted"])
        self.assertFalse(report["dma_transfer_executed"])

    def test_halted_initial_cpu_with_latched_start_matches_native_gate(self):
        backend, report = self.run_fake("initial_halted_start_latched")
        self.assertTrue(report["passed"])
        self.assertEqual(backend.stage_calls, 1)
        self.assertEqual(report["sec2_stage"]["status_after"]["initial"]["cpuctl"], 0x12)

    def test_exact_observed_riscv_bootstrap_uses_second_check_and_new_falcon_baseline(self):
        backend, report = self.run_fake("riscv_bootstrap")
        self.assertTrue(report["passed"])
        self.assertEqual(backend.stage_calls, 1)
        status = report["sec2_stage"]["status_after"]
        self.assertEqual(status["initial"]["transcfg"], 0xBADF5620)
        self.assertEqual(status["final"]["transcfg"], status["baseline"]["transcfg"])
        self.assertEqual(status["mode_check_reads"], 5)
        self.assertTrue(report["mode_init_verified"])

    def test_readable_halted_falcon_branch_preserves_native_latched_riscv_selector_tolerance(self):
        _, report = self.run_fake("readable_riscv_selected")
        self.assertTrue(report["passed"])
        self.assertFalse(report["mode_init_attempted"])
        self.assertEqual(report["sec2_stage"]["status_after"]["initial"]["bcr"], 0x11)

    def test_unknown_riscv_mode_unverified_second_check_or_missing_baseline_prevents_finish(self):
        for fault in ("riscv_unknown_bcr", "riscv_unknown_badf", "riscv_second_mismatch", "riscv_second_missing",
                      "riscv_invalid_baseline", "riscv_restore_initial", "riscv_missing_baseline_flag",
                      "riscv_mode_unverified", "riscv_baseline_mask_missing"):
            with self.subTest(fault=fault):
                backend, report = self.failure(fault)
                self.assertNotIn(6, [call[0] for call in backend.calls])
                self.assertFalse(report["sec2_stage"]["passed"])

    def test_stale_stage_and_hidden_old_result_rejected_before_command(self):
        for fault in ("stale", "stale_hidden"):
            with self.subTest(fault=fault):
                backend, _ = self.failure(fault)
                self.assertEqual(backend.stage_calls, 0)
                self.assertNotIn(6, [call[0] for call in backend.calls])

    def test_ambiguous_stage_reply_is_not_retried_and_still_captures_all_raw_evidence(self):
        backend, report = self.failure("stage_error")
        self.assertEqual(backend.stage_calls, 1)
        self.assertNotIn(6, [call[0] for call in backend.calls])
        self.assertEqual(report["sec2_stage"]["dmem_words"], backend.dmem)
        self.assertEqual(report["sec2_stage"]["dma_completions"], backend.completions)
        self.assertIn("stage_error", report["sec2_stage"])
        self.assertTrue(report["dma_transfer_executed"])

    def test_failed_stage_exports_only_native_dmem_prefix_and_all_completions(self):
        backend, report = self.failure("prefix")
        self.assertEqual(report["sec2_stage"]["dmem_words"], backend.dmem[:12])
        self.assertEqual(len(report["sec2_stage"]["dma_completions"]), client.BLOCKS)
        self.assertNotIn(6, [call[0] for call in backend.calls])
        self.assertFalse(report["sec2_stage"]["capture_errors"])

    def test_bad_pio_completions_or_raw_cleanup_never_calls_finish(self):
        for fault in ("dmem_corrupt", "completion_corrupt", "raw_cleanup", "incomplete", "generation", "initial_not_halted"):
            with self.subTest(fault=fault):
                backend, report = self.failure(fault)
                self.assertEqual(backend.stage_calls, 1)
                self.assertNotIn(6, [call[0] for call in backend.calls])
                self.assertFalse(report["sec2_stage"]["passed"])

    def test_partial_array_and_changed_status_preserve_capture_errors(self):
        for fault in ("array_partial", "status_drift"):
            with self.subTest(fault=fault):
                backend, report = self.failure(fault)
                self.assertTrue(report["sec2_stage"]["capture_errors"])
                self.assertEqual(len(report["sec2_stage"]["dma_completions"]), client.BLOCKS)
                self.assertNotIn(6, [call[0] for call in backend.calls])

    def test_unreadable_status_preserves_unknown_execution_extent_and_raw_bytes(self):
        backend, report = self.failure("status_magic")
        self.assertIsNone(report["dma_transfer_executed"])
        self.assertIsNone(report["reset_executed"])
        self.assertIn("status_after_hex", report["sec2_stage"])
        self.assertEqual(len(report["sec2_stage"]["dma_completions"]), client.BLOCKS)
        self.assertEqual(report["sec2_stage"]["dmem_words"], [])
        self.assertNotIn(6, [call[0] for call in backend.calls])

    def test_native_unsafe_retention_never_reports_freed_mappings(self):
        backend, report = self.failure("retain")
        self.assertFalse(report["cleanup_verified"])
        self.assertIsNone(report["native_allocations_live"])
        self.assertTrue(report["sec2_stage"]["status_after"]["unsafe"])
        self.assertNotIn(6, [call[0] for call in backend.calls])

    def test_finish_failure_is_not_retried_and_preserves_successful_stage_proof(self):
        backend, report = self.failure(host_fault="finish")
        self.assertEqual(backend.stage_calls, 1)
        self.assertEqual(sum(call[0] == 6 for call in backend.calls), 1)
        self.assertTrue(report["sec2_stage"]["passed"])
        self.assertFalse(report["passed"])

    def test_bad_selected_image_closes_connection_before_begin(self):
        for error in (ValueError("bad pin"), client.BindingError("bad package")):
            self.prepare_mock.side_effect = error
            backend, report = self.failure()
            self.assertEqual(backend.begin_calls, 0)
            self.assertFalse(report["sec2_stage"]["stage_selector_attempted"])

    def test_decoder_rejects_truncation_reserved_fields_and_oversized_read_extent(self):
        raw = pack_status(success_status())
        words = list(struct.unpack("<160Q", raw))
        for index, value in ((0, 0), (1, 1), (2, 2), (37, client.DMEM_WORDS + 1),
                             (47, 13), (48, 1 << 32), (76, 0), (77, 1 << 32),
                             (91, 1 << 14), (92, 15), (95, 2), (101, 6), (102, 1)):
            with self.subTest(word=index):
                changed = words[:]; changed[index] = value
                with self.assertRaises(ValueError): client.decode_stage_info(struct.pack("<160Q", *changed))
        with self.assertRaises(ValueError): client.decode_stage_info(raw[:-1])

    def test_live_backend_does_not_load_iokit_on_windows(self):
        with patch.object(client.sys, "platform", "win32"), patch.object(client.ctypes, "CDLL") as library:
            with self.assertRaises(OSError): client.MacIOKitBackend()
            library.assert_not_called()

    def test_ctypes_stage_status_arrays_use_exact_abi_and_reject_selector_11(self):
        backend = object.__new__(client.MacIOKitBackend)
        backend.closed, backend.connection = False, 123
        calls = []
        class IO:
            def IOConnectCallMethod(self, connection, selector, scalars, count, data, size,
                                    scalar_output, scalar_count, output, output_count):
                calls.append((selector, list(scalars) if scalars else [], output_count._obj.value))
                if output is not None: ctypes.memset(output, 0, output_count._obj.value)
                return 0
        backend.io = IO()
        backend._request(8)
        backend._request(9, output_size=1280)
        backend._request(10, (0, 1024, 1024), output_size=4096)
        self.assertEqual(calls, [(8, [], 0), (9, [], 1280), (10, [0, 1024, 1024], 4096)])
        with self.assertRaises(ValueError): backend._request(11)

    def test_existing_evidence_file_rejected_before_connection_open(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            path.write_text("preserve", encoding="utf-8")
            with patch.object(client, "MacIOKitBackend") as backend:
                with self.assertRaises(FileExistsError):
                    client.main(["--fuse-snapshot", "fixture", "--output", str(path)])
                backend.assert_not_called()
            self.assertEqual(path.read_text(encoding="utf-8"), "preserve")


if __name__ == "__main__":
    unittest.main()
