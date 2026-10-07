"""Real standard Metal client -> XPC251 -> native250, one retained GPU session."""
from pathlib import Path
import ctypes,hashlib,json,os,subprocess,threading,time,traceback
from common244 import ROOT,save
SERVICE='local.emre.RTXGraphicsBroker251.native252.v1'
PUBLIC=Path('/private/var/tmp/rtx-graphics-native252-v1')
Begin=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p)
Draw=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_uint64))
Life=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32)
class Callbacks(ctypes.Structure):
 _fields_=[('abi',ctypes.c_uint32),('bytes',ctypes.c_uint32),('context',ctypes.c_void_p),('generation',ctypes.c_uint64),('begin',Begin),('draw',Draw),('retire',Begin),('reserved',ctypes.c_uint64)]
class Lifecycle(ctypes.Structure):
 _fields_=[('abi',ctypes.c_uint32),('bytes',ctypes.c_uint32),('context',ctypes.c_void_p),('function',Life),('reserved',ctypes.c_uint64)]
def graphics(backend,output):
 import native244,machine244,audit_draw250,draw250
 assert type(backend)is native244.MacIOKitBackend and backend.armed and backend.owns and not backend.closed
 assert ctypes.sizeof(Callbacks)==56 and ctypes.sizeof(Lifecycle)==32
 before=machine244.observe();machine244.require_armed(before,backend.binding);save(output/'graphics-machine-before.json',before)
 # Public app files are a separate immutable copy of the frozen root package.
 pins=json.loads((ROOT/'graphics252-public.json').read_bytes())
 for row in pins:
  p=PUBLIC/row['path'];assert p.resolve()==p and p.stat().st_uid==0 and not p.stat().st_mode&0o022 and hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256']
 root=output/'broker251';root.mkdir(mode=0o700);clientout=PUBLIC/'client';assert clientout.is_dir()and not any(clientout.iterdir())
 library=ctypes.CDLL(str(ROOT/'graphics252/RTXGraphicsBrokerServer251-normal.dylib'))
 library.rtx_owned_broker_serve251.argtypes=[ctypes.c_char_p,ctypes.c_uint32,ctypes.POINTER(Callbacks),ctypes.c_char_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(Lifecycle)];library.rtx_owned_broker_serve251.restype=ctypes.c_int
 library.rtx_owned_broker_close_permitted251.argtypes=[];library.rtx_owned_broker_close_permitted251.restype=ctypes.c_int
 audits=[];events=[];client_state={};retired=False;begun=False
 def failure(kind):
  path=output/('callback-'+kind+'-error.txt')
  if not path.exists():path.write_text(traceback.format_exc())
  return 1
 @Begin
 def begin(_):
  nonlocal begun
  try:
   assert not begun;begun=True;code=backend.owner.rtx_native_graphics_begin243();save(output/'graphics-begin-call.json',dict(returncode=code,pid=os.getpid()));return int(code)
  except BaseException:return failure('begin')
 @Draw
 def draw(_,input,n,output_ptr,size,completion):
  try:
   assert begun and not retired and 160<=n<=1114272 and size==n-160 and output_ptr and completion
   request=ctypes.string_at(input,n);binding=draw250.decode(request);serial=binding['serial'];assert binding['generation']==backend.generation and serial==len(audits)+1 and serial<=3
   source=ctypes.create_string_buffer(request,n);image=ctypes.create_string_buffer(size);done=ctypes.c_uint64(0)
   save(output/('draw250-attempt-%d.json'%serial),dict(pid=os.getpid(),generation=backend.generation,serial=serial,request_sha256=hashlib.sha256(request).hexdigest(),standard_metal_client=True))
   code=backend.owner.rtx_native_graphics_draw250(source,n,image,size,ctypes.byref(done));save(output/('draw250-returned-%d.json'%serial),dict(returncode=code,completion=done.value,pid=os.getpid(),payload_sha256=hashlib.sha256(image.raw).hexdigest()))
   native244.check(code,'native draw250');assert done.value==serial
   audit=audit_draw250.verify(backend.evidence/'native-session',serial);assert audit['passed']
   assert(backend.evidence/('native-session/graphics-draw250-%d/application-result.bin'%serial)).read_bytes()==image.raw
   save(output/('draw250-audit-%d.json'%serial),audit);audits.append(audit)
   ctypes.memmove(output_ptr,image,size);completion[0]=done.value;return 0
  except BaseException:return failure('draw')
 @Begin
 def retire(_):
  nonlocal retired
  try:
   assert not retired;retired=True;save(output/'graphics-retired252.json',dict(pid=os.getpid(),native_close_deferred_until_server_drained=True));return 0
  except BaseException:return failure('retire')
 @Life
 def lifecycle(_,event):
  try:
   assert(event==1 and events==[])or(event==2 and events==[1]);events.append(event);save(output/('lifecycle252-%d.json'%event),dict(pid=os.getpid(),event=event,global_metal_publication=False));return 0
  except BaseException:return failure('lifecycle')
 def client():
  proc=None
  try:
   for _ in range(600):
    if(root/'activated251.json').is_file():break
    time.sleep(.05)
   assert(root/'activated251.json').is_file()
   bundle=PUBLIC/'application.bundle';argv=['/usr/bin/sudo','-n','-u','emre',str(PUBLIC/'xpc-client251-normal'),str(bundle),str(bundle/'Contents/Resources/graphics247.rtxlib'),SERVICE,str(backend.generation),str(clientout),'native']
   with(output/'client252.stdout').open('xb')as so,(output/'client252.stderr').open('xb')as se:
    proc=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=so,stderr=se);save(output/'client252-started.json',dict(pid=proc.pid,argv=argv));code=proc.wait(timeout=35)
   client_state.update(pid=proc.pid,returncode=code,terminal=True);save(output/'client252-returned.json',client_state)
  except BaseException:
   if proc is not None and proc.poll()is None:proc.kill();proc.wait()
   client_state.update(error=True);(output/'client252-error.txt').write_text(traceback.format_exc())
 cb=Callbacks(251,ctypes.sizeof(Callbacks),None,backend.generation,begin,draw,retire,0);lc=Lifecycle(202,ctypes.sizeof(Lifecycle),None,lifecycle,0)
 thread=threading.Thread(target=client,name='graphics252-client');thread.start();backend.graphics252_close_blocked=True
 code=library.rtx_owned_broker_serve251(SERVICE.encode(),501,ctypes.byref(cb),str(root).encode(),5,30,ctypes.byref(lc))
 permitted=library.rtx_owned_broker_close_permitted251();backend.graphics252_close_blocked=permitted!=1
 save(output/'server252-returned.json',dict(returncode=code,close_permitted=permitted,pid=os.getpid()));thread.join(timeout=40)
 assert not thread.is_alive()and client_state.get('returncode')==0 and code==0 and permitted==1 and retired and events==[1,2]and len(audits)==3
 result=json.loads((clientout/'client-result.json').read_bytes());assert result['passed']and not result['cpu_model']and result['actual_xpc']and result['standard_draw_primitives']and result['completed_commands']==3 and result['generation']==backend.generation
 save(output/'metal-client-result252.json',result)
 for serial in(1,2,3):
  request=(backend.evidence/('native-session/graphics-draw250-%d/application-request.bin'%serial)).read_bytes();binding=draw250.decode(request);reply=(backend.evidence/('native-session/graphics-draw250-%d/application-result.bin'%serial)).read_bytes()
  for kind in('vertex','before','after'):
   raw=(clientout/('frame-%d-%s.bin'%(serial,kind))).read_bytes();expected=request[160:160+binding['vb']]if kind=='vertex'else request[160+binding['vb']:]if kind=='before'else reply[binding['vb']:];assert raw==expected
   (output/('metal-frame-%d-%s.bin'%(serial,kind))).write_bytes(raw)
 after=machine244.observe();parent,_=machine244.require_loaded(after,backend.binding);machine244.fields(parent,dict(OwnedGraphicsActive=True,OwnedGraphicsCompleted=3,OwnedGraphicsRetained=False));save(output/'graphics-machine-after.json',after)
 final=dict(passed=all(a['image']['exact_ideal_match']for a in audits),native_transactions_verified=True,actual_gpu_jobs=3,private_device_factory=True,standard_metal_draw_api=True,actual_xpc=True,application_readback_matches_native=True,frames=audits,presentation_tested=False,metal_conformance_proven=False)
 save(output/'graphics-audit.json',final);return final
