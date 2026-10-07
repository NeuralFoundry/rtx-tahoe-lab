"""Exercise the actual native transport method and preserve rejected replies."""
from pathlib import Path
import tempfile
import unittest
import gsp_execution_client as client
import gsp_execution_native as n
import test_gsp_execution_native as fixtures

class NativeFFI:
    def __init__(self):self.calls=[]
    def IOConnectCallMethod(self,connection,selector,scalars,nscalars,data,nbytes,out_scalars,scalar_count,output,count):
        self.calls.append((connection,selector,tuple(scalars[i] for i in range(nscalars)),nbytes,count._obj.value))
        self.last_output=output
        return 0

class Diagnostics(unittest.TestCase):
    def test_actual_mac_transport_accepts_all_new_read_wrappers(self):
        # Deliberately bypass only construction/IOServiceOpen, not _invoke.
        backend=object.__new__(client.MacIOKitBackend);backend.io=NativeFFI();backend.connection=123;backend.closed=False
        self.assertEqual(backend.compute_memory_info(),bytes(512))
        self.assertEqual(backend.compute_submit_info(),bytes(640))
        self.assertEqual(backend.compute_capture_info(),bytes(512))
        for which in range(3):self.assertEqual(backend.compute_capture_data(which,0,4096),bytes(4096))
        self.assertEqual([c[1] for c in backend.io.calls],[56,57,58,59,59,59])
        self.assertEqual([c[2] for c in backend.io.calls[-3:] ],[(0,0,4096),(1,0,4096),(2,0,4096)])
        for selector in (-1,64,True,56.0,'56'):
            with self.assertRaises(ValueError):backend._invoke(selector,(),b'',512)
        self.assertEqual(len(backend.io.calls),6)
    def test_actual_mac_transport_rejects_short_ffi_output(self):
        class Short(NativeFFI):
            def IOConnectCallMethod(self,*args):args[-1]._obj.value-=1;return 0
        backend=object.__new__(client.MacIOKitBackend);backend.io=Short();backend.connection=123
        with self.assertRaises(client.BindingError):backend.compute_submit_info()
    def collect(self,backend):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);golden_dir=root/'golden';golden_dir.mkdir();out=root/'capture'
            for name in ('root','children'):(golden_dir/(name+'-capture.bin')).write_bytes(backend.files['golden-'+name+'.bin'])
            golden=dict(passed=True,plan=n.old.plan(backend.files['golden-plan.bin'],fixtures.GEN),rm=n.old.rm(backend.files['golden-rm.bin'],fixtures.GEN))
            result=n.capture(backend,fixtures.GEN,out,golden,golden_dir)
            return result,{p.name:p.read_bytes() for p in out.iterdir() if p.is_file()}
    def test_unexecuted_host_cannot_hide_native_reply(self):
        f=fixtures.Fake();f.files['device-capture.bin']=bytes(12288)
        r,files=self.collect(f);self.assertFalse(r['passed']);self.assertIn('command/USERD/fence',r['error'])
        for name in ('index.bin','records.bin','requests.bin'):self.assertEqual(files[name],f.files[name])
        self.assertLess(next(i for i,c in enumerate(f.calls) if c[0]==48),next(i for i,c in enumerate(f.calls) if c[0]==54))
    def test_bad_index_still_preserves_all_raw_evidence(self):
        f=fixtures.Fake();f.files['index.bin']=fixtures.changed(f.files['index.bin'],0,1)
        r,files=self.collect(f);self.assertFalse(r['passed'])
        for name in ('index.bin','records.bin','requests.bin','device-capture.bin','root-capture.bin','children-capture.bin'):self.assertEqual(files[name],f.files[name])
    def test_each_capture_transport_failure_keeps_other_images(self):
        for selector,missing in ((47,'index.bin'),(48,'records.bin'),(49,'requests.bin'),(51,'root-capture.bin'),(54,'device-capture.bin')):
            f=fixtures.Fake();f.error_selector=selector;r,files=self.collect(f);self.assertFalse(r['passed'])
            for name in ('index.bin','records.bin','requests.bin','device-capture.bin'):
                if name!=missing:self.assertEqual(files[name],f.files[name])
    def test_partial_record_stream_is_saved_before_later_failure(self):
        class Failing(fixtures.Fake):
            def _invoke(self,selector,scalars,data,size):
                if selector==48 and scalars[0]==4096:raise client.BindingError('Second record page unavailable')
                return super()._invoke(selector,scalars,data,size)
        f=Failing();r,files=self.collect(f);self.assertFalse(r['passed'])
        self.assertEqual(files['records.bin'],f.files['records.bin'][:4096]);self.assertEqual(files['requests.bin'],f.files['requests.bin'])
        self.assertEqual(files['device-capture.bin'],f.files['device-capture.bin'])

if __name__=='__main__':unittest.main()
