"""Cross-language simulated ABI, corrupt snapshots, and diagnostic failure paths."""
from pathlib import Path
import struct
import tempfile
import unittest
import gsp_compute_native as n
import gsp_execution_client as client

FIXTURE=Path(__file__).resolve().parent/'changes/gsp-compute-0.25/entry/windows'
GEN=0x100000123

def changed(raw,index,value):
    b=bytearray(raw);struct.pack_into('<Q',b,index*8,value);return bytes(b)

class Fake(client.RestrictedBackend):
    def __init__(self):
        self.files={p.name:p.read_bytes() for p in FIXTURE.glob('*.bin')};self.calls=[];self.error_selector=None;self.short=None;self.fail_read=0;self.reads=0
    def _invoke(self,selector,scalars,data,size):
        self.calls.append((selector,scalars,size))
        if selector==self.error_selector:raise client.BindingError('Simulated summary failure')
        if selector in (56,57,58):out=self.files['simulated-'+{56:'memory',57:'submit',58:'capture'}[selector]+'-info.bin']
        elif selector==59:
            self.reads+=1
            if self.reads==self.fail_read:raise client.BindingError('Simulated page read failure')
            which,off,count=scalars;out=self.files['simulated-'+('root','children','device')[which]+'.bin'][off:off+count]
        else:raise AssertionError('Unexpected non-diagnostic selector')
        return out[:-1] if selector==self.short else out

class Compute(unittest.TestCase):
    def collect(self,f,alter=None):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder);before=p/'before';before.mkdir()
            for name in ('root','children'):(before/(name+'-capture.bin')).write_bytes(f.files['simulated-before-'+name+'.bin'])
            prior=dict(passed=True,host_command_verified=True,table_readback_verified=True,rm=dict(generation=GEN,candidate=4))
            if alter:alter(prior)
            result=n.capture(f,GEN,p/'compute',prior,before)
            return result,{path.name:path.read_bytes() for path in (p/'compute').iterdir()}
    def test_cpp_fixture_full_capture(self):
        f=Fake();r,files=self.collect(f);self.assertTrue(r['passed'],r.get('error'))
        self.assertFalse(r['compute_verified']);self.assertFalse(r['metal_verified']);self.assertTrue(r['bytes']['table_readback_verified'])
        self.assertEqual(f.reads,22);self.assertEqual([s for s,_,_ in f.calls[:3]],[56,57,58])
        for name in ('root','children','device'):self.assertEqual(files[name+'-capture.bin'],f.files['simulated-'+name+'.bin'])
    def test_abi_identity_boolean_and_constants(self):
        f=Fake()
        for name,decoder,fields in (('memory',n.memory,n.MEMORY_FIELDS),('submit',n.submit,n.SUBMIT_FIELDS),('capture',n.capture_info,n.CAPTURE_FIELDS)):
            raw=f.files['simulated-'+name+'-info.bin']
            self.assertTrue(decoder(raw,GEN)['passed'])
            for bad in (raw[:-1],raw+b'\0',changed(raw,0,0),changed(raw,1,2),changed(raw,2,GEN+1),changed(raw,fields.index('passed'),2),changed(raw,len(fields)-1,1)):
                with self.subTest(name=name),self.assertRaises(ValueError):decoder(bad,GEN)
    def test_memory_success_requires_real_publication(self):
        for key,value in [('zeroed_bytes',24576),('links_published',5),('verified_backing_bytes',0),('inv_passed',0),('inv_writes',2),('window_saved',0),
            ('write_phase',17),('register_phase',2),('host_verified',0),('claimed',0),('mapped',0),('lease',0),('owned',0),('pci_command',0),
            ('mapping_physical',0),('elapsed_ns',90_000_000_000),('child_bytes',8193)]:
            f=Fake();name='simulated-memory-info.bin';f.files[name]=changed(f.files[name],n.MEMORY_FIELDS.index(key),value)
            with self.subTest(key=key):self.assertFalse(self.collect(f)[0]['passed'])
    def test_submission_requires_both_markers_and_guards(self):
        for key,value in [('output',0),('completion',0),('get',1),('put',1),('initial_output',0x30602501),('initial_completion',0x306025f0),
            ('native_phase',2),('native_notified',0),('writes',2),('polls',0),('stable',0),('immutable_verified',0),('guards_verified',0),
            ('command_physical',0),('entry',0),('word5',0),('token',5),('elapsed_ns',5_000_000_000),('operations',65537)]:
            f=Fake();name='simulated-submit-info.bin';f.files[name]=changed(f.files[name],n.SUBMIT_FIELDS.index(key),value)
            with self.subTest(key=key):self.assertFalse(self.collect(f)[0]['passed'])
    def test_capture_dimensions_and_reads(self):
        for key,value in [('root_bytes',8192),('child_bytes',4096),('device_bytes',32768),('requested',8192),('reads',21),('last_address',0),
            ('budget_ns',1),('address8',0),('owner_phase',16),('pinned',0),('elapsed_ns',5_000_000_000)]:
            f=Fake();name='simulated-capture-info.bin';f.files[name]=changed(f.files[name],n.CAPTURE_FIELDS.index(key),value)
            with self.subTest(key=key):self.assertFalse(self.collect(f)[0]['passed'])
    def test_changed_table_program_constant_queue_and_output(self):
        cases=[('root',900),('children',4128),('children',450),('before-root',900),('before-children',4128),
            ('device',8),('device',0x888),('device',0x88c),('device',4096),('device',4096+64),('device',8192),
            ('device',12288+32),('device',12288+8192+16),('device',12288+12544),('device',12288+16384),('device',12288+20480),
            ('device',12288+16400),('device',36863)]
        for name,offset in cases:
            f=Fake();name='simulated-'+name+'.bin';b=bytearray(f.files[name]);b[offset]^=1;f.files[name]=bytes(b)
            with self.subTest(name=name,offset=offset):self.assertFalse(self.collect(f)[0]['passed'])
    def test_hardware_mutable_qmd_is_retained(self):
        f=Fake();name='simulated-device.bin';b=bytearray(f.files[name]);b[12288+12288:12288+12544]=bytes([0xa7])*256;f.files[name]=bytes(b)
        r,files=self.collect(f);self.assertTrue(r['passed'],r.get('error'));self.assertEqual(files['device-capture.bin'],bytes(b))
    def test_summary_errors_do_not_hide_other_evidence(self):
        for selector in (56,57,58):
            f=Fake();f.error_selector=selector;r,files=self.collect(f);self.assertFalse(r['passed'])
            for name in ('memory','submit','capture'):
                if name!={56:'memory',57:'submit',58:'capture'}[selector]:self.assertIn(name+'-info.bin',files)
            if selector!=58:self.assertEqual(files['device-capture.bin'],f.files['simulated-device.bin'])
    def test_rejected_identity_still_allows_bounded_capture(self):
        f=Fake();name='simulated-capture-info.bin';f.files[name]=changed(f.files[name],2,GEN+1)
        r,files=self.collect(f);self.assertFalse(r['passed']);self.assertEqual(files['device-capture.bin'],f.files['simulated-device.bin'])
    def test_out_of_bounds_counts_cannot_trigger_reads(self):
        for index,value in ((6,12289),(7,45057),(8,36865),(8,2**64-1)):
            f=Fake();name='simulated-capture-info.bin';f.files[name]=changed(f.files[name],index,value)
            self.assertFalse(self.collect(f)[0]['passed']);self.assertEqual(f.reads,0)
    def test_each_capture_read_failure_preserves_other_images(self):
        for step in range(1,23):
            f=Fake();f.fail_read=step;r,files=self.collect(f);self.assertFalse(r['passed'])
            if step<=13:self.assertEqual(files['device-capture.bin'],f.files['simulated-device.bin'])
            else:self.assertEqual(files['root-capture.bin'],f.files['simulated-root.bin'])
    def test_short_transport_is_rejected(self):
        for selector in (56,57,58,59):
            f=Fake();f.short=selector;self.assertFalse(self.collect(f)[0]['passed'])
    def test_same_run_owner_and_prior_required(self):
        for alter in (lambda r:r.update(passed=False),lambda r:r.update(host_command_verified=False),lambda r:r.update(table_readback_verified=False),
                      lambda r:r['rm'].update(generation=GEN+1),lambda r:r['rm'].update(candidate=5)):
            self.assertFalse(self.collect(Fake(),alter)[0]['passed'])
        for name,fields,key,value in (('memory',n.MEMORY_FIELDS,'physical_mode',1),('memory',n.MEMORY_FIELDS,'window_observed',0),('submit',n.SUBMIT_FIELDS,'owner_phase',7)):
            f=Fake();name='simulated-'+name+'-info.bin';f.files[name]=changed(f.files[name],fields.index(key),value);self.assertFalse(self.collect(f)[0]['passed'])
    def test_transport_bounds_and_closed(self):
        f=Fake()
        for args in ((3,0,4096),(True,0,4096),(0,-1,4096),(0,12288,1),(1,45055,2),(2,36864,1),(0,0,0),(0,0,4097)):
            with self.assertRaises(ValueError):f.compute_capture_data(*args)
        self.assertEqual(f.calls,[]);f.closed=True
        with self.assertRaises(client.BindingError):f.compute_memory_info()

if __name__=='__main__':unittest.main()
