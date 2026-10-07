"""Build publication253 KEXT, typed reader and control tool; no loading/publication."""
from pathlib import Path
import datetime,hashlib,json,os,plistlib,shlex,subprocess,sys,tarfile,traceback
root=Path(__file__).resolve().parent;out=root.parent.parent/'build/child';out.mkdir(parents=True);boot=sys.argv[1]
H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();rows=json.loads((root/'source-manifest.json').read_bytes());commands=[];passed=False;error=None;closures={}
env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def run(argv,name,timeout=60):
 argv=list(map(str,argv))
 with subprocess.Popen(argv,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)as p:
  try:stdout,stderr=p.communicate(timeout=timeout)
  except subprocess.TimeoutExpired:p.kill();stdout,stderr=p.communicate();raise
 (out/name).write_bytes(stdout);(out/(name+'.stderr')).write_bytes(stderr);commands.append(dict(argv=argv,log=name,pid=p.pid,returncode=p.returncode));assert p.returncode==0,(name,p.returncode,stderr.decode(errors='replace')[-3000:]);return stdout
def sources():
 for r in rows:assert (root/r['path']).stat().st_size==r['bytes'] and H(root/r['path'])==r['sha256']
def snapshot(phase):
 assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-'+phase+'.txt').decode().strip()==boot
 loaded=run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-'+phase+'.txt')
 assert b'AMDRadeon' in loaded
 if phase=='after':assert sorted(x for x in loaded.splitlines() if b'local.emre.RTX' in x)==sorted(x for x in (out/'loaded-before.txt').read_bytes().splitlines() if b'local.emre.RTX' in x)
def closure(tag):
 names=shlex.split((out/(tag+'.d')).read_text().replace('\\\n',' '))[1:]
 # SDK headers are system dependencies and excluded by -MMD.
 closure=[]
 for name in sorted(set(names)):
  p=(root/name).resolve();assert p.is_relative_to(root);closure.append(dict(path=p.relative_to(root).as_posix(),sha256=H(p)))
 closures[tag]=closure
try:
 assert os.geteuid()==501;sources();snapshot('before')
 for mode,flags in [('normal',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
  for name,args in [('publication',[]),('binding',[root/'memory199.bin'])]:
   tag=name+'-'+mode
   run(['xcrun','clang++','-std=c++14','-O1','-g','-Wall','-Wextra','-Werror',*flags,'-MMD','-MF',out/(tag+'.d'),name+'-test.cpp','-o',out/tag],'compile-'+tag+'.txt');closure(tag)
   test=json.loads(run([out/tag,*args],tag+'.json'));assert test['passed'] and test['gpu_jobs']==0
  objects=[]
  for name in ('RTXAcceleratorIdentity103','binding-properties-test','publication-control200'):
   tag=name+'-'+mode
   run(['xcrun','clang++','-std=c++14','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fno-objc-arc','-fobjc-exceptions',*flags,'-MMD','-MF',out/(tag+'.d'),'-c',name+'.mm','-o',out/(tag+'.o')],'compile-'+tag+'.txt');closure(tag);objects.append(out/(tag+'.o'))
  for index,name in ((1,'binding-properties'),(2,'publication-control200')):
   tag=name+'-'+mode
   run(['xcrun','clang++',*flags,objects[0],objects[index],'-framework','Foundation','-framework','IOKit','-o',out/tag],'link-'+tag+'.txt')
   if index==1:
    test=json.loads(run([out/tag,root/'parent253.plist'],tag+'.json'));assert test['passed'] and test['gpu_jobs']==0 and test['actual_framework_types'] and not test['actual_iokit']
 sdk=run(['xcrun','--show-sdk-path'],'sdk.txt').decode().strip();version=run(['xcrun','--show-sdk-version'],'sdk-version.txt').decode().strip()
 opts=['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter','-Wframe-larger-than=4096','-fstack-usage','-MMD','-MF',out/'accelerator.d','-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers']
 run([*opts,'-c','RTXMetalAccelerator.cpp','-o',out/'accelerator.o'],'compile-accelerator.txt');closure('accelerator')
 usage=[dict(function=s.split('\t')[0],bytes=int(s.split('\t')[1]))for s in(out/'accelerator.su').read_text().splitlines()];assert usage and max(s['bytes']for s in usage)<=4096
 (out/'frames.json').write_text(json.dumps(usage,indent=2)+'\n')
 bundle=out/'RTXMetalAccelerator-0.253.0.kext';contents=bundle/'Contents';(contents/'MacOS').mkdir(parents=True)
 info=dict(CFBundleIdentifier='local.emre.RTXMetalAccelerator132',CFBundleName='RTXMetalAccelerator132',CFBundleExecutable='RTXMetalAccelerator',CFBundlePackageType='KEXT',CFBundleVersion='0.253.0',CFBundleShortVersionString='0.253.0',OSBundleLibraries={'com.apple.kpi.iokit':'8.0.0','com.apple.kpi.libkern':'8.0.0','com.apple.kpi.mach':'8.0.0','com.apple.kpi.unsupported':'8.0.0','com.apple.iokit.IOGraphicsFamily':'600'},IOKitPersonalities={'RTXMetalAccelerator':dict(CFBundleIdentifier='local.emre.RTXMetalAccelerator132',IOClass='RTXMetalAccelerator132',IOProviderClass='RTXProbe',IOMatchCategory='RTXMetalAccelerator',IOProbeScore=1000,IOPropertyMatch=dict(ProbeVersion='0.83.1',ProbeComplete=True,ProbePassed=True,TargetIdentity=0x252010de,TargetSubsystem=0x104c1043))})
 (contents/'Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True));binary=contents/'MacOS/RTXMetalAccelerator'
 run(['xcrun','clang++','-arch','x86_64','-nostdlib','-isysroot',sdk,'-mmacosx-version-min='+version,'-Wl,-kext','-Wl,-undefined,dynamic_lookup','-Wl,-no_fixup_chains',out/'accelerator.o','-lkmod','-lkmodc++','-lcc_kext','-o',binary],'link-accelerator.txt')
 run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',bundle],'sign.txt');run(['/usr/bin/codesign','--verify','--strict',bundle],'signature.txt')
 run(['xcrun','nm','-u',binary],'undefined.txt');run(['xcrun','nm','-gU',binary],'exports.txt');run(['xcrun','otool','-l',binary],'load-commands.txt')
 sources();snapshot('after');passed=True
except Exception as ex:error=repr(ex);traceback.print_exc()
finally:
 report=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,boot_uuid=boot,source_inputs=len(rows),kext_loaded=False,gpu_jobs=0,publication_executed=False)
 for name,value in [('verification.json',report),('commands.json',commands),('closures.json',closures)]: (out/name).write_text(json.dumps(value,indent=2)+'\n')
 artifacts=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=H(p))for p in sorted(out.rglob('*'))if p.is_file()]
 (out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
 with tarfile.open(out.parent/'child-review.tar.gz','x:gz')as tar:
  for p in sorted(out.rglob('*')):
   if p.is_file():tar.add(p,arcname='mac/'+p.relative_to(out).as_posix(),recursive=False)
 print(json.dumps(dict(passed=passed,error=error,archive_sha256=H(out.parent/'child-review.tar.gz'))))
raise SystemExit(0 if passed else 1)
