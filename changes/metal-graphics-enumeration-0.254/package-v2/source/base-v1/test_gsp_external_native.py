"""Native C++ bytes replayed through Python and the actual IOKit FFI wrapper."""
from pathlib import Path
import tempfile
import unittest
import gsp_external_native as n
import gsp_execution_client as client
from test_gsp_execution_native import Fake,GEN,changed
from test_gsp_compute_diagnostics import NativeFFI

class External(unittest.TestCase):
    def collect(self,f,mutate=None):
        golden=dict(passed=True,rm=n.old.rm(f.files['golden-rm.bin'],GEN))
        if mutate:mutate(golden)
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'external';result=n.capture(f,GEN,out,golden)
            return result,{p.name:p.read_bytes() for p in out.iterdir()}
    def test_five_cpp_messages_and_directory_proof(self):
        result,_=self.collect(Fake())
        self.assertTrue(result['passed'],result.get('error'))
        self.assertEqual(result['info']['directory_checks'],2)
        self.assertEqual([r['function'] for r in result['records']],[103]*4+[54])
        self.assertEqual(result['info']['tx_writer'],19)
        self.assertFalse(result['compute_verified']);self.assertFalse(result['metal_verified'])
    def test_actual_ffi_all_four_read_wrappers_and_bounds(self):
        b=object.__new__(client.MacIOKitBackend);b.io=NativeFFI();b.connection=123;b.closed=False
        self.assertEqual(b.external_info(),bytes(512))
        self.assertEqual(b.external_index(0,5),bytes(360))
        self.assertEqual(b.external_data(0,4096),bytes(4096))
        self.assertEqual(b.external_request(4),bytes(4096))
        self.assertEqual([c[1:3] for c in b.io.calls],[(60,()),(61,(0,5)),(62,(0,4096)),(63,(16384,4096))])
        for method,args in [('external_index',(15,2)),('external_index',(True,1)),('external_data',(131071,2)),('external_data',(0,0)),('external_request',(5,)),('external_request',(True,))]:
            with self.subTest(method=method,args=args),self.assertRaises(ValueError):getattr(b,method)(*args)
        self.assertEqual(len(b.io.calls),4)
    def test_summary_mutations_fail(self):
        for key,value in [('client',n.prep.CLIENT),('device',0xcf000001),('vaspace',0xcf000003),('root_physical',0x1003000),('directory_flags',0),
            ('directory_checks',1),('directory_acknowledged',0),('complete',0),('consumed',4),('sent',4),('count',17),('pages',33),
            ('tx_reader',18),('initial_sequence',0xffffffff),('pinned',0),('owner_phase',0),('last_function',76),('elapsed_ns',15_000_000_000)]:
            f=Fake();f.files['external-info.bin']=changed(f.files['external-info.bin'],n.FIELDS.index(key),value)
            with self.subTest(key=key):self.assertFalse(self.collect(f)[0]['passed'])
    def test_each_request_reply_and_index_is_checked_and_preserved(self):
        for step in range(5):
            for name,offset in [('external-requests.bin',step*4096+80),('external-records.bin',step*4096+(64 if step==4 else 80)),('external-index.bin',step*72+16)]:
                f=Fake();raw=bytearray(f.files[name]);raw[offset]^=1;f.files[name]=bytes(raw)
                with self.subTest(step=step,name=name):
                    result,files=self.collect(f);self.assertFalse(result['passed'])
                    self.assertEqual(files[name.replace('external-','')],f.files[name])
    def test_returned_extent_and_prefix_must_match(self):
        f=Fake();f.files['external-info.bin']=changed(f.files['external-info.bin'],n.FIELDS.index('base'),0x2000)
        self.assertFalse(self.collect(f)[0]['passed'])
        self.assertFalse(self.collect(Fake(),lambda g:g['rm'].update(rx_sequence=19))[0]['passed'])
    def test_failure_does_not_discard_raw_evidence(self):
        f=Fake();raw=f.files['external-info.bin']
        for key,value in [('passed',0),('complete',0),('failure',4)]:raw=changed(raw,n.FIELDS.index(key),value)
        f.files['external-info.bin']=raw;result,files=self.collect(f)
        self.assertFalse(result['passed']);self.assertEqual(result['native_stop']['stage'],'external_va')
        self.assertEqual(files['records.bin'],f.files['external-records.bin'])
        self.assertEqual(files['requests.bin'],f.files['external-requests.bin'])

if __name__=='__main__':unittest.main()
