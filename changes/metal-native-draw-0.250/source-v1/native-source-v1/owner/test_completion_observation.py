from pathlib import Path
import os,struct,unittest
import completion_observation as c
ROOT=Path(os.environ.get('RTX_OBSERVATION_FIXTURES',str(Path(__file__).resolve().parent.parent/'windows-v3')))
SUFFIX=os.environ.get('RTX_OBSERVATION_SUFFIX','')
def raw(mode,name):return (ROOT/(mode+SUFFIX)/name).read_bytes()
def decode(data,legacy=False):gen,serial=struct.unpack_from('<QQ',data,16);return c.decode(data,gen,serial,allow_legacy=legacy)
def change(data,index,value):b=bytearray(data);struct.pack_into('<Q',b,index*8,value);return bytes(b)
class Observation(unittest.TestCase):
    def test_all_native_cpu_records(self):
        count=0
        for mode in ('legacy','ordered','uploaded'):
            for p in (ROOT/(mode+SUFFIX)).glob('*.bin'):
                if mode=='uploaded' and not p.name.endswith('-observation.bin'):continue
                decode(p.read_bytes(),mode=='legacy');count+=1
        self.assertEqual(count,841)
    def test_empty(self):
        r=decode(raw('ordered','empty.bin'));self.assertEqual(r['serial'],0);self.assertEqual(r['observations'],0);self.assertEqual(r['last']['stage'],0)
        retained=decode(raw('ordered','empty-retained.bin'));self.assertEqual(retained['closed'],1);self.assertEqual(retained['serial'],0);self.assertFalse(retained['attempted'])
    def test_interleaving_diagnostic(self):
        old=decode(raw('legacy','mode-2.bin'),True);new=decode(raw('ordered','mode-2.bin'))
        self.assertEqual((old['failure'],old['mismatch_mask'],old['last']['qmd'],old['last']['timeline']),(10,16,0,1))
        self.assertEqual((new['passed'],new['last']['stage'],new['last']['qmd'],new['last']['timeline']),(1,5,1,1))
    def test_legacy_requires_explicit_cpu_option(self):
        with self.assertRaises(ValueError):decode(raw('legacy','mode-2.bin'))
    def test_scope_and_size(self):
        b=raw('ordered','mode-0.bin');gen,serial=struct.unpack_from('<QQ',b,16)
        for bad in (b[:-1],b+b'\0',bytearray(b)):
            with self.assertRaises(ValueError):c.decode(bad,gen,serial)
        for g,s in ((gen+1,serial),(gen,serial+1),(True,serial),(gen,True)):
            with self.assertRaises(ValueError):c.decode(b,g,s)
    def test_malformed_fields(self):
        b=raw('ordered','mode-0.bin');gen,serial=struct.unpack_from('<QQ',b,16)
        cases={0:0,1:2,2:gen+1,3:serial+1,4:2,6:15,7:2,8:0,9:0,10:8,11:2,12:65537,13:65537,16:0,17:32,18:5,19:0,20:2,21:90113,22:0,23:2,24:0,25:15,26:0,27:2,32:6,33:0,34:0,35:0,36:0,37:2**64-1,38:2**64-1,39:31,40:31,41:2,42:2,48:1,49:0,50:0,51:0,52:0,55:31,56:31,57:2,58:2}
        for index in (*range(28,32),*range(43,48),*range(59,64)):cases[index]=1
        for index,value in cases.items():
            with self.subTest(index=index):
                with self.assertRaises(ValueError):c.decode(change(b,index,value),gen,serial)
    def test_persistent_bad_completion_still_rejected(self):
        a=decode(raw('ordered','mode-5.bin'));b=decode(raw('ordered','mode-7.bin'))
        self.assertEqual(a['mismatch_mask'],16);self.assertEqual(b['mismatch_mask'],20)
        for row in (a,b):self.assertEqual(row['failure'],10);self.assertFalse(row['passed']);self.assertEqual(row['phase'],3)
    def test_timeout_does_not_invent_mismatch(self):
        r=decode(raw('ordered','mode-6.bin'));self.assertEqual(r['failure'],7);self.assertEqual(r['mismatch_mask'],0);self.assertFalse(r['passed'])
    def test_runtime_failure_after_core_completion(self):
        rows=[decode(p.read_bytes()) for p in (ROOT/('ordered'+SUFFIX)).glob('fault-*.bin')];late=[r for r in rows if r['core_passed'] and not r['passed']]
        self.assertTrue(late)
        for r in late:self.assertEqual(r['failure'],13);self.assertEqual(r['core_failure'],0);self.assertEqual(r['completed'],2);self.assertEqual(r['backing_completed'],1)
    def test_partial_reads_not_complete(self):
        rows=[decode(p.read_bytes()) for p in (ROOT/('ordered'+SUFFIX)).glob('fault-*.bin')]
        partial=[r for r in rows if r['last']['read_attempted'] and not r['last']['read_passed']]
        self.assertTrue(partial)
        for r in partial:self.assertFalse(r['last']['complete']);self.assertEqual(r['core_failure'],9)
    def test_uploaded_all_serials_and_ring_wraps(self):
        for serial in range(1,66):
            r=decode(raw('uploaded','job-%d-observation.bin'%serial));self.assertTrue(r['passed']);self.assertEqual(r['serial'],serial);self.assertEqual(r['last']['get'],(serial+1)&31)
    def test_mismatch_mask_matches_record(self):
        b=raw('ordered','mode-5.bin');g,s=struct.unpack_from('<QQ',b,16)
        for mask in (1,2,4,8,20,31):
            with self.assertRaises(ValueError):c.decode(change(b,17,mask),g,s)
    def test_full_supported_child_capture_bound(self):
        b=raw('ordered','mode-0.bin');g,s=struct.unpack_from('<QQ',b,16)
        self.assertEqual(c.decode(change(b,21,94208),g,s)['capture_bytes'],94208)
        for size in (94209,94208+4096,1):
            with self.assertRaises(ValueError):c.decode(change(b,21,size),g,s)
if __name__=='__main__':unittest.main(verbosity=2)
