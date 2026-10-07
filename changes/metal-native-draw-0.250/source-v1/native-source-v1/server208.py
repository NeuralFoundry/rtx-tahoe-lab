"""Actual root Registry/RootCallback and XPC server, explicit CPU backend only."""
from pathlib import Path
import ctypes,hashlib,json,os,sys,threading,time
source=Path(__file__).resolve().parent;profile=Path(sys.argv[1]);p=json.loads(profile.read_bytes());folder=profile.parent
def save(name,v):
 with(folder/name).open('x')as f:json.dump(v,f,indent=2);f.write('\n')
assert os.geteuid()==0 and not profile.is_symlink() and profile.stat().st_uid==0 and profile.stat().st_mode&0o022==0
with(folder/'launch-once.json').open('x')as f:json.dump(dict(pid=os.getpid(),one_attempt=True),f)
sys.path.insert(0,str(source/'admission'));import resident_admission as admission
release=json.loads((source/'admission/admission-release.json').read_bytes());approved={r['manifest_sha256']:release['compiler_review_sha256']for r in release['programs']}
registry=admission.Registry(approved)
for row in release['programs']:registry.install(source/'admission'/row['path'],row['manifest_sha256'])
first=source/'admission'/release['programs'][0]['path'];manifest=json.loads((first/'runtime-pipeline.json').read_bytes());image=(first/manifest['files']['container']['path']).read_bytes()
assert registry.admit(image[640:])is not None
callback=admission.RootCallback(registry)
class Callbacks(ctypes.Structure):_fields_=[('abi',ctypes.c_uint32),('bytes',ctypes.c_uint32),('context',ctypes.c_void_p),('function',ctypes.c_void_p),('reserved',ctypes.c_uint64)]
assert ctypes.sizeof(Callbacks)==32
def library(name):
 row=p['libraries'][name];path=Path(row['path']);assert not path.is_symlink()and path.stat().st_uid==0 and path.stat().st_mode&0o022==0 and hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256']
 return ctypes.CDLL(str(path),mode=ctypes.RTLD_LOCAL)
broker=library('broker');model=library('model');ptr=lambda fn:ctypes.cast(fn,ctypes.c_void_p)
init=model.rtx_model208_initialize;init.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_char_p,ctypes.c_char_p];init.restype=ctypes.c_int
buf=ctypes.create_string_buffer(image);assert init(buf,len(image),os.fsencode(folder),p['case'].encode())==0
life=Callbacks(202,32,None,ptr(model.RTXModelLifecycle208),0);gate=Callbacks(208,32,callback.context,ptr(callback.function),0)
serve=broker.rtx_owned_broker_serve208;serve.argtypes=[ctypes.c_char_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_void_p];serve.restype=ctypes.c_int
stop=broker.rtx_owned_broker_stop208;stop.argtypes=[];stop.restype=ctypes.c_int
close=broker.rtx_owned_broker_close_permitted208;close.argtypes=[];close.restype=ctypes.c_int
report=model.rtx_model208_report;report.argtypes=[ctypes.c_int,ctypes.c_int];report.restype=ctypes.c_int
assert stop()==1;done=threading.Event()
def control():
 while not done.wait(.05):
  if(folder/'stop-request').exists()and stop()==0:return
thread=threading.Thread(target=control);thread.start()
try:rc=serve(p['service'].encode(),501,ptr(model.RTXModelClaim208),ptr(model.RTXModelRetire208),os.fsencode(folder),p['stop_after'],p['timeout'],ctypes.byref(life),ctypes.byref(gate))
finally:done.set();thread.join(5);assert not thread.is_alive()
permitted=close();assert report(rc,permitted)==0 and stop()==1
save('admission-result208.json',callback.snapshot());callback.retire();registry.close()
# These failure-injection processes contain only CPU model objects. A real
# native owner must stay alive on closepermission0; no native owner is loaded.
save('server-terminal208.json',dict(pid=os.getpid(),returncode=rc,close_permitted=bool(permitted),registry_closed=True,callback_retired=True,cpu_backend=True,actual_xpc=True,actual_root_registry=True,native_opened=False,gpu_executed=False))
raise SystemExit(rc)
