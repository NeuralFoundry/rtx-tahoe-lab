"""CPU checks and full0.80 Kernel SDK build; no driver loading or GPU access."""
from pathlib import Path
import hashlib,json,os,plistlib,subprocess,sys,traceback
s=Path(__file__).resolve().parent;r=s.parent;out=r/'build';out.mkdir()
rows=[]
def save(p,v):
 with p.open('x')as f:json.dump(v,f,indent=2);f.write('\n')
def run(argv,name,env=None):
 with(out/(name+'.stdout')).open('xb')as stdout,(out/(name+'.stderr')).open('xb')as stderr:
  p=subprocess.Popen(argv,cwd=s,env=env,stdin=subprocess.DEVNULL,stdout=stdout,stderr=stderr)
  try:rc=p.wait(timeout=240)
  except BaseException:p.kill();p.wait();raise
 rows.append(dict(name=name,argv=argv,pid=p.pid,returncode=rc))
 assert rc==0,(name,rc)
 return (out/(name+'.stdout')).read_bytes()
try:
 assert sys.platform=='darwin'and os.geteuid()==501
 boot=subprocess.check_output(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],text=True).strip();assert boot==sys.argv[1]
 raw=(s/'source-manifest.json').read_bytes()
 for row in json.loads(raw):
  p=s/row['path'];b=p.read_bytes();assert not p.is_symlink()and len(b)==row['bytes']and hashlib.sha256(b).hexdigest()==row['sha256']
 run(['/usr/bin/clang++','--version'],'compiler')
 # The eight v1 CPU suites passed. Their actual dependency closures are
 # verified unchanged independently; v2 repeats only the affected kernel build.
 sdk=run(['/usr/bin/xcrun','--show-sdk-path'],'sdk').decode().strip()
 version=run(['/usr/bin/xcrun','--show-sdk-version'],'sdk-version').decode().strip()
 clang=run(['/usr/bin/xcrun','--find','clang++'],'clang-path').decode().strip()
 flags=['-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter','-Wframe-larger-than=4096','-fstack-usage','-MMD','-MF',str(out/'kernel.d'),'-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers']
 run([clang,*flags,'-c',str(s/'probe/kernel/driver/GSPLibraryUploadProbe.cpp'),'-o',str(out/'kernel.o')],'kernel-compile')
 frames=[]
 for line in(out/'kernel.su').read_text().splitlines():
  function,size,kind=line.split('\t');assert kind=='static'and int(size)<=4096;frames.append(dict(function=function,bytes=int(size)))
 info=plistlib.loads((s/'probe/kernel/driver/Info.plist').read_bytes());assert info['CFBundleVersion']==info['CFBundleShortVersionString']=='0.80.0'
 kext=out/'RTXProbe-0.80.0.kext';(kext/'Contents/MacOS').mkdir(parents=True);(kext/'Contents/Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True));binary=kext/'Contents/MacOS/RTXProbe'
 run([clang,'-arch','x86_64','-nostdlib','-isysroot',sdk,'-mmacosx-version-min='+version,'-Wl,-kext','-Wl,-undefined,dynamic_lookup','-Wl,-no_fixup_chains',str(out/'kernel.o'),'-lkmod','-lkmodc++','-lcc_kext','-o',str(binary)],'kernel-link')
 run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',str(kext)],'kernel-sign')
 run(['/usr/bin/codesign','--verify','--strict',str(kext)],'kernel-verify')
 for name,args in [('undefined',['nm','-u']),('exports',['nm','-gU']),('loadcommands',['otool','-l'])]:run(['/usr/bin/xcrun',*args,str(binary)],name)
 save(out/'kernel-result.json',dict(passed=True,kext_version='0.80.0',binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),binary_bytes=binary.stat().st_size,sdk=sdk,sdk_version=version,clang_path=clang,clang_sha256=hashlib.sha256(Path(clang).read_bytes()).hexdigest(),frames=frames,max_frame=max(x['bytes']for x in frames),linked=True,signed=True,loaded=False,gpu_executed=False))
 assert subprocess.check_output(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],text=True).strip()==boot
 save(out/'result.json',dict(passed=True,source_manifest_sha256=hashlib.sha256(raw).hexdigest(),commands=len(rows),gpu_executed=False))
finally:save(out/'commands.json',rows)
