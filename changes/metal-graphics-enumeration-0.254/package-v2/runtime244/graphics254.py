"""Real standard Metal client -> XPC251 -> native250, one retained GPU session."""
from pathlib import Path
import ctypes,hashlib,json,os,subprocess,threading,time,traceback
from common244 import ROOT,save
SERVICE='local.emre.RTXGraphicsBroker251.publication253.v1'
PUBLIC=Path('/private/var/tmp/rtx-graphics-discovery253-v1')
Begin=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p)
Draw=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_uint64))
Life=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32)
class Callbacks(ctypes.Structure):
 _fields_=[('abi',ctypes.c_uint32),('bytes',ctypes.c_uint32),('context',ctypes.c_void_p),('generation',ctypes.c_uint64),('begin',Begin),('draw',Draw),('retire',Begin),('reserved',ctypes.c_uint64)]
class Lifecycle(ctypes.Structure):
 _fields_=[('abi',ctypes.c_uint32),('bytes',ctypes.c_uint32),('context',ctypes.c_void_p),('function',Life),('reserved',ctypes.c_uint64)]
def graphics(backend,output):
 import native244,machine244,machine253,audit_draw250,draw250
 from publication253 import Publication,PropertyCommand
 from ready253 import spec_valid
 assert type(backend)is native244.MacIOKitBackend and backend.armed and backend.owns and not backend.closed
 assert ctypes.sizeof(Callbacks)==56 and ctypes.sizeof(Lifecycle)==32
 before=machine244.observe();machine244.require_armed(before,backend.binding);save(output/'graphics-machine-before.json',before)
 # Public app files are a separate immutable copy of the frozen root package.
 pins=json.loads((ROOT/'graphics254-public.json').read_bytes())
 for row in pins:
  p=PUBLIC/row['path'];assert p.resolve()==p and p.stat().st_uid==0 and not p.stat().st_mode&0o022 and hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256']
 root=output/'broker251';root.mkdir(mode=0o700);clientout=PUBLIC/'client254-v1';assert clientout.is_dir()and not any(clientout.iterdir())
 library=ctypes.CDLL(str(ROOT/'graphics254/RTXGraphicsBrokerServer251-normal.dylib'))
 library.rtx_owned_broker_serve251.argtypes=[ctypes.c_char_p,ctypes.c_uint32,ctypes.POINTER(Callbacks),ctypes.c_char_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(Lifecycle)];library.rtx_owned_broker_serve251.restype=ctypes.c_int
 library.rtx_owned_broker_close_permitted251.argtypes=[];library.rtx_owned_broker_close_permitted251.restype=ctypes.c_int
 library.rtx_owned_broker_stop251.argtypes=[];library.rtx_owned_broker_stop251.restype=ctypes.c_int
 backend.close_guard.attach(library)
 request=json.loads((ROOT/'start-request.json').read_bytes());session=request['publication_session'];assert type(session)is int and 0<session<1<<63
 installed=Path('/Library/GPUBundles/RTXMetalGraphics253-normal.bundle')
 for row in json.loads((ROOT/'application253-manifest.json').read_bytes()):
  f=installed/row['path'];assert f.resolve()==f and f.stat().st_uid==0 and not f.stat().st_mode&0o022 and hashlib.sha256(f.read_bytes()).hexdigest()==row['sha256']
 spec=json.loads((installed/'Contents/Resources/runtime-config253.json').read_bytes());spec_valid(spec)
 primitive=ROOT/'graphics254/publication-control253-normal';command=PropertyCommand(primitive,hashlib.sha256(primitive.read_bytes()).hexdigest(),root,backend.binding,session)
 publication=Publication(spec,backend.binding,session,command)
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
   current=machine253.observe();parent,child,_=machine253.require_loaded(current,backend.binding)
   machine253.fields(parent,dict(OwnedGraphicsActive=True,OwnedGraphicsRetained=False))
   machine253.fields(child,dict(RTXMetalGPUReady=True,MetalPluginName='RTXMetalGraphics253-normal',MetalPluginClassName='RTXMetalApplicationDevice209',RTXMetalPublicationEpoch=publication.epoch,RTXMetalPublicationSession=publication.session))
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
   assert(event==1 and events==[])or(event==2 and events==[1]);events.append(event)
   if event==1:publication.activate()
   else:publication.withdraw()
   save(output/('lifecycle254-%d.json'%event),dict(pid=os.getpid(),event=event,global_metal_publication=True,records=publication.records));return 0
  except BaseException:return failure('lifecycle')
 def client():
  proc=None
  try:
   for _ in range(600):
    if(root/'activated251.json').is_file():break
    time.sleep(.05)
   assert(root/'activated251.json').is_file()
   argv=[str(PUBLIC/'metal-client254-normal'),str(clientout),str(backend.generation),str(backend.binding['child_registry']),str(PUBLIC/'graphics247.rtxlib'),'gpu']
   with(output/'client252.stdout').open('xb')as so,(output/'client252.stderr').open('xb')as se:
    proc=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=so,stderr=se,user=501,group=20,extra_groups=[],start_new_session=True);save(output/'client252-started.json',dict(pid=proc.pid,argv=argv));code=proc.wait(timeout=45)
   client_state.update(pid=proc.pid,returncode=code,terminal=True);save(output/'client252-returned.json',client_state)
  except BaseException:
   if proc is not None and proc.poll()is None:proc.kill();proc.wait()
   client_state.update(error=True);(output/'client252-error.txt').write_text(traceback.format_exc())
  finally:
   stop=library.rtx_owned_broker_stop251();save(output/'client254-stop.json',dict(returncode=stop))
 cb=Callbacks(251,ctypes.sizeof(Callbacks),None,backend.generation,begin,draw,retire,0);lc=Lifecycle(202,ctypes.sizeof(Lifecycle),None,lifecycle,0)
 thread=threading.Thread(target=client,name='graphics254-client')
 # ctypes callbacks and the pending property subprocess remain reachable even
 # if a withdrawal failure quarantines the server/native owner.
 backend.graphics254_keepalive=(library,publication,command,cb,lc,thread)
 backend.close_guard.enter();thread.start()
 code=library.rtx_owned_broker_serve251(SERVICE.encode(),501,ctypes.byref(cb),str(root).encode(),0,60,ctypes.byref(lc))
 backend.close_guard.returned(int(code));permitted=library.rtx_owned_broker_close_permitted251()
 save(output/'server252-returned.json',dict(returncode=code,close_permitted=permitted,pid=os.getpid()));thread.join(timeout=40)
 assert not thread.is_alive()and client_state.get('returncode')==0 and code==0 and permitted==1 and retired and events==[1,2]and len(audits)==3
 result=json.loads((clientout/'client-result.json').read_bytes());assert result['passed']and not result['cpu_model']and result['actual_xpc']and result['standard_draw_primitives']and result['completed_commands']==3 and result['generation']==backend.generation and result['child_registry']==backend.binding['child_registry']and result['standard_metal_enumeration']and not result['private_factory_called']
 save(output/'metal-client-result252.json',result)
 for name in ('enumeration.json','admission.json'):
  raw=(clientout/name).read_bytes();save(output/('metal-'+name),json.loads(raw))
 machine253.require_hidden(machine253.observe(),backend.binding);assert publication.withdrawn and not Path(spec['ready_path']).exists()
 for serial in(1,2,3):
  request=(backend.evidence/('native-session/graphics-draw250-%d/application-request.bin'%serial)).read_bytes();binding=draw250.decode(request);reply=(backend.evidence/('native-session/graphics-draw250-%d/application-result.bin'%serial)).read_bytes()
  for kind in('vertex','before','after'):
   raw=(clientout/('frame-%d-%s.bin'%(serial,kind))).read_bytes();expected=request[160:160+binding['vb']]if kind=='vertex'else request[160+binding['vb']:]if kind=='before'else reply[binding['vb']:];assert raw==expected
   (output/('metal-frame-%d-%s.bin'%(serial,kind))).write_bytes(raw)
 after=machine244.observe();parent,_=machine244.require_loaded(after,backend.binding);machine244.fields(parent,dict(OwnedGraphicsActive=True,OwnedGraphicsCompleted=3,OwnedGraphicsRetained=False));save(output/'graphics-machine-after.json',after)
 final=dict(passed=all(a['image']['exact_ideal_match']for a in audits),native_transactions_verified=True,actual_gpu_jobs=3,private_device_factory=False,standard_metal_enumeration=True,standard_metal_draw_api=True,actual_xpc=True,application_readback_matches_native=True,frames=audits,presentation_tested=False,metal_conformance_proven=False)
 save(output/'graphics-audit.json',final);return final
