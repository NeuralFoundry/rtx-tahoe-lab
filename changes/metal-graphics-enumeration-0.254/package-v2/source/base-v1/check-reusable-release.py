"""Build and verify the 0.35 release on macOS without loading or opening it."""
from pathlib import Path
import datetime,hashlib,json,os,plistlib,shlex,struct,subprocess,tarfile
from verify_reusable_fixture import verify
root=Path(__file__).resolve().parent;out=root.parent/'release-v1';assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=json.loads((root/'reusable-release-inputs.json').read_bytes());manifest={r['path']:r for r in inputs};commands=[]
assert len(inputs)==len(manifest)

def source_check():
    for r in inputs:
        p=root/r['path'];assert p.resolve().is_relative_to(root) and not p.is_symlink() and p.stat().st_size==r['bytes'] and sha(p)==r['sha256'],r['path']

def run(argv,name,timeout=360):
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with (out/name).open('wb') as f:p=subprocess.run(argv,cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    commands.append(dict(argv=argv,log=name,returncode=p.returncode))
    if p.returncode:raise RuntimeError(name+': '+str(p.returncode))

def snapshot(phase):
    run(['/usr/sbin/sysctl','kern.bootsessionuuid','kern.boottime'],'boot-'+phase+'.txt')
    run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-'+phase+'.txt')
    run(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c','RTXProbe'],'rtx-'+phase+'.plist')
    b=(out/('rtx-'+phase+'.plist')).read_bytes();assert not (plistlib.loads(b) if b.strip() else [])
    b=(out/('loaded-'+phase+'.txt')).read_bytes();assert b'RTXProbe' not in b and b'AMDRadeon' in b
    assert b'8338067A-9B1D-427A-B82B-3EB7A79AF9D2' in (out/('boot-'+phase+'.txt')).read_bytes()

def dependencies(name):
    words=shlex.split((out/name).read_text().replace('\\\n',' '));assert words[0].endswith(':');found={}
    for word in words[1:]:
        p=Path(word);p=p if p.is_absolute() else root/p;relative=p.resolve().relative_to(root).as_posix()
        assert relative in manifest and sha(p)==manifest[relative]['sha256'];found[relative]=manifest[relative]
    return [found[k] for k in sorted(found)]

passed=False;error=None;stacks=[];report={}
try:
    source_check();snapshot('before')
    reference=json.loads((root/'reusable-windows-reference.json').read_bytes())
    for mode,extra in [('normal',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
        run(['xcrun','clang++','-O2','-g','-std=c++17','-Wall','-Wextra','-Werror',*extra,'-MMD','-MF',str(out/(mode+'.d')),'reusable-client-fixture.cpp','-o',str(out/('fixture-'+mode))],'compile-'+mode+'.log')
        raw=out/('raw-'+mode);raw.mkdir();run([str(out/('fixture-'+mode)),str(root),str(raw)],mode+'.json')
        assert json.loads((out/(mode+'.json')).read_bytes())==reference['native']
        for r in reference['raw']:assert (raw/r['path']).stat().st_size==r['bytes'] and sha(raw/r['path'])==r['sha256'],r['path']
        (out/('oracle-'+mode+'.json')).write_text(json.dumps(verify(root,raw),indent=2)+'\n')
    component='changes/gsp-program-library-0.33/'
    run(['xcrun','clang++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
         '-MMD','-MF',str(out/'baseline.d'),component+'client/test_fixture.cpp','-o',str(out/'baseline-fixture')],'compile-baseline.log')
    old=out/'baseline-raw';old.mkdir();evidence='changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/'
    run([str(out/'baseline-fixture'),evidence+'record-011.bin',evidence+'record-010.bin',component,str(old)],'baseline-native.json')
    (old/'native.json').write_bytes((out/'baseline-native.json').read_bytes())
    for p in (root/'program-msl-windows-v2').glob('*.bin'):assert p.read_bytes()==(old/p.name).read_bytes()
    assert json.loads((old/'native.json').read_bytes())==json.loads((root/'program-msl-windows-v2/native.json').read_bytes())
    run(['/usr/bin/python3','-B','run_reusable_tests.py',str(out/'python'),str(old),str(out/'raw-normal')],'python.log',timeout=420)
    tests=json.loads((out/'python/test-result.json').read_bytes());assert tests['passed'] and tests['tests']==651
    for r in reference['python']:
        p=out/'python'/r['path'];b=p.read_bytes().replace(b'\r\n',b'\n') if p.suffix=='.json' else p.read_bytes()
        assert len(b)==r['bytes'] and hashlib.sha256(b).hexdigest()==r['sha256'],r['path']
    sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();version=subprocess.check_output(['xcrun','--show-sdk-version'],text=True).strip()
    run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin',
         '-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,
         '-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter','-Wframe-larger-than=4096','-fstack-usage','-MMD','-MF',str(out/'kernel.d'),
         '-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers','-c','driver/GSPReusableProbe.cpp','-o',str(out/'GSPReusableProbe.o')],'compile-kernel.log')
    for p in out.glob('*.su'):
        for line in p.read_text().splitlines():
            fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();stacks.append(int(fields[1]))
    assert stacks and max(stacks)<=4096
    closure={name:dependencies(name+'.d') for name in ('normal','asan','baseline','kernel')}
    (out/'compiled-inputs.json').write_text(json.dumps(closure,indent=2)+'\n')
    bundle=out/'RTXProbe-0.35.0.kext';(bundle/'Contents/MacOS').mkdir(parents=True)
    info=plistlib.loads((root/'driver/Info.plist').read_bytes());info['CFBundleVersion']=info['CFBundleShortVersionString']='0.35.0'
    (bundle/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    binary=bundle/'Contents/MacOS/RTXProbe'
    run(['xcrun','clang++','-arch','x86_64','-nostdlib','-isysroot',sdk,'-mmacosx-version-min='+version,'-Wl,-kext','-Wl,-undefined,dynamic_lookup','-Wl,-no_fixup_chains',
         str(out/'GSPReusableProbe.o'),'-lkmod','-lkmodc++','-lcc_kext','-o',str(binary)],'link.log')
    run(['plutil','-lint',str(bundle/'Contents/Info.plist')],'plist.log')
    run(['codesign','--force','--sign','-','--timestamp=none',str(bundle)],'sign.log')
    run(['codesign','--verify','--verbose=2',str(bundle)],'signature-verify.log')
    run(['codesign','--display','--verbose=4',str(bundle)],'signature-display.log')
    header=struct.unpack_from('<4I',binary.read_bytes());assert header[0]==0xfeedfacf and header[1]==0x1000007 and header[3]==11
    assert b'0.35.0' in binary.read_bytes()
    report=dict(passed=True,python_tests=651,native_checks=reference['native']['checks'],native_raw_files=459,simulated_jobs=65,ring_wraps=2,
                compiled_input_counts={k:len(v) for k,v in closure.items()},maximum_kernel_stack_frame=max(stacks),
                binary_sha256=sha(binary),plist_sha256=sha(bundle/'Contents/Info.plist'),sdk_version=version,
                cpu_simulation_only=True,kext_built=True,kext_loaded=False,kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    run(['xcrun','clang++','--version'],'compiler.txt');(out/'sdk.txt').write_text(version+'\n')
    snapshot('after');assert (out/'boot-before.txt').read_bytes()==(out/'boot-after.txt').read_bytes();source_check();passed=True
except Exception as ex:error=repr(ex)
finally:
    (out/'release-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    r=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,inputs=inputs,
           kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    (out/'verification.json').write_text(json.dumps(r,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    rows=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()]
    (out/'artifact-manifest.json').write_text(json.dumps(rows,indent=2)+'\n');archive=root.parent/'reusable-release-v1.tar.gz';assert not archive.exists()
    with tarfile.open(archive,'w:gz') as tar:
        for p in sorted(out.rglob('*')):
            if p.is_file():tar.add(p,arcname='release-v1/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(passed=passed,error=error,artifacts=len(rows),archive=str(archive),sha256=sha(archive))),flush=True)
raise SystemExit(0 if passed else 1)
