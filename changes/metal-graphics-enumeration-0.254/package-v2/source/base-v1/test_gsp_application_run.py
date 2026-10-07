"""Runner integration against captured C++ CPU-model bytes and injected faults."""
import copy
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import gsp_application as runner
import gsp_application_run as work
import gsp_application_client as client
from test_gsp_application_entry import raw,changed
from test_gsp_batch_native import FIXTURE as OLD
import test_gsp_batch_finalize as old_finalize

def runtime(completed):
    if completed==4:return raw('runtime-after.bin')
    data=raw('runtime-before.bin')
    for key in ('completed','queue_completed'):data=changed(data,work.native.INFO_FIELDS.index(key),completed)
    return data

def initial_device():
    device=bytearray(raw('job-0-device.bin'))
    device[8:256]=bytes(248);struct.pack_into('<II',device,0x888,1,1)
    device[4096+64:8192]=bytes(4096-64)
    device[12288:]=work.native.canonical([],7)
    return bytes(device)

class Backend(client.RestrictedBackend):
    def __init__(self):self.completed=0;self.calls=[];self.fail=None;self.corrupt=None;self.closed=False
    def close(self):self.closed=True
    def _invoke(self,selector,scalars,data,size):
        self.calls.append((selector,scalars))
        if selector==69:
            if data!=raw('job-'+str(self.completed)+'-request.bin'):raise ValueError('Unexpected application wire')
            self.completed+=1
            if self.fail=='submit':raise client.BindingError('Failure after possible exposure')
            return b''
        if selector==68:result=runtime(self.completed)
        elif selector==64:result=raw('memory-info.bin')
        elif selector==66:
            result=(OLD/'simulated-capture-info.bin').read_bytes()
            for i,v in [(0,work.native.CAPTURE_MAGIC),(2,7),(14,18)]:result=changed(result,i,v)
        elif selector==67:
            part,offset,length=scalars
            result=(initial_device() if part==2 else raw('job-0-'+work.PARTS[part]+'.bin'))[offset:offset+length]
        elif selector==65:result=raw('job-'+str(scalars[0])+'-submit.bin')
        elif selector==70:result=raw('job-'+str(scalars[0])+'-info.bin')
        elif selector==71:
            index,part,offset,length=scalars
            result=raw('job-'+str(index)+'-'+work.PARTS[part]+'.bin')[offset:offset+length]
            if self.fail==('part',part):raise client.BindingError('Injected capture read failure')
        else:raise client.BindingError('Injected early bootstrap failure')
        if self.fail==selector:raise client.BindingError('Injected getter failure')
        return self.corrupt(selector,scalars,result) if self.corrupt else result

class Run(unittest.TestCase):
    def test_runtime_ready_launch_phase(self):
        import test_gsp_first_boot151_client as old
        row=old.words()
        for key in ('borrowed_owner_verified post_fwsec_seal fwsec_passed fwsec_start_attempted fwsec_start_accepted fwsec_cleanup sec2_staged gsp_reset_verified sec2_start_attempted sec2_start_accepted sec2_halted gsp_active rom_stable boot_passed status_header_valid record_captured').split():row[key]=1
        row.update(phase=18,start_mask=3,pci_command=6,sec2_command_enabled=6,sec2_imem=137,sec2_dmem=98,sec2_matched=6272,gsp_riscv=0x80,record_bytes=4096,record_function=0x1002)
        self.assertTrue(runner.decode_launch(old.pack(row),17)['boot_passed'])
        for phase in (17,19):
            row['phase']=phase
            with self.assertRaises(ValueError):runner.decode_launch(old.pack(row),17)
    def test_phase18_channel_full_capture(self):
        import test_gsp_channel_native as old
        codec=runner.channel_codec;b=old.Fake()
        for name,fields in [('ring',codec.MEMORY_FIELDS),('contexts',codec.MEMORY_FIELDS),('rm',codec.RM_FIELDS),('snapshot',codec.SNAP_FIELDS)]:
            key=name+'-info.bin';b.files[key]=changed(b.files[key],fields.index('owner_phase'),18)
        with tempfile.TemporaryDirectory() as folder:
            result=codec.capture(b,777,Path(folder)/'channel');self.assertTrue(result['passed'],result.get('error'))
    def test_phase18_execution_and_external_full_capture(self):
        import test_gsp_execution_native as old
        codec=runner.execution_codec;b=old.Fake()
        mappings=[('fixed-info.bin',codec.MEMORY_FIELDS),('contexts-info.bin',codec.MEMORY_FIELDS),
            ('rm-info.bin',codec.RM_FIELDS),('snapshot-info.bin',codec.old.SNAP_FIELDS),
            ('fence-info.bin',codec.FENCE_FIELDS),('device-info.bin',codec.CAPTURE_FIELDS),
            ('external-info.bin',codec.external.FIELDS),('golden-rm.bin',codec.old.RM_FIELDS)]
        for key,fields in mappings:b.files[key]=changed(b.files[key],fields.index('owner_phase'),18)
        with patch.object(old,'n',codec):
            result=old.Execution().capture(b);self.assertTrue(result['passed'],result.get('error'))
    def setup_run(self,backend,root):
        before=root/'before';before.mkdir()
        for name in ('root','children'):(before/(name+'-capture.bin')).write_bytes(raw('before-'+name+'.bin'))
        prior=dict(passed=True,host_command_verified=True,table_readback_verified=True,rm=dict(generation=7,candidate=4))
        initial=work.bootstrap(backend,7,root/'initial',prior,before)
        return initial,prior,before
    def test_full_application_loop(self):
        b=Backend()
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);initial,prior,before=self.setup_run(b,root)
            self.assertTrue(initial['passed'],initial.get('error'))
            self.assertEqual(initial['bytes']['jobs'],0)
            result=work.dispatch(b,7,root/'work',initial,prior,before)
            self.assertTrue(result['passed'],result.get('error'))
            self.assertEqual(result['active_elements'],131);self.assertEqual(result['inactive_elements'],125)
            self.assertEqual(sum(s==69 for s,_ in b.calls),4)
            self.assertFalse(result['hardware_accessed']);self.assertFalse(result['metal_verified'])
            self.assertEqual((root/'work/job-3/device-capture.bin').read_bytes(),raw('job-3-device.bin'))
    def test_submission_error_never_retries_and_preserves_captures(self):
        b=Backend();b.fail='submit'
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);initial,prior,before=self.setup_run(b,root)
            result=work.dispatch(b,7,root/'work',initial,prior,before)
            self.assertFalse(result['passed']);self.assertEqual(b.completed,1)
            self.assertEqual(sum(s==69 for s,_ in b.calls),1)
            for name in work.PARTS:self.assertTrue((root/('work/job-0/'+name+'-capture.bin')).is_file())
            self.assertTrue((root/'work/job-0/submit-info.bin').is_file())
    def test_each_diagnostic_failure_preserves_independent_parts(self):
        for fail in (65,68,70,('part',0),('part',1),('part',2),('part',3),('part',4)):
            with self.subTest(fail=fail),tempfile.TemporaryDirectory() as folder:
                b=Backend();b.completed=1;b.fail=fail;root=Path(folder)
                result=work.collect(b,7,root/'capture',0)
                self.assertTrue(result['diagnostic_errors'])
                if fail!=70:
                    good='children' if fail==('part',0) else 'root'
                    self.assertEqual((root/('capture/'+good+'-capture.bin')).read_bytes(),raw('job-0-'+good+'.bin'))
    def test_bad_header_identity_still_collects_bounded_raw_bytes(self):
        for selector in (64,66,68,70):
            b=Backend();b.completed=int(selector==70)
            b.corrupt=lambda s,args,data:changed(data,0,0) if s==selector else data
            with self.subTest(selector=selector),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);r=work.collect(b,7,root/'capture',0 if selector==70 else None)
                self.assertTrue(r['diagnostic_errors']);self.assertEqual((root/'capture/root-capture.bin').stat().st_size,12288)
    def test_corrupt_input_plan_output_or_cleanup_stops_at_first_job(self):
        cases=[('plan',lambda s,a,d:bytes([d[0]^1])+d[1:] if s==71 and a[1]==4 and a[2]==0 else d),
               ('output',lambda s,a,d:bytes([d[0]^1])+d[1:] if s==71 and a[1]==2 and a[2]==28672 else d),
               ('cleanup',lambda s,a,d:changed(d,13,0) if s==70 else d),
               ('count',lambda s,a,d:changed(d,7,4) if s==70 else d),
               ('phase',lambda s,a,d:changed(d,5,5) if s==70 else d)]
        for name,corrupt in cases:
            with self.subTest(name=name),tempfile.TemporaryDirectory() as folder:
                b=Backend();root=Path(folder);initial,prior,before=self.setup_run(b,root);b.corrupt=corrupt
                result=work.dispatch(b,7,root/'work',initial,prior,before)
                self.assertFalse(result['passed']);self.assertEqual(b.completed,1)
    def test_invalid_future_request_rejected_before_any_submission(self):
        b=Backend()
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);initial,prior,before=self.setup_run(b,root);wires=work.requests(7);wires[3]=wires[0]
            self.assertFalse(work.dispatch(b,7,root/'work',initial,prior,before,wires)['passed'])
            self.assertFalse(any(s==69 for s,_ in b.calls))
    def test_bootstrap_rejects_already_submitted_image(self):
        b=Backend();b.corrupt=lambda s,a,d:raw('job-0-device.bin')[a[1]:a[1]+a[2]] if s==67 and a[0]==2 else d
        with tempfile.TemporaryDirectory() as folder:
            initial,_,_=self.setup_run(b,Path(folder));self.assertFalse(initial['passed'])
            self.assertFalse(any(s==69 for s,_ in b.calls))
    def test_early_runner_failure_closes_and_cannot_claim_compute(self):
        b=Backend()
        with tempfile.TemporaryDirectory() as folder:result=runner.run(b,Path(folder),Path(folder),Path(folder))
        self.assertTrue(b.closed);self.assertFalse(result['passed']);self.assertFalse(result['compute_verified'])
    def test_finalize_requires_entire_loop_and_no_late_error(self):
        with tempfile.TemporaryDirectory() as folder:
            b=Backend();root=Path(folder);initial,prior,before=self.setup_run(b,root)
            result=old_finalize.Finalize().sample();result['runtime_bootstrap']=initial
            result['application']=work.dispatch(b,7,root/'work',initial,prior,before)
            self.assertTrue(runner.finalize_result(copy.deepcopy(result))['passed'])
            self.assertFalse(runner.finalize_result(copy.deepcopy(result))['compute_verified'])
            for key in ('error','launch_error','close_error'):
                bad=copy.deepcopy(result);bad[key]='injected';self.assertFalse(runner.finalize_result(bad)['passed'])
            for j in range(4):
                bad=copy.deepcopy(result);bad['application']['jobs'][j]['passed']=False
                self.assertFalse(runner.finalize_result(bad)['passed'])
            bad=copy.deepcopy(result);bad['connection_closed']=False;self.assertFalse(runner.finalize_result(bad)['passed'])

if __name__=='__main__':unittest.main()
