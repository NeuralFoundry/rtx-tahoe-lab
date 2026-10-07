"""Injected CPU transport tests. Never open IOKit or run firmware."""
from pathlib import Path
import copy,ctypes,json,os,struct,tempfile,unittest
import gsp_reusable_client as client
import gsp_reusable_run as work
import gsp_reusable as runner
import reusable_request as request
import reusable_model as model
import reusable_native as native
ROOT=Path(__file__).resolve().parent
FIXTURE=Path(os.environ['RTX_REUSABLE_FIXTURES'])
GEN=0x30603501
def raw(name):return (FIXTURE/name).read_bytes()
def change(b,index,value):return b[:index*8]+struct.pack('<Q',value)+b[index*8+8:]
class Backend(client.RestrictedBackend):
    def __init__(self):self.completed=0;self.closed=False;self.calls=[];self.fail=None;self.corrupt=None
    def _close(self):pass
    def _invoke(self,selector,scalars,data,size):
        self.calls.append((selector,tuple(scalars),len(data),size))
        if selector==69:
            serial=self.completed+1
            if data!=raw('job-%d-request.bin'%serial):raise ValueError('Unexpected fixture request')
            self.completed=serial
            if self.fail=='submit':raise client.BindingError('Uncertain submission')
            return b''
        if selector==68:b=raw('job-%d-info.bin'%self.completed) if self.completed else raw('initial-info.bin')
        elif selector in (64,66):b=change((ROOT/'reusable-fixture-baseline'/('memory-info.bin' if selector==64 else 'capture-info.bin')).read_bytes(),2,GEN)
        elif selector==67:
            part,off,length=scalars;b=raw('initial-'+work.PARTS[part]+'.bin')[off:off+length]
        elif selector==70:
            if scalars!=(self.completed,) or not self.completed:raise client.BindingError('Stale serial')
            b=raw('job-%d-job.bin'%self.completed)
        elif selector==71:
            serial,part,off,length=scalars
            if part<5:
                if serial!=self.completed or not serial:raise client.BindingError('Stale capture serial')
                b=raw('job-%d-%s.bin'%(serial,work.PARTS[part]))
            else:b=model.sealed()[part-5]
            b=b[off:off+length]
            if self.fail==('part',part):raise client.BindingError('Injected part read failure')
        else:raise client.BindingError('Unexpected CPU selector')
        if self.fail==selector:raise client.BindingError('Injected getter failure')
        return self.corrupt(selector,scalars,b) if self.corrupt else b
def setup(backend,root):
    before=root/'execution';before.mkdir()
    for name in work.PARTS[:3]:(before/(name+'-capture.bin')).write_bytes((ROOT/'reusable-fixture-baseline'/('before-'+name+'.bin')).read_bytes())
    prior=dict(passed=True,host_command_verified=True,table_readback_verified=True,rm=dict(generation=GEN,candidate=4))
    bootstrap=root/'runtime-bootstrap';initial=work.bootstrap(backend,GEN,bootstrap,prior,before)
    return initial,prior,before,bootstrap
def binding(program):
    return dict(passed=True,connection_closed=True,hardware_backend=False,init_done_observed=True,
        rm_exchange=dict(exchanges_verified=True),bar1=dict(passed=True),bar1_readback=dict(readback_verified=True),
        page_tables=dict(passed=True),page_rm=dict(passed=True),page_rm_exchange=dict(exchanges_verified=True),page_table_captures=dict(captures_verified=True),
        channel=dict(passed=True),execution=dict(passed=True,host_command_verified=True),runtime_bootstrap=dict(passed=True),program=program)
class Reusable(unittest.TestCase):
    def run_work(self,backend,root,wires=None):
        initial,prior,before,bootstrap=setup(backend,root);self.assertTrue(initial['passed'],initial.get('error'))
        return work.dispatch(backend,GEN,root/'program',initial,prior,before,bootstrap,wires)
    def test_native_plan_request_and_abi_all_65(self):
        wires=request.requests(GEN)
        for serial,wire in enumerate(wires,1):
            with self.subTest(serial=serial):
                self.assertEqual(wire,raw('job-%d-request.bin'%serial));self.assertEqual(model.plan(wire),raw('job-%d-plan.bin'%serial))
                self.assertEqual(native.info(raw('job-%d-info.bin'%serial),GEN)['completed'],serial)
                self.assertTrue(native.job(raw('job-%d-job.bin'%serial),GEN,serial,40960)['passed'])
    def test_complete_65_job_rehearsal(self):
        with tempfile.TemporaryDirectory() as folder:
            b=Backend();result=self.run_work(b,Path(folder));self.assertTrue(result['passed'],result.get('error'))
            self.assertEqual((result['active_elements'],result['ring_wraps']),(4160,2));self.assertTrue(result['final']['ready']);self.assertFalse(result['final']['exhausted'])
            self.assertEqual(sum(c[0]==69 for c in b.calls),65);self.assertFalse(any(c[0]==65 for c in b.calls))
            final=runner.finalize_result(binding(result));self.assertTrue(final['passed']);self.assertFalse(final['compute_verified']);self.assertFalse(final['metal_verified'])
            for key in ('error','launch_error','close_error','final_diagnostic_error'):
                wrong=binding(result);wrong[key]='failure';self.assertFalse(runner.finalize_result(wrong)['passed'])
            for key in ('connection_closed','init_done_observed'):
                wrong=binding(result);wrong[key]=False;self.assertFalse(runner.finalize_result(wrong)['passed'])
            wrong=binding(copy.deepcopy(result));wrong['program']['jobs'].pop();self.assertFalse(runner.finalize_result(wrong)['passed'])
            wrong=binding(copy.deepcopy(result));wrong['program']['final']['completed']=4;self.assertFalse(runner.finalize_result(wrong)['passed'])
    def test_uncertain_submit_collects_without_retry(self):
        with tempfile.TemporaryDirectory() as folder:
            b=Backend();b.fail='submit';root=Path(folder);r=self.run_work(b,root)
            self.assertFalse(r['passed']);self.assertEqual(sum(c[0]==69 for c in b.calls),1)
            for name in work.PARTS:self.assertTrue((root/('program/job-1/'+name+'-capture.bin')).is_file())
    def test_future_wire_rejected_before_first_submit(self):
        with tempfile.TemporaryDirectory() as folder:
            b=Backend();wires=request.requests(GEN);wires[64]=change(wires[64],2,GEN+1);r=self.run_work(b,Path(folder),wires)
            self.assertFalse(r['passed']);self.assertFalse(any(c[0]==69 for c in b.calls))
    def test_independent_getter_failures_preserve_other_parts(self):
        for failure in (68,70,*[('part',i) for i in range(7)]):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as folder:
                b=Backend();b.completed=1;b.fail=failure;p=Path(folder)/'capture';r=work.collect(b,GEN,p,1,40960)
                self.assertTrue(r['diagnostic_errors']);good='children' if failure==('part',0) else 'root'
                self.assertEqual((p/(good+'-capture.bin')).read_bytes(),raw('job-1-'+good+'.bin'))
    def test_corrupt_input_output_plan_queue_or_cleanup_stops(self):
        cases=[(71,0,0,0),(71,1,0,100),(71,2,16384,512),(71,2,0,0x840),(71,2,32768,16),(71,4,0,100),(70,None,None,26*8)]
        for selector,part,offset,at in cases:
            with self.subTest(case=(selector,part,offset,at)),tempfile.TemporaryDirectory() as folder:
                b=Backend();root=Path(folder);initial,prior,before,bootstrap=setup(b,root);self.assertTrue(initial['passed'])
                def corrupt(s,args,data):
                    if s==selector and (s==70 or (args[1],args[2])==(part,offset)):return data[:at]+bytes([data[at]^1])+data[at+1:]
                    return data
                b.corrupt=corrupt;r=work.dispatch(b,GEN,root/'program',initial,prior,before,bootstrap)
                self.assertFalse(r['passed']);self.assertEqual(sum(c[0]==69 for c in b.calls),1)
    def test_bad_headers_retain_bounded_raw_evidence(self):
        for selector in (64,66,68,70):
            with self.subTest(selector=selector),tempfile.TemporaryDirectory() as folder:
                b=Backend();b.completed=int(selector==70);b.corrupt=lambda s,args,data:change(data,0,0) if s==selector else data
                p=Path(folder)/'capture';r=work.collect(b,GEN,p,1 if selector==70 else None,40960)
                self.assertTrue(r['diagnostic_errors']);self.assertEqual((p/'root-capture.bin').stat().st_size,12288)
    def test_info_and_job_corruptions(self):
        info=raw('job-1-info.bin');job=raw('job-1-job.bin')
        for index,value in [(0,0),(1,2),(2,GEN+1),(3,2),(5,4),(6,2111),(7,2),(10,7),(16,0),(18,1),(19,2),(20,511),(24,1),(27,24),(28,94209),(30,5_000_000_000),(35,40959),(63,1)]:
            with self.subTest(info=(index,value)),self.assertRaises(ValueError):native.info(change(info,index,value),GEN)
        for index,value in [(0,0),(1,2),(2,GEN+1),(3,2),(9,2),(10,14),(11,6),(12,0),(15,5_000_000_000),(18,1),(20,0),(26,0),(29,9),(33,5_000_000_000),(40,3),(41,2),(44,21),(45,90000),(48,5_000_000_000),(50,0),(60,3),(61,0),(62,2),(127,1)]:
            with self.subTest(job=(index,value)),self.assertRaises(ValueError):native.job(change(job,index,value),GEN,1,40960)
    def test_malformed_request_and_wide_serials(self):
        wire=raw('job-1-request.bin')
        for offset in (0,8,12,40,63,832,2111):
            with self.subTest(offset=offset),self.assertRaises(ValueError):request.decode(wire[:offset]+bytes([wire[offset]^1])+wire[offset+1:])
        for offset in (16,24):
            with self.assertRaises(ValueError):request.decode(wire[:offset]+bytes(8)+wire[offset+8:])
        for serial in (0xffffffff,0x100000000,request.MAX_SERIAL):
            w=request.encode(GEN,serial,2,[0xffffffff]*64,[0x80000000]*64);self.assertEqual(request.decode(w)['serial'],serial)
            q=int.from_bytes(model.plan(w)[3136:3392],'little');self.assertEqual((q>>832)&(2**64-1),serial)
        for serial in (0,-1,2**64,True):
            with self.assertRaises(ValueError):request.encode(GEN,serial,0,[0]*64,[0]*64)
        with self.assertRaises(ValueError):request.encode(GEN,1,0,[True]*64,[0]*64)
    def test_wrappers_scope_serials(self):
        b=Backend()
        for args in ((0,0,0,1),(1,5,0,1),(1,7,0,1),(1,2,36863,2),(True,2,0,1)):
            with self.subTest(args=args),self.assertRaises(ValueError):b.program_data(*args)
        with self.assertRaises(ValueError):b.program_job_info(0)
        with self.assertRaises(ValueError):b.program_submit_info(0)
        self.assertFalse(b.calls)
    def test_real_ctypes_transport_preserves_64bit_scalars(self):
        seen=[]
        class IOKit:
            def IOConnectCallMethod(self,connection,selector,scalars,count,source,size,out_scalars,out_count,dest,dest_size):
                seen.append((selector,tuple(scalars[i] for i in range(count)),ctypes.string_at(source,size) if size else b''))
                n=ctypes.cast(dest_size,ctypes.POINTER(ctypes.c_size_t)).contents.value
                if n:ctypes.memset(dest,0,n)
                return 0
        b=client.MacIOKitBackend.__new__(client.MacIOKitBackend);b.closed=False;b.connection=123;b.io=IOKit()
        for serial in (1,32,33,0xffffffff,0x100000000,request.MAX_SERIAL):
            self.assertEqual(len(b.program_job_info(serial)),1024);self.assertEqual(seen[-1][1],(serial,))
            b.program_data(serial,2,0,4096);self.assertEqual(seen[-1][1],(serial,2,0,4096))
        b.program_submit(raw('job-1-request.bin'));self.assertEqual(seen[-1][2],raw('job-1-request.bin'))
        count=len(seen)
        for selector,scalars,data,size in ((65,(1,),b'',512),(70,(0,),b'',1024),(71,(1,5,0,512),b'',512),(69,(),bytes(2112),0)):
            with self.assertRaises(ValueError):b._invoke(selector,scalars,data,size)
        self.assertEqual(len(seen),count)
    def test_early_boot_failure_always_closes(self):
        class Early:
            closed=False
            def begin(self):raise client.BindingError('CPU early failure')
            def close(self):self.closed=True
        with tempfile.TemporaryDirectory() as folder:
            b=Early();r=runner.run(b,None,None,Path(folder));self.assertFalse(r['passed']);self.assertTrue(b.closed);self.assertTrue(r['connection_closed']);self.assertFalse(r['launch_requested'])
