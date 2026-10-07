"""Explicit CPU-injected ABI boundary tests; never opens IOKit."""
from pathlib import Path
import ast,ctypes,hashlib,json,os,struct,tempfile,unittest
from unittest.mock import patch
import native_metal_client as client
import native_binary_release as release
import uploaded_library,uploaded_request
ROOT=Path(__file__).resolve().parent
CATALOG=uploaded_library.load(ROOT/'selected-library.json',(ROOT/'selected-library.sha256').read_text().strip())
CONTAINER=(ROOT/'selected.rtxlib').read_bytes();GEN=0x130603601
CPU_RELEASE=dict(abi=1,probe_version='0.37.0',owner_path=release.OWNER_PATH,app_path=release.APP_PATH,
                 owner_sha256='1'*64,app_sha256='2'*64,app_owner_sha256='1'*64,native_source_manifest_sha256=release.NATIVE_SOURCE,
                 app_source_manifest_sha256=release.APP_SOURCE,native_cpu_archive_sha256='3'*64,app_cpu_archive_sha256='4'*64,container_sha256=release.CONTAINER)
def catalog():return CATALOG
class Function:
    def __init__(self,f):self.f=f
    def __call__(self,*args):return self.f(*args)
class CPUABI:
    def __init__(self):
        self.o=dict.fromkeys(client.OWNER_FIELDS,0);self.a=dict.fromkeys(client.APP_FIELDS,0)
        self.o.update(magic=0x5254584d434f3336,abi=1,bytes=192,process_id=os.getpid())
        self.a.update(magic=0x5254584150503336,abi=1,bytes=128,process_id=os.getpid())
        self.calls=[];self.fail=None;self.close_calls=0;self.app_close_calls=0;self.submits=0;self.bad_count=False;self.bad_magic=False;self.corrupt_host=False
        self.session=None;self.observation_error=False;self.corrupt_observation=False;self.corrupt_native_observation=False;self.missing_native_observation=False;self.stale_observation=False
        for name in ('native_info','standard_info','native_open','native_arm','standard_start','standard_submit','native_call','standard_close','native_close'):setattr(self,'rtx_'+name,Function(getattr(self,name)))
    def _info(self,row,fields,out,n):
        assert n==len(fields)*8;values=dict(row)
        if self.bad_magic:values['magic']^=1
        ctypes.memmove(out,struct.pack('<%dQ'%len(fields),*(values[k] for k in fields)),n);return 0
    def native_info(self,out,n):return self._info(self.o,client.OWNER_FIELDS,out,n)
    def standard_info(self,out,n):return self._info(self.a,client.APP_FIELDS,out,n)
    def native_open(self,container,n):
        assert n==5248 and ctypes.string_at(container,n)==CONTAINER
        if self.fail=='loser':self.o.update(state=1,open_attempts=1,io_opens=1);return 0xe3600004
        self.o.update(state=1,open_attempts=1,io_opens=1,calls=1,last_selector=1,registry_id=GEN)
        if self.fail=='open':return 0xe3600005
        return 0
    def native_arm(self,path):
        self.o.update(arm_attempted=1)
        if self.fail=='arm':return 0xe3600005
        self.session=Path(os.fsdecode(path));self.session.mkdir();self.o.update(state=2,armed=1);return 0
    def standard_start(self,container,n):
        assert ctypes.string_at(container,n)==CONTAINER
        self.a.update(starts=1,state=2)
        if self.fail=='start':return 0xe3610004
        self.a.update(state=1,generation=GEN,program_count=4,live_buffers=4,allocated_bytes=16384);return 0
    def standard_submit(self,source,n,out,cap):
        assert n==2112 and cap==2048;self.submits+=1;serial=self.submits
        self.a.update(submit_attempts=serial,scheduled_handlers=serial,completed_handlers=serial)
        # Exact 0.37 kernel CPU output, injected only by this explicit fixture.
        folder=self.session/('job-%d'%serial);folder.mkdir()
        record=(ROOT/'completion-cpu-fixtures'/('job-%d-observation.bin'%serial)).read_bytes()
        if self.corrupt_native_observation:record=bytes([record[0]^1])+record[1:]
        if not self.missing_native_observation:(folder/'completion-observation.bin').write_bytes(record)
        if self.fail=='submit':self.a.update(state=2,last_status=5,last_error=0xe3610005);return 0xe3610005
        answer,_=uploaded_request.evaluate(CATALOG,ctypes.string_at(source,n))
        if self.corrupt_host:answer=bytes([answer[0]^1])+answer[1:]
        ctypes.memmove(out,answer,cap);self.o.update(completed=serial);self.a.update(completed=serial,last_status=4);return 0
    def native_call(self,selector,scalars,count,source,n,out,cap,actual):
        self.calls.append((selector,tuple(scalars) if scalars is not None else (),ctypes.string_at(source,n) if source else b'',cap));self.o['calls']+=1;self.o['last_selector']=selector
        result=bytes([0x36])*cap
        if selector==77 and self.submits:
            if self.observation_error:return 0xe00002bc
            serial=self.submits-1 if self.stale_observation and self.submits>1 else self.submits
            result=(ROOT/'completion-cpu-fixtures'/('job-%d-observation.bin'%serial)).read_bytes()
            if self.corrupt_observation:result=bytes([result[0]^1])+result[1:]
        if cap:ctypes.memmove(out,result,cap)
        ctypes.cast(actual,ctypes.POINTER(ctypes.c_size_t)).contents.value=cap+(1 if self.bad_count else 0)
        return 0
    def standard_close(self):self.app_close_calls+=1;self.a.update(state=3,live_buffers=0,allocated_bytes=0);return 0
    def native_close(self):
        self.close_calls+=1;self.o.update(state=3,io_closes=self.o['io_opens'],service_releases=self.o['io_opens'],states_destroyed=self.o['io_opens']);return 0

class Bridge(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.folder=Path(self.tmp.name).resolve();self.abi=CPUABI()
        self.patches=[patch.object(client.sys,'platform','darwin'),patch.object(client.os,'geteuid',return_value=0,create=True),patch.object(client.ctypes,'CDLL',return_value=self.abi),patch.object(client,'file_bytes',side_effect=lambda path,expected,macho=False:b'cpu-explicit-binary' if macho else CONTAINER),patch.object(release,'load',return_value=release.validate(CPU_RELEASE))]
        for p in self.patches:p.start()
    def tearDown(self):
        for p in reversed(self.patches):p.stop()
        self.tmp.cleanup()
    def create(self):return client.MacIOKitBackend(CATALOG,ROOT/'selected.rtxlib',self.folder/'evidence')
    def test_65_ctypes_copies_and_closure(self):
        b=self.create();self.assertEqual(b.runtime_info(),bytes([0x36])*512)
        for wire in uploaded_request.requests(CATALOG,GEN):self.assertEqual(b.program_submit(wire),b'')
        self.assertEqual(b.completed,65);self.assertEqual(self.abi.submits,65);b.close();b.close();self.assertEqual(self.abi.close_calls,1);self.assertEqual(self.abi.app_close_calls,1)
        self.assertTrue(json.loads((b.evidence/'closure.json').read_bytes())['passed'])
    def test_exact_bootstrap_input_copies(self):
        b=self.create();payload=CATALOG.library+CATALOG.code
        b.upload_append(0,payload[:1024]);self.assertEqual(self.abi.calls[-1],(74,(0,),payload[:1024],0));b.close()
    def test_direct_compute_denied_before_native(self):
        b=self.create()
        with self.assertRaises(ValueError):b._invoke(69,(),uploaded_request.requests(CATALOG,GEN,1)[0],0)
        self.assertFalse(self.abi.calls);b.close()
    def test_malformed_sizes_denied(self):
        b=self.create()
        for args in [(68,(),b'',511),(71,(1,2,36864,1),b'',1),(74,(4096,),bytes(1024),0),(76,(0,511,2),b'',2),(65,(),b'',0),(70,(0,),b'',1024),(75,(),b'x',0)]:
            with self.assertRaises(ValueError):b._invoke(*args)
        self.assertFalse(self.abi.calls);b.close()
    def test_partial_return_is_rejected(self):
        b=self.create();self.abi.bad_count=True
        with self.assertRaises(ValueError):b.runtime_info()
        b.close()
    def test_process_guard_before_calls(self):
        b=self.create()
        with patch.object(client.os,'getpid',return_value=b.pid+1):
            for f in [b.runtime_info,lambda:b.program_submit(uploaded_request.requests(CATALOG,GEN,1)[0])]:
                with self.assertRaises(client.BindingError):f()
        self.assertFalse(self.abi.calls);b.close()
    def test_generation_serial_rejected_without_submit(self):
        b=self.create()
        for wire in [uploaded_request.requests(CATALOG,GEN+1,1)[0],uploaded_request.requests(CATALOG,GEN,2)[1]]:
            with self.assertRaises(ValueError):b.program_submit(wire)
        self.assertEqual(self.abi.submits,0);b.close()
    def test_failed_arm_start_submit_no_replay(self):
        for fail in ('arm','start','submit'):
            with self.subTest(fail=fail):
                self.abi=CPUABI();self.abi.fail=fail
                with patch.object(client.ctypes,'CDLL',return_value=self.abi):
                    b=client.MacIOKitBackend(CATALOG,ROOT/'selected.rtxlib',self.folder/fail);wire=uploaded_request.requests(CATALOG,GEN,1)[0]
                    with self.assertRaises(client.BindingError):b.program_submit(wire)
                    with self.assertRaises(client.BindingError):b.program_submit(wire)
                    self.assertEqual(self.abi.submits,1 if fail=='submit' else 0);b.close();self.assertEqual(self.abi.close_calls,1)
    def test_oracle_detects_wrong_host_result(self):
        b=self.create();self.abi.corrupt_host=True;wire=uploaded_request.requests(CATALOG,GEN,1)[0]
        with self.assertRaises(ValueError):b.program_submit(wire)
        with self.assertRaises(client.BindingError):b.program_submit(wire)
        self.assertEqual(self.abi.submits,1);self.assertEqual(b.completed,0);b.close()
    def test_close_still_runs_if_evidence_save_fails(self):
        b=self.create()
        with patch.object(b,'_snapshot',side_effect=OSError('CPU disk failure')):
            with self.assertRaises(client.BindingError):b.close()
        self.assertTrue(b.closed);self.assertEqual(self.abi.close_calls,1);self.assertEqual(self.abi.app_close_calls,1)
    def test_failed_constructor_releases_its_owner(self):
        self.abi.fail='open'
        with self.assertRaises(client.BindingError):self.create()
        self.assertEqual(self.abi.close_calls,1)
    def test_losing_constructor_does_not_close_winner(self):
        self.abi.fail='loser'
        with self.assertRaises(client.BindingError):self.create()
        self.assertEqual(self.abi.close_calls,0);self.assertEqual(self.abi.app_close_calls,0)
    def test_bad_metadata_prevents_open(self):
        self.abi.bad_magic=True
        with self.assertRaises(ValueError):self.create()
        self.assertEqual(self.abi.o['open_attempts'],0)
    def test_runner_bootstrap_oracles_unchanged(self):
        def funcs(name):return {n.name:ast.dump(n,include_attributes=False) for n in ast.parse((ROOT/name).read_text().replace("probe_version='0.37.0'","probe_version='0.36.0'")).body if isinstance(n,ast.FunctionDef) and n.name!='main'}
        self.assertEqual(funcs('gsp_uploaded.py'),funcs('gsp_standard_metal.py'))
    def test_selector77_exact_shape_and_wide_scalar(self):
        b=self.create()
        for count in range(6):
            for size in (0,1):
                for capacity in (0,511,512,513):
                    args=(77,(2**64-1,)*count,bytes(size),capacity);before=len(self.abi.calls)
                    if (count,size,capacity)==(1,0,512):self.assertEqual(len(b._invoke(*args)),512)
                    else:
                        with self.assertRaises(ValueError):b._invoke(*args)
                        self.assertEqual(len(self.abi.calls),before)
        b.close()
    def test_binary_receipt_required_before_dylib_load(self):
        with patch.object(release,'load',side_effect=ValueError('missing verified binaries')),patch.object(client.ctypes,'CDLL') as load:
            with self.assertRaises(ValueError):self.create()
            load.assert_not_called()
        self.assertEqual(self.abi.o['open_attempts'],0)
    def test_all_observations_are_same_native_and_direct_records(self):
        b=self.create()
        for serial,wire in enumerate(uploaded_request.requests(CATALOG,GEN),1):
            b.program_submit(wire);expected=(ROOT/'completion-cpu-fixtures'/('job-%d-observation.bin'%serial)).read_bytes()
            self.assertEqual((b.evidence/('job-%d-observation.bin'%serial)).read_bytes(),expected)
            self.assertTrue(json.loads((b.evidence/('job-%d-observation.json'%serial)).read_bytes())['passed'])
        self.assertEqual([call[0] for call in self.abi.calls],[77]*65);b.close()
    def test_bad_or_missing_observation_retires_without_host_publication(self):
        for field in ('observation_error','corrupt_observation','corrupt_native_observation','missing_native_observation'):
            with self.subTest(field=field):
                self.abi=CPUABI();setattr(self.abi,field,True)
                with patch.object(client.ctypes,'CDLL',return_value=self.abi):
                    b=client.MacIOKitBackend(CATALOG,ROOT/'selected.rtxlib',self.folder/field);wire=uploaded_request.requests(CATALOG,GEN,1)[0]
                    with self.assertRaises(client.BindingError):b.program_submit(wire)
                    with self.assertRaises(client.BindingError):b.program_submit(wire)
                    self.assertEqual(self.abi.submits,1);self.assertEqual(b.completed,0);self.assertFalse((b.evidence/'host-1.bin').exists());b.close()
    def test_stale_previous_job_observation_is_not_accepted(self):
        b=self.create();wires=uploaded_request.requests(CATALOG,GEN,2);b.program_submit(wires[0]);self.abi.stale_observation=True
        with self.assertRaises(client.BindingError):b.program_submit(wires[1])
        self.assertEqual(b.completed,1);self.assertEqual(self.abi.submits,2);self.assertFalse((b.evidence/'host-2.bin').exists());b.close()
    def test_uncertain_submit_collects_trace_but_preserves_primary_error(self):
        for diagnostics_fail in (False,True):
            self.abi=CPUABI();self.abi.fail='submit';self.abi.observation_error=diagnostics_fail
            with patch.object(client.ctypes,'CDLL',return_value=self.abi):
                b=client.MacIOKitBackend(CATALOG,ROOT/'selected.rtxlib',self.folder/('uncertain-'+str(diagnostics_fail)));wire=uploaded_request.requests(CATALOG,GEN,1)[0]
                with self.assertRaisesRegex(client.BindingError,'standard Metal submit 1 failed: 0xe3610005'):b.program_submit(wire)
                self.assertEqual([c[0] for c in self.abi.calls],[77]);self.assertFalse((b.evidence/'host-1.bin').exists())
                if not diagnostics_fail:self.assertTrue((b.evidence/'job-1-observation.bin').exists())
                b.close()
    def test_metadata_failure_does_not_hide_submit_error(self):
        b=self.create();self.abi.fail='submit';snapshot=b._snapshot
        def save(stem):
            if stem.startswith('job-'):raise OSError('CPU metadata failure')
            return snapshot(stem)
        with patch.object(b,'_snapshot',side_effect=save):
            with self.assertRaisesRegex(client.BindingError,'standard Metal submit 1 failed: 0xe3610005'):b.program_submit(uploaded_request.requests(CATALOG,GEN,1)[0])
        self.assertTrue((b.evidence/'job-1-observation.bin').exists());b.close()
    def test_observation_save_failure_does_not_publish(self):
        b=self.create();save=b._save
        def write(name,data):
            if name=='job-1-observation.bin':raise OSError('CPU evidence disk failure')
            return save(name,data)
        with patch.object(b,'_save',side_effect=write):
            with self.assertRaises(client.BindingError):b.program_submit(uploaded_request.requests(CATALOG,GEN,1)[0])
        self.assertEqual(b.completed,0);self.assertFalse((b.evidence/'host-1.bin').exists());b.close()

class BinaryRelease(unittest.TestCase):
    def test_receipt_loads_exact_regular_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory).resolve();receipt=root/'binary-release.json'
            with patch.object(release,'__file__',str(root/'native_binary_release.py')):
                with self.assertRaises(ValueError):release.load()
                receipt.write_text(json.dumps(CPU_RELEASE),encoding='utf-8')
                self.assertEqual(release.load(),CPU_RELEASE)
    def test_receipt_reader_rejects_duplicate_oversized_and_malformed_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory).resolve();receipt=root/'binary-release.json'
            with patch.object(release,'__file__',str(root/'native_binary_release.py')):
                for data in (b'',b' '*4097,b'{"abi":1,"abi":1}',b'not json',b'\xff'):
                    receipt.write_bytes(data)
                    with self.assertRaises(ValueError):release.load()
    def test_exact_pair_and_source_identity(self):
        self.assertEqual(release.validate(CPU_RELEASE),CPU_RELEASE)
        for key in CPU_RELEASE:
            bad=dict(CPU_RELEASE);bad[key]=0
            with self.subTest(key=key),self.assertRaises(ValueError):release.validate(bad)
        bad=dict(CPU_RELEASE,app_owner_sha256='5'*64)
        with self.assertRaises(ValueError):release.validate(bad)
    def test_missing_extra_legacy_and_malformed_hashes(self):
        cases=[dict(CPU_RELEASE,extra=True),dict(CPU_RELEASE,probe_version='0.36.0'),dict(CPU_RELEASE,owner_path='/tmp/owner.dylib'),dict(CPU_RELEASE,abi=True)]
        for key in CPU_RELEASE:
            row=dict(CPU_RELEASE);del row[key];cases.append(row)
        for digest in ('', 'a'*63, 'a'*65, 'A'*64, '0'*64, '../'+'a'*61):cases.append(dict(CPU_RELEASE,owner_sha256=digest,app_owner_sha256=digest))
        for row in cases:
            with self.assertRaises(ValueError):release.validate(row)
        with self.assertRaises(ValueError):json.loads('{"abi":1,"abi":1}',object_pairs_hook=release.pairs)

if __name__=='__main__':unittest.main(verbosity=2)
