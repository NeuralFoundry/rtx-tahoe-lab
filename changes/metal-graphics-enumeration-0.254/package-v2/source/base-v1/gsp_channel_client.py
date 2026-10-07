"""Restricted 0.15 one-shot firmware bootstrap; importing performs no I/O.
Launch runs FWSEC, SEC2 and GSP; close retains exposed resources until reboot.
No client-selected MMIO, arbitrary physical mapping or sequencer execution."""
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
MAGIC = 0x525458444D413133
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
        for key, limit in (("written_bytes", (1 << 64) - 1), ("read_bytes", (1 << 64) - 1),
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

    def prepare_startup(self): self._request(10)
    def seal(self): self._request(11)
    def seal_info(self): return self._request(12, output_size=256)
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

    def sync(self, resource, direction):
        _integer(resource, "resource", 0, 8)
        _integer(direction, "sync direction", 1, 2)
        self._request(8, (resource, direction))

    def sync_info(self): return self._request(9, output_size=640)
    def finish(self): self._request(6)
    def abort(self): self._request(7)

    def launch(self): self._request(13)
    def launch_info(self): return self._request(14, output_size=512)

    def event_info(self): return self._request(16, output_size=256)
    def sequence_info(self): return self._request(19, output_size=512)
    def bar1_info(self): return self._request(27,output_size=512)
    def bar1_data(self,offset,length):
        _integer(offset,'BAR1 capture offset',0,16383)
        _integer(length,'BAR1 capture length',1,4096)
        if offset+length>16384: raise ValueError('BAR1 capture exceeds fixed capacity')
        return self._request(28,(offset,length),output_size=length)
    def page_info(self): return self._request(29,output_size=512)
    def page_data(self,which,offset,length):
        _integer(which,'page-table image',0,1)
        _integer(offset,'page-table offset',0,12287)
        _integer(length,'page-table length',1,4096)
        if offset+length>12288: raise ValueError('Page-table capture exceeds fixed capacity')
        return self._request(30,(which,offset,length),output_size=length)
    def page_rm_info(self): return self._request(31,output_size=512)
    def page_rm_index(self,start,count):
        _integer(start,'page-table RM index',0,15)
        _integer(count,'page-table RM records',1,16)
        if start+count>16: raise ValueError('Page-table RM index exceeds capacity')
        return self._request(32,(start,count),output_size=count*72)
    def page_rm_data(self,offset,length):
        _integer(offset,'page-table RM offset',0,32*4096-1)
        _integer(length,'page-table RM bytes',1,4096)
        if offset+length>32*4096: raise ValueError('Page-table RM capture exceeds capacity')
        return self._request(33,(offset,length),output_size=length)
    def page_rm_request(self): return self._request(34,(0,4096),output_size=4096)
    def channel_memory_info(self,which):
        _integer(which,'channel memory stage',0,1)
        return self._request(35+which,output_size=512)
    def channel_rm_info(self): return self._request(37,output_size=512)
    def channel_rm_index(self,start,count):
        _integer(start,'channel record start',0,15);_integer(count,'channel record count',1,16)
        if start+count>16: raise ValueError('Channel record index capacity')
        return self._request(38,(start,count),output_size=count*72)
    def channel_rm_data(self,offset,length):
        _integer(offset,'channel record offset',0,131071);_integer(length,'channel record length',1,4096)
        if offset+length>131072: raise ValueError('Channel record byte capacity')
        return self._request(39,(offset,length),output_size=length)
    def channel_request(self,step):
        _integer(step,'channel request step',0,4)
        return self._request(40,(step*4096,4096),output_size=4096)
    def channel_plan_info(self): return self._request(41,output_size=1024)
    def channel_snapshot_data(self,which,offset,length):
        _integer(which,'channel snapshot image',0,1)
        maximum=45056 if which else 12288
        _integer(offset,'channel snapshot offset',0,maximum-1);_integer(length,'channel snapshot length',1,4096)
        if offset+length>maximum: raise ValueError('Channel snapshot capacity')
        return self._request(42,(which,offset,length),output_size=length)
    def channel_snapshot_info(self): return self._request(43,output_size=512)
    def rm_info(self): return self._request(23, output_size=512)

    def rm_index(self, start, count):
        _integer(start, 'RM record start', 0, 15)
        _integer(count, 'RM record count', 1, 16)
        if start+count > 16: raise ValueError('RM index exceeds capacity')
        return self._request(24, (start, count), output_size=count*72)

    def rm_data(self, offset, length):
        _integer(offset, 'RM record offset', 0, 32*4096-1)
        _integer(length, 'RM record length', 1, CHUNK)
        if offset+length > 32*4096: raise ValueError('RM records exceed capacity')
        return self._request(25, (offset, length), output_size=length)

    def rm_requests(self, offset, length):
        _integer(offset, 'RM request offset', 0, 6*4096-1)
        _integer(length, 'RM request length', 1, CHUNK)
        if offset+length > 6*4096: raise ValueError('RM requests exceed capacity')
        return self._request(26, (offset, length), output_size=length)
    def after_info(self): return self._request(20, output_size=256)

    def after_index(self, start, count):
        _integer(start, 'event start', 0, 61)
        _integer(count, 'event count', 1, 32)
        if start+count > 62: raise ValueError('Event index exceeds capture capacity')
        return self._request(21, (start, count), output_size=count*72)

    def after_data(self, offset, length):
        _integer(offset, 'event offset', 0, 62*4096-1)
        _integer(length, 'event length', 1, CHUNK)
        if offset+length > 62*4096: raise ValueError('Event data exceeds capture capacity')
        return self._request(22, (offset, length), output_size=length)

    def event_index(self, start, count):
        _integer(start, 'event start', 0, 61)
        _integer(count, 'event count', 1, 32)
        if start+count > 62: raise ValueError('Event index exceeds capture capacity')
        return self._request(17, (start, count), output_size=count*72)

    def event_data(self, offset, length):
        _integer(offset, 'event offset', 0, 62*4096-1)
        _integer(length, 'event length', 1, CHUNK)
        if offset+length > 62*4096: raise ValueError('Event data exceeds capture capacity')
        return self._request(18, (offset, length), output_size=length)

    def first_record(self, offset, length):
        _integer(offset, 'record offset', 0, 65535)
        _integer(length, 'record length', 1, CHUNK)
        if offset + length > 65536:
            raise ValueError('Record read exceeds fixed capture')
        return self._request(15, (offset, length), output_size=length)

    def close(self):
        if getattr(self, 'closed', False):
            return
        try:
            self._close()  # Native close retains all device-exposed buffers.
        finally:
            self.closed = True

class MacIOKitBackend(RestrictedBackend):
    kind = "macOS-IOKit-RTXProbe-0.23.0"

    def __init__(self):
        if sys.platform != "darwin":
            raise OSError("The live IOKit backend is available only on macOS")
        if os.geteuid() != 0:
            raise PermissionError("RTXProbe 0.15 user client requires an administrator/root process")
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
            if len(services) != 1 or self._string_property(services[0], "ProbeVersion") != "0.23.0":
                raise BindingError("Exactly one RTXProbe 0.23.0 service is required")
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
        if selector not in range(44) or len(scalars) > 3 or len(data) > CHUNK or output_size > CHUNK:
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
