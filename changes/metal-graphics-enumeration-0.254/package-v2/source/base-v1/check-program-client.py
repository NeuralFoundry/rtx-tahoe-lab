"""Validate 0.33 client/entry, link and sign its KEXT. Never load or open it."""
from pathlib import Path
import datetime,hashlib,json,os,plistlib,shlex,struct,subprocess,tarfile
root=Path(__file__).resolve().parent
out=root/'mac-program-client';assert not out.exists();out.mkdir()
rows=json.loads((root/'program-client-inputs.json').read_text());manifest={r['path']:r for r in rows};assert len(rows)==len(manifest)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def sources():
    for r in rows:
        p=root/r['path'];assert p.resolve().is_relative_to(root.resolve()) and not p.is_symlink()
        assert p.stat().st_size==r['bytes'] and sha(p)==r['sha256'],r['path']
def run(argv,name,timeout=180,env=None):
    with (out/name).open('w') as log:
        p=subprocess.run(argv,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=timeout,
                         env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',**(env or {})))
    if p.returncode:raise RuntimeError(name+': '+str(p.returncode))
def dependencies(name):
    words=shlex.split((out/name).read_text().replace('\\\n',' '));assert words[0].endswith(':');result={}
    for word in words[1:]:
        p=Path(word);p=p if p.is_absolute() else root/p;name=p.resolve().relative_to(root.resolve()).as_posix()
        assert name in manifest and sha(p)==manifest[name]['sha256'];result[name]=manifest[name]
    return [result[k] for k in sorted(result)]
passed=False;error=None
try:
    sources();run(['sysctl','kern.bootsessionuuid','kern.boottime'],'boot-before.txt');run(['kmutil','showloaded','--list-only'],'loaded-before.txt')
    component='changes/gsp-program-library-0.33/'
    run(['xcrun','clang++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
         '-MMD','-MF',str(out/'cpu.d'),component+'client/test_fixture.cpp','-o',str(out/'fixture')],'cpu-compile.log')
    native=out/'native';native.mkdir();evidence='changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/'
    run([str(out/'fixture'),evidence+'record-011.bin',evidence+'record-010.bin',component,str(native)],'native.log')
    (native/'native.json').write_bytes((out/'native.log').read_bytes())
    expected=root/'program-client-windows-v1'
    raw=sorted(p.name for p in expected.glob('*.bin'));assert len(raw)==44
    for name in raw:assert (native/name).read_bytes()==(expected/name).read_bytes(),name
    assert json.loads((native/'native.json').read_text())==json.loads((expected/'native.json').read_text())
    run(['/usr/bin/python3','run_program_tests.py',str(out/'python'),str(native)],'python.log',timeout=240)
    tests=json.loads((out/'python/test-result.json').read_text());assert tests['passed'] and tests['tests']==639
    win=root/'program-client-python-windows-v2'
    windows=json.loads((win/'test-result.json').read_text());assert windows['passed'] and windows['tests']==tests['tests']
    rehearsal=[]
    for p in sorted((win/'rehearsal').rglob('*')):
        if p.is_file():
            name=p.relative_to(win);assert (out/'python'/name).read_bytes()==p.read_bytes(),str(name);rehearsal.append(name.as_posix())
    assert (out/'python/rehearsal-calls.json').read_bytes()==(win/'rehearsal-calls.json').read_bytes()
    sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    version=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-version'],text=True).strip()
    run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin','-fno-exceptions',
         '-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,
         '-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-deprecated-declarations','-Wframe-larger-than=4096','-fstack-usage',
         '-MMD','-MF',str(out/'kernel.d'),'-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers',
         '-c','driver/GSPProgramProbe.cpp','-o',str(out/'GSPProgramProbe.o')],'kernel-compile.log')
    closure=dict(cpu=dependencies('cpu.d'),kernel=dependencies('kernel.d'))
    (out/'compiled-inputs.json').write_text(json.dumps(closure,indent=2)+'\n')
    stacks=[int(line.split('\t')[1]) for line in (out/'GSPProgramProbe.su').read_text().splitlines()];assert stacks and max(stacks)<=4096
    bundle=out/'RTXProbe-0.33.0.kext';(bundle/'Contents/MacOS').mkdir(parents=True)
    info=plistlib.loads((root/'driver/Info.plist').read_bytes());info['CFBundleVersion']=info['CFBundleShortVersionString']='0.33.0'
    (bundle/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    run(['xcrun','clang++','-arch','x86_64','-nostdlib','-isysroot',sdk,'-mmacosx-version-min='+version,'-Wl,-kext','-Wl,-undefined,dynamic_lookup','-Wl,-no_fixup_chains',
         str(out/'GSPProgramProbe.o'),'-lkmod','-lkmodc++','-lcc_kext','-o',str(bundle/'Contents/MacOS/RTXProbe')],'link.log')
    run(['plutil','-lint',str(bundle/'Contents/Info.plist')],'plist.log')
    run(['codesign','--force','--sign','-','--timestamp=none',str(bundle)],'sign.log')
    run(['codesign','--verify','--verbose=2',str(bundle)],'signature-verify.log')
    run(['codesign','--display','--verbose=4',str(bundle)],'signature-display.log')
    binary=(bundle/'Contents/MacOS/RTXProbe').read_bytes();header=struct.unpack_from('<4I',binary)
    assert header[0]==0xfeedfacf and header[1]==0x1000007 and header[3]==11
    assert b'0.33.0' in binary and b'gsp-program-runtime' in binary
    report=dict(passed=True,python_tests=tests['tests'],native_checks=24600,native_raw_files=44,rehearsal_files=len(rehearsal),
                compiled_cpu_inputs=len(closure['cpu']),compiled_kernel_inputs=len(closure['kernel']),maximum_kernel_stack_frame=max(stacks),
                binary_sha256=sha(bundle/'Contents/MacOS/RTXProbe'),plist_sha256=sha(bundle/'Contents/Info.plist'),sdk_version=version,
                cpu_simulation_only=True,kext_built=True,kext_loaded=False,kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    (out/'release-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    run(['sysctl','kern.bootsessionuuid','kern.boottime'],'boot-after.txt');run(['kmutil','showloaded','--list-only'],'loaded-after.txt')
    assert (out/'boot-before.txt').read_bytes()==(out/'boot-after.txt').read_bytes()
    for name in ('loaded-before.txt','loaded-after.txt'):
        text=(out/name).read_text();assert 'RTXProbe' not in text and 'AMDRadeon' in text
    run(['xcrun','clang++','--version'],'compiler.txt');sources();passed=True
except Exception as ex:error=repr(ex)
finally:
    result=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,source_inputs=len(rows),
                kext_loaded=False,kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    artifacts=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()]
    (out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
    archive=root.parent/'program-client-release-v1.tar.gz';assert not archive.exists()
    with tarfile.open(archive,'w:gz') as t:
        for p in sorted(out.rglob('*')):
            if p.is_file():t.add(p,arcname='mac-program-client/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(passed=passed,error=error,artifacts=len(artifacts),archive=str(archive),sha256=sha(archive))))
raise SystemExit(0 if passed else 1)
