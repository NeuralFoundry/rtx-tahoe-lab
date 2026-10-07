"""Synthetic native ABI fixtures and bounded capture regressions; no IOKit."""
from pathlib import Path
import struct
import tempfile
import unittest
import gsp_channel_native as n
import gsp_channel_client as client
import gsp_channel as runner

FIXTURE=Path(__file__).resolve().parent/'changes/gsp-channel-0.23/native/windows-fixture'

class Fake:
    def __init__(self):
        self.files={p.name:p.read_bytes() for p in FIXTURE.glob('*.bin')}
    def channel_memory_info(self,i):return self.files[('contexts' if i else 'ring')+'-info.bin']
    def channel_rm_info(self):return self.files['rm-info.bin']
    def channel_plan_info(self):return self.files['plan-info.bin']
    def channel_snapshot_info(self):return self.files['snapshot-info.bin']
    def channel_rm_index(self,start,count):return self.files['index.bin'][start*72:(start+count)*72]
    def channel_rm_data(self,off,size):return self.files['records.bin'][off:off+size]
    def channel_request(self,step):return self.files['requests.bin'][step*4096:(step+1)*4096]
    def channel_snapshot_data(self,i,off,size):return self.files[('children' if i else 'root')+'-capture.bin'][off:off+size]

def changed(raw,index,value):
    b=bytearray(raw);struct.pack_into('<Q',b,index*8,value);return bytes(b)

class Native(unittest.TestCase):
    def capture(self,fake):
        with tempfile.TemporaryDirectory() as folder:
            return n.capture(fake,777,Path(folder)/'capture')
    def test_native_cpp_abi_and_five_synthetic_exchanges(self):
        r=self.capture(Fake())
        self.assertTrue(r['passed']);self.assertTrue(r['table_readback_verified'])
        self.assertFalse(r['hardware_compute_verified']);self.assertFalse(r['metal_verified'])
        self.assertEqual(len(r['records']),5)
        self.assertEqual(r['plan']['total_backing_bytes'],13254656)
    def test_reserved_size_generation_rejected(self):
        f=Fake()
        for name,decode in [('ring',lambda b:n.memory(b,777,0)),('contexts',lambda b:n.memory(b,777,1)),
            ('rm',lambda b:n.rm(b,777)),('snapshot',lambda b:n.snapshot(b,777)),('plan',lambda b:n.plan(b,777))]:
            raw=f.files[name+'-info.bin']
            for corrupt in (raw[:-1],raw+b'\0',changed(raw,2,778),changed(raw,len(raw)//8-1,1)):
                with self.subTest(name=name),self.assertRaises(ValueError):decode(corrupt)
    def test_memory_success_proof_mutations(self):
        raw=Fake().files['ring-info.bin']
        for key,value in [('failure',1),('pinned',0),('pci_command',0),('owner_phase',16),('mapping_physical',0),
            ('backing_verified',0),('inv_passed',0),('inv_writes',2),('inv_last_value',0x80000000),
            ('verified_child_bytes',4096),('window_after',0),('zeroed_bytes',4096),('elapsed_ns',90_000_000_000)]:
            with self.subTest(key=key),self.assertRaises(ValueError):n.memory(changed(raw,n.MEMORY_FIELDS.index(key),value),777,0)
    def test_rm_success_proof_mutations(self):
        raw=Fake().files['rm-info.bin']
        for key,value in [('completed',4),('doorbells',4),('tx_reader',13),('context_prepared',0),('failure',1),
            ('owner_phase',7),('snapshot_passed',0),('request_bytes',4096),('pinned',0),('claimed',0)]:
            with self.subTest(key=key),self.assertRaises(ValueError):n.rm(changed(raw,n.RM_FIELDS.index(key),value),777)
    def test_snapshot_proof_mutations(self):
        raw=Fake().files['snapshot-info.bin']
        for key,value in [('root_bytes',8192),('child_bytes',0),('reads',0),('elapsed_ns',5_000_000_000),('failure',1),('pinned',0)]:
            with self.subTest(key=key),self.assertRaises(ValueError):n.snapshot(changed(raw,n.SNAP_FIELDS.index(key),value),777)
    def test_captured_bytes_corruption_rejected(self):
        for name,offset in [('root-capture.bin',n.gmmu.PARENT_OFFSET),('children-capture.bin',4096),('records.bin',80),('requests.bin',128),('index.bin',0)]:
            f=Fake();raw=bytearray(f.files[name]);raw[offset]^=1;f.files[name]=bytes(raw)
            with self.subTest(name=name),self.assertRaises(ValueError):self.capture(f)
    def test_native_plan_cannot_substitute_historical_gr(self):
        f=Fake();f.files['plan-info.bin']=changed(f.files['plan-info.bin'],8+4,4096)
        with self.assertRaises(ValueError):self.capture(f)
    def test_truncated_diagnostics_rejected(self):
        for name in ('index.bin','records.bin','root-capture.bin','children-capture.bin'):
            f=Fake();f.files[name]=f.files[name][:-1]
            with self.subTest(name=name),self.assertRaises(ValueError):self.capture(f)
    def test_partial_snapshot_keeps_read_bytes_without_success(self):
        f=Fake();raw=f.files['snapshot-info.bin']
        for key,value in [('passed',0),('failure',5),('child_bytes',0),('reads',4)]:raw=changed(raw,n.SNAP_FIELDS.index(key),value)
        f.files['snapshot-info.bin']=raw;raw=f.files['rm-info.bin']
        for key in ('snapshot_passed','complete'):raw=changed(raw,n.RM_FIELDS.index(key),0)
        f.files['rm-info.bin']=raw
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'capture';r=n.capture(f,777,out)
            self.assertFalse(r['passed']);self.assertFalse(r['table_readback_verified'])
            self.assertEqual((out/'root-capture.bin').read_bytes(),f.files['root-capture.bin'])
            self.assertEqual((out/'children-capture.bin').read_bytes(),b'')
    def test_final_result_requires_channel_and_connection(self):
        base=dict(passed=True,connection_closed=True,init_done_observed=True,rm_exchange={'exchanges_verified':True},
            bar1={'passed':True},bar1_readback={'readback_verified':True},page_tables={'passed':True},page_rm={'passed':True},
            page_rm_exchange={'exchanges_verified':True},page_table_captures={'captures_verified':True},channel={'passed':True})
        self.assertTrue(runner.finalize_result(dict(base))['passed'])
        for key,value in [('channel',{}),('connection_closed',False),('error','late error'),('close_error','late close')]:
            r=dict(base);r[key]=value;self.assertFalse(runner.finalize_result(r)['passed'])
        self.assertFalse(runner.finalize_result(dict(base))['metal_verified'])
    def test_client_bounds_reject_before_transport(self):
        class Backend(client.RestrictedBackend):
            closed=False
            def _invoke(self,*args):raise AssertionError('Unexpected transport')
        b=Backend()
        for method,args in [('channel_memory_info',(2,)),('channel_request',(5,)),('channel_request',(True,)),
            ('channel_rm_index',(15,2)),('channel_rm_data',(131071,2)),('channel_snapshot_data',(0,12287,2)),
            ('channel_snapshot_data',(1,45055,2)),('channel_snapshot_data',(2,0,1))]:
            with self.subTest(method=method,args=args),self.assertRaises(ValueError):getattr(b,method)(*args)

if __name__=='__main__':unittest.main()
