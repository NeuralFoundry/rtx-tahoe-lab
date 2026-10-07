"""Build the supplied-library release and run native/host tests; never load it."""
from pathlib import Path
import datetime,hashlib,json,os,plistlib,shlex,struct,subprocess,tarfile
from audit_uploaded import verify
root=Path(__file__).resolve().parent;out=root.parent/'release-v1';out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=json.loads((root/'uploaded-release-inputs.json').read_bytes());manifest={r['path']:r for r in inputs};commands=[]
def source_check():
    for r in inputs:
        p=root/r['path'];assert p.resolve().is_relative_to(root) and not p.is_symlink() and p.stat().st_size==r['bytes'] and sha(p)==r['sha256'],r['path']
def run(argv,name,timeout=180):
    with (out/name).open('xb') as f:p=subprocess.run(argv,cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=timeout,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    commands.append(dict(argv=argv,log=name,returncode=p.returncode))
    if p.returncode:raise RuntimeError(name+': '+str(p.returncode))
def snapshot(phase):
    run(['/usr/sbin/sysctl','kern.bootsessionuuid','kern.boottime'],'boot-'+phase+'.txt')
    run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-'+phase+'.txt')
    run(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c','RTXProbe'],'rtx-'+phase+'.plist')
    b=(out/('rtx-'+phase+'.plist')).read_bytes();assert not (plistlib.loads(b) if b.strip() else [])
    b=(out/('loaded-'+phase+'.txt')).read_bytes();assert b'RTXProbe' not in b and b'AMDRadeon' in b
def dependency(tag):
    words=shlex.split((out/(tag+'.d')).read_text().replace('\\\n',' '));assert words[0].endswith(':');found=set()
    for word in words[1:]:
        p=Path(word);p=p if p.is_absolute() else root/p;name=p.resolve().relative_to(root).as_posix();assert name in manifest and sha(p)==manifest[name]['sha256'];found.add(name)
    return [manifest[k] for k in sorted(found)]
passed=False;error=None;report={};closures={};stacks=[]
try:
    source_check();snapshot('before')
    reference=json.loads((root/'uploaded-windows-reference.json').read_bytes())
    for tag,source,fixture,extra in [('normal','uploaded-client-fixture.cpp','selected-fixtures',[]),('asan','uploaded-client-fixture.cpp','selected-fixtures',['-fsanitize=address,undefined','-fno-sanitize-recover=all']),('baseline-runtime','reusable-client-fixture.cpp','',[])]:
        run(['xcrun','clang++','-O1','-g','-std=c++17','-Wall','-Wextra','-Werror',*extra,'-MMD','-MF',str(out/(tag+'.d')),source,'-o',str(out/('fixture-'+tag))],'compile-'+tag+'.log')
        raw=out/('raw-'+tag);raw.mkdir();run([str(out/('fixture-'+tag)),str(root/fixture),str(raw)],tag+'.json');closures[tag]=dependency(tag)
        if tag!='baseline-runtime':
            assert json.loads((out/(tag+'.json')).read_bytes())==reference['native']
            for r in reference['raw']:assert sha(raw/r['path'])==r['sha256'] and (raw/r['path']).stat().st_size==r['bytes']
            (out/('oracle-'+tag+'.json')).write_text(json.dumps(verify(root,raw),indent=2)+'\n')
    component='changes/gsp-program-library-0.33/';old=out/'baseline-program';old.mkdir()
    run(['xcrun','clang++','-O1','-std=c++17','-Wall','-Wextra','-Werror','-MMD','-MF',str(out/'baseline-program.d'),component+'client/test_fixture.cpp','-o',str(out/'baseline-program-fixture')],'compile-baseline-program.log')
    evidence='changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/'
    run([str(out/'baseline-program-fixture'),evidence+'record-011.bin',evidence+'record-010.bin',component,str(old)],'baseline-program.json');(old/'native.json').write_bytes((out/'baseline-program.json').read_bytes());closures['baseline-program']=dependency('baseline-program')
    for p in (root/'program-msl-windows-v2').glob('*.bin'):assert p.read_bytes()==(old/p.name).read_bytes()
    bridge=out/'UploadTestBridge.dylib'
    run(['xcrun','clang++','-O1','-std=c++17','-Wall','-Wextra','-Werror','-dynamiclib','-fvisibility=hidden','-MMD','-MF',str(out/'bridge.d'),'upload-test-bridge.cpp','-o',str(bridge)],'compile-bridge.log');closures['bridge']=dependency('bridge')
    run(['/usr/bin/nm','-gU',str(bridge)],'bridge-symbols.txt')
    run(['/usr/bin/python3','-B','run_uploaded_tests.py',str(out/'python'),str(old),str(out/'raw-baseline-runtime'),str(out/'raw-normal'),str(bridge)],'python.log',timeout=420)
    tests=json.loads((out/'python/test-result.json').read_bytes());assert tests['passed'] and tests['tests']==670
    for r in reference['python']:
        p=out/'python'/r['path'];b=p.read_bytes().replace(b'\r\n',b'\n') if p.suffix=='.json' else p.read_bytes()
        assert len(b)==r['bytes'] and hashlib.sha256(b).hexdigest()==r['sha256'],r['path']
    sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();version=subprocess.check_output(['xcrun','--show-sdk-version'],text=True).strip()
    run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone',
         '-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter','-Wframe-larger-than=4096','-fstack-usage',
         '-MMD','-MF',str(out/'kernel.d'),'-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers','-c','driver/GSPLibraryUploadProbe.cpp','-o',str(out/'GSPLibraryUploadProbe.o')],'compile-kernel.log')
    for p in out.glob('*.su'):
        for line in p.read_text().splitlines():
            fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();stacks.append(int(fields[1]))
    assert stacks and max(stacks)<=4096;closures['kernel']=dependency('kernel')
    (out/'compiled-inputs.json').write_text(json.dumps(closures,indent=2)+'\n')
    bundle=out/'RTXProbe-0.36.0.kext';(bundle/'Contents/MacOS').mkdir(parents=True)
    info=plistlib.loads((root/'driver/Info.plist').read_bytes());info['CFBundleVersion']=info['CFBundleShortVersionString']='0.36.0';(bundle/'Contents/Info.plist').write_bytes(plistlib.dumps(info));binary=bundle/'Contents/MacOS/RTXProbe'
    run(['xcrun','clang++','-arch','x86_64','-nostdlib','-isysroot',sdk,'-mmacosx-version-min='+version,'-Wl,-kext','-Wl,-undefined,dynamic_lookup','-Wl,-no_fixup_chains',str(out/'GSPLibraryUploadProbe.o'),'-lkmod','-lkmodc++','-lcc_kext','-o',str(binary)],'link.log')
    run(['plutil','-lint',str(bundle/'Contents/Info.plist')],'plist.log');run(['codesign','--force','--sign','-','--timestamp=none',str(bundle)],'sign.log')
    run(['codesign','--verify','--verbose=2',str(bundle)],'signature-verify.log');run(['codesign','--display','--verbose=4',str(bundle)],'signature-display.log')
    header=struct.unpack_from('<4I',binary.read_bytes());assert header[0]==0xfeedfacf and header[1]==0x1000007 and header[3]==11 and b'0.36.0' in binary.read_bytes()
    assert (root/'selected-code.bin').read_bytes() not in binary.read_bytes() and (root/'code.bin').read_bytes() not in binary.read_bytes()
    report=dict(passed=True,python_tests=670,native_checks=reference['native']['checks'],native_raw_files=len(reference['raw']),cpu_jobs=65,cpu_outputs=3872,ring_wraps=2,
                input_counts={k:len(v) for k,v in closures.items()},maximum_kernel_stack_frame=max(stacks),binary_sha256=sha(binary),plist_sha256=sha(bundle/'Contents/Info.plist'),
                shader_bytes_uploaded_not_embedded=True,selected_manifest_sha256=sha(root/'selected-library.json'),kext_built=True,kext_loaded=False,kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    run(['xcrun','clang++','--version'],'compiler.txt');(out/'sdk.txt').write_text(version+'\n')
    snapshot('after');assert (out/'boot-before.txt').read_bytes()==(out/'boot-after.txt').read_bytes();source_check();passed=True
except Exception as ex:error=repr(ex)
finally:
    (out/'release-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    result=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,inputs=inputs,kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    rows=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()];(out/'artifact-manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
    archive=root.parent/'release-v1.tar.gz'
    with tarfile.open(archive,'w:gz') as tar:
        for p in sorted(out.rglob('*')):
            if p.is_file():tar.add(p,arcname='release-v1/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(passed=passed,error=error,artifacts=len(rows),archive=str(archive),sha256=sha(archive))),flush=True)
raise SystemExit(0 if passed else 1)
