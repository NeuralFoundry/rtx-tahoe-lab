"""Actual ctypes transport method and unchanged pre-vector diagnostic sizes."""
from pathlib import Path
import re,unittest
import gsp_vector_client as client
from test_gsp_compute_diagnostics import NativeFFI
import test_gsp_vector_native as fixtures
import gsp_vector_native as native

class Transport(unittest.TestCase):
    def backend(self,ffi=None):
        b=object.__new__(client.MacIOKitBackend);b.io=ffi or NativeFFI();b.connection=123;b.closed=False;return b

    def test_actual_ffi_accepts_new_vector_summary_and_page_wrappers(self):
        b=self.backend()
        self.assertEqual(b.vector_memory_info(),bytes(512));self.assertEqual(b.vector_submit_info(),bytes(1536));self.assertEqual(b.vector_capture_info(),bytes(512))
        for which in range(3):self.assertEqual(b.vector_capture_data(which,0,4096),bytes(4096))
        self.assertEqual([c[1] for c in b.io.calls],[64,65,66,67,67,67])
        for s in (-1,56,57,58,59,68,True,64.0,'64'):
            with self.assertRaises(ValueError):b._invoke(s,(),b'',512)
        self.assertEqual(len(b.io.calls),6)

    def test_actual_ffi_rejects_truncated_vector_summary(self):
        class Short(NativeFFI):
            def IOConnectCallMethod(self,*args):args[-1]._obj.value-=8;return 0
        with self.assertRaises(client.BindingError):self.backend(Short()).vector_submit_info()

    def test_execution_rm_size_is_preserved_in_driver_and_transport(self):
        b=self.backend();self.assertEqual(b.execution_rm_info(),bytes(640));self.assertEqual(b.io.calls[-1][1],46)
        text=(Path(__file__).resolve().parent/'driver/GSPVectorProbe.cpp').read_text()
        for selector,size in ((46,640),(64,512),(65,1536),(66,512)):
            self.assertRegex(text,r'if\(selector=='+str(selector)+r'\)\{\s*if\(!shape\(0,0,'+str(size)+r'\)\)')
        self.assertIn('if(selector>=64&&selector<=67)return computeMethod(selector,a);',text)
        self.assertNotIn('if(selector>=56&&selector<=59)',text)

    def test_raw_summary_keeps_all_64_initial_and_observed_values(self):
        f=fixtures.Fake();r=native.submit(f.files['simulated-submit-info.bin'],fixtures.GEN)
        self.assertEqual(r['completed_elements'],61);self.assertEqual(r['count'],61)
        self.assertEqual(r['initial_output0'],0xffffffff);self.assertEqual(r['output0'],0)
        for i in (1,31,32,60,61,62,63):
            key='output'+str(i);raw=fixtures.changed(f.files['simulated-submit-info.bin'],native.SUBMIT_FIELDS.index(key),r[key]^1)
            with self.subTest(i=i),self.assertRaises(ValueError):native.submit(raw,fixtures.GEN)

    def test_old_compute_capture_and_wrong_vector_identity_rejected(self):
        f=fixtures.Fake()
        for name,decoder,oldmagic in (('memory',native.memory,native.legacy.MAGICS[0]),('submit',native.submit,native.legacy.MAGICS[1]),('capture',native.capture_info,native.legacy.MAGICS[2])):
            with self.subTest(name=name),self.assertRaises(ValueError):decoder(fixtures.changed(f.files['simulated-'+name+'-info.bin'],0,oldmagic),fixtures.GEN)

if __name__=='__main__':unittest.main()
