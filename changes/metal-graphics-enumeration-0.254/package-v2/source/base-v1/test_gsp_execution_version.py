"""Exercise the real IOKit constructor against CPU stubs, including version rejection."""
import ctypes
from pathlib import Path
import re
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import gsp_execution_client as client

class Function:
    def __init__(self, fn=lambda *args:0): self.fn=fn
    def __call__(self, *args): return self.fn(*args)

class U32(ctypes.c_uint32):
    @classmethod
    def in_dll(cls, lib, name):
        assert name=='mach_task_self_'
        return cls(7)

class IOKitStub:
    def __init__(self, services, error=0):
        self.iterator=iter([*services,0]);self.opened=[];self.released=[];self.closed=[]
        self.io=SimpleNamespace(**{name:Function() for name in (
            'IOServiceMatching','IOServiceGetMatchingServices','IOIteratorNext','IOObjectRelease',
            'IORegistryEntryCreateCFProperty','IOServiceOpen','IOServiceClose','IOConnectCallMethod')})
        self.cf=SimpleNamespace(**{name:Function() for name in (
            'CFStringCreateWithCString','CFGetTypeID','CFStringGetTypeID','CFStringGetCString','CFRelease')})
        self.io.IOServiceMatching=Function(lambda name:123 if name==b'RTXProbe' else 0)
        def matching(port, dictionary, iterator): iterator._obj.value=17;return 0
        self.io.IOServiceGetMatchingServices=Function(matching)
        self.io.IOIteratorNext=Function(lambda iterator:next(self.iterator))
        self.io.IOObjectRelease=Function(lambda service:self.released.append(service) or 0)
        def opening(service, task, kind, connection):
            self.opened.append((service,task,kind));connection._obj.value=91;return error
        self.io.IOServiceOpen=Function(opening)
        self.io.IOServiceClose=Function(lambda connection:self.closed.append(connection) or 0)
        def no_gpu_call(*args): raise AssertionError('Constructor must not begin DMA or submit firmware')
        self.io.IOConnectCallMethod=Function(no_gpu_call)
    def library(self, path):
        if 'IOKit.framework' in path:return self.io
        if 'CoreFoundation.framework' in path:return self.cf
        if path=='/usr/lib/libSystem.B.dylib':return SimpleNamespace()
        raise AssertionError(path)

class ExecutionVersionTests(unittest.TestCase):
    def connect(self, stub, version):
        with patch.object(client.sys,'platform','darwin'),patch.object(client.os,'geteuid',return_value=0,create=True),\
             patch.object(client.ctypes,'CDLL',side_effect=stub.library),patch.object(client.ctypes,'c_uint32',U32),\
             patch.object(client.MacIOKitBackend,'_string_property',return_value=version):
            return client.MacIOKitBackend()

    def test_current_driver_version_opens_and_releases_handles(self):
        source=(Path(__file__).parent/'driver/GSPExecutionProbe.cpp').read_text()
        version=re.search(r'setProperty\("ProbeVersion",\s*"([^"]+)"\)',source).group(1)
        self.assertEqual(version,'0.29.0')
        self.assertIn('KMOD_EXPLICIT_DECL(local.emre.RTXProbe, "'+version+'"',source)
        stub=IOKitStub([71]);backend=self.connect(stub,version)
        self.assertEqual(backend.connection,91)
        self.assertEqual(backend.kind,'macOS-IOKit-RTXProbe-'+version)
        self.assertEqual(stub.opened,[(71,7,0)]);self.assertEqual(stub.released,[71,17])
        backend.close();backend.close();self.assertEqual(stub.closed,[91])

    def test_old_or_unexpected_version_is_rejected_before_open(self):
        for version in ('0.25.1','0.25.0','0.26.0','0.26.1','0.26.2','0.27.0','0.27.1','0.28.0','0.28.1','0.29.1','',None):
            with self.subTest(version=version):
                stub=IOKitStub([71])
                with self.assertRaises(client.BindingError):self.connect(stub,version)
                self.assertEqual(stub.opened,[]);self.assertEqual(stub.released,[71,17])

    def test_zero_or_multiple_services_are_rejected_before_open(self):
        for services in ([],[71,72],list(range(71,88))):
            with self.subTest(services=services):
                stub=IOKitStub(services)
                with self.assertRaises(client.BindingError):self.connect(stub,'0.29.0')
                self.assertEqual(stub.opened,[]);self.assertEqual(stub.released,[*services,17])

    def test_open_error_releases_registry_handles(self):
        stub=IOKitStub([71],error=5)
        with self.assertRaises(client.BindingError):self.connect(stub,'0.29.0')
        self.assertEqual(stub.opened,[(71,7,0)]);self.assertEqual(stub.released,[71,17])

if __name__=='__main__':unittest.main()
