"""Cross-language CPU fixtures, hostile diagnostics and bounded transport tests."""
from pathlib import Path
import struct
import tempfile
import unittest
import gsp_execution_native as n
import gsp_execution_client as client
import gsp_execution as runner

FIXTURE=Path(__file__).resolve().parent/'changes/gsp-submit-0.24/native/windows-client'
GEN=0x12345678

def changed(raw,index,value):
    b=bytearray(raw);struct.pack_into('<Q',b,index*8,value);return bytes(b)

class Fake(client.RestrictedBackend):
    closed=False
    def __init__(self):
        self.files={p.name:p.read_bytes() for p in FIXTURE.glob('*.bin')};self.calls=[];self.short=None;self.error_selector=None
    def _invoke(self,selector,scalars,data,size):
        self.calls.append((selector,scalars,size))
        if selector==self.error_selector:raise client.BindingError('simulated diagnostic failure')
        names={44:'fixed-info.bin',45:'contexts-info.bin',46:'rm-info.bin',50:'plan-info.bin',52:'snapshot-info.bin',53:'fence-info.bin',55:'device-info.bin',60:'external-info.bin'}
        if selector in names:out=self.files[names[selector]]
        elif selector in (47,48,49,54,61,62,63):
            name={47:'index.bin',48:'records.bin',49:'requests.bin',54:'device-capture.bin',61:'external-index.bin',62:'external-records.bin',63:'external-requests.bin'}[selector]
            off,count=scalars;stride=72 if selector in (47,61) else 1;out=self.files[name][off*stride:(off+count)*stride]
        elif selector==51:
            which,off,count=scalars;out=self.files[('children' if which else 'root')+'-capture.bin'][off:off+count]
        else:raise AssertionError('Non-diagnostic transport call')
        return out[:-1] if selector==self.short else out

class Execution(unittest.TestCase):
    def golden(self,f):
        return dict(passed=True,plan=n.old.plan(f.files['golden-plan.bin'],GEN),rm=n.old.rm(f.files['golden-rm.bin'],GEN))
    def capture(self,f,alter_golden=None):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);g=root/'golden';g.mkdir()
            (g/'root-capture.bin').write_bytes(f.files['golden-root.bin']);(g/'children-capture.bin').write_bytes(f.files['golden-children.bin'])
            golden=self.golden(f)
            if alter_golden:alter_golden(golden)
            return n.capture(f,GEN,root/'execution',golden,g)
    def test_cpp_fixture_and_ten_rm_full_capture(self):
        result=self.capture(Fake())
        self.assertTrue(result['passed'],result.get('error'));self.assertTrue(result['host_command_verified'])
        self.assertTrue(result['table_readback_verified']);self.assertEqual(result['journal']['completed'],n.EXECUTION_STEPS)
        self.assertFalse(result['compute_verified']);self.assertFalse(result['metal_verified'])
    def test_all_summary_sizes_generation_and_reserved_rejected(self):
        f=Fake();golden=self.golden(f)['plan']
        for name,decode in [('fixed',lambda b:n.memory(b,GEN,0)),('contexts',lambda b:n.memory(b,GEN,1)),('rm',lambda b:n.rm(b,GEN)),
            ('plan',lambda b:n.plan(b,GEN,golden)),('snapshot',lambda b:n.snapshot(b,GEN)),('fence',lambda b:n.fence(b,GEN)),('device',lambda b:n.device(b,GEN))]:
            raw=f.files[name+'-info.bin']
            for bad in (raw[:-1],raw+b'\0',changed(raw,2,GEN+1),changed(raw,len(raw)//8-1,1)):
                with self.subTest(name=name),self.assertRaises(ValueError):decode(bad)
    def test_memory_success_mutations(self):
        for stage,name in [(0,'fixed'),(1,'contexts')]:
            for key,value in [('failure',1),('owner_phase',16),('pinned',0),('mapped',0),('pci_command',0),('inv_passed',0),('inv_writes',2),
                ('verified_child_bytes',4096),('elapsed_ns',90_000_000_000),('links_published',0),('zeroed_bytes',4096)]:
                f=Fake();f.files[name+'-info.bin']=changed(f.files[name+'-info.bin'],n.MEMORY_FIELDS.index(key),value)
                with self.subTest(stage=stage,key=key):self.assertFalse(self.capture(f)['passed'])
    def test_rm_success_mutations(self):
        for key,value in [('completed',9),('doorbells',9),('tx_reader',23),('consumed',9),('fixed_prepared',0),('context_prepared',0),
            ('channel_id',3),('raw_token',3),('candidate',5),('runlist_id',128),('pbdmas',3),('pbdma1',0),('pinned',0),('request_bytes',4096),
            ('elapsed_ns',15_000_000_000),('count',17),('initial_sequence',0xffffffff)]:
            f=Fake();f.files['rm-info.bin']=changed(f.files['rm-info.bin'],n.RM_FIELDS.index(key),value)
            with self.subTest(key=key):self.assertFalse(self.capture(f)['passed'])
    def test_host_fence_success_mutations(self):
        for key,value in [('failure',1),('native_claimed',0),('native_notified',0),('native_phase',2),('initial_get',1),('initial_put',1),('initial_fence',0x30602401),
            ('last_get',0),('last_put',0),('last_fence',0),('elapsed_ns',5_000_000_000),('operations',65538),('writes',2),('doorbell',0x110c00),
            ('command_va',0),('word0',0),('entry',0),('pinned',0),('token',5)]:
            f=Fake();f.files['fence-info.bin']=changed(f.files['fence-info.bin'],n.FENCE_FIELDS.index(key),value)
            with self.subTest(key=key):self.assertFalse(self.capture(f)['passed'])
    def test_device_and_table_corruptions(self):
        for name,offset in [('device-capture.bin',0),('device-capture.bin',8),('device-capture.bin',0x888),('device-capture.bin',0x88c),
            ('device-capture.bin',4096),('device-capture.bin',4116),('device-capture.bin',8192),('device-capture.bin',8196),
            ('root-capture.bin',0),('children-capture.bin',4096),('golden-children.bin',4096),('requests.bin',128),('records.bin',80),('index.bin',0)]:
            f=Fake();b=bytearray(f.files[name]);b[offset]^=1;f.files[name]=bytes(b)
            with self.subTest(name=name,offset=offset):self.assertFalse(self.capture(f)['passed'])
    def test_every_record_and_request_is_checked(self):
        for step in range(n.EXECUTION_STEPS):
            for name in ('records.bin','requests.bin'):
                f=Fake();b=bytearray(f.files[name]);b[step*4096+80]^=1;f.files[name]=bytes(b)
                with self.subTest(step=step,name=name):self.assertFalse(self.capture(f)['passed'])
    def test_missing_or_wrong_golden_prefix(self):
        for change in [lambda g:g.update(passed=False),lambda g:g['rm'].update(generation=GEN+1),
                       lambda g:g['rm'].update(channel_id=4),lambda g:g['rm'].update(rx_sequence=0),lambda g:g['rm'].update(rx_reader=0)]:
            self.assertFalse(self.capture(Fake(),change)['passed'])
    def test_all_diagnostic_transport_truncations(self):
        for selector in range(44,56):
            f=Fake();f.short=selector
            with self.subTest(selector=selector):self.assertFalse(self.capture(f)['passed'])
    def test_failed_summary_does_not_hide_other_raw_summaries(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'e';f=Fake();f.error_selector=44;r=n.capture(f,GEN,out)
            self.assertFalse(r['passed']);self.assertIn('fixed',r['summary_errors'])
            for name in ('contexts','rm','plan','snapshot','fence','device'):self.assertTrue((out/(name+'-info.bin')).is_file())
    def test_partial_device_capture_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'e';f=Fake();b=f.files['device-info.bin']
            for key,value in [('passed',0),('failure',3),('bytes',8192)]:b=changed(b,n.CAPTURE_FIELDS.index(key),value)
            f.files['device-info.bin']=b;r=n.capture(f,GEN,out)
            self.assertFalse(r['passed']);self.assertEqual((out/'device-capture.bin').read_bytes(),f.files['device-capture.bin'][:8192])
    def test_owner_phase_and_window_cleanup_required(self):
        for name,fields,key,value in [('fixed',n.MEMORY_FIELDS,'window_restored',0),('fixed',n.MEMORY_FIELDS,'physical_mode',1),
            ('contexts',n.MEMORY_FIELDS,'owner_phase',7),('rm',n.RM_FIELDS,'owner_phase',7),('fence',n.FENCE_FIELDS,'owner_phase',7)]:
            f=Fake();f.files[name+'-info.bin']=changed(f.files[name+'-info.bin'],fields.index(key),value)
            self.assertFalse(self.capture(f)['passed'])
    def test_finalizer_requires_execution_and_close(self):
        base=dict(passed=True,connection_closed=True,init_done_observed=True,rm_exchange={'exchanges_verified':True},bar1={'passed':True},
            bar1_readback={'readback_verified':True},page_tables={'passed':True},page_rm={'passed':True},page_rm_exchange={'exchanges_verified':True},
            page_table_captures={'captures_verified':True},channel={'passed':True},execution={'passed':True,'host_command_verified':True},
            compute={'passed':True,'bytes':{'table_readback_verified':True}})
        self.assertTrue(runner.finalize_result(dict(base))['passed'])
        self.assertFalse(runner.finalize_result(dict(base))['compute_verified'])
        live=dict(base,hardware_backend=True);self.assertTrue(runner.finalize_result(live)['compute_verified'])
        for key,value in [('compute',{}),('compute',{'passed':True}),('execution',{}),('channel',{}),('connection_closed',False),('error','late error'),('close_error','late close'),('launch_error','native failure')]:
            r=dict(base);r[key]=value;self.assertFalse(runner.finalize_result(r)['passed']);self.assertFalse(r['host_command_verified'])
        self.assertFalse(runner.finalize_result(dict(base))['metal_verified'])
    def test_invalid_wrapper_inputs_never_reach_transport(self):
        class Backend(client.RestrictedBackend):
            closed=False
            def _invoke(self,*args):raise AssertionError('Unexpected transport')
        b=Backend()
        for method,args in [('execution_memory_info',(2,)),('execution_memory_info',(True,)),('execution_request',(n.EXECUTION_STEPS,)),('execution_request',(True,)),
            ('execution_rm_index',(15,2)),('execution_rm_data',(131071,2)),('execution_snapshot_data',(0,12287,2)),
            ('execution_snapshot_data',(1,45055,2)),('execution_snapshot_data',(2,0,1)),('execution_device_data',(12287,2))]:
            with self.subTest(method=method,args=args),self.assertRaises(ValueError):getattr(b,method)(*args)

if __name__=='__main__':unittest.main()
