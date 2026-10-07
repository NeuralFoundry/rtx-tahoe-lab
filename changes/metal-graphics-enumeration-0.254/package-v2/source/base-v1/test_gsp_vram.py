import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import gsp_vram_codec as v
import gsp_vram_client as client


def info():
    r=dict.fromkeys(v.FIELDS,0);r.update({k:1 for k in v.BOOLS})
    r.update(magic=0x52545856524d3230,abi=1,generation=42,saved_words=2048,written_words=4096,checked_words=4096,
             captured_bytes=16384,restored_words=2048,ticks=20000,window_before=0x12340,window_after=0x12340,
             failed_word=0xffffffff,lease_start=0x173a00000,lease_end=0x173c40000,start=v.START,end=v.START+8192,
             bytes=8192,capture_bytes=16384,budget_ns=15_000_000_000,cleanup_budget_ns=5_000_000_000,max_ticks=60000,
             owner_phase=17,pci_command=6)
    return r


def packed(r):return struct.pack('<64Q',*(r[k] for k in v.FIELDS))


class Backend:
    def __init__(self):self.data=v.patterns()
    def vram_data(self,offset,length):return self.data[offset:offset+length]


class VramTests(unittest.TestCase):
    def test_patterns_unique_and_inverted(self):
        words=struct.unpack('<4096I',v.patterns())
        self.assertEqual(len(set(words)),4096)
        for a,b in zip(words[:2048],words[2048:]):self.assertEqual(a^b,0xffffffff)

    def test_info_success_and_profile(self):
        r=v.decode(packed(info()),42);self.assertTrue(r['passed'])
        self.assertEqual(r['start']//0x100000+1,(r['end']-1)//0x100000)

    def test_info_rejects_missing_proof_or_changed_region(self):
        bad=dict(magic=1,abi=2,generation=41,failure=1,saved_words=2047,written_words=4095,checked_words=4095,
                 captured_bytes=16380,restored_words=2047,window_after=0,failed_word=0,owner_phase=7,pci_command=2,
                 lease_start=0,lease_end=0,start=0,end=0,bytes=1,capture_bytes=1,budget_ns=1,cleanup_budget_ns=1,
                 max_ticks=0,ticks=60002,elapsed_ns=15_000_000_000,cleanup_ns=5_000_000_000,reserved50=1)
        bad.update({k:0 for k in v.BOOLS if k!='passed'})
        for key,value in bad.items():
            with self.subTest(key=key):
                r=info();r[key]=value
                with self.assertRaises(ValueError):v.decode(packed(r),42)

    def test_failure_record_keeps_partial_progress(self):
        r=info();r.update(passed=0,failure=9,checked_words=4,captured_bytes=20,failed_word=4,owner_phase=7)
        self.assertFalse(v.decode(packed(r),42)['passed'])

    def test_capture_verifies_bytes_without_compute_claim(self):
        with tempfile.TemporaryDirectory() as folder:
            r=v.capture(Backend(),info(),Path(folder))
            self.assertTrue(r['readback_verified']);self.assertFalse(r['gpu_compute_verified']);self.assertFalse(r['metal_verified'])
            self.assertEqual(r['sha256'],hashlib.sha256(v.patterns()).hexdigest())

    def test_capture_rejects_corruption_and_truncation(self):
        for offset in (0,4095,8192,16383):
            b=Backend();raw=bytearray(b.data);raw[offset]^=1;b.data=bytes(raw)
            with tempfile.TemporaryDirectory() as folder:
                with self.assertRaises(ValueError):v.capture(b,info(),Path(folder))
        b=Backend();b.data=b.data[:-1]
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):v.capture(b,info(),Path(folder))

    def test_rejects_wrong_info_storage(self):
        for raw in (b'',bytes(511),bytearray(packed(info()))):
            with self.assertRaises(ValueError):v.decode(raw,42)

    def test_client_read_bounds(self):
        class Mock(client.RestrictedBackend):
            def _invoke(self,selector,scalars,data,size):return bytes(size)
        b=Mock();self.assertEqual(len(b.vram_info()),512);self.assertEqual(len(b.vram_data(12288,4096)),4096)
        for off,n in ((-1,4),(16384,1),(16383,2),(0,4097),(0,0)):
            with self.assertRaises(ValueError):b.vram_data(off,n)


if __name__=='__main__':unittest.main()
