"""Build experimental texture shader225. CPU models only; no install or GPU open."""
from pathlib import Path
import hashlib,json,os,plistlib,shlex,shutil,subprocess,sys
s=Path(__file__).resolve().parent;app=s/'app';out=s.parent/'build';out.mkdir();commands=[];closures={};tests={};loaded_before=None
H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(name,value):
 with(out/name).open('x')as f:json.dump(value,f,indent=2);f.write('\n')
def run(argv,name):
 argv=list(map(str,argv))
 with(out/(name+'.stdout')).open('xb')as so,(out/(name+'.stderr')).open('xb')as se:
  p=subprocess.Popen(argv,cwd=s,stdin=subprocess.DEVNULL,stdout=so,stderr=se,env=env)
  try:rc=p.wait(timeout=180)
  except BaseException:p.kill();p.wait();raise
 commands.append(dict(name=name,argv=argv,pid=p.pid,returncode=rc));assert rc==0,(name,rc,(out/(name+'.stderr')).read_text(errors='replace')[-4000:]);return(out/(name+'.stdout')).read_bytes()
def closure(tag):
 names=shlex.split((out/(tag+'.d')).read_text().replace('\\\n',' '))[1:];paths=sorted(set((s/n).resolve()for n in names));closures[tag]=[dict(path=p.relative_to(s).as_posix(),sha256=H(p))for p in paths]
def verify():
 for row in inputs:assert (s/row['path']).stat().st_size==row['bytes']and H(s/row['path'])==row['sha256']
def snapshot(when):
 global loaded_before
 assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-'+when).decode().strip()==boot
 loaded=run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-'+when)
 rtx=sorted(line for line in loaded.splitlines()if b'local.emre.RTX'in line)
 assert b'AMDRadeon'in loaded
 if when=='before':loaded_before=rtx
 else:assert rtx==loaded_before,'CPU build changed loaded RTX modules'

env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
try:
 assert sys.platform=='darwin'and os.geteuid()==501;boot=sys.argv[1];raw=(s/'source-manifest.json').read_bytes();inputs=json.loads(raw);verify();snapshot('before')
 clang=run(['/usr/bin/xcrun','--find','clang++'],'clang-path').decode().strip();run([clang,'--version'],'clang-version')
 base='/private/var/tmp/rtx-resident-metal209-v1'
 config=dict(abi=209,service='local.emre.RTXOwnedBroker208.publication209.v1',application_version='0.209.1',ready_path=base+'/public/ready.json',compiler_helper=base+'/helper/backend_client062.py',compiler_helper_sha256='a6875cb56141c72ba276eff6cd3bd273e78230363c97a4d715b92b5be3e49c50',compiler_configuration=base+'/public/compiler-client.json',diagnostics_root='/Users/DEVELOPER/rtx-resident-metal209-diagnostics-v1',container_sha256=H(app/'selected.rtxlib'))
 assert H(app/'helper/backend_client062.py')==config['compiler_helper_sha256']
 frameworks=['-framework','Foundation','-framework','Metal','-framework','IOKit','-framework','IOSurface']
 for mode,extra in [('normal',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
  cpp=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',*extra];objc=[*cpp,'-Wno-deprecated-declarations','-fno-objc-arc','-fobjc-weak','-fblocks'];objects=[]
  for p in sorted(app.glob('RTX*.mm')):
   tag=p.stem+'-'+mode;obj=out/(tag+'.o')
   run([clang,*objc,'-MMD','-MF',out/(tag+'.d'),'-c',p,'-o',obj],'compile-'+tag);closure(tag);objects.append(obj)
  assert len(objects)==22
  bundle=out/('RTXMetalTextureShader225-'+mode+'.bundle');contents=bundle/'Contents';(contents/'MacOS').mkdir(parents=True);(contents/'Resources').mkdir()
  info=dict(CFBundleIdentifier='local.emre.RTXMetalTextureShader225.'+mode,CFBundleExecutable='RTXMetalApplication',CFBundleName='RTXMetalTextureShader225',CFBundlePackageType='BNDL',CFBundleVersion='0.225.0',CFBundleShortVersionString='0.225.0',NSPrincipalClass='RTXMetalApplicationDevice209')
  (contents/'Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True));(contents/'Resources/selected.rtxlib').write_bytes((app/'selected.rtxlib').read_bytes());(contents/'Resources/runtime-config209.json').write_text(json.dumps(config,indent=2)+'\n')
  binary=contents/'MacOS/RTXMetalApplication';run([clang,*objc,'-bundle','-Wl,-U,_OBJC_CLASS_$__MTLDevice',*objects,*frameworks,'-o',binary],'link-bundle-'+mode)
  run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',bundle],'sign-'+mode);run(['/usr/bin/codesign','--verify','--strict',bundle],'signature-'+mode)
  for kind,args in [('exports',['nm','-gU']),('undefined',['nm','-u']),('loads',['otool','-l'])]:run(['/usr/bin/xcrun',*args,binary],kind+'-'+mode)
  for stem,ext,args in [('admission-test209','.cpp',[app/'memory199.bin']),('owned-admission-test209','.cpp',[s/'programs/coordinates.rtxlib',app/'memory199.bin']),('application-owned-test209','.mm',[bundle,s/'programs/coordinates.rtxlib']),('application-programs209','.mm',[bundle,s/'programs',out/('programs-'+mode+'-results')])]:
   tag=stem+'-'+mode;flags=objc if ext=='.mm'else cpp;libs=frameworks if ext=='.mm'else []
   run([clang,*flags,'-MMD','-MF',out/(tag+'.d'),s/(stem+ext),*libs,'-o',out/tag],'compile-'+tag);closure(tag)
   result=json.loads(run([out/tag,*args],'test-'+tag));assert result['passed']and result['gpu_jobs']==0 and not result['actual_xpc'];tests[tag]=result
   if stem=='application-owned-test209':
    assert result['texture_api_checks']>0
    result=json.loads(run([out/tag,bundle,s/'programs/first.rtxlib'],'test-'+tag+'-abi1'));assert result['passed']and result['cpu_model_jobs']==5 and result['texture_api_checks']>0;tests[tag+'-abi1']=result
  tag='application-textures225-'+mode
  run([clang,*objc,'-MMD','-MF',out/(tag+'.d'),s/'application-textures225.mm',*frameworks,'-o',out/tag],'compile-'+tag);closure(tag)
  result=json.loads(run([out/tag,bundle,s/'texture225.rtxlib',out/('texture-bindings-'+mode+'-results')],'test-'+tag))
  assert result['passed'] and result['cpu_model_jobs']==2 and not result['gpu_jobs'] and not result['actual_xpc'];tests[tag]=result
  # Exercise the actual bundle-resolved catalog reader, including the old guard.
  tag='bundle-catalog214-'+mode
  run([clang,*objc,s/'bundle-catalog214.mm',*frameworks,'-o',out/tag],'compile-'+tag)
  legacy=out/('legacy-port214-'+mode+'.o')
  run([clang,*objc,'-I',app,'-c',s/'regression/RTXApplicationPortLegacy214.mm','-o',legacy],'compile-legacy-port-'+mode)
  for case,image,accept,old_guard in [('abi1',app/'selected.rtxlib',True,False),('abi2',s/'programs/coordinates.rtxlib',True,False),('corrupt',app/'selected.rtxlib',False,False),('legacy-abi1',app/'selected.rtxlib',False,True),('legacy-abi2',s/'programs/coordinates.rtxlib',True,True)]:
   fixture=out/('catalog-'+case+'-'+mode+'.bundle');shutil.copytree(bundle,fixture)
   selected=fixture/'Contents/Resources/selected.rtxlib';raw_image=bytearray(image.read_bytes())
   if case=='corrupt':raw_image[0]^=1
   selected.write_bytes(raw_image)
   if old_guard:
    linked=[legacy if q.name=='RTXApplicationPort-'+mode+'.o'else q for q in objects]
    run([clang,*objc,'-bundle','-Wl,-U,_OBJC_CLASS_$__MTLDevice',*linked,*frameworks,'-o',fixture/'Contents/MacOS/RTXMetalApplication'],'link-legacy-'+case+'-'+mode)
   run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',fixture],'sign-catalog-'+case+'-'+mode)
   result=json.loads(run([out/tag,fixture,selected,'accept'if accept else'reject'],'test-catalog-'+case+'-'+mode));assert result['passed'] and result['accepted']==accept and not result['gpu_executed'];tests['catalog-'+case+'-'+mode]=result
 verify();snapshot('after');save('result.json',dict(passed=True,source_manifest_sha256=hashlib.sha256(raw).hexdigest(),clang_sha256=H(Path(clang)),tests=tests,installed=False,kext_loaded=False,native_opened=False,actual_xpc=False,gpu_executed=False,standard_metal_enumeration=False))
finally:save('commands.json',commands);save('closures.json',closures)
