"""Rebuild the actual owner and reproduce/fix claim lifetime using CPU only."""
from pathlib import Path
import hashlib,json,os,plistlib,shlex,subprocess,sys,traceback
s=Path(__file__).resolve().parent;native=s/'native-source-v1';out=s.parent/'build';out.mkdir();rows=[];closures={};tests={}
H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(name,value):
 with(out/name).open('x')as f:json.dump(value,f,indent=2);f.write('\n')
def run(argv,name,env=None):
 argv=list(map(str,argv))
 with(out/(name+'.stdout')).open('xb')as so,(out/(name+'.stderr')).open('xb')as se:
  p=subprocess.Popen(argv,cwd=s,stdin=subprocess.DEVNULL,stdout=so,stderr=se,env=env)
  (out/'running.json').write_text(json.dumps(dict(name=name,pid=p.pid,argv=argv))+'\n')
  try:rc=p.wait(timeout=180)
  except BaseException:p.kill();p.wait();raise
 rows.append(dict(name=name,pid=p.pid,returncode=rc,state='terminal'));(out/'progress.json').write_text(json.dumps(rows)+'\n')
 assert rc==0,(name,rc,(out/(name+'.stderr')).read_text(errors='replace')[-4000:]);return(out/(name+'.stdout')).read_bytes()
def build(source,tag,flags):
 target=out/(tag+'.o');run([clang,*flags,'-I',native/'owner','-MMD','-MF',out/(tag+'.d'),'-c',source,'-o',target],'compile-'+tag)
 names=shlex.split((out/(tag+'.d')).read_text().replace('\\\n',' '))[1:];paths=sorted(set((s/n).resolve()for n in names))
 closures[tag]=[dict(path=p.relative_to(s).as_posix(),sha256=H(p))for p in paths];return target
try:
 assert sys.platform=='darwin'and os.geteuid()==501
 boot=sys.argv[1];assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-before').decode().strip()==boot
 loaded=run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-before');assert b'AMDRadeon'in loaded;rtx=sorted(line for line in loaded.splitlines()if b'local.emre.RTX'in line)
 raw=(s/'source-manifest247.json').read_bytes();inputs=json.loads(raw)
 for row in inputs:assert(s/row['path']).stat().st_size==row['bytes']and H(s/row['path'])==row['sha256']
 clang=run(['/usr/bin/xcrun','--find','clang++'],'clang-path').decode().strip();run([clang,'--version'],'clang-version')
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',PYTHONDONTWRITEBYTECODE='1')
 production=['RTXMetalLibrary.mm','RTXHostBuffer.mm','RTXMetalCommand.mm','RTXNativeOwner.mm','RTXDeviceConstruction.mm','RTXBundleEntry.mm','RTXDeviceLimits.mm','RTXDeviceFeatures.mm','RTXAcceleratorIdentity103.mm','RTXColdParent108.mm','RTXOwnedBrokerTransport188.mm','RTXOwnedXPC188.mm','RTXOwnedBrokerServer188.mm']
 frameworks=['-framework','Foundation','-framework','Metal','-framework','IOKit']
 for mode in('normal','asan'):
  extra=[]if mode=='normal'else['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
  flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-DRTX_GRAPHICS242','-Wno-deprecated-declarations','-fno-objc-arc','-fblocks',*extra]
  objects={n:build(native/'owner'/n,Path(n).stem+'-'+mode,flags)for n in production}
  for stem in ('legacy-run250','test-native250'):
   exe=out/(stem+'-'+mode);run([clang,*flags,s/(stem+'.cpp'),'-o',exe],'build-'+stem+'-'+mode)
   report=json.loads(run([exe,s/'fixture236',native/'probe/kernel/bootstrap-device.bin',s/'reference-rgba8.bin',out/(stem+'-'+mode+'-results')],'run-'+stem+'-'+mode,env));assert report['passed']and not report['actual_native_io']and not report['gpu_executed'];tests[stem+'-'+mode]=report
  bundle=out/('RTXMetalNativeOwner250-'+mode+'.bundle');(bundle/'Contents/MacOS').mkdir(parents=True)
  info=dict(CFBundleIdentifier='local.emre.RTXMetalNativeOwner250.'+mode,CFBundleName='RTXMetalNativeOwner250',CFBundleVersion='0.250.0',CFBundlePackageType='BNDL',CFBundleExecutable='RTXMetalDriver',NSPrincipalClass='RTXMetalNativeOwner108')
  (bundle/'Contents/Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True));binary=bundle/'Contents/MacOS/RTXMetalDriver'
  run([clang,*flags,'-bundle','-Wl,-U,_OBJC_CLASS_$__MTLDevice',*objects.values(),*frameworks,'-o',binary],'link-owner-'+mode)
  run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',bundle],'sign-owner-'+mode);run(['/usr/bin/codesign','--verify','--strict',bundle],'verify-owner-'+mode)
  for kind,args in(('exports',['nm','-gU']),('undefined',['nm','-u']),('loads',['otool','-l'])):run(['/usr/bin/xcrun',*args,binary],'owner-'+kind+'-'+mode)
  assert 'rtx_native_graphics_draw250'in(out/('owner-exports-'+mode+'.stdout')).read_text()
  tests['owner-'+mode]=dict(sha256=H(binary),bytes=binary.stat().st_size)
 run(['/usr/bin/python3','-B',s/'build-kernel250.py',boot],'kernel-build')
 kernel=json.loads((s.parent/'kernel-build/result.json').read_bytes());assert kernel['passed'];tests['kernel']=dict(sha256=kernel['binary_sha256'],bytes=kernel['binary_bytes'],max_frame=kernel['max_frame'])
 assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-after').decode().strip()==boot
 after=run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-after');assert b'AMDRadeon'in after and rtx==sorted(line for line in after.splitlines()if b'local.emre.RTX'in line)
 for row in inputs:assert H(s/row['path'])==row['sha256']
 save('result.json',dict(passed=True,boot_uuid=boot,source_manifest_sha256=hashlib.sha256(raw).hexdigest(),tests=tests,native_opened=False,kext_loaded=False,gpu_executed=False))
except BaseException:
 (out/'error.txt').write_text(traceback.format_exc());raise
finally:save('commands.json',rows);save('closures.json',closures)
