"""Compile native owner and both program transports; CPU tests only."""
from pathlib import Path
import hashlib,json,os,plistlib,shlex,subprocess,sys
s=Path(__file__).resolve().parent;out=s.parent/'build';out.mkdir();rows=[];closures={};tests={}
H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(name,value):
 with(out/name).open('x')as f:json.dump(value,f,indent=2);f.write('\n')
def run(argv,name,env=None):
 argv=list(map(str,argv))
 with(out/(name+'.stdout')).open('xb')as so,(out/(name+'.stderr')).open('xb')as se:
  p=subprocess.Popen(argv,cwd=s,stdin=subprocess.DEVNULL,stdout=so,stderr=se,env=env)
  try:rc=p.wait(timeout=180)
  except BaseException:p.kill();p.wait();raise
 rows.append(dict(name=name,argv=argv,pid=p.pid,returncode=rc));assert rc==0,(name,rc,(out/(name+'.stderr')).read_text(errors='replace')[-3000:]);return(out/(name+'.stdout')).read_bytes()
def closure(tag):
 names=shlex.split((out/(tag+'.d')).read_text().replace('\\\n',' '))[1:];paths=sorted(set((s/n).resolve()for n in names))
 closures[tag]=[dict(path=p.relative_to(s).as_posix(),sha256=H(p))for p in paths]
def build(source,tag,flags,obj=False):
 target=out/(tag+('.o'if obj else ''));run([clang,*flags,'-MMD','-MF',out/(tag+'.d'),*(['-c']if obj else []),s/source,'-o',target],'compile-'+tag);closure(tag);return target
try:
 assert sys.platform=='darwin'and os.geteuid()==501
 boot=sys.argv[1];assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-before').decode().strip()==boot
 raw=(s/'source-manifest.json').read_bytes();inputs=json.loads(raw)
 for row in inputs:assert (s/row['path']).stat().st_size==row['bytes']and H(s/row['path'])==row['sha256']
 clang=run(['/usr/bin/xcrun','--find','clang++'],'clang-path').decode().strip()
 run([clang,'--version'],'clang-version')
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',PYTHONDONTWRITEBYTECODE='1')
 production=['RTXMetalLibrary.mm','RTXHostBuffer.mm','RTXMetalCommand.mm','RTXNativeOwner.mm','RTXDeviceConstruction.mm','RTXBundleEntry.mm','RTXDeviceLimits.mm','RTXDeviceFeatures.mm','RTXAcceleratorIdentity103.mm','RTXColdParent108.mm','RTXOwnedBrokerTransport188.mm','RTXOwnedXPC188.mm','RTXOwnedBrokerServer188.mm']
 frameworks=['-framework','Foundation','-framework','Metal','-framework','IOKit']
 reuse=json.loads((s/'reuse208-v1.json').read_bytes());prior=Path(reuse['root']);assert H(prior.parent/'source/source-manifest.json')==reuse['source_manifest_sha256']
 for name,digest in reuse['files'].items():assert H(prior/name)==digest
 for tag,items in reuse['closures'].items():
  for item in items:assert H(s/item['path'])==item['sha256'],(tag,item['path'])
 save('reused-normal.json',reuse)
 for mode in ('normal','asan'):
  extra=[]if mode=='normal'else['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
  flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',*extra]
  objc=[*flags,'-Wno-deprecated-declarations','-fno-objc-arc','-fblocks']
  if mode=='normal':
   tests[mode]=json.loads((prior/'normal-results/result.json').read_bytes());tests['initial-'+mode]=json.loads((prior/'initial-normal-results/result.json').read_bytes())
   objects={n:prior/(Path(n).stem+'-normal.o')for n in production};bundle=prior/'RTXMetalNativeOwner208-normal.bundle'
   transport=[prior/(Path(n).stem+'-normal.o')for n in ('RTXOwnedBrokerTransport208.mm','RTXOwnedXPC208.mm')]
  else:
    exe=build('transport-tests208.cpp','transport-'+mode,[*flags,'-Wno-unused-function']);run([exe,s,out/(mode+'-results')],'transport-test-'+mode,env);tests[mode]=json.loads((out/(mode+'-results/result.json')).read_bytes());assert tests[mode]['passed']and tests[mode]['recorded_jobs']==35
    exe=build('initial-tests208.cpp','initial-'+mode,[*flags,'-Wno-unused-function']);run([exe,s,out/('initial-'+mode+'-results')],'initial-test-'+mode,env)
    tests['initial-'+mode]=json.loads((out/('initial-'+mode+'-results/result.json')).read_bytes())
    objects={n:build('owner/'+n,Path(n).stem+'-'+mode,objc,True)for n in production}
    bundle=out/('RTXMetalNativeOwner208-'+mode+'.bundle');(bundle/'Contents/MacOS').mkdir(parents=True)
    info=dict(CFBundleIdentifier='local.emre.RTXMetalNativeOwner208.'+mode,CFBundleName='RTXMetalNativeOwner208',CFBundleVersion='0.208.0',CFBundlePackageType='BNDL',CFBundleExecutable='RTXMetalDriver',NSPrincipalClass='RTXMetalNativeOwner108')
    (bundle/'Contents/Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True));binary=bundle/'Contents/MacOS/RTXMetalDriver'
    run([clang,*objc,'-bundle','-Wl,-U,_OBJC_CLASS_$__MTLDevice',*objects.values(),*frameworks,'-o',binary],'link-owner-'+mode)
    server=build('owner/RTXOwnedBrokerServer208.mm','server208-'+mode,objc,True);broker=out/('libRTXOwnedBroker208-'+mode+'.dylib')
    run([clang,*objc,'-dynamiclib','-install_name','@rpath/'+broker.name,server,*frameworks,'-o',broker],'link-server-'+mode)
    transport=[build('owner/'+n,Path(n).stem+'-'+mode,objc,True)for n in ('RTXOwnedBrokerTransport208.mm','RTXOwnedXPC208.mm')]
    app=out/('libRTXOwnedTransport208-'+mode+'.dylib');run([clang,*objc,'-dynamiclib','-install_name','@rpath/'+app.name,*transport,*frameworks,'-o',app],'link-transport-'+mode)
    for name,path in [('owner',bundle),('broker',broker),('transport',app)]:
     run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',path],'sign-'+name+'-'+mode);run(['/usr/bin/codesign','--verify','--strict',path],'verify-'+name+'-'+mode)
     b=binary if name=='owner'else path
     for kind,args in [('exports',['nm','-gU']),('undefined',['nm','-u']),('loads',['otool','-l'])]:run(['/usr/bin/xcrun',*args,b],name+'-'+kind+'-'+mode)
  modelObj=prior/'model208-normal.o'if mode=='normal'else build('model208.mm','model208-'+mode,objc,True);model=out/('libRTXModel208-'+mode+'.dylib')
  run([clang,*objc,'-dynamiclib','-install_name','@rpath/'+model.name,modelObj,*frameworks,'-o',model],'link-model-'+mode)
  clientObj=build('client208.mm','client208-'+mode,objc,True);client=out/('client208-'+mode)
  run([clang,*objc,clientObj,*transport,*frameworks,'-o',client],'link-client-'+mode)
  for name,path in [('model',model),('client',client)]:
   run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',path],'sign-'+name+'-'+mode);run(['/usr/bin/codesign','--verify','--strict',path],'verify-'+name+'-'+mode)
   run(['/usr/bin/xcrun','nm','-u',path],name+'-imports-'+mode)
  for name in ['cold-parent-test.mm','owner-bundle-test.mm']:
   obj=build('owner/'+name,Path(name).stem+'-'+mode,objc,True);exe=out/(Path(name).stem+'-'+mode)
   dependencies=[objects['RTXColdParent108.mm'],objects['RTXAcceleratorIdentity103.mm']]if name.startswith('cold-')else []
   run([clang,*objc,obj,*dependencies,*frameworks,'-o',exe],'link-'+name+'-'+mode)
   if name.startswith('cold-'):
    run([exe,s/'owner/fixtures/memory107.bin',out/('cold-parent-'+mode+'.json')],'test-cold-'+mode,env)
   else:
    for kind in ('principal','named'):run([exe,bundle,kind,out/('bundle-'+kind+'-'+mode+'.json')],'test-bundle-'+kind+'-'+mode,env)
 assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-after').decode().strip()==boot
 for row in inputs:assert H(s/row['path'])==row['sha256']
 save('result.json',dict(passed=True,source_manifest_sha256=hashlib.sha256(raw).hexdigest(),tests=tests,clang_sha256=H(Path(clang)),native_opened=False,kext_loaded=False,actual_xpc=False,gpu_executed=False))
finally:save('commands.json',rows);save('closures.json',closures)
