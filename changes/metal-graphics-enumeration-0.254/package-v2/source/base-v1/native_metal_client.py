"""Actual native IOKit owner and standard Metal bridge for RTXProbe 0.37.

Import performs no I/O. Only the production constructor can open the native
owner. CPU fixtures in separate tests never authorize a hardware run.
"""
import ctypes,hashlib,json,os,struct,sys,threading,time
from pathlib import Path
import gsp_uploaded_client as base
from gsp_uploaded_client import BindingError,decode_info,INFO_SIZE,NAMES,CHUNK,PAGE
from uploaded_library import Catalog,need
import uploaded_request
from native_call_shape import validate_call
import completion_observation,native_binary_release

OWNER_PATH=Path(native_binary_release.OWNER_PATH)
APP_PATH=Path(native_binary_release.APP_PATH)
CONTAINER_SHA='1118527372cafe7ffab131bc513e96dd1548fc7921289e290ce49a9152a5737f'
OWNER_FIELDS=('magic abi bytes process_id state open_attempts io_opens io_closes service_releases states_destroyed registry_id calls last_selector arm_attempted armed completed session_failure session_calls session_elapsed open_result close_result reserved0 reserved1 reserved2').split()
APP_FIELDS=('magic abi bytes process_id state starts submit_attempts completed generation program_count live_buffers allocated_bytes last_status last_error scheduled_handlers completed_handlers').split()
_PROCESS=os.getpid();_OPEN_LOCK=threading.Lock()
def check(code,operation):
    if code:raise BindingError(operation+' failed: 0x%08x'%(code&0xffffffff))
def file_bytes(path,expected,macho=False):
    need(type(path) is Path or isinstance(path,Path),'path')
    need(path.is_absolute() and path.resolve()==path and path.is_file() and not path.is_symlink(),'canonical regular absolute file')
    data=path.read_bytes();need(hashlib.sha256(data).hexdigest()==expected,'binary/container SHA-256')
    if macho:
        need(len(data)>=32,'Mach-O bytes');v=struct.unpack_from('<4I',data);need(v[0]==0xfeedfacf and v[1]==0x1000007 and v[3]==6,'x86_64 dylib')
    return data
def bind(owner,app):
    u32,u64,ptr,size=ctypes.c_uint32,ctypes.c_uint64,ctypes.c_void_p,ctypes.c_size_t
    prototypes=[(owner,'rtx_native_info',[ptr,size]),(owner,'rtx_native_open',[ptr,size]),
        (owner,'rtx_native_call',[u32,ctypes.POINTER(u64),u32,ptr,size,ptr,size,ctypes.POINTER(size)]),
        (owner,'rtx_native_arm',[ctypes.c_char_p]),(owner,'rtx_native_close',[]),
        (app,'rtx_standard_info',[ptr,size]),(app,'rtx_standard_start',[ptr,size]),
        (app,'rtx_standard_submit',[ptr,size,ptr,size]),(app,'rtx_standard_close',[])]
    for library,name,args in prototypes:
        f=getattr(library,name);f.argtypes=args;f.restype=u32
def read_info(library,kind,pid):
    fields=OWNER_FIELDS if kind=='owner' else APP_FIELDS;magic=0x5254584d434f3336 if kind=='owner' else 0x5254584150503336
    buffer=(ctypes.c_uint64*len(fields))();fn=library.rtx_native_info if kind=='owner' else library.rtx_standard_info
    check(fn(buffer,ctypes.sizeof(buffer)),kind+' info');raw=bytes(buffer);row=dict(zip(fields,struct.unpack('<%dQ'%len(fields),raw)))
    need((row['magic'],row['abi'],row['bytes'],row['process_id'])==(magic,1,len(raw),pid),kind+' metadata identity/ABI')
    need(row['state']<=(4 if kind=='owner' else 3),kind+' phase')
    if kind=='owner':need(not any(row['reserved'+str(i)] for i in range(3)) and row['armed']<=1 and row['arm_attempted']<=1,'owner reserved/flags')
    return row,raw

class MacIOKitBackend(base.RestrictedBackend):
    kind='macOS-native-standard-Metal-RTXProbe-0.37.0'
    def __init__(self,catalog,container_path,evidence):
        need(type(catalog) is Catalog,'reviewed catalog')
        if sys.platform!='darwin':raise OSError('Native RTX backend requires macOS')
        if os.geteuid()!=0:raise PermissionError('Native RTX owner requires root')
        if os.getpid()!=_PROCESS:raise BindingError('Backend process changed')
        self.release=native_binary_release.load()
        self.catalog=catalog;self.pid=_PROCESS;self.closed=False;self.owns=False;self.armed=False;self.failed=False;self.completed=0;self.generation=0
        self.evidence=Path(evidence);need(self.evidence.is_absolute() and self.evidence.resolve()==self.evidence and not self.evidence.exists(),'new canonical evidence directory');self.evidence.mkdir(mode=0o700)
        self.container=file_bytes(Path(container_path),CONTAINER_SHA);need(len(self.container)==5248 and self.container[640:]==catalog.library+catalog.code,'exact selected Metal payload')
        file_bytes(OWNER_PATH,self.release['owner_sha256'],True);file_bytes(APP_PATH,self.release['app_sha256'],True)
        self.owner=ctypes.CDLL(str(OWNER_PATH),mode=ctypes.RTLD_GLOBAL);self.app=ctypes.CDLL(str(APP_PATH));bind(self.owner,self.app)
        self._save_json('provenance.json',dict(self.release,pid=self.pid,bootstrap_via_native_call=True,submit_via_standard_metal=True,completion_observation_required=True,metal_registered=False))
        with _OPEN_LOCK:
            owner,app=self._snapshot('cold')
            need(owner['state']==0 and owner['open_attempts']==owner['io_opens']==owner['calls']==0 and app['state']==0 and app['starts']==0,'unused native owner and app')
            blob=ctypes.create_string_buffer(self.container,len(self.container));code=self.owner.rtx_native_open(blob,len(self.container))
            # STATE/PROCESS are pre-entry rejections. A losing caller never
            # closes an owner won by another caller outside this Python module.
            self.owns=code not in (0xe3600003,0xe3600004,0xe3600001)
            try:
                check(code,'native open');owner,app=self._snapshot('opened')
                need(owner['state']==1 and owner['open_attempts']==owner['io_opens']==1 and owner['io_closes']==owner['service_releases']==0 and owner['calls']==1 and owner['last_selector']==1 and owner['registry_id']>0 and not owner['armed'],'one fresh native lease')
                self.generation=owner['registry_id']
            except Exception:
                if self.owns:self.close()
                raise
    def _process(self):
        if os.getpid()!=self.pid:raise BindingError('Backend process changed')
    def _save(self,name,data):
        with (self.evidence/name).open('xb') as f:f.write(data)
    def _save_json(self,name,data):self._save(name,(json.dumps(data,indent=2)+'\n').encode())
    def _snapshot(self,stem):
        result=[]
        for kind,library in [('owner',self.owner),('app',self.app)]:
            row,raw=read_info(library,kind,self.pid);self._save(stem+'-'+kind+'.bin',raw);self._save_json(stem+'-'+kind+'.json',row);result.append(row)
        return tuple(result)
    def _invoke(self,selector,scalars,data,output_size):
        self._process()
        if self.closed or not self.owns:raise BindingError('Native connection unavailable')
        validate_call(self.catalog,selector,scalars,data,output_size)
        scalar=(ctypes.c_uint64*len(scalars))(*scalars) if scalars else None
        source=ctypes.create_string_buffer(data,len(data)) if data else None
        output=ctypes.create_string_buffer(output_size) if output_size else None;actual=ctypes.c_size_t()
        code=self.owner.rtx_native_call(selector,scalar,len(scalars),source,len(data),output,output_size,ctypes.byref(actual))
        check(code,'native selector '+str(selector));need(actual.value==output_size,'native exact output size')
        return output.raw if output is not None else b''
    def _observation(self,serial):
        raw=self._invoke(77,(serial,),b'',512)
        self._save('job-%d-observation.bin'%serial,raw)
        row=completion_observation.decode(raw,self.generation,serial)
        self._save_json('job-%d-observation.json'%serial,row)
        native=self.evidence/'native-session'/('job-%d'%serial)/'completion-observation.bin'
        need(native.is_file() and not native.is_symlink() and native.resolve()==native and native.stat().st_size==512,'native completion observation file')
        need(native.read_bytes()==raw,'direct and command-worker completion observations differ')
        return row
    def program_submit(self,wire):
        self._process()
        if self.closed or not self.owns or self.failed:raise BindingError('Standard application retired')
        request=uploaded_request.decode(self.catalog,wire);need(request['generation']==self.generation and request['serial']==self.completed+1,'next same-generation application job')
        # The CPU evaluator is an independent verifier; its bytes are never
        # sent to the native application or used as the host result.
        expected,oracle=uploaded_request.evaluate(self.catalog,wire)
        serial=request['serial']
        try:
            if not self.armed:
                code=self.owner.rtx_native_arm(os.fsencode(self.evidence/'native-session'));self._snapshot('arm');check(code,'native arm')
                owner,_=read_info(self.owner,'owner',self.pid);need(owner['state']==2 and owner['armed']==1 and owner['completed']==0 and owner['registry_id']==self.generation,'armed native runtime')
                blob=ctypes.create_string_buffer(self.container,len(self.container));code=self.app.rtx_standard_start(blob,len(self.container));self._snapshot('started');check(code,'standard application start');self.armed=True
            source=ctypes.create_string_buffer(wire,len(wire));output=ctypes.create_string_buffer(2048)
            self._save('request-%d.bin'%serial,wire)
            code=self.app.rtx_standard_submit(source,len(wire),output,2048)
            # The native owner preserves the attempted job's observations even
            # after an uncertain submit. Capture them without replaying and
            # retain the original submit status if later diagnostics fail.
            post_errors=[];observation=None
            try:observation=self._observation(serial)
            except Exception as error:post_errors.append('observation: '+repr(error))
            try:owner,app=self._snapshot('job-%d'%serial)
            except Exception as error:post_errors.append('metadata: '+repr(error))
            if post_errors:
                try:self._save_json('job-%d-diagnostic-errors.json'%serial,dict(submit_code=code,errors=post_errors))
                except Exception as error:post_errors.append('diagnostic save: '+repr(error))
            check(code,'standard Metal submit '+str(serial))
            if post_errors:raise BindingError('Post-submit evidence: '+'; '.join(post_errors))
            need(observation['passed'] and observation['capture_kind']==4 and observation['capture_passed']==1 and not observation['closed'],'completed same-job observation')
            actual=output.raw;self._save('host-%d.bin'%serial,actual)
            need(actual==expected,'actual standard Metal host readback differs from CPU oracle')
            need(owner['state']==2 and owner['armed']==1 and owner['completed']==serial and owner['session_failure']==0 and owner['registry_id']==self.generation and owner['io_opens']==1 and owner['io_closes']==0,'native completed owner')
            need(app['state']==1 and app['generation']==self.generation and app['starts']==1 and app['program_count']==len(self.catalog.programs) and app['live_buffers']==4 and app['allocated_bytes']==16384 and app['last_status']==4 and app['last_error']==0,'standard app state/resources')
            need(all(app[k]==serial for k in ('submit_attempts','completed','scheduled_handlers','completed_handlers')),'standard app exact completion/callbacks')
            self._save_json('job-%d-oracle.json'%serial,dict(passed=True,serial=serial,host_sha256=hashlib.sha256(actual).hexdigest(),oracle=oracle))
            self.completed=serial;return b''
        except Exception:
            self.failed=True
            raise
    def _close(self):
        self._process()
        if not self.owns:return
        failures=[]
        try:self._snapshot('preclose')
        except Exception as error:failures.append(repr(error))
        try:check(self.app.rtx_standard_close(),'standard application close')
        except Exception as error:failures.append(repr(error))
        try:check(self.owner.rtx_native_close(),'native owner close')
        except Exception as error:failures.append(repr(error))
        self.owns=False
        try:
            deadline=time.monotonic()+2
            while True:
                owner,_=read_info(self.owner,'owner',self.pid)
                if owner['states_destroyed']==owner['io_opens'] or time.monotonic()>=deadline:break
                time.sleep(.005)
            owner,app=self._snapshot('closed')
            need(owner['state']==3 and owner['io_opens']==owner['io_closes']==owner['service_releases']==owner['states_destroyed'] and owner['close_result']==0,'one native lease fully destroyed')
            need(app['state']==3 and app['live_buffers']==app['allocated_bytes']==0,'application resources closed')
        except Exception as error:failures.append(repr(error))
        self._save_json('closure.json',dict(passed=not failures,errors=failures,completed=self.completed,failed_submission=self.failed,kernel_reset_still_required=True))
        if failures:raise BindingError('Native closure: '+'; '.join(failures))
