"""Restricted RTXProbe 0.11 DMA buffer transport; never starts GPU firmware.

Only Begin/Info/Pages/Write/Publish/Read/Finish/Abort exist. There is no MMIO,
physical mapping, GPU execution, or caller-selected allocation-size interface.
IOKit signatures: https://github.com/apple-oss-distributions/IOKitUser/blob/main/IOKitLib.h
Importing this module does not load IOKit or open a device.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

import prepare_gsp

PAGE = CHUNK = 4096
MAX_PAGE_BATCH = 256
MAGIC = 0x475350444D413031
NAMES = ("radix3", "bootloader", "signature", "metadata", "queues", "rmargs",
         "libos_args", "logs", "booter_load")
INFO_WORDS = 16 + 9 * 16
INFO_SIZE = INFO_WORDS * 8
HEADER = ("magic", "abi", "state", "resource_count", "page_size", "fuse_offset",
          "fuse_first", "fuse_second", "fuse_reads", "signature_index", "total_bytes",
          "generation", "allocated", "published", "cleanup_verified", "pci_command")
ROW = ("id", "bytes", "page_count", "written_bytes", "synchronized", "read_bytes",
       "enumerated_pages", "memory_prepare_return", "set_descriptor_return",
       "dma_prepare_return", "sync_return", "dma_complete_return", "clear_return",
       "memory_complete_return", "resources_retained", "mapping_was_prepared")
STATES = {0: "idle", 1: "writing", 2: "published", 3: "finished", 4: "aborted", 5: "failed"}
PREPARE_RETURNS = ("memory_prepare_return", "set_descriptor_return", "dma_prepare_return")
CLEANUP_RETURNS = ("dma_complete_return", "clear_return", "memory_complete_return")


class BindingError(RuntimeError):
    def __init__(self, message, report=None):
        super().__init__(message)
        self.report = report


def _integer(value, name, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError("Invalid " + name)
    return value


def decode_info(data):
    if type(data) is not bytes or len(data) != INFO_SIZE:
        raise ValueError("Info must contain exactly 160 little-endian u64 words")
    words = struct.unpack("<160Q", data)
    result = dict(zip(HEADER, words[:16]))
    if (result["magic"], result["abi"], result["resource_count"], result["page_size"]) != (MAGIC, 1, 9, PAGE):
        raise ValueError("Unsupported Info ABI")
    if result["state"] not in STATES or result["generation"] == 0 or result["pci_command"] != 0:
        raise ValueError("Invalid state, generation or PCI command")
    if (result["fuse_offset"], result["fuse_first"], result["fuse_second"],
            result["fuse_reads"], result["signature_index"]) != (0x824148, 1, 1, 2, 0):
        raise ValueError("Fresh SEC2 fuse does not match the supported measured profile")
    for key in ("allocated", "published", "cleanup_verified"):
        _integer(result[key], key, 0, 1)
    resources = {}
    for index, name in enumerate(NAMES):
        row = dict(zip(ROW, words[16 + index * 16:32 + index * 16]))
        if row["id"] != index or row["bytes"] <= 0 or row["bytes"] % PAGE or row["page_count"] != row["bytes"] // PAGE:
            raise ValueError("Invalid resource geometry: " + name)
        if row["bytes"] > 128 << 20:
            raise ValueError("Resource exceeds bounded size: " + name)
        for key, limit in (("written_bytes", row["bytes"]), ("read_bytes", row["bytes"]),
                           ("enumerated_pages", row["page_count"]), ("synchronized", 1),
                           ("resources_retained", 1), ("mapping_was_prepared", 1)):
            _integer(row[key], key, 0, limit)
        for key in PREPARE_RETURNS + ("sync_return",) + CLEANUP_RETURNS:
            _integer(row[key], key, 0, 0xffffffff)
        resources[name] = row
    if result["total_bytes"] != sum(r["bytes"] for r in resources.values()):
        raise ValueError("Info allocation total differs from its resources")
    result["resources"] = resources
    result["state_name"] = STATES[result["state"]]
    return result


class RestrictedBackend:
    """Fixed protocol wrappers. Implementations supply only _invoke and _close."""
    kind = "injected-test-backend"

    def _request(self, selector, scalars=(), data=b"", output_size=0):
        if getattr(self, "closed", False):
            raise BindingError("Connection already closed")
        result = self._invoke(selector, scalars, data, output_size)
        if type(result) is not bytes or len(result) != output_size:
            raise BindingError("Partial or oversized structure output for selector " + str(selector))
        return result

    def begin(self): self._request(0)
    def info(self): return self._request(1, output_size=INFO_SIZE)

    def pages(self, resource, start, count):
        _integer(resource, "resource", 0, 8)
        _integer(start, "page start", 0, 32767)
        _integer(count, "page count", 1, MAX_PAGE_BATCH)
        data = self._request(2, (resource, start, count), output_size=count * 8)
        return list(struct.unpack("<" + str(count) + "Q", data))

    def write(self, resource, offset, data):
        _integer(resource, "resource", 0, 8)
        _integer(offset, "write offset", 0, (128 << 20) - 1)
        if type(data) is not bytes or not 1 <= len(data) <= CHUNK:
            raise ValueError("Write needs 1..4096 immutable bytes")
        self._request(3, (resource, offset), data)

    def publish(self): self._request(4)

    def read(self, resource, offset, length):
        _integer(resource, "resource", 0, 8)
        _integer(offset, "read offset", 0, (128 << 20) - 1)
        _integer(length, "read length", 1, CHUNK)
        return self._request(5, (resource, offset, length), output_size=length)

    def finish(self): self._request(6)
    def abort(self): self._request(7)

    def close(self):
        if getattr(self, "closed", False):
            return
        try:
            self.abort()  # Idempotent, also after a successful Finish.
        finally:
            try:
                self._close()
            finally:
                self.closed = True


class MacIOKitBackend(RestrictedBackend):
    kind = "macOS-IOKit-RTXProbe-0.11.0"

    def __init__(self):
        if sys.platform != "darwin":
            raise OSError("The live IOKit backend is available only on macOS")
        if os.geteuid() != 0:
            raise PermissionError("RTXProbe 0.11 user client requires an administrator/root process")
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
            if len(services) != 1 or self._string_property(services[0], "ProbeVersion") != "0.11.0":
                raise BindingError("Exactly one RTXProbe 0.11.0 service is required")
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
        # All callers are the fixed wrappers above; never accept arbitrary ABI.
        if selector not in range(8) or len(scalars) > 3 or len(data) > CHUNK or output_size > CHUNK:
            raise ValueError("Request exceeds restricted transport ABI")
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


def _validate_snapshot(info, requirements, baseline=None, state=None):
    if state is not None and info["state"] != state:
        raise ValueError("Unexpected transaction state: " + info["state_name"])
    if baseline is not None:
        for key in HEADER[:2] + HEADER[3:12] + ("pci_command",):
            if info[key] != baseline[key]: raise ValueError("Transaction identity changed: " + key)
    for name in NAMES:
        row, expected = info["resources"][name], requirements[name]
        if (row["bytes"], row["page_count"]) != (expected["pages"] * PAGE, expected["pages"]):
            raise ValueError("Native resource differs from pinned package: " + name)


def _require_clean(info):
    if info["state"] not in (3, 4) or (info["allocated"], info["published"], info["cleanup_verified"]) != (0, 0, 1):
        raise ValueError("Native resources were not verifiably released")
    if any(row["resources_retained"] for row in info["resources"].values()):
        raise ValueError("Native resource retention remains")
    if any(row["mapping_was_prepared"] and any(row[key] for key in CLEANUP_RETURNS)
           for row in info["resources"].values()):
        raise ValueError("Prepared mapping cleanup IOReturn failure")


def run_binding(backend, firmware, fuse_snapshot):
    """One attempt, complete upload/readback, verified release, then close.

    Never retry non-idempotent operations. A failed IPC can already have changed
    native state. Only Abort is repeated during close. Failure carries evidence
    in BindingError.report; saved IOVM addresses are historical after cleanup.
    """
    report = {"schema": "ga106-gsp-dma-binding-v1", "probe_version": "0.11.0",
              "backend": backend.kind, "passed": False, "snapshots": {}, "bindings": {},
              "buffer_checks": {}, "cleanup_errors": [], "connection_closed": False,
              "native_dma_prepared_during_transaction": False, "native_allocations_live": False,
              "bindings_are_historical": True, "gpu_commands_submitted": False,
              "firmware_executed": False, "dma_transfer_executed": False, "reset_executed": False,
              "gsp_init_done_observed": False, "compute_tested": False, "metal_supported": False,
              "ready_for_hardware_boot": False}
    failure = None
    finished = False
    try:
        plan, unused = prepare_gsp.prepare(firmware, fuse_snapshot)
        del unused
        selection = plan["booter_signature_selection"]
        if not selection or (selection["fuse_raw"], selection["signature_index"], selection["register"]) != (1, 0, "0x824148"):
            raise ValueError("A verified 0.10 SEC2 fuse=1 snapshot is required")
        requirements = plan["host_resources"]
        backend.begin()
        initial = decode_info(backend.info())
        report["snapshots"]["allocated"] = initial
        _validate_snapshot(initial, requirements, state=1)
        if (initial["allocated"], initial["published"], initial["cleanup_verified"]) != (1, 0, 0):
            raise ValueError("Begin did not establish an unpublished allocation")
        for row in initial["resources"].values():
            if any(row[key] for key in PREPARE_RETURNS) or not row["mapping_was_prepared"]:
                raise ValueError("Native DMA preparation failed")
            if row["enumerated_pages"] != row["page_count"]:
                raise ValueError("Native IOVM page enumeration is incomplete")
            if row["written_bytes"] or row["read_bytes"] or row["synchronized"] or row["resources_retained"]:
                raise ValueError("Begin returned reused resource state")
        report["native_dma_prepared_during_transaction"] = True
        seen = set()
        for resource, name in enumerate(NAMES):
            count = requirements[name]["pages"]
            pages = []
            for start in range(0, count, MAX_PAGE_BATCH):
                batch = backend.pages(resource, start, min(MAX_PAGE_BATCH, count - start))
                for address in batch:
                    _integer(address, "GPU-visible page", PAGE, (1 << 40) - PAGE)
                    if address % PAGE or address in seen:
                        raise ValueError("Unaligned or aliased native GPU-visible page")
                    seen.add(address)
                pages.extend(batch)
            report["bindings"][name] = pages
        bound, buffers = prepare_gsp.bind(firmware, report["bindings"], fuse_snapshot)
        if bound["booter_signature_selection"] != selection:
            raise ValueError("Fuse evidence changed during binding")
        report["package_selection"] = selection
        report["package_bound_hashes"] = bound["bound_buffer_hashes"]
        report["host_bytes"] = initial["total_bytes"]
        if set(buffers) != set(NAMES): raise ValueError("Binder omitted or added a resource")
        for resource, name in enumerate(NAMES):
            data = buffers[name]
            if type(data) is not bytes or len(data) != requirements[name]["pages"] * PAGE:
                raise ValueError("Binder resource size/type mismatch")
            if hashlib.sha256(data).hexdigest() != bound["bound_buffer_hashes"][name]:
                raise ValueError("Binder hash mismatch")
            for offset in range(0, len(data), CHUNK):
                backend.write(resource, offset, data[offset:offset + CHUNK])
        backend.publish()
        published = decode_info(backend.info())
        report["snapshots"]["published"] = published
        _validate_snapshot(published, requirements, initial, 2)
        if (published["allocated"], published["published"], published["cleanup_verified"]) != (1, 1, 0):
            raise ValueError("Publish did not retain all allocations")
        for row in published["resources"].values():
            if (row["written_bytes"], row["enumerated_pages"], row["read_bytes"], row["synchronized"], row["sync_return"]) != (row["bytes"], row["page_count"], 0, 1, 0):
                raise ValueError("Incomplete native upload or synchronization")
            if any(row[key] for key in PREPARE_RETURNS) or not row["mapping_was_prepared"] or row["resources_retained"]:
                raise ValueError("Published mapping preparation proof changed")
        for resource, name in enumerate(NAMES):
            expected = bound["bound_buffer_hashes"][name]
            digest = hashlib.sha256()
            for offset in range(0, len(buffers[name]), CHUNK):
                block = backend.read(resource, offset, min(CHUNK, len(buffers[name]) - offset))
                digest.update(block)
            actual = digest.hexdigest()
            report["buffer_checks"][name] = {"bytes": len(buffers[name]), "expected_sha256": expected,
                                             "readback_sha256": actual, "passed": actual == expected}
            if actual != expected: raise ValueError("Native readback differs: " + name)
        readback = decode_info(backend.info())
        report["snapshots"]["readback"] = readback
        _validate_snapshot(readback, requirements, initial, 2)
        if (readback["allocated"], readback["published"], readback["cleanup_verified"]) != (1, 1, 0):
            raise ValueError("Allocation lifetime changed during readback")
        if any(row["read_bytes"] != row["bytes"] for row in readback["resources"].values()):
            raise ValueError("Native did not record complete readback")
        backend.finish()
        final = decode_info(backend.info())
        report["snapshots"]["finished"] = final
        _validate_snapshot(final, requirements, initial, 3)
        _require_clean(final)
        for row in final["resources"].values():
            if any(row[key] for key in PREPARE_RETURNS + ("sync_return",) + CLEANUP_RETURNS):
                raise ValueError("Native lifecycle IOReturn failure")
            if (not row["mapping_was_prepared"] or row["read_bytes"] != row["bytes"]
                    or row["written_bytes"] != row["bytes"] or row["enumerated_pages"] != row["page_count"]):
                raise ValueError("Native final lifecycle proof is incomplete")
        finished = True
    except BaseException as error:
        failure = error
        report["error"] = type(error).__name__ + ": " + str(error)
    finally:
        if not finished:
            try:
                backend.abort()
                cleanup = decode_info(backend.info())
                report["snapshots"]["aborted"] = cleanup
                _require_clean(cleanup)
            except BaseException as error:
                report["cleanup_errors"].append("Abort: " + str(error))
        try:
            backend.close()
            report["connection_closed"] = True
        except BaseException as error:
            report["cleanup_errors"].append("Close: " + str(error))
    report["cleanup_verified"] = bool(finished or "aborted" in report["snapshots"]) and not report["cleanup_errors"]
    report["native_allocations_live"] = False if report["cleanup_verified"] else None
    report["passed"] = finished and failure is None and not report["cleanup_errors"]
    if not report["passed"]:
        if isinstance(failure, (KeyboardInterrupt, SystemExit)): raise failure
        raise BindingError(report.get("error", "Native cleanup/close failed"), report) from failure
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, default=Path(__file__).resolve().parent / "firmware/570.144")
    parser.add_argument("--fuse-snapshot", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    # Reserve the evidence path before opening IOKit; never overwrite prior data.
    with args.output.open("x", encoding="utf-8") as output:
        try:
            result = run_binding(MacIOKitBackend(), args.firmware, args.fuse_snapshot)
        except (BindingError, OSError, ValueError) as error:
            result = getattr(error, "report", None) or {"passed": False, "error": str(error)}
            json.dump(result, output, indent=2); output.write("\n")
            return 1
        json.dump(result, output, indent=2); output.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
