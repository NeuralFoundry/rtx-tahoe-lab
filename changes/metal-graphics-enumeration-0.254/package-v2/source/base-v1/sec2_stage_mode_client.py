"""RTXProbe 0.12.1 SEC2 staging client; imports IOKit only when explicitly run.

Reuses the frozen 0.11 host binding transaction, with a genuine 0.12 service
check and one additional stage/readback step before host mappings are released.
There is no CPU-start selector or automatic retry of a staging command.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

import gsp_dma_client as dma_client

BindingError = dma_client.BindingError
CHUNK = 4096
STAGE_INFO_SIZE = 1280
DMEM_OFFSET = 0x8A00
DMEM_BYTES = 0x6200
DMEM_WORDS = DMEM_BYTES // 4
IMEM_BLOCKS = 137
DMEM_BLOCKS = 98
BLOCKS = IMEM_BLOCKS + DMEM_BLOCKS
IMAGE_BYTES = 60416
MAX_ARRAY_WORDS = 1024
STAGE_MAGIC = 0x5345433253544731
STAGE_FIELDS = (
    "magic", "abi", "attempted", "passed", "release_safe", "completed", "has_riscv", "image_matched",
    "environment_matched", "cpu_intact", "staged", "quiescent", "targets_cleared", "final_verified",
    "any_dma", "reset_attempted", "memory_attempted", "master_attempted", "targets_attempted",
    "command_before", "command_enabled", "command_after", "initial_mask", "final_mask",
    "initial_reads", "final_reads", "hwcfg", "reset_count", "reset_polls", "drain_polls", "dma_polls",
    "synchronize_count", "canary_matched", "imem_submitted", "imem_completed",
    "dmem_submitted", "dmem_completed", "dmem_reads", "dmem_matched", "mismatch_word", "mismatch_value",
    "device_status_before", "device_status_after", "provider_open", "unsafe", "session_state",
    "current_command", "snapshot_count")
STAGE_BOOL_FIELDS = STAGE_FIELDS[2:19] + ("provider_open", "unsafe")
REGISTERS = ("engine", "hwcfg2", "hwcfg", "cpuctl", "dmactl", "dmacmd", "bcr", "riscvcpu",
             "transcfg", "fbifctl", "dmabase", "dmabase1", "dmaoffset", "fboffset")


def decode_stage_info(data):
    if type(data) is not bytes or len(data) != STAGE_INFO_SIZE:
        raise ValueError("SEC2 status must contain exactly 160 little-endian u64 words")
    words = struct.unpack("<160Q", data)
    result = dict(zip(STAGE_FIELDS, words[:48]))
    if (result["magic"], result["abi"], result["snapshot_count"]) != (STAGE_MAGIC, 2, 14):
        raise ValueError("Unsupported SEC2 staging status ABI")
    if any(words[102:]): raise ValueError("SEC2 status has unknown reserved fields")
    for key in STAGE_FIELDS[2:]:
        dma_client._integer(result[key], key, 0, 1 if key in STAGE_BOOL_FIELDS else 0xFFFFFFFF)
    if result["session_state"] not in dma_client.STATES or not words[76]:
        raise ValueError("Invalid SEC2 mapping generation or session state")
    for key, maximum in (("canary_matched", DMEM_WORDS), ("imem_submitted", IMEM_BLOCKS),
                         ("imem_completed", IMEM_BLOCKS), ("dmem_submitted", DMEM_BLOCKS),
                         ("dmem_completed", DMEM_BLOCKS), ("dmem_reads", DMEM_WORDS),
                         ("dmem_matched", DMEM_WORDS), ("initial_reads", 14), ("final_reads", 14)):
        dma_client._integer(result[key], key, 0, maximum)
    for key in ("initial_mask", "final_mask"):
        dma_client._integer(result[key], key, 0, (1 << 14) - 1)
    for key in ("command_before", "command_enabled", "command_after", "current_command",
                "device_status_before", "device_status_after"):
        dma_client._integer(result[key], key, 0, 0xFFFF)
    for word in words[48:76] + words[77:91] + words[96:101]:
        dma_client._integer(word, "SEC2 register value", 0, 0xFFFFFFFF)
    result["initial"] = dict(zip(REGISTERS, words[48:62]))
    result["final"] = dict(zip(REGISTERS, words[62:76]))
    result["generation"] = words[76]
    result["baseline"] = dict(zip(REGISTERS, words[77:91]))
    result["baseline_mask"], result["baseline_reads"] = words[91:93]
    for key, index in (("mode_init_attempted", 93), ("mode_init_verified", 94), ("baseline_verified", 95)):
        result[key] = dma_client._integer(words[index], key, 0, 1)
    dma_client._integer(result["baseline_mask"], "baseline mask", 0, (1 << 14) - 1)
    dma_client._integer(result["baseline_reads"], "baseline reads", 0, 14)
    result["mode_check"] = dict(zip(("engine", "hwcfg2", "hwcfg", "bcr", "riscvcpu"), words[96:101]))
    result["mode_check_reads"] = dma_client._integer(words[101], "mode check reads", 0, 5)
    result["raw_words"] = list(words)
    return result


def _readable(value):
    return value != 0xFFFFFFFF and value & 0xFFFF0000 not in (0xBADF0000, 0xBAD00000)


def verify_stage(status, baseline, expected_image, dmem, completions):
    """Validate raw lifecycle evidence and every returned DMEM/completion word."""
    if type(expected_image) is not bytes or len(expected_image) != IMAGE_BYTES:
        raise ValueError("The selected booter must contain exactly 60,416 bytes")
    if status["generation"] != baseline["generation"]:
        raise ValueError("SEC2 staging crossed mapping generations")
    true_fields = ("attempted", "completed", "passed", "release_safe", "image_matched",
                   "environment_matched", "cpu_intact", "staged", "quiescent", "targets_cleared",
                   "final_verified", "any_dma", "reset_attempted", "memory_attempted",
                   "master_attempted", "targets_attempted", "provider_open", "baseline_verified")
    if any(status[key] != 1 for key in true_fields):
        raise ValueError("SEC2 staging or cleanup evidence is incomplete")
    if (status["unsafe"], status["session_state"], status["current_command"], status["command_before"],
            status["command_enabled"], status["command_after"]) != (0, 2, 0, 0, 6, 0):
        raise ValueError("SEC2 did not restore PCI state while retaining its host mappings")
    counts = {"imem_submitted": IMEM_BLOCKS, "imem_completed": IMEM_BLOCKS,
              "dmem_submitted": DMEM_BLOCKS, "dmem_completed": DMEM_BLOCKS,
              "canary_matched": DMEM_WORDS, "dmem_reads": DMEM_WORDS, "dmem_matched": DMEM_WORDS,
              "reset_count": 2, "synchronize_count": 1, "mismatch_word": 0xFFFFFFFF, "mismatch_value": 0}
    if any(status[key] != expected for key, expected in counts.items()):
        raise ValueError("SEC2 staging counts differ from the complete pinned image")
    required_mask = ((1 << 14) - 1) & (~0 if status["has_riscv"] else ~((1 << 6) | (1 << 7)))
    required_reads = bin(required_mask).count("1")
    if any(status[key] != required_mask for key in ("baseline_mask", "final_mask")) or any(
            status[key] != required_reads for key in ("baseline_reads", "final_reads")):
        raise ValueError("Incomplete SEC2 register snapshots")
    initial, baseline_regs, final = status["initial"], status["baseline"], status["final"]
    if status["mode_init_attempted"]:
        bootstrap = {"engine": 0, "hwcfg2": 0x67F7, "hwcfg": 0x80420100, "bcr": 0x110, "riscvcpu": 0x10}
        expected_initial = dict.fromkeys(REGISTERS, 0xBADF5620)
        expected_initial.update(bootstrap)
        if (status["has_riscv"], status["mode_init_verified"], status["initial_mask"], status["initial_reads"]) != (1, 1, 0xC7, 14):
            raise ValueError("SEC2 mode initialization lacks its exact initial profile")
        if initial != expected_initial:
            raise ValueError("Unsupported initial SEC2 RISC-V bootstrap profile")
        if status["mode_check_reads"] != 5 or status["mode_check"] != bootstrap:
            raise ValueError("SEC2 RISC-V profile was not independently rechecked before reset")
        readable_phases = ("baseline", "final")
    else:
        if status["mode_init_verified"] or status["mode_check_reads"] or any(status["mode_check"].values()):
            raise ValueError("Unexpected mode-initialization result for an accessible Falcon")
        if status["initial_mask"] != required_mask or status["initial_reads"] != required_reads:
            raise ValueError("Incomplete initial Falcon snapshot")
        if (initial["engine"] & 1 or initial["hwcfg2"] & 0x1000 or initial["dmactl"] & 6 or
                initial["dmacmd"] & 3 != 2 or not initial["cpuctl"] & 0x10 or
                (status["has_riscv"] and initial["riscvcpu"] & 0x80)):
            raise ValueError("SEC2 initial engine state was not idle in Falcon mode")
        readable_phases = ("initial", "baseline", "final")
    for phase in readable_phases:
        values = status[phase]
        for index, name in enumerate(REGISTERS):
            if required_mask & (1 << index):
                if not _readable(values[name]): raise ValueError("Unreadable SEC2 register: " + name)
            elif values[name] != 0: raise ValueError("SEC2 reported an absent RISC-V register")
        if bool(values["hwcfg2"] & 0x400) != bool(status["has_riscv"]):
            raise ValueError("SEC2 architecture changed during staging")
    if (baseline_regs["engine"] & 1 or baseline_regs["hwcfg2"] & 0x1000 or baseline_regs["dmactl"] & 6 or
            baseline_regs["dmacmd"] & 3 != 2 or baseline_regs["cpuctl"] & 2 or
            (status["has_riscv"] and (baseline_regs["riscvcpu"] & 0x80 or baseline_regs["bcr"] & 0x10))):
        raise ValueError("SEC2 post-reset Falcon baseline is not independently verified")
    if (final["engine"] & 1 or final["hwcfg2"] & 0x1000 or final["cpuctl"] & 2 or
            final["dmacmd"] & 3 != 2 or final["dmactl"] & 7 != 1 or final["fbifctl"] & 0x80 or
            any(final[key] for key in ("dmabase", "dmabase1", "dmaoffset", "fboffset")) or
            final["transcfg"] != baseline_regs["transcfg"] or
            (status["has_riscv"] and (final["riscvcpu"] & 0x80 or final["bcr"] & 0x10))):
        raise ValueError("SEC2 reset/target cleanup is not independently verified")
    for key in ("device_status_before", "device_status_after"):
        if status[key] == 0xFFFF or status[key] & 0x20:
            raise ValueError("PCIe transactions were not drained")
    if not _readable(status["hwcfg"]) or (status["hwcfg"] & 0x1FF) << 8 < 0x8900 or (
            status["hwcfg"] & 0x3FE00) >> 1 < DMEM_BYTES:
        raise ValueError("SEC2 memory capacity is insufficient")
    if status["dma_polls"] < 3 * BLOCKS or status["reset_polls"] < 4 or status["drain_polls"] < 2:
        raise ValueError("SEC2 polling evidence is incomplete")
    if len(completions) != BLOCKS or any(not _readable(value) or value & 3 != 2 for value in completions):
        raise ValueError("SEC2 completion array is incomplete or contains failed DMA commands")
    expected = expected_image[DMEM_OFFSET:DMEM_OFFSET + DMEM_BYTES]
    if len(dmem) != DMEM_WORDS or tuple(dmem) != struct.unpack("<%dI" % DMEM_WORDS, expected):
        raise ValueError("SEC2 PIO DMEM readback differs from the pinned selected booter")
    return {"passed": True, "dmem_words_checked": DMEM_WORDS, "dma_completions_checked": BLOCKS,
            "dmem_sha256": hashlib.sha256(struct.pack("<%dI" % DMEM_WORDS, *dmem)).hexdigest(),
            "selected_image_sha256": hashlib.sha256(expected_image).hexdigest(),
            "firmware_executed": False, "gsp_init_done_observed": False}


class MacIOKitBackend(dma_client.RestrictedBackend):
    """Separate version check and selectors 0..10; never opens a 0.11 service."""
    kind = "macOS-IOKit-RTXProbe-0.12.1"

    def __init__(self):
        if sys.platform != "darwin":
            raise OSError("The live IOKit backend is available only on macOS")
        if os.geteuid() != 0:
            raise PermissionError("RTXProbe 0.12.1 user client requires an administrator/root process")
        self.closed = False
        self.connection = 0
        self.io = ctypes.CDLL("/System/Library/Frameworks/IOKit.framework/IOKit")
        self.cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
        self.system = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
        u32, u64, ptr, size = ctypes.c_uint32, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t
        def prototype(lib, name, args, result):
            function = getattr(lib, name); function.argtypes = args; function.restype = result
        prototype(self.io, "IOServiceMatching", [ctypes.c_char_p], ptr)
        prototype(self.io, "IOServiceGetMatchingServices", [u32, ptr, ctypes.POINTER(u32)], ctypes.c_int32)
        prototype(self.io, "IOIteratorNext", [u32], u32)
        prototype(self.io, "IOObjectRelease", [u32], ctypes.c_int32)
        prototype(self.io, "IORegistryEntryCreateCFProperty", [u32, ptr, ptr, u32], ptr)
        prototype(self.io, "IOServiceOpen", [u32, u32, u32, ctypes.POINTER(u32)], ctypes.c_int32)
        prototype(self.io, "IOServiceClose", [u32], ctypes.c_int32)
        prototype(self.io, "IOConnectCallMethod", [u32, u32, ctypes.POINTER(u64), u32, ptr, size,
                  ctypes.POINTER(u64), ctypes.POINTER(u32), ptr, ctypes.POINTER(size)], ctypes.c_int32)
        prototype(self.cf, "CFStringCreateWithCString", [ptr, ctypes.c_char_p, u32], ptr)
        prototype(self.cf, "CFGetTypeID", [ptr], ctypes.c_ulong)
        prototype(self.cf, "CFStringGetTypeID", [], ctypes.c_ulong)
        prototype(self.cf, "CFStringGetCString", [ptr, ptr, ctypes.c_long, u32], ctypes.c_bool)
        prototype(self.cf, "CFRelease", [ptr], None)
        iterator, services = u32(), []
        matching = self.io.IOServiceMatching(b"RTXProbe")
        if not matching:
            raise BindingError("IOServiceMatching returned no dictionary")
        self._check(self.io.IOServiceGetMatchingServices(0, matching, ctypes.byref(iterator)), "matching")
        try:
            while True:
                service = self.io.IOIteratorNext(iterator.value)
                if not service: break
                services.append(service)
                if len(services) > 16: raise BindingError("Unexpected RTXProbe service count")
            if len(services) != 1 or self._string_property(services[0], "ProbeVersion") != "0.12.1":
                raise BindingError("Exactly one RTXProbe 0.12.1 service is required")
            connection = u32()
            task = u32.in_dll(self.system, "mach_task_self_").value
            self._check(self.io.IOServiceOpen(services[0], task, 0, ctypes.byref(connection)), "open")
            self.connection = connection.value
            if not self.connection: raise BindingError("IOServiceOpen returned a null connection")
        finally:
            for service in services: self.io.IOObjectRelease(service)
            self.io.IOObjectRelease(iterator.value)

    @staticmethod
    def _check(result, operation):
        if result != 0:
            raise BindingError("IOKit " + operation + " failed: 0x%08x" % (result & 0xffffffff))

    def _string_property(self, service, name):
        key = self.cf.CFStringCreateWithCString(None, name.encode("ascii"), 0x08000100)
        if not key: raise BindingError("CFString allocation failed")
        value = None
        try:
            value = self.io.IORegistryEntryCreateCFProperty(service, key, None, 0)
            if not value or self.cf.CFGetTypeID(value) != self.cf.CFStringGetTypeID():
                raise BindingError("Missing string property: " + name)
            buffer = ctypes.create_string_buffer(128)
            if not self.cf.CFStringGetCString(value, buffer, len(buffer), 0x08000100):
                raise BindingError("Invalid string property: " + name)
            return buffer.value.decode("utf-8")
        finally:
            if value: self.cf.CFRelease(value)
            self.cf.CFRelease(key)

    def _invoke(self, selector, scalars, data, output_size):
        if selector not in range(11) or len(scalars) > 3 or len(data) > CHUNK or output_size > CHUNK:
            raise ValueError("Request exceeds restricted 0.12.1 transport ABI")
        scalar_input = (ctypes.c_uint64 * len(scalars))(*scalars) if scalars else None
        structure_input = ctypes.create_string_buffer(data, len(data)) if data else None
        structure_output = ctypes.create_string_buffer(output_size) if output_size else None
        count = ctypes.c_size_t(output_size)
        scalar_count = ctypes.c_uint32(0)
        result = self.io.IOConnectCallMethod(self.connection, selector, scalar_input, len(scalars),
                    structure_input, len(data), None, ctypes.byref(scalar_count),
                    structure_output, ctypes.byref(count))
        self._check(result, "selector " + str(selector))
        if scalar_count.value != 0 or count.value != output_size:
            raise BindingError("IOKit returned unexpected output counts")
        return structure_output.raw if structure_output is not None else b""

    def _close(self):
        connection, self.connection = self.connection, 0
        if connection: self._check(self.io.IOServiceClose(connection), "close")


class StageTransaction:
    """Proxy 0..7 to a native backend; replace Finish with stage/verify/Finish."""
    def __init__(self, backend, expected_image):
        if type(expected_image) is not bytes or len(expected_image) != IMAGE_BYTES:
            raise ValueError("The selected booter must contain exactly 60,416 bytes")
        self.backend = backend
        self.kind = backend.kind
        self.expected_image = expected_image
        self.stage_attempted = False
        self.finish_attempted = False
        self.report = {"passed": False, "stage_selector_attempted": False,
                       "capture_errors": [], "dmem_words": [], "dma_completions": [],
                       "firmware_executed": False}

    def __getattr__(self, name):
        if name in ("begin", "info", "pages", "write", "publish", "read", "abort", "close"):
            return getattr(self.backend, name)
        raise AttributeError(name)

    def _status(self, key):
        raw = self.backend._request(9, output_size=STAGE_INFO_SIZE)
        self.report[key + "_hex"] = raw.hex()
        decoded = decode_stage_info(raw)
        self.report[key] = decoded
        return decoded

    def _array(self, kind, count, key):
        dma_client._integer(kind, "SEC2 array kind", 0, 1)
        dma_client._integer(count, "SEC2 array count", 0, DMEM_WORDS if kind == 0 else BLOCKS)
        output = self.report[key]
        for first in range(0, count, MAX_ARRAY_WORDS):
            batch = min(MAX_ARRAY_WORDS, count - first)
            raw = self.backend._request(10, (kind, first, batch), output_size=batch * 4)
            output.extend(struct.unpack("<%dI" % batch, raw))

    def _capture(self):
        status = None
        try:
            status = self._status("status_after")
        except BaseException as error:
            self.report["capture_errors"].append("Status: " + type(error).__name__ + ": " + str(error))
        # A failed stage may have read only a DMEM prefix. The native array API
        # exports exactly that prefix; never invent a longer readable extent.
        if status is not None:
            try:
                self._array(0, status["dmem_reads"], "dmem_words")
            except BaseException as error:
                self.report["capture_errors"].append("DMEM: " + type(error).__name__ + ": " + str(error))
        else:
            self.report["capture_errors"].append("DMEM: readable extent unavailable without valid status")
        try:
            self._array(1, BLOCKS, "dma_completions")
        except BaseException as error:
            self.report["capture_errors"].append("Completions: " + type(error).__name__ + ": " + str(error))
        try:
            final = self._status("status_after_arrays")
            if status is not None and final["raw_words"] != status["raw_words"]:
                raise ValueError("SEC2 status changed during diagnostic readback")
        except BaseException as error:
            self.report["capture_errors"].append("Final status: " + type(error).__name__ + ": " + str(error))
        return status

    def finish(self):
        if self.finish_attempted:
            raise BindingError("SEC2 Stage/Finish cannot be retried")
        self.finish_attempted = True
        baseline = dma_client.decode_info(self.backend.info())
        self.report["mapping_generation"] = baseline["generation"]
        if (baseline["state"], baseline["allocated"], baseline["published"], baseline["cleanup_verified"]) != (2, 1, 1, 0):
            raise BindingError("SEC2 stage requires live published allocations")
        if any(row["read_bytes"] != row["bytes"] for row in baseline["resources"].values()):
            raise BindingError("SEC2 stage requires full host readback first")
        before = self._status("status_before")
        if (before["attempted"], before["completed"], before["provider_open"], before["unsafe"],
                before["session_state"], before["current_command"], before["generation"]) != (0, 0, 1, 0, 2, 0, baseline["generation"]):
            raise BindingError("SEC2 stage state is stale or belongs to another allocation")
        if any(before["raw_words"][2:43]) or any(before["raw_words"][48:76]) or any(before["raw_words"][77:102]):
            raise BindingError("SEC2 status contains a previous stage result")
        stage_error = None
        self.stage_attempted = self.report["stage_selector_attempted"] = True
        try:
            self.backend._request(8)  # Exactly once, even if its reply is lost.
        except BaseException as error:
            stage_error = error
            self.report["stage_error"] = type(error).__name__ + ": " + str(error)
        finally:
            status = self._capture()
        if stage_error is not None:
            raise stage_error
        if self.report["capture_errors"] or status is None:
            raise BindingError("SEC2 diagnostic capture is incomplete")
        self.report["verification"] = verify_stage(status, baseline, self.expected_image,
            self.report["dmem_words"], self.report["dma_completions"])
        self.report["passed"] = True
        self.backend.finish()  # Ordinary native host cleanup only after proof.


def _correct_report(report, stage):
    report = dict(report or {})
    status = stage.report.get("status_after")
    def observed(field):
        return bool(status[field]) if status is not None else (None if stage.stage_attempted else False)
    report.update(schema="ga106-sec2-mode-stage-v1", probe_version="0.12.1", sec2_stage=stage.report,
                  gpu_commands_submitted=observed("any_dma"), dma_transfer_executed=observed("any_dma"),
                  reset_executed=observed("reset_attempted"), bus_master_enabled=observed("master_attempted"),
                  firmware_executed=False, gsp_init_done_observed=False, ready_for_hardware_boot=False,
                  sec2_staging_verified=stage.report["passed"], mode_init_attempted=observed("mode_init_attempted"),
                  mode_init_verified=observed("mode_init_verified"))
    return report


def run_staging(backend, firmware, fuse_snapshot):
    """One fresh host-binding transaction plus one SEC2 stage-only experiment."""
    stage = None
    try:
        plan, files = dma_client.prepare_gsp.prepare(firmware, fuse_snapshot)
        selection = plan.get("booter_signature_selection")
        if not selection or (selection.get("fuse_raw"), selection.get("signature_index"),
                selection.get("register")) != (1, 0, "0x824148"):
            raise ValueError("A verified fuse=1/index=0 SEC2 selection is required")
        expected = files.get("booter-payload-selected.bin")
        stage = StageTransaction(backend, expected)
        del files
        result = dma_client.run_binding(stage, firmware, fuse_snapshot)
        return _correct_report(result, stage)
    except BaseException as error:
        # Once the frozen binding transaction runs, it owns Abort/Close even on
        # failure. Only a preparation failure before that point needs closing.
        if stage is not None:
            if isinstance(error, BindingError):
                error.report = _correct_report(error.report, stage)
            raise
        cleanup_errors = []
        try:
            backend.close()
        except BaseException as close_error:
            cleanup_errors.append(type(close_error).__name__ + ": " + str(close_error))
        report = {"schema": "ga106-sec2-mode-stage-v1", "probe_version": "0.12.1", "passed": False,
                  "error": type(error).__name__ + ": " + str(error), "cleanup_errors": cleanup_errors,
                  "connection_closed": bool(getattr(backend, "closed", False)),
                  "firmware_executed": False, "dma_transfer_executed": False, "reset_executed": False,
                  "sec2_stage": {"passed": False, "stage_selector_attempted": False}}
        if isinstance(error, (KeyboardInterrupt, SystemExit)): raise
        raise BindingError(str(error), report) from error


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, default=Path(__file__).resolve().parent / "firmware/570.144")
    parser.add_argument("--fuse-snapshot", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    with args.output.open("x", encoding="utf-8") as output:
        try:
            result = run_staging(MacIOKitBackend(), args.firmware, args.fuse_snapshot)
        except (BindingError, OSError, ValueError) as error:
            result = getattr(error, "report", None) or {"passed": False, "error": str(error),
                "probe_version": "0.12.1", "schema": "ga106-sec2-mode-stage-v1"}
            json.dump(result, output, indent=2); output.write("\n")
            return 1
        json.dump(result, output, indent=2); output.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
