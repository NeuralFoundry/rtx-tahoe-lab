"""Replay real 0.26.1 diagnostic bytes through the capture code, without IOKit."""
from pathlib import Path
import hashlib,json,tempfile,unittest
import legacy_execution_diagnostics0261 as n
from test_gsp_execution_native import Fake

EVIDENCE=Path(__file__).resolve().parent/'evidence/context-diagnostics0261'
GEN=4294969441

class Saved(Fake):
    def __init__(self):
        self.files={p.name:p.read_bytes() for p in EVIDENCE.glob('*.bin')}
        self.calls=[];self.short=None;self.error_selector=None

class ActualDiagnostics(unittest.TestCase):
    def collect(self,backend):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'capture';r=n.capture(backend,GEN,out)
            return r,{p.name:p.read_bytes() for p in out.iterdir()}

    def test_real_fixture_hashes(self):
        m=json.loads((EVIDENCE/'manifest.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(m['generation'],GEN);self.assertEqual(len(m['files']),13)
        for f in m['files']:
            b=(EVIDENCE/f['path']).read_bytes()
            self.assertEqual(len(b),f['bytes']);self.assertEqual(hashlib.sha256(b).hexdigest(),f['sha256'])

    def test_real_rm_stop_and_assertions_visible_without_host(self):
        f=Saved();r,files=self.collect(f)
        self.assertFalse(r['passed']);self.assertFalse(r['host_command_verified'])
        self.assertFalse(r['compute_verified']);self.assertFalse(r['metal_verified'])
        self.assertEqual(r['native_stop'],dict(stage='execution_rm',failure=17,reason='record_capacity',step=3,completed=3,sent=4,records=16))
        self.assertIn('record_capacity',r['error']);self.assertNotIn('USERD/fence',r['error'])
        self.assertEqual(len(r['records']),16);self.assertEqual(r['firmware_assertions'],list(range(24,37)))
        self.assertEqual(r['records'][3]['nocat']['error_code'],'0x1af4ef0')
        self.assertTrue(r['firmware_assertions_unresolved']);self.assertFalse(r['device_bytes_verified'])
        self.assertEqual(r['fence']['command_attempted'],0)
        for name in ('records.bin','requests.bin','index.bin','device-capture.bin'):
            self.assertEqual(files[name],f.files[name])
        self.assertEqual(sum(name.startswith('record-') for name in files),16)
        self.assertTrue(all(44<=c[0]<=55 for c in f.calls))

    def test_bad_actual_checksum_preserves_native_stop_and_raw_frames(self):
        f=Saved();b=bytearray(f.files['records.bin']);b[3*4096+176]^=1;f.files['records.bin']=bytes(b)
        r,files=self.collect(f)
        self.assertFalse(r['passed']);self.assertEqual(r['native_stop']['failure'],17)
        self.assertIn('checksum',r['error'].lower());self.assertEqual(files['records.bin'],bytes(b))

    def test_bad_actual_index_preserves_native_stop_and_raw_frames(self):
        f=Saved();b=bytearray(f.files['index.bin']);b[0]^=1;f.files['index.bin']=bytes(b)
        r,files=self.collect(f)
        self.assertFalse(r['passed']);self.assertEqual(r['native_stop']['failure'],17)
        self.assertIn('index geometry',r['error']);self.assertEqual(files['index.bin'],bytes(b))

if __name__=='__main__':unittest.main()
