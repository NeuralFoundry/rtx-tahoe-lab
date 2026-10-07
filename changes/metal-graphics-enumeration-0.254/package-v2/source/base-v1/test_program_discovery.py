"""Exercise the real backend constructor through fake IOKit/CF entry points."""
from pathlib import Path
import ctypes,os,sys,types,unittest
from unittest.mock import patch

import gsp_program_client as client

class Function:
    def __init__(self,callback):self.callback=callback;self.argtypes=None;self.restype=None
    def __call__(self,*values):return self.callback(*values)

class World:
    def __init__(self,versions=(b'0.34.0',),**options):
        self.versions=versions;self.options=options;self.released=[];self.cf_released=[]
        self.opens=[];self.closes=[];self.property_keys=[];self.task_reads=0;self.dispatches=0
        self.iterator=iter(range(100,100+len(versions)))
        self.io=types.SimpleNamespace();self.cf=types.SimpleNamespace();self.system=object()
        def function(lib,name,callback):setattr(lib,name,Function(callback))
        function(self.io,'IOServiceMatching',lambda name:0 if options.get('no_dictionary') else (71 if name==b'RTXProbe' else 0))
        def matching(port,dictionary,output):
            assert port==0 and dictionary==71;output._obj.value=42;return options.get('matching_error',0)
        function(self.io,'IOServiceGetMatchingServices',matching)
        function(self.io,'IOIteratorNext',lambda iterator:next(self.iterator,0) if iterator==42 else 0)
        function(self.io,'IOObjectRelease',lambda obj:self.released.append(obj) or 0)
        def property_value(service,key,allocator,flags):
            assert key==17 and allocator is None and flags==0
            return 0 if versions[service-100] is None else service+1000
        function(self.io,'IORegistryEntryCreateCFProperty',property_value)
        def open_service(service,task,kind,connection):
            assert task==7 and kind==0
            self.opens.append(service);connection._obj.value=options.get('connection',91)
            return options.get('open_error',0)
        function(self.io,'IOServiceOpen',open_service)
        function(self.io,'IOServiceClose',lambda conn:self.closes.append(conn) or 0)
        def dispatch(*values):self.dispatches+=1;raise AssertionError('Constructor/close dispatched a GPU protocol selector')
        function(self.io,'IOConnectCallMethod',dispatch)
        def make_string(allocator,name,encoding):
            assert allocator is None and encoding==0x08000100;self.property_keys.append(name);return 17
        function(self.cf,'CFStringCreateWithCString',make_string)
        function(self.cf,'CFGetTypeID',lambda value:12 if options.get('wrong_type') else 11)
        function(self.cf,'CFStringGetTypeID',lambda:11)
        def get_string(value,buffer,capacity,encoding):
            assert encoding==0x08000100
            data=versions[value-1100]+b'\0'
            if options.get('decode_failure') or len(data)>capacity:return False
            ctypes.memmove(buffer,data,len(data));return True
        function(self.cf,'CFStringGetCString',get_string)
        function(self.cf,'CFRelease',lambda obj:self.cf_released.append(obj))
        world=self
        class UInt32(ctypes.c_uint32):
            @classmethod
            def in_dll(cls,library,name):
                assert library is world.system and name=='mach_task_self_'
                world.task_reads+=1;return cls(7)
        self.ffi=types.SimpleNamespace(**{name:getattr(ctypes,name) for name in dir(ctypes) if not name.startswith('__')})
        self.ffi.c_uint32=UInt32
        def load(path):
            return {'/System/Library/Frameworks/IOKit.framework/IOKit':self.io,
                    '/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation':self.cf,
                    '/usr/lib/libSystem.B.dylib':self.system}[path]
        self.ffi.CDLL=load
    def create(self):
        with patch.object(client.sys,'platform','darwin'),patch.object(client.os,'geteuid',lambda:0,create=True),patch.object(client,'ctypes',self.ffi):
            return client.MacIOKitBackend()

class Discovery(unittest.TestCase):
    def test_accepts_actual_034_service_and_releases_objects(self):
        w=World();b=w.create()
        self.assertEqual(b.kind,'macOS-IOKit-RTXProbe-0.34.0');self.assertEqual(b.connection,91)
        self.assertEqual(w.opens,[100]);self.assertEqual(w.released,[100,42])
        self.assertEqual(w.property_keys,[b'ProbeVersion']);self.assertEqual(w.cf_released,[1100,17])
        self.assertEqual(w.task_reads,1);self.assertEqual(w.dispatches,0)
        b.close();b.close();self.assertEqual(w.closes,[91]);self.assertTrue(b.closed)
        self.assertEqual(len(w.io.IOServiceOpen.argtypes),4)
    def test_rejects_previous_031_without_open(self):
        w=World((b'0.31.0',))
        with self.assertRaisesRegex(client.BindingError,'0.34.0'):w.create()
        self.assertEqual(w.opens,[]);self.assertEqual(w.task_reads,0);self.assertEqual(w.released,[100,42])
    def test_rejects_wrong_or_partial_version(self):
        for value in (b'0.32.0',b'0.33.0',b'0.34',b'0.34.1',b'0.340.0',b'',b'0.34.0 trailing'):
            with self.subTest(value=value):
                w=World((value,))
                with self.assertRaises(client.BindingError):w.create()
                self.assertEqual(w.opens,[]);self.assertEqual(w.released,[100,42])
    def test_rejects_missing_service(self):
        w=World(())
        with self.assertRaises(client.BindingError):w.create()
        self.assertEqual(w.opens,[]);self.assertEqual(w.released,[42])
    def test_rejects_ambiguous_services(self):
        w=World((b'0.34.0',b'0.34.0'))
        with self.assertRaises(client.BindingError):w.create()
        self.assertEqual(w.opens,[]);self.assertEqual(w.released,[100,101,42]);self.assertEqual(w.property_keys,[])
    def test_enumeration_is_bounded_and_releases_collected_services(self):
        w=World((b'0.34.0',)*18)
        with self.assertRaisesRegex(client.BindingError,'count'):w.create()
        self.assertEqual(w.released,list(range(100,117))+[42]);self.assertEqual(w.opens,[])
    def test_missing_version_releases_key(self):
        w=World((None,))
        with self.assertRaisesRegex(client.BindingError,'Missing string'):w.create()
        self.assertEqual(w.cf_released,[17]);self.assertEqual(w.released,[100,42]);self.assertEqual(w.opens,[])
    def test_wrong_property_type_releases_cf_objects(self):
        w=World(wrong_type=True)
        with self.assertRaises(client.BindingError):w.create()
        self.assertEqual(w.cf_released,[1100,17]);self.assertEqual(w.opens,[])
    def test_failed_string_conversion_releases_cf_objects(self):
        w=World(decode_failure=True)
        with self.assertRaises(client.BindingError):w.create()
        self.assertEqual(w.cf_released,[1100,17]);self.assertEqual(w.opens,[])
    def test_matching_failures_never_open(self):
        for options in ({'no_dictionary':True},{'matching_error':1}):
            w=World(**options)
            with self.assertRaises(client.BindingError):w.create()
            self.assertEqual(w.opens,[]);self.assertEqual(w.task_reads,0)
    def test_open_failure_releases_service_and_iterator(self):
        w=World(open_error=0xe00002bc)
        with self.assertRaisesRegex(client.BindingError,'open failed'):w.create()
        self.assertEqual(w.released,[100,42]);self.assertEqual(w.closes,[]);self.assertEqual(w.dispatches,0)
    def test_null_connection_is_not_accepted(self):
        w=World(connection=0)
        with self.assertRaisesRegex(client.BindingError,'null connection'):w.create()
        self.assertEqual(w.released,[100,42]);self.assertEqual(w.dispatches,0)

if __name__=='__main__':unittest.main()
