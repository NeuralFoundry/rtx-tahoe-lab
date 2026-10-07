import ast
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import unittest

import runtime_dma_client as wire
from tinygrad_bootstrap_memory import BootstrapMemory, HostMemoryView, SIZES, NAMES, decode_sync


class LocalMemory:
    def __init__(self): self.data = bytearray(32768); self.closed = False; self.failed = False
    def _check(self):
        if self.closed: raise wire.BindingError("closed")
    def read(self, r, off, length): self._check(); return bytes(self.data[off:off + length])
    def write(self, r, off, data): self._check(); self.data[off:off + len(data)] = data


class BounceBackend:
    """Software-only separate host/device copies; never used as live evidence."""
    kind = "unit-test-bounce-model"
    def __init__(self):
        self.state = 0; self.closed = False; self.generation = 123
        self.host = {}; self.device = {}; self.events = []
        self.written = [0]*9; self.reads = [0]*9
        self.we = [0]*9; self.oe = [0]*9; self.ie = [0]*9; self.oc = [0]*9; self.ic = [0]*9
    def begin(self): self.state = 1; self.we = [1]*9
    def info(self):
        w = [wire.MAGIC, 1, self.state, 9, 4096, 0x824148, 1, 1, 2, 0, sum(SIZES), self.generation,
             int(self.state in (1, 2)), int(self.state == 2), int(self.state in (3, 4)), 0]
        for i, size in enumerate(SIZES):
            w += [i, size, size//4096, self.written[i], int(self.oe[i] == self.we[i] != 0), self.reads[i],
                  size//4096 if self.state else 0, 0, 0, 0, 0, 0, 0, 0, 0, int(self.state != 0)]
        return struct.pack("<160Q", *w)
    def sync_info(self):
        w = [0x52545853594E4331, 1, self.generation, 9, int(self.state in (1, 2)), 0, int(self.state in (1, 2)), 0]
        for i in range(9): w += [i, self.we[i], self.oe[i], self.ie[i], self.oc[i], self.ic[i], self.written[i], self.reads[i]]
        return struct.pack("<80Q", *w)
    def pages(self, resource, start, count):
        base = 0x100000 + sum(SIZES[:resource])
        return [base + (start+i)*4096 for i in range(count)]
    def publish(self):
        for r in range(9): self.sync(r, 2)
    def sync(self, r, direction):
        self.events.append(("sync", r, direction))
        if direction == 1:
            if self.oe[r] != self.we[r]: raise wire.BindingError("would overwrite dirty CPU data")
            for (resource, page), value in list(self.device.items()):
                if resource == r: self.host[(r, page)] = bytearray(value)
            self.ie[r] = self.oe[r]; self.ic[r] += 1
        else:
            for (resource, page), value in list(self.host.items()):
                if resource == r: self.device[(r, page)] = bytearray(value)
            self.oe[r] = self.we[r]; self.ie[r] = 0; self.oc[r] += 1
        self.state = 2 if self.oe == self.we else 1
    def write(self, r, off, data):
        self.events.append(("write", r, off, len(data)))
        for i, b in enumerate(data):
            page, at = divmod(off+i, 4096)
            if (r, page) not in self.host: self.host[(r, page)] = bytearray(4096)
            self.host[(r, page)][at] = b
        self.we[r] += 1; self.written[r] += len(data); self.state = 1
    def read(self, r, off, size):
        if self.ie[r] != self.we[r]: raise wire.BindingError("read before import")
        self.reads[r] += size
        return bytes(self.host.get((r, (off+i)//4096), bytes(4096))[(off+i)%4096] for i in range(size))
    def finish(self): self.state = 3
    def abort(self):
        if self.state != 3: self.state = 4
    def close(self): self.closed = True


class ViewTests(unittest.TestCase):
    def setUp(self): self.mem = LocalMemory(); self.view = HostMemoryView(self.mem, 0, 16384)
    def test_alias_and_cross_page(self):
        nested = self.view.view(4092, 16).view(4, 8, "I")
        nested[:] = [0x12345678, 0xfedcba98]
        self.assertEqual(self.view[4096:4104], bytes.fromhex("7856341298badcfe"))
        self.assertEqual(nested[-1], 0xfedcba98)
    def test_zero_length_view_slice_and_write(self):
        self.assertEqual(len(self.view.view(16384, 0)), 0)
        self.assertEqual(self.view[:0], b"")
        self.view[:0] = b""
        self.assertEqual(self.view.view(0, 0).view().nbytes, 0)
    def test_bounds(self):
        for offset, size in ((16384, 1), (-1, 1), (1 << 64, 1), (3, 16384)):
            with self.subTest(offset=offset), self.assertRaises(ValueError): self.view.view(offset, size)
        for i in (-16385, 16384, 1 << 64):
            with self.assertRaises(IndexError): _ = self.view[i]
    def test_assignment_cannot_resize(self):
        for value in (b"", b"12345"):
            with self.assertRaises(ValueError): self.view[0:4] = value
        with self.assertRaises(TypeError): self.view[0:4] = 4
    def test_alignment_format_and_stride(self):
        for offset, size, fmt in ((1, 4, "I"), (0, 3, "I"), (0, 4, "P")):
            with self.assertRaises(ValueError): self.view.view(offset, size, fmt)
        with self.assertRaises(ValueError): _ = self.view[::2]
    def test_closed_alias(self):
        sub = self.view.view(0, 16, "Q"); self.mem.closed = True
        for operation in (lambda: sub[0], lambda: sub.__setitem__(0, 3), lambda: sub.view()):
            with self.assertRaises(wire.BindingError): operation()
    def test_pinned_upstream_class_behavior(self):
        root = Path(__file__).parent / "research/tinygrad-adapter-33cd373ad353"
        manifest = json.loads((root / "source-manifest.json").read_text())
        self.assertEqual(manifest["commit"], "33cd373ad35371ccb483c9645d0c0637a04debc2")
        for row in manifest["files"]:
            self.assertEqual(hashlib.sha256((root / row["path"]).read_bytes()).hexdigest(), row["sha256"])
        source = ast.parse((root / "tinygrad/runtime/support/memory.py").read_text())
        # Only execute the inspected pure memory-view class, with a local ctypes
        # buffer. No tinygrad imports, installer, device enumeration or NVDev.
        cls = next(n for n in source.body if isinstance(n, ast.ClassDef) and n.name == "MMIOInterface")
        future = ast.ImportFrom(module="__future__", names=[ast.alias(name="annotations")], level=0)
        module = ast.fix_missing_locations(ast.Module(body=[future, cls], type_ignores=[]))
        ns = {"struct": struct, "to_mv": lambda addr, n: memoryview((ctypes.c_ubyte*n).from_address(addr)).cast("B")}
        exec(compile(module, "pinned-upstream-MMIOInterface-only", "exec"), ns)
        ram = ctypes.create_string_buffer(16384)
        reference = ns["MMIOInterface"](ctypes.addressof(ram), 16384)
        for fmt, values in (("B", b"abcdefgh"), ("I", [1, 0xffffffff]), ("Q", [0x123456789abcdef0])):
            left, right = self.view.view(4096, 8, fmt), reference.view(4096, 8, fmt)
            if fmt == "B": left[:] = right[:] = values
            else:
                for i, v in enumerate(values): left[i] = right[i] = v
            self.assertEqual(list(left[:]), list(right[:]))
            self.assertEqual(left[-1], right[-1])
        self.assertEqual(bytes(reference[:]), self.view[:])


class SessionTests(unittest.TestCase):
    def test_bounce_order_repeat_and_import(self):
        backend = BounceBackend(); memory = BootstrapMemory(backend)
        view = memory.resource("queues").view(4092, 16)
        for pattern in (bytes(range(16)), bytes(255-i for i in range(16))):
            view[:] = pattern; self.assertEqual(view[:], pattern)
        # Simulated device write proves the Python read issues import each time.
        # This test does not assert a real GPU transfer.
        backend.device[(4, 1)][0] = 99
        self.assertEqual(view[4], 99)
        self.assertEqual(memory.sync_info()["resources"]["queues"]["in_count"], 3)
        memory.close(finish=True)
        with self.assertRaises(wire.BindingError): _ = view[0]
        with self.assertRaises(wire.BindingError): memory.pages("queues")
        self.assertTrue(backend.closed)
    def test_initial_zero_publication(self):
        b = BounceBackend(); m = BootstrapMemory(b)
        self.assertEqual(m.resource("metadata")[:8], b"\0"*8)
        self.assertEqual(b.events[:9], [("sync", i, 2) for i in range(9)])
        m.close()
    def test_chunking_and_exact_publish(self):
        b = BounceBackend(); m = BootstrapMemory(b); view = m.resource("queues")
        b.events.clear(); view[3000:12000] = b"a"*9000
        self.assertEqual(b.events, [("write",4,3000,4096),("write",4,7096,4096),("write",4,11192,808),("sync",4,2)])
        self.assertEqual(view[3000:12000], b"a"*9000); m.close()
    def test_partial_write_failure_invalidates_all_aliases(self):
        b = BounceBackend(); m = BootstrapMemory(b); view = m.resource("queues")
        original = b.write
        def broken(r, off, data):
            if off: raise wire.BindingError("injected write failure")
            original(r, off, data)
        b.write = broken
        with self.assertRaises(wire.BindingError): view[:5000] = b"a"*5000
        with self.assertRaises(wire.BindingError): _ = m.resource("metadata")
        m.close(); self.assertEqual(b.state, 4)
    def test_duplicate_native_page_rejected(self):
        b = BounceBackend(); original = b.pages
        b.pages = lambda r, s, n: [0x100000]*n if r == 0 else original(r,s,n)
        with self.assertRaises(wire.BindingError): BootstrapMemory(b)
        self.assertTrue(b.closed)
    def test_generation_changed_rejected(self):
        b = BounceBackend(); m = BootstrapMemory(b); b.generation += 1
        with self.assertRaises(wire.BindingError): m.info()
        with self.assertRaises(wire.BindingError): m.close()
        self.assertTrue(b.closed)
    def test_sync_wire_rejects_wrong_id_and_epochs(self):
        b = BounceBackend(); m = BootstrapMemory(b); w = list(struct.unpack("<80Q", b.sync_info()))
        for index, value in ((0,0),(2,124),(5,2),(7,6),(8,8),(11,2)):
            bad = w[:]; bad[index] = value
            with self.assertRaises(wire.BindingError): decode_sync(struct.pack("<80Q",*bad),123)
        m.close()
    def test_live_transport_selectors_are_bounded(self):
        class Test(wire.RestrictedBackend):
            def _invoke(self, selector, scalars, data, output_size): return b"\0"*(output_size-1)
        t = Test()
        with self.assertRaises(wire.BindingError): t.sync_info()
        with self.assertRaises(ValueError): t.sync(0, 3)
        with self.assertRaises(ValueError): t.sync(9, 1)
        with self.assertRaises(ValueError): t.write(0, 0, b"x"*4097)


if __name__ == "__main__": unittest.main()
