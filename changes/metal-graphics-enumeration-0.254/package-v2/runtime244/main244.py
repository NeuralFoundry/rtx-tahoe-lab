"""One private C797 triangle after verified GSP bootstrap, then native close."""
from pathlib import Path
import ctypes,hashlib,json,os,sys,traceback
from common244 import ROOT,save,verify
from activate212 import activate
from graphics254 import graphics

def main():
 package=verify();request=json.loads((ROOT/'start-request.json').read_bytes());assert request['package_sha256']==package['manifest_sha256']
 save(ROOT/'worker-claim.json',dict(pid=os.getpid(),binding=request['binding'],package_sha256=package['manifest_sha256']))
 output=ROOT/'evidence';output.mkdir(mode=0o700);backend=None;result=dict(passed=False)
 try:
  catalog,image,receipt=activate(ROOT/'source');sys.path.insert(0,str(ROOT/'runtime244'))
  import native244,gsp_bootstrap244,machine244
  before=machine244.observe();binding=machine244.require_cold(before);assert binding==request['binding'];save(output/'machine-before.json',before);save(output/'bootstrap-admission.json',receipt)
  # Keep the object reachable if its constructor raises after native open.
  backend=native244.MacIOKitBackend.__new__(native244.MacIOKitBackend)
  backend.__init__(catalog,image,output/'native-host',ROOT/'owner.bundle/Contents/MacOS/RTXMetalDriver',ROOT/'RTXProbe.kext/Contents/MacOS/RTXProbe',binding)
  base=ROOT/'source/base-v1'
  result=gsp_bootstrap244.run(backend,base/'firmware/570.144',base/'results/gsp-preflight-20260906T122322Z/snapshot.plist',output,lambda owner:graphics(owner,output))
 except BaseException as error:
  result.update(passed=False,error_type=type(error).__name__);(output/'main-error.txt').write_text(traceback.format_exc())
 finally:
  if backend is not None and not getattr(backend,'closed',False)and (not hasattr(backend,'close_guard')or backend.close_guard.permitted()):
   try:
    if not hasattr(backend,'owner'):backend.closed=True;backend.closure_verified=True
    else:backend.close()
   except BaseException as error:result.update(passed=False,close_error_type=type(error).__name__)
  closed=backend is None or(backend.closed and backend.closure_verified)
  if not closed:result.update(passed=False,native_close_unproven=True)
  result['actual_graphics_gpu_verified']=bool(result.get('passed')and closed and result.get('hardware_backend')and result.get('application212',{}).get('passed'))
  result['full_metal_complete']=False;result['kernel_reset_still_required']=True
  save(output/'result.json',result)
  if not closed:
   save(ROOT/'worker-quarantined.json',dict(pid=os.getpid(),native_closed=False,retained_for_recovery=True))
   # Keep native owner/callbacks/pending property process alive until the
   # operator or authorized recovery reboots this kernel. Never silently exit.
   import threading
   held=threading.Event()
   while True:held.wait(30)
  save(ROOT/'worker-returned.json',dict(pid=os.getpid(),passed=result.get('passed')is True,native_closed=closed,hardware_started=bool(result.get('launch_requested')),actual_graphics_gpu_verified=result['actual_graphics_gpu_verified'],kernel_resources_retained_until_restart=True))
 return 0 if result.get('passed')else 1
if __name__=='__main__':raise SystemExit(main())
