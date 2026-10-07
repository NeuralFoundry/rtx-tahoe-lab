from pathlib import Path
import datetime,hashlib,json,os,plistlib,shlex,subprocess,sys,tarfile,unittest
from audit_upload import verify,info
import library_upload
root=Path(__file__).resolve().parent;out=root.parent/'cpu-v1';out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=json.loads((root/'source-manifest.json').read_bytes());manifest={r['path']:r for r in inputs};commands=[]
def source_check():
    for r in inputs:
        p=root/r['path'];assert p.resolve().is_relative_to(root) and not p.is_symlink() and p.stat().st_size==r['bytes'] and sha(p)==r['sha256']
def run(argv,name,timeout=120):
    with (out/name).open('xb') as f:p=subprocess.run(argv,cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=timeout,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    commands.append(dict(argv=argv,log=name,returncode=p.returncode))
    if p.returncode:raise RuntimeError(name+': '+str(p.returncode))
def snapshot(phase):
    run(['/usr/sbin/sysctl','kern.bootsessionuuid','kern.boottime'],'boot-'+phase+'.txt')
    run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-'+phase+'.txt')
    run(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c','RTXProbe'],'rtx-'+phase+'.plist')
    b=(out/('rtx-'+phase+'.plist')).read_bytes();assert not (plistlib.loads(b) if b.strip() else [])
    b=(out/('loaded-'+phase+'.txt')).read_bytes();assert b'RTXProbe' not in b and b'AMDRadeon' in b
def closure(name):
    words=shlex.split((out/(name+'.d')).read_text().replace('\\\n',' '));assert words[0].endswith(':');found=set()
    for word in words[1:]:
        p=Path(word);p=p if p.is_absolute() else root/p
        key=p.resolve().relative_to(root).as_posix();assert key in manifest and sha(p)==manifest[key]['sha256'];found.add(key)
    return [manifest[k] for k in sorted(found)]
passed=False;error=None;stacks={};closures={};tests={}
try:
    source_check();snapshot('before')
    for name,source in (('upload','upload-tests.cpp'),('entry','upload-entry-tests.cpp')):
        for mode,extra in (('normal',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])):
            tag=name+'-'+mode;exe=out/tag;raw=out/('raw-'+tag);raw.mkdir()
            run(['xcrun','clang++','-O1','-g','-std=c++17','-Wall','-Wextra','-Werror',*extra,'-MMD','-MF',str(out/(tag+'.d')),source,'-o',str(exe)],'compile-'+tag+'.log')
            run([str(exe),str(root),str(raw)],tag+'.json');tests[tag]=json.loads((out/(tag+'.json')).read_bytes());assert tests[tag]['passed']
            if name=='upload':
                r=verify(root,raw,(root/'fixtures/baseline-qmd.bin').read_bytes())
                (out/('oracle-'+tag+'.json')).write_text(json.dumps(r,indent=2)+'\n')
                for p in raw.glob('*info.bin'):library_upload.decode_info(p.read_bytes())
            else:info((raw/'entry-info.bin').read_bytes(),3,payload=(root/'library.bin').read_bytes()+(root/'code.bin').read_bytes())
            closures[tag]=closure(tag)
        assert tests[name+'-normal']==tests[name+'-asan']
        left=out/('raw-'+name+'-normal');right=out/('raw-'+name+'-asan')
        assert {p.name for p in left.iterdir()}=={p.name for p in right.iterdir()}
        for p in left.iterdir():assert p.read_bytes()==(right/p.name).read_bytes()
    suite=unittest.defaultTestLoader.discover(str(root),pattern='test_codec.py')
    with (out/'python-tests.log').open('w') as log:r=unittest.TextTestRunner(stream=log,verbosity=2).run(suite)
    pr=dict(passed=r.wasSuccessful(),tests=r.testsRun,errors=len(r.errors),failures=len(r.failures),skipped=len(r.skipped))
    (out/'python-tests.json').write_text(json.dumps(pr,indent=2)+'\n');assert pr['passed'] and pr['tests']==6
    sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();version=subprocess.check_output(['xcrun','--show-sdk-version'],text=True).strip()
    for tag,source in (('kernel-smoke','kernel-smoke.cpp'),('kernel-entry','driver/GSPLibraryUploadProbe.cpp')):
        run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin','-fno-exceptions','-fno-rtti',
             '-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter',
             '-Wframe-larger-than=4096','-fstack-usage','-MMD','-MF',str(out/(tag+'.d')),'-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers',
             '-c',source,'-o',str(out/(tag+'.o'))],'compile-'+tag+'.log')
        su=out/(tag+'.su');frames=[]
        for line in su.read_text().splitlines():
            fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();frames.append(int(fields[1]))
        assert frames and max(frames)<=4096;stacks[tag]=max(frames);closures[tag]=closure(tag)
    (out/'compiled-inputs.json').write_text(json.dumps(closures,indent=2)+'\n')
    run(['xcrun','clang++','--version'],'compiler.txt');(out/'sdk.txt').write_text(version+'\n')
    snapshot('after');assert (out/'boot-before.txt').read_bytes()==(out/'boot-after.txt').read_bytes();source_check();passed=True
except Exception as ex:error=repr(ex)
finally:
    result=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,inputs=inputs,
        maximum_kernel_frames=stacks,tests=tests,driver_entry_compiled='kernel-entry' in stacks,kext_linked=False,kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    rows=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()]
    (out/'artifact-manifest.json').write_text(json.dumps(rows,indent=2)+'\n');archive=root.parent/'cpu-v1.tar.gz'
    with tarfile.open(archive,'w:gz') as tar:
        for p in sorted(out.rglob('*')):
            if p.is_file():tar.add(p,arcname='cpu-v1/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(passed=passed,error=error,artifacts=len(rows),archive=str(archive),sha256=sha(archive))),flush=True)
raise SystemExit(0 if passed else 1)
