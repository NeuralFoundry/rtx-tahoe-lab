"""Single native owner, initial SYS-root admission experiment. No job submission."""
import ctypes,hashlib,json,os,plistlib,struct,subprocess,sys,time
from pathlib import Path
import gsp_uploaded_client as base
from gsp_uploaded_client import BindingError,decode_info,INFO_SIZE,NAMES,CHUNK,PAGE
from uploaded_library import Catalog,need
from native_call_shape import validate_call
import root_diagnostics196,root_evidence196

OWNER_SHA='249739befc2090913f21ecf66e310fc84cbbd823c19731b4168196c45e37cf44'
KERNEL_SHA='e9e7ae4979d852a03a5e53b451d9ea87907e2a7672e949bc76611e52bd5c85b7'
KERNEL_UUID='07FE2680-BDF7-3F7B-8D4B-A4025993AD59'
OWNER_FIELDS=('magic abi bytes process_id state open_attempts io_opens io_closes service_releases states_destroyed registry_id calls last_selector arm_attempted armed completed session_failure session_calls session_elapsed open_result close_result reserved0 reserved1 reserved2').split()

def check(code,operation):
    if code:raise BindingError(operation+' failed: 0x%08x'%(code&0xffffffff))

def verified_file(path,sha,kind=None):
    path=Path(path)
    need(path.is_absolute() and path.resolve()==path and path.is_file() and not path.is_symlink(),'Canonical artifact')
    raw=path.read_bytes();need(hashlib.sha256(raw).hexdigest()==sha,'Reviewed artifact SHA-256')
    if kind is not None:need(len(raw)>=32 and struct.unpack_from('<4I',raw)[0:2]==(0xfeedfacf,0x1000007) and struct.unpack_from('<I',raw,12)[0]==kind,'x86_64 artifact type')
    return raw

def read_info(owner,pid):
    buf=(ctypes.c_uint64*24)();check(owner.rtx_native_info(buf,192),'owner info');raw=bytes(buf);r=dict(zip(OWNER_FIELDS,struct.unpack('<24Q',raw)))
    need((r['magic'],r['abi'],r['bytes'],r['process_id'])==(0x5254584d434f3336,1,192,pid),'Owner metadata identity')
    need(r['state']<=4 and r['armed']<=1 and r['arm_attempted']<=1 and not any(r['reserved'+str(i)] for i in range(3)),'Owner metadata phase/reserved')
    return r,raw

def opened(r,generation):
    need(r['state']==1 and r['registry_id']==generation and generation>0 and r['open_attempts']==r['io_opens']==1 and r['calls']==3 and r['last_selector']==101,'Fresh196 three-call open')
    need(not any(r[k] for k in ('io_closes','service_releases','states_destroyed','arm_attempted','armed','completed','session_failure','session_calls','session_elapsed','open_result','close_result')),'Pristine native196 owner')

def flush_property(raw,generation):
    need(type(raw)is bytes and 0<len(raw)<=8*1024*1024,'Bounded registry capture')
    rows=plistlib.loads(raw);need(type(rows)is list and len(rows)==1 and type(rows[0])is dict,'Unique RTX parent')
    row=rows[0];need(row.get('IORegistryEntryID')==generation and row.get('ProbeVersion')=='0.79.0','Flush parent binding')
    value=row.get('GSPFlushInfo');need(type(value)is bytes and len(value)==512,'Flush property extent')
    return value

def bind(owner):
    u32,u64,ptr,size=ctypes.c_uint32,ctypes.c_uint64,ctypes.c_void_p,ctypes.c_size_t
    for name,args in [('rtx_native_info',[ptr,size]),('rtx_native_open',[ptr,size]),('rtx_native_call',[u32,ctypes.POINTER(u64),u32,ptr,size,ptr,size,ctypes.POINTER(size)]),('rtx_native_arm_owned187',[ctypes.c_char_p,ptr,size]),('rtx_native_close',[])]:
        f=getattr(owner,name);f.argtypes=args;f.restype=u32

def verify_admission(folder,generation,owned_container):
    folder=Path(folder);arm=folder/'arm';result=root_evidence196.verify(arm)
    need(struct.unpack_from('<Q',(arm/'owned-root-info-before.bin').read_bytes(),16)[0]==generation,'Captured native generation')
    summary=json.loads((arm/'native-session.json').read_bytes())
    for k,v in dict(passed=True,generation=generation,completed=0,failure=0,io_result=0,first_io_error=0,native_iokit=True,probe_version='0.79.0',owned_root_abi=195,external_setup_abi=2,dispatch_abi=183,owned_data_verified=True,owned_root_verified=True,owned_root_failure=0,shader_arithmetic_checked=False,metal_registered=False).items():
        need(type(summary[k])is type(v) and summary[k]==v,'Native admission summary: '+k)
    before=struct.pack('<8Q',0x5254584441544131,181,generation,1,0,0,0,0)
    after=struct.pack('<8Q',0x5254584441544131,181,generation,1,1301,0,0,0)
    need((arm/'owned-data-info-before.bin').read_bytes()==before and (arm/'owned-data-info-after.bin').read_bytes()==after,'Same-connection data counters')
    pages=(arm/'owned-data-pages.bin').read_bytes();rows=(arm/'owned-root-rows.bin').read_bytes();legacy=result['legacy_stages'][2];need(len(pages)==1301*40,'Owned data page evidence')
    offset=0
    for slot,n in enumerate((4097,65537,1048577,4194305)):
        for page in range((n+4095)//4096):
            b,index,pa,dma,extent=struct.unpack_from('<5Q',pages,offset*40)
            expected=struct.unpack_from('<Q',rows,(legacy+offset)*40+8)[0]
            need((b,index,pa,dma)==(slot,page,expected,expected) and extent>=4096,'Owned data page and table association');offset+=1
    p=folder/'owned-program187';cold=[0]*16;cold[:3]=[0x5254584744503138,183,generation];ready=cold.copy();ready[3]=2;ready[13]=1;ready[14]=4096
    need((p/'dispatch-cold.bin').read_bytes()==struct.pack('<16Q',*cold),'Cold owned dispatch')
    need((p/'dispatch-ready.bin').read_bytes()==(p/'dispatch-stable.bin').read_bytes()==struct.pack('<16Q',*ready),'Prepared owned dispatch, zero jobs')
    need((p/'payload.bin').read_bytes()==owned_container[640:] and (p/'library.bin').read_bytes()==owned_container[640:1152] and (p/'code.bin').read_bytes()==owned_container[1152:],'Owned program exact upload')
    return dict(passed=True,root=result,data_pages=1301,owned_dispatch_prepared=True,jobs_submitted=0,compute_verified=False,metal_verified=False)

class MacIOKitBackend(base.RestrictedBackend):
    kind='macOS-native-initial-root197'
    def __init__(self,catalog,container,owned_container,evidence,owner_path,kernel_path,binding):
        need(sys.platform=='darwin' and os.geteuid()==0,'macOS root native owner')
        need(type(catalog)is Catalog and type(container)is bytes and len(container)==5248 and container[640:]==catalog.library+catalog.code,'Reviewed bootstrap catalog')
        need(type(owned_container)is bytes and len(owned_container)==5248 and struct.unpack_from('<I',owned_container,8)[0]==2,'Full owned ABI2 container')
        self.pid=os.getpid();self.catalog=catalog;self.container=container;self.owned_container=owned_container;self.binding=binding
        self.closed=False;self.owns=False;self.armed=False;self.generation=binding['generation'];self.flush_reads=0
        self.evidence=Path(evidence);need(self.evidence.is_absolute() and self.evidence.resolve()==self.evidence and not self.evidence.exists(),'New native evidence directory');self.evidence.mkdir(mode=0o700)
        verified_file(owner_path,OWNER_SHA,8);verified_file(kernel_path,KERNEL_SHA,11)
        self._current_binding('before-open')
        self.owner=ctypes.CDLL(str(owner_path),mode=ctypes.RTLD_GLOBAL);bind(self.owner)
        self._save_json('provenance.json',dict(binding,pid=self.pid,owner_sha256=OWNER_SHA,kernel_sha256=KERNEL_SHA,cpu_mock_used=False,shader_jobs_requested=0))
        r=self._snapshot('cold');need(r['state']==0 and r['open_attempts']==r['io_opens']==r['calls']==0,'Unused native owner')
        blob=ctypes.create_string_buffer(container,len(container));code=self.owner.rtx_native_open(blob,len(container))
        self.owns=code not in (0xe3600003,0xe3600004,0xe3600001)
        try:
            check(code,'native open');opened(self._snapshot('opened'),self.generation);self._current_binding('after-open')
        except Exception:
            if self.owns:self.close()
            raise
    def _process(self):need(os.getpid()==self.pid and os.geteuid()==0,'Original native owner process')
    def _save(self,name,data):
        with(self.evidence/name).open('xb')as f:f.write(data)
    def _save_json(self,name,value):self._save(name,(json.dumps(value,indent=2)+'\n').encode())
    def _snapshot(self,label):
        row,raw=read_info(self.owner,self.pid);self._save(label+'-owner.bin',raw);self._save_json(label+'-owner.json',row);return row
    def _current_binding(self,label):
        from machine197 import observe,require_loaded
        value=observe();self._save_json(label+'-machine.json',value);require_loaded(value,self.binding)
    def _invoke(self,selector,scalars,data,output_size):
        self._process();need(self.owns and not self.closed,'Existing native connection');validate_call(self.catalog,selector,scalars,data,output_size)
        scalar=(ctypes.c_uint64*len(scalars))(*scalars)if scalars else None
        source=ctypes.create_string_buffer(data,len(data))if data else None;output=ctypes.create_string_buffer(output_size)if output_size else None;actual=ctypes.c_size_t()
        code=self.owner.rtx_native_call(selector,scalar,len(scalars),source,len(data),output,output_size,ctypes.byref(actual))
        check(code,'native selector '+str(selector));need(actual.value==output_size,'Exact native return size');return output.raw if output is not None else b''
    def flush_info(self):
        self._process();need(self.owns and not self.closed,'Active flush owner');ordinal=self.flush_reads;self.flush_reads+=1
        raw=subprocess.check_output(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c','RTXProbe'],timeout=20);self._save('flush-registry-'+str(ordinal)+'.plist',raw)
        return flush_property(raw,self.generation)
    def launch(self):
        try:return super().launch()
        finally:
            def read(selector,scalars,size):
                self._process();need(self.owns and not self.closed,'Retained diagnostic connection');return root_diagnostics196.native_read(self.owner,selector,scalars,size)
            try:root_diagnostics196.capture(read,self.evidence/'root-diagnostic196',self.generation)
            except Exception as error:self._save_json('root-diagnostic196-error.json',dict(error=str(error),admission_granted=False))
    def program_submit(self,wire):raise BindingError('Initial-root experiment does not submit shader jobs')
    def admit_root(self):
        self._process();need(self.owns and not self.closed and not self.armed,'Single native root admission');self._current_binding('before-arm')
        path=self.evidence/'native-session';buf=ctypes.create_string_buffer(self.owned_container,len(self.owned_container))
        code=self.owner.rtx_native_arm_owned187(os.fsencode(path),buf,len(self.owned_container));r=self._snapshot('arm');check(code,'native owned187 arm')
        need(r['state']==2 and r['armed']==1 and r['arm_attempted']==1 and r['completed']==r['session_failure']==0 and r['registry_id']==self.generation,'Armed native owner, zero jobs');self.armed=True
        result=verify_admission(path,self.generation,self.owned_container);self._save_json('admission.json',result);return result
    def _close(self):
        self._process()
        if not self.owns:return
        errors=[]
        try:self._snapshot('preclose')
        except Exception as error:errors.append(repr(error))
        try:check(self.owner.rtx_native_close(),'native close')
        except Exception as error:errors.append(repr(error))
        self.owns=False
        try:
            deadline=time.monotonic()+2
            while True:
                r,_=read_info(self.owner,self.pid)
                if r['states_destroyed']==r['io_opens'] or time.monotonic()>=deadline:break
                time.sleep(.005)
            r=self._snapshot('closed');need(r['state']==3 and r['io_opens']==r['io_closes']==r['service_releases']==r['states_destroyed'] and r['close_result']==0,'Native lease fully destroyed')
        except Exception as error:errors.append(repr(error))
        self._save_json('closure.json',dict(passed=not errors,errors=errors,kernel_reset_still_required=True))
        if errors:raise BindingError('Native close: '+'; '.join(errors))
