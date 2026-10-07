"""Actual ctypes/native upload ABI plus explicitly CPU-injected runtime records."""
from pathlib import Path
import ctypes,hashlib,json,os,struct,tempfile,unittest
from unittest.mock import patch
import gsp_uploaded_client as client
import gsp_uploaded_run as work
import gsp_uploaded as runner
import uploaded_transport as upload
import uploaded_request as request
import uploaded_library as library
import uploaded_native as native
ROOT=Path(__file__).resolve().parent;GEN=0x130603601
RAW=Path(os.environ['RTX_UPLOADED_NATIVE']);BRIDGE=Path(os.environ['RTX_UPLOADED_BRIDGE'])
def catalog():return library.load(ROOT/'selected-library.json',(ROOT/'selected-library.sha256').read_text().strip())
def raw(name):return (RAW/name).read_bytes()
def change(b,index,value):return b[:index*8]+struct.pack('<Q',value)+b[index*8+8:]
class Shim:
    def __init__(self,backend):self.backend=backend
    def IOConnectCallMethod(self,connection,selector,scalars,count,source,size,out_scalars,out_count,dest,dest_size):
        b=self.backend;n=ctypes.cast(dest_size,ctypes.POINTER(ctypes.c_size_t)).contents.value
        return b.library.rtx_upload_fixture_call(b.handle,selector,scalars,count,source,size,dest,n)
class Backend(client.RestrictedBackend):
    def __init__(self):
        self.catalog=catalog();self.closed=False;self.connection=1;self.calls=[];self.fail=None;self.corrupt=None;self.completed=0;self.begun=False
        self.library=ctypes.CDLL(str(BRIDGE));self.library.rtx_upload_fixture_new.argtypes=[ctypes.c_uint64];self.library.rtx_upload_fixture_new.restype=ctypes.c_void_p
        self.library.rtx_upload_fixture_call.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint64),ctypes.c_uint32,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t];self.library.rtx_upload_fixture_call.restype=ctypes.c_uint32
        self.library.rtx_upload_fixture_delete.argtypes=[ctypes.c_void_p];self.library.rtx_upload_fixture_delete.restype=None
        self.handle=self.library.rtx_upload_fixture_new(GEN);assert self.handle;self.io=Shim(self)
    def _check(self,code,label):
        if code:raise client.BindingError(label+': native CPU upload error '+str(code))
    def _invoke(self,selector,scalars,data,size):
        self.calls.append((selector,tuple(scalars),len(data),size))
        if selector>=72 or selector==0:
            b=client.MacIOKitBackend._invoke(self,selector,scalars,data,size)
            if selector==0:self.begun=True
        elif selector==1:
            v=[0]*160;v[:16]=[client.MAGIC,1,int(self.begun),9,4096,0x824148,1,1,2,0,9*4096,GEN,int(self.begun),0,0,0]
            for i in range(9):v[16+i*16:19+i*16]=[i,4096,1]
            b=struct.pack('<160Q',*v)
        elif selector==2:b=struct.pack('<Q',0x100000+scalars[0]*4096)
        elif selector==69:
            serial=self.completed+1
            if data!=raw('job-%d-request.bin'%serial):raise ValueError('unexpected native fixture request')
            self.completed=serial;b=b''
        elif selector==68:b=raw('job-%d-info.bin'%self.completed) if self.completed else raw('initial-info.bin')
        elif selector in (64,66):b=change((ROOT/'reusable-fixture-baseline'/('memory-info.bin' if selector==64 else 'capture-info.bin')).read_bytes(),2,GEN)
        elif selector==67:
            part,off,n=scalars;b=raw('initial-'+work.PARTS[part]+'.bin')[off:off+n]
        elif selector==70:
            if scalars!=(self.completed,) or not self.completed:raise client.BindingError('stale serial')
            b=raw('job-%d-job.bin'%self.completed)
        elif selector==71:
            serial,part,off,n=scalars
            if part<5:
                if serial!=self.completed or not serial:raise client.BindingError('stale capture')
                b=raw('job-%d-%s.bin'%(serial,work.PARTS[part]))
            else:b=(self.catalog.library,self.catalog.code)[part-5]
            b=b[off:off+n]
        else:raise client.BindingError('unsupported CPU fixture selector '+str(selector))
        if self.fail==selector or self.fail==(selector,tuple(scalars)) or (selector==69 and self.fail==('submit',self.completed)):raise client.BindingError('uncertain injected call')
        return self.corrupt(selector,scalars,b) if self.corrupt else b
    def _close(self):
        handle,self.handle=self.handle,None;self.connection=0
        if handle:self.library.rtx_upload_fixture_delete(handle)

def setup(backend,root):
    transferred=upload.prepare(backend,backend.catalog,GEN,root/'shader-upload');assert transferred['passed'];backend.begin();upload.consumed(backend,backend.catalog,GEN,root/'consumed.bin')
    before=root/'execution';before.mkdir()
    for name in work.PARTS[:3]:(before/(name+'-capture.bin')).write_bytes((ROOT/'reusable-fixture-baseline'/('before-'+name+'.bin')).read_bytes())
    prior=dict(passed=True,host_command_verified=True,table_readback_verified=True,rm=dict(generation=GEN,candidate=4))
    bootstrap=root/'runtime-bootstrap';initial=work.bootstrap(backend,GEN,bootstrap,prior,before);assert initial['passed'],initial
    return initial,prior,before,bootstrap

class Upload(unittest.TestCase):
    def test_actual_ctypes_native_transfer(self):
        with tempfile.TemporaryDirectory() as name:
            b=Backend()
            try:
                r=upload.prepare(b,b.catalog,GEN,Path(name)/'upload');self.assertTrue(r['passed']);self.assertEqual(len(r['phases']),9)
                self.assertEqual(sum(c[0]==73 for c in b.calls),1);self.assertEqual(sum(c[0]==74 for c in b.calls),5);self.assertFalse(any(c[0]==0 for c in b.calls))
                b.begin();self.assertEqual(upload.consumed(b,b.catalog,GEN,Path(name)/'consumed.bin')['phase'],3)
                with self.assertRaises(client.BindingError):b.begin()
                with self.assertRaises(client.BindingError):b.upload_append(0,(b.catalog.library+b.catalog.code)[:1024])
            finally:b.close()
    def test_uncertain_upload_never_retries(self):
        for fail in (73,75,(74,(0,)),(74,(1024,)),(74,(4096,))):
            with self.subTest(fail=fail),tempfile.TemporaryDirectory() as name:
                b=Backend();b.fail=fail
                try:
                    r=upload.prepare(b,b.catalog,GEN,Path(name)/'upload');self.assertFalse(r['passed']);self.assertFalse(any(c[0]==0 for c in b.calls))
                    calls=[c for c in b.calls if c[0]==74];self.assertEqual(len(calls),len({c[1][0] for c in calls}))
                finally:b.close()
    def test_corrupt_and_partial_readback(self):
        for partial in (False,True):
            with self.subTest(partial=partial),tempfile.TemporaryDirectory() as name:
                b=Backend();b.corrupt=lambda s,a,data:((data[:-1] if partial else bytes([data[0]^1])+data[1:]) if s==76 else data)
                try:self.assertFalse(upload.prepare(b,b.catalog,GEN,Path(name)/'upload')['passed']);self.assertFalse(any(c[0]==0 for c in b.calls))
                finally:b.close()
    def test_ctypes_transport_shape_rejection_before_native(self):
        b=Backend()
        try:
            for selector,scalars,data,n in [(72,(1,),b'',256),(73,(),b'x'*127,0),(74,(1,),bytes(1024),0),(74,(2**63,),bytes(1024),0),(75,(),b'X',0),(76,(0,511,2),b'',2),(76,(2,0,1),b'',1),(77,(),b'',0)]:
                with self.subTest(selector=selector,scalars=scalars),self.assertRaises(ValueError):client.MacIOKitBackend._invoke(b,selector,scalars,data,n)
            self.assertEqual(library.parse((ROOT/'selected-library.json').read_bytes())['abi'],1)
            self.assertEqual(struct.unpack_from('<I',b.upload_info(),24)[0],0)
        finally:b.close()
    def test_full_runner_upload_failure_precedes_begin(self):
        with tempfile.TemporaryDirectory() as name:
            b=Backend();b.fail=75
            with patch.object(runner.prepare_gsp,'bind',side_effect=AssertionError('must not bind firmware')):
                result=runner.run(b,None,None,Path(name))
            self.assertFalse(result['passed']);self.assertTrue(b.closed);self.assertFalse(any(c[0] in (0,13) for c in b.calls))
    def test_full_runner_checks_consumption_before_firmware(self):
        with tempfile.TemporaryDirectory() as name:
            b=Backend()
            with patch.object(runner.prepare_gsp,'bind',side_effect=ValueError('stop before firmware buffers')):
                result=runner.run(b,None,None,Path(name))
            self.assertFalse(result['passed']);self.assertTrue(result['shader_upload']['passed']);self.assertEqual(result['library_consumed']['phase'],3);self.assertTrue(b.closed)
            self.assertEqual(sum(c[0]==0 for c in b.calls),1);self.assertFalse(any(c[0]==13 for c in b.calls))

class Runtime(unittest.TestCase):
    def test_complete_native_rehearsal(self):
        with tempfile.TemporaryDirectory() as name:
            b=Backend();root=Path(name)
            try:
                initial,prior,before,bootstrap=setup(b,root);r=work.dispatch(b,GEN,root/'program',initial,prior,before,bootstrap)
                self.assertTrue(r['passed'],r.get('error'));self.assertEqual(r['results_checked'],3872);self.assertEqual(r['active_elements'],3872);self.assertEqual(r['ring_wraps'],2)
                self.assertEqual(r['programs'],[i%4 for i in range(65)])
            finally:b.close()
    def test_uncertain_submit_after_ring_wrap(self):
        with tempfile.TemporaryDirectory() as name:
            b=Backend();b.fail=('submit',33);root=Path(name)
            try:
                initial,prior,before,bootstrap=setup(b,root);r=work.dispatch(b,GEN,root/'program',initial,prior,before,bootstrap)
                self.assertFalse(r['passed']);self.assertEqual(sum(c[0]==69 for c in b.calls),33);self.assertTrue((root/'program/job-33/device-capture.bin').exists())
            finally:b.close()
    def test_program_count_and_group_binding(self):
        c=catalog();v=raw('job-1-info.bin');j=raw('job-1-job.bin')
        self.assertTrue(native.info(v,GEN,c)['ready']);self.assertTrue(native.job(j,GEN,1,40960,c)['passed'])
        for count in (0,1,2,3,5):
            with self.assertRaises(ValueError):native.info(change(v,19,count),GEN,c)
        for groups in (0,3,2**32):
            with self.assertRaises(ValueError):native.job(change(j,61,groups),GEN,1,40960,c)
    def test_output_corruption_stops(self):
        with tempfile.TemporaryDirectory() as name:
            b=Backend();root=Path(name)
            try:
                initial,prior,before,bootstrap=setup(b,root)
                def corrupt(s,a,data):
                    if s==71 and a[:3]==(1,2,16384):return data[:768]+bytes([data[768]^1])+data[769:]
                    return data
                b.corrupt=corrupt;r=work.dispatch(b,GEN,root/'program',initial,prior,before,bootstrap)
                self.assertFalse(r['passed']);self.assertEqual(sum(c[0]==69 for c in b.calls),1)
            finally:b.close()
if __name__=='__main__':unittest.main()
