"""Verify package, firmware inputs and unused native exports; never open a GPU."""
from pathlib import Path
import ctypes,hashlib,json,os,subprocess,sys
from common244 import ROOT,verify,save
from activate212 import activate
def run(cold):
 package=verify();catalog,image,receipt=activate(ROOT/'source');sys.path.insert(0,str(ROOT/'runtime244'))
 import native244,gsp_bootstrap244,machine244,triangle244
 from gsp_firmware import inspect_gsp_assets
 assert gsp_bootstrap244.client.MacIOKitBackend is native244.MacIOKitBackend
 owner_path=ROOT/'owner.bundle/Contents/MacOS/RTXMetalDriver';native244.verified_file(owner_path,native244.OWNER_SHA,8);native244.verified_file(ROOT/'RTXProbe.kext/Contents/MacOS/RTXProbe',native244.KERNEL_SHA,11)
 for path in(ROOT/'owner.bundle',ROOT/'RTXProbe.kext'):subprocess.run(['/usr/bin/codesign','--verify','--strict',str(path)],capture_output=True,check=True,timeout=20)
 import graphics254
 assert ctypes.sizeof(graphics254.Callbacks)==56 and ctypes.sizeof(graphics254.Lifecycle)==32
 for path in(ROOT/'graphics254/RTXGraphicsBrokerServer251-normal.dylib',ROOT/'graphics254/public/metal-client254-normal',ROOT/'application253.bundle',ROOT/'RTXMetalAccelerator.kext',ROOT/'graphics254/publication-control253-normal'):subprocess.run(['/usr/bin/codesign','--verify','--strict',str(path)],capture_output=True,check=True,timeout=20)
 server=ctypes.CDLL(str(ROOT/'graphics254/RTXGraphicsBrokerServer251-normal.dylib'));server.rtx_owned_broker_close_permitted251.restype=ctypes.c_int;assert server.rtx_owned_broker_close_permitted251()==1
 owner=ctypes.CDLL(str(owner_path),mode=ctypes.RTLD_GLOBAL);native244.bind(owner);info,_=native244.read_info(owner,os.getpid())
 assert info['state']==info['io_opens']==info['calls']==info['open_attempts']==0
 observe=machine244.observe();binding=machine244.require_cold(observe)if cold else None
 if cold:assert binding['boot_uuid']!='0AE8F2A6-176D-4659-A3E2-B93C05DBA809'and not(ROOT/'worker-claim.json').exists()and not(ROOT/'start-request.json').exists()
 firmware=inspect_gsp_assets(ROOT/'source/base-v1/firmware/570.144');assert all(k in firmware for k in('gsp','bootloader','booter_load'))
 public=Path('/private/var/tmp/rtx-graphics-discovery253-v1/public');assert not(public/'ready.json').exists()and not(public/'compiler.sock').exists()
 service=subprocess.run(['/bin/launchctl','print','system/local.emre.RTXGraphicsBroker251.publication253.v1'],capture_output=True,text=True,timeout=15);assert service.returncode!=0 and 'Could not find service'in service.stderr
 result=dict(passed=True,pid=os.getpid(),package=package,cold_required=cold,binding=binding,observation=observe,native_cold=info,firmware_inputs={k:firmware[k]['sha256']for k in('gsp','bootloader','booter_load')},bootstrap_container_sha256=hashlib.sha256(image).hexdigest(),triangle_reference=triangle244.check(triangle244.reference()),native_opened=False,kext_loaded=False,firmware_started=False,gpu_executed=False)
 save(ROOT/('preflight-cold.json'if cold else'preflight-staged.json'),result);return result
if __name__=='__main__':
 assert len(sys.argv)==2 and sys.argv[1]in('staged','cold');print(json.dumps(run(sys.argv[1]=='cold'),indent=2))
