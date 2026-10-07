import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import gsp_bar1_codec as v
import gsp_bar1_client as client
import gsp_bar1
import copy


def info():
    r=dict.fromkeys(v.FIELDS,0);r.update({k:1 for k in v.BOOLS})
    r.update(magic=0x5254584241523231,abi=1,generation=42,saved_words=2048,written_words=4096,checked_words=4096,
             captured_bytes=16384,restored_words=2048,ticks=20000,window_before=0x12340,window_after=0x12340,
             failed_word=0xffffffff,lease_start=0x01000000,lease_end=0x01010000,start=v.START,end=v.START+8192,
             bytes=8192,capture_bytes=16384,budget_ns=15_000_000_000,cleanup_budget_ns=5_000_000_000,max_ticks=60000,
             owner_phase=17,pci_command=6,bar1_mapped=1,bar1_base=0x400000000,mapping_physical=0x401000000,mapping_offset=v.START,mapping_bytes=8192,bar1_bytes=0x4000000)
    return r


def packed(r):return struct.pack('<64Q',*(r[k] for k in v.FIELDS))


class Backend:
    def __init__(self):self.data=v.patterns()
    def bar1_data(self,offset,length):return self.data[offset:offset+length]


class Bar1Tests(unittest.TestCase):
    def test_late_failure_cannot_preserve_preparation_success(self):
        good=dict(passed=True,connection_closed=True,init_done_observed=True,
                  rm_exchange=dict(exchanges_verified=True),bar1=dict(passed=True),bar1_readback=dict(readback_verified=True))
        self.assertTrue(gsp_bar1.finalize_result(copy.deepcopy(good))['passed'])
        for key in ('error','launch_error','close_error'):
            r=copy.deepcopy(good);r[key]='late transport/decoder failure'
            self.assertFalse(gsp_bar1.finalize_result(r)['passed'])
        for key in ('passed','connection_closed','init_done_observed'):
            r=copy.deepcopy(good);r[key]=False
            self.assertFalse(gsp_bar1.finalize_result(r)['passed'])
        for parent,key in (('rm_exchange','exchanges_verified'),('bar1','passed'),('bar1_readback','readback_verified')):
            r=copy.deepcopy(good);r[parent][key]=False
            self.assertFalse(gsp_bar1.finalize_result(r)['passed'])
            r=copy.deepcopy(good);del r[parent]
            self.assertFalse(gsp_bar1.finalize_result(r)['passed'])

    def test_patterns_unique_and_inverted(self):
        words=struct.unpack('<4096I',v.patterns())
        self.assertEqual(len(set(words)),4096)
        for a,b in zip(words[:2048],words[2048:]):self.assertEqual(a^b,0xffffffff)

    def test_info_success_and_profile(self):
        r=v.decode(packed(info()),42);self.assertTrue(r['passed'])
        self.assertLess(r['end'],0x4000000)

    def test_info_rejects_missing_proof_or_changed_region(self):
        bad=dict(magic=1,abi=2,generation=41,failure=1,saved_words=2047,written_words=4095,checked_words=4095,
                 captured_bytes=16380,restored_words=2047,window_after=0,failed_word=0,owner_phase=7,pci_command=2,
                 lease_start=0,lease_end=0,start=0,end=0,bytes=1,capture_bytes=1,budget_ns=1,cleanup_budget_ns=1,
                 max_ticks=0,ticks=60002,elapsed_ns=15_000_000_000,cleanup_ns=5_000_000_000,reserved56=1,bar1_mapped=0,mapping_physical=0,mapping_offset=0,mapping_bytes=0,bar1_bytes=0,bar1_base=0)
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
        b=Mock();self.assertEqual(len(b.bar1_info()),512);self.assertEqual(len(b.bar1_data(12288,4096)),4096)
        for off,n in ((-1,4),(16384,1),(16383,2),(0,4097),(0,0)):
            with self.assertRaises(ValueError):b.bar1_data(off,n)


if __name__=='__main__':unittest.main()
