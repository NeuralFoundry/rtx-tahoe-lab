"""Build actual IOKit graphics driver. Never load, open a device or reboot."""
from pathlib import Path
import hashlib,json,os,plistlib,shlex,subprocess,sys,traceback
s=Path(__file__).resolve().parent;out=s.parent/'kernel-build';out.mkdir();commands=[];H=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def run(argv,name,env=None):
 argv=list(map(str,argv))
 with (out/(name+'.stdout')).open('xb') as so,(out/(name+'.stderr')).open('xb') as se:
  p=subprocess.Popen(argv,cwd=s,stdin=subprocess.DEVNULL,stdout=so,stderr=se,env=env);row=dict(name=name,pid=p.pid,argv=argv,state='running');commands.append(row);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  try:rc=p.wait(timeout=180)
  except BaseException:p.kill();p.wait();raise
  row.update(state='terminal',returncode=rc);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 assert rc==0,(name,rc,(out/(name+'.stderr')).read_text()[-6000:]);return (out/(name+'.stdout')).read_bytes()
report=dict(passed=False,kext_loaded=False,gpu_executed=False)
try:
 assert sys.platform=='darwin' and os.geteuid()==501
 for row in json.loads((s/'source-manifest247.json').read_bytes()):assert (s/row['path']).stat().st_size==row['bytes'] and H(s/row['path'])==row['sha256']
 boot=run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-before').decode().strip();assert boot==sys.argv[1]
 before=run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-before');assert b'AMDRadeon' in before
 clang=run(['/usr/bin/xcrun','--find','clang++'],'clang').decode().strip();run([clang,'--version'],'compiler');sdk=run(['/usr/bin/xcrun','--show-sdk-path'],'sdk').decode().strip();version=run(['/usr/bin/xcrun','--show-sdk-version'],'sdk-version').decode().strip()
 tests={}
 flags=['-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-DRTX_GRAPHICS242','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mgeneral-regs-only','-mno-red-zone','-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter','-Wframe-larger-than=4096','-fstack-usage','-MMD','-MF',out/'kernel.d','-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers']
 run([clang,*flags,'-c',s/'native-source-v1/probe/kernel/driver/GSPLibraryUploadProbe.cpp','-o',out/'kernel.o'],'kernel-compile')
 frames=[]
 for line in (out/'kernel.su').read_text().splitlines():
  function,size,kind=line.split('\t');assert kind=='static' and int(size)<=4096;frames.append(dict(function=function,bytes=int(size)))
 closure=sorted(set((s/n).resolve() for n in shlex.split((out/'kernel.d').read_text().replace('\\\n',' '))[1:]))
 rows=[dict(path=p.relative_to(s).as_posix(),sha256=H(p)) for p in closure];(out/'kernel-closure.json').write_text(json.dumps(rows,indent=2)+'\n')
 for wanted in ('gpu241/GraphicsSubmit241.hpp','gpu242/GraphicsABI242.hpp','gpu242/GraphicsLayout242.hpp','root195/MacOwnedRoot195.hpp'):assert any(x['path'].endswith(wanted) for x in rows),wanted
 source=(s/'native-source-v1/probe/kernel/driver/GSPLibraryUploadProbe.cpp').read_text();assert 'setProperty("ProbeVersion", "0.83.1")'in source and 'KMOD_EXPLICIT_DECL(local.emre.RTXProbe, "0.83.1"'in source
 info=plistlib.loads((s/'native-source-v1/probe/kernel/driver/Info.plist').read_bytes());assert info['CFBundleVersion']==info['CFBundleShortVersionString']=='0.83.1'
 bundle=out/'RTXProbe-0.83.1.kext';(bundle/'Contents/MacOS').mkdir(parents=True);(bundle/'Contents/Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True));binary=bundle/'Contents/MacOS/RTXProbe'
 run([clang,'-arch','x86_64','-nostdlib','-isysroot',sdk,'-mmacosx-version-min='+version,'-Wl,-kext','-Wl,-undefined,dynamic_lookup','-Wl,-no_fixup_chains',out/'kernel.o','-lkmod','-lkmodc++','-lcc_kext','-o',binary],'kernel-link')
 run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',bundle],'sign');run(['/usr/bin/codesign','--verify','--strict',bundle],'verify')
 for name,args in [('undefined',['nm','-u']),('symbols',['nm']),('loads',['otool','-l'])]:run(['/usr/bin/xcrun',*args,binary],name)
 symbols=(out/'symbols.stdout').read_text();assert 'graphicsMethod' in symbols and 'graphicsCall' in symbols and 'RTXGraphicsSubmit241' in symbols
 assert run(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],'boot-after').decode().strip()==boot
 after=run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-after');assert [x for x in before.splitlines() if b'local.emre.RTX' in x]==[x for x in after.splitlines() if b'local.emre.RTX' in x]
 report.update(passed=True,tests=tests,boot_uuid=boot,source_manifest_sha256=H(s/'source-manifest247.json'),binary_sha256=H(binary),binary_bytes=binary.stat().st_size,frames=frames,max_frame=max(x['bytes'] for x in frames),linked=True,signed=True,graphics_selector_and_mac_adapter_linked=True,graphics_allocations_compiled=True,closure_files=len(rows),loaded_rtx_unchanged=True)
except BaseException:
 (out/'error.txt').write_text(traceback.format_exc());raise
finally:(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='frames'},indent=2))
