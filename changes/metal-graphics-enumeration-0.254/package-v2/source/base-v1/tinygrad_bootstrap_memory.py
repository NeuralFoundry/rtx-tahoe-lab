"""Copy-backed host memory views for the pinned tinygrad MMIOInterface contract.

Bootstrap subset only: nine named, prepared GSP buffers. No alloc_sysmem,
CPU pointer, BAR mapping, reset, GPU launch, or whole NV backend registration.
Reference: tinygrad 33cd373ad35371ccb483c9645d0c0637a04debc2, MIT; original
license is retained in research/tinygrad-adapter-33cd373ad353/LICENSE.
"""
import struct
import threading

from runtime_dma_client import (BindingError, CHUNK, NAMES, PAGE, PREPARE_RETURNS,
                                CLEANUP_RETURNS, decode_info, _integer)

UPSTREAM_COMMIT = "33cd373ad35371ccb483c9645d0c0637a04debc2"
SIZES = (63676416, 24576, 4096, 4096, 528384, 4096, 4096, 2097152, 61440)


def span(offset, size, capacity):
    _integer(offset, "offset", 0, capacity)
    _integer(size, "size", 0, capacity - offset)


def decode_sync(data, generation):
    if type(data) is not bytes or len(data) != 640:
        raise BindingError("SyncInfo must contain 80 u64 words")
    w = struct.unpack("<80Q", data)
    if w[:4] != (0x52545853594E4331, 1, generation, 9) or any(v not in (0, 1) for v in w[4:7]) or w[7]:
        raise BindingError("Invalid synchronization identity or PCI command")
    keys = ("id", "write_epoch", "out_epoch", "in_epoch", "out_count", "in_count", "written", "read")
    rows = {}
    for i, name in enumerate(NAMES):
        row = dict(zip(keys, w[8 + i * 8:16 + i * 8]))
        if row["id"] != i or not row["in_epoch"] <= row["out_epoch"] <= row["write_epoch"]:
            raise BindingError("Invalid synchronization epochs")
        rows[name] = row
    return dict(generation=w[2], provider_open=bool(w[4]), pinned=bool(w[5]), held=bool(w[6]), resources=rows)


class BootstrapMemory:
    def __init__(self, backend):
        self.backend, self.lock = backend, threading.RLock()
        self.closed, self.failed, self.generation = False, False, 0
        self._pages = {}
        try:
            initial = decode_info(backend.info())
            if initial["state"] != 0 or initial["allocated"] or initial["cleanup_verified"]:
                raise BindingError("A fresh bootstrap service is required")
            self.generation = initial["generation"]
            backend.begin()
            info = self.info()
            if info["state"] != 1 or not info["allocated"] or info["cleanup_verified"]:
                raise BindingError("Begin did not retain fresh buffers")
            seen = set()
            for i, name in enumerate(NAMES):
                r = info["resources"][name]
                if r["bytes"] != SIZES[i] or r["enumerated_pages"] != SIZES[i] // PAGE:
                    raise BindingError("Unexpected native buffer geometry")
                if any(r[k] for k in PREPARE_RETURNS) or not r["mapping_was_prepared"] or r["resources_retained"]:
                    raise BindingError("Native mapping preparation failed")
                if r["written_bytes"] or r["read_bytes"] or r["synchronized"]:
                    raise BindingError("Reused native mapping state")
                pages = []
                for start in range(0, r["page_count"], 256):
                    pages.extend(backend.pages(i, start, min(256, r["page_count"] - start)))
                if len(pages) != r["page_count"]:
                    raise BindingError("Page list length mismatch")
                for p in pages:
                    _integer(p, "DMA page", PAGE, (1 << 40) - PAGE)
                    if p % PAGE or p in seen:
                        raise BindingError("Unaligned or overlapping DMA pages")
                    seen.add(p)
                if name in ("bootloader", "logs") and any(b != a + PAGE for a, b in zip(pages, pages[1:])):
                    raise BindingError("Direct-addressed buffer is not contiguous")
                self._pages[name] = tuple(pages)
            # Publish the native zero fill before any import, including bounce buffers.
            backend.publish()
            sync = self.sync_info()
            if not sync["provider_open"] or not sync["held"] or sync["pinned"]:
                raise BindingError("Native provider does not own the prepared mappings")
            if any((r["write_epoch"], r["out_epoch"], r["in_epoch"], r["out_count"], r["in_count"])
                   != (1, 1, 0, 1, 0) for r in sync["resources"].values()):
                raise BindingError("Initial publication was not recorded")
        except BaseException:
            self.failed = True
            try: self.close()
            except BaseException: pass  # Preserve original failure; native runner checks ownership.
            raise

    def _check(self):
        if self.closed or self.failed:
            raise BindingError("Memory session is closed or failed; views and DMA pages expired")

    def info(self):
        info = decode_info(self.backend.info())
        if info["generation"] != self.generation:
            self.failed = True
            raise BindingError("Native generation changed")
        return info

    def sync_info(self): return decode_sync(self.backend.sync_info(), self.generation)

    def pages(self, name):
        with self.lock:
            self._check()
            return self._pages[name]  # Immutable metadata valid only during this owned session.

    def resource(self, name, fmt="B"):
        with self.lock:
            self._check()
            index = NAMES.index(name)
            return HostMemoryView(self, index, SIZES[index], fmt=fmt)

    def read(self, resource, offset, length):
        with self.lock:
            self._check()
            _integer(resource, "resource", 0, 8)
            span(offset, length, SIZES[resource])
            if not length: return b""
            try:
                self.backend.sync(resource, 1)
                return b"".join(self.backend.read(resource, offset + i, min(CHUNK, length - i))
                                for i in range(0, length, CHUNK))
            except BaseException:
                self.failed = True
                raise

    def write(self, resource, offset, data):
        with self.lock:
            self._check()
            _integer(resource, "resource", 0, 8)
            if type(data) is not bytes: raise TypeError("Write requires immutable bytes")
            span(offset, len(data), SIZES[resource])
            if not data: return
            try:
                for i in range(0, len(data), CHUNK):
                    self.backend.write(resource, offset + i, data[i:i + CHUNK])
                self.backend.sync(resource, 2)
            except BaseException:
                self.failed = True
                raise

    def close(self, finish=False):
        with self.lock:
            if self.closed: return
            self.closed = True  # Invalidate all aliases before entering native teardown.
            try:
                if finish and not self.failed: self.backend.finish()
                else: self.backend.abort()
                info, sync = self.info(), self.sync_info()
                if info["state"] not in (3, 4) or info["allocated"] or not info["cleanup_verified"]:
                    raise BindingError("Native cleanup was not verified")
                if sync["provider_open"] or sync["held"] or sync["pinned"]:
                    raise BindingError("Native ownership was retained")
                for r in info["resources"].values():
                    if r["mapping_was_prepared"] and any(r[k] for k in CLEANUP_RETURNS):
                        raise BindingError("A native completion failed")
                self.final_info, self.final_sync = info, sync
            finally:
                self._pages.clear()
                self.backend.close()


class HostMemoryView:
    """MMIOInterface-shaped host view; all offsets/sizes are in bytes for view()."""
    def __init__(self, memory, resource, nbytes, fmt="B", offset=0):
        _integer(resource, "resource", 0, 8)
        span(offset, nbytes, SIZES[resource])
        if fmt not in ("B", "H", "I", "Q"): raise ValueError("Unsupported element format")
        self.el_sz = struct.calcsize("<" + fmt)
        if nbytes % self.el_sz or offset % self.el_sz:
            raise ValueError("Misaligned typed view")
        self.memory, self.resource_id, self.nbytes = memory, resource, nbytes
        self.fmt, self.off = fmt, offset

    def __len__(self): return self.nbytes // self.el_sz

    def _indices(self, key):
        self.memory._check()
        if isinstance(key, slice):
            start, stop, step = key.indices(len(self))
            if step != 1: raise ValueError("Strided host views are not supported")
            return start, max(0, stop - start), True
        if type(key) is not int: raise TypeError("Expected integer or slice")
        index = key + len(self) if key < 0 else key
        if not 0 <= index < len(self): raise IndexError("View index out of bounds")
        return index, 1, False

    def __getitem__(self, key):
        start, count, sliced = self._indices(key)
        data = self.memory.read(self.resource_id, self.off + start * self.el_sz, count * self.el_sz)
        if self.fmt == "B": return data if sliced else data[0]
        values = list(struct.unpack("<" + str(count) + self.fmt, data))
        return values if sliced else values[0]

    def __setitem__(self, key, value):
        start, count, sliced = self._indices(key)
        if sliced and self.fmt == "B":
            if not isinstance(value, (bytes, bytearray, memoryview)):
                raise TypeError("Byte slice assignment requires a byte buffer")
            data = bytes(value)
        else:
            values = list(value) if sliced else [value]
            data = struct.pack("<" + str(len(values)) + self.fmt, *values)
        if len(data) != count * self.el_sz: raise ValueError("Assignment cannot resize a view")
        self.memory.write(self.resource_id, self.off + start * self.el_sz, data)

    def view(self, offset=0, size=None, fmt=None):
        self.memory._check()
        _integer(offset, "view offset", 0, self.nbytes)
        length = self.nbytes - offset if size is None else size
        span(offset, length, self.nbytes)
        return HostMemoryView(self.memory, self.resource_id, length, self.fmt if fmt is None else fmt, self.off + offset)
