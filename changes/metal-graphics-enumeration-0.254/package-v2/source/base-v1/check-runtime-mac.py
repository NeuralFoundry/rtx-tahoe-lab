from pathlib import Path
import datetime,hashlib,json,os,plistlib,shlex,subprocess,tarfile
from verify_backing import verify
root=Path(__file__).resolve().parent;out=root.parent/'runtime-mac-v1';assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=json.loads((root/'source-manifest.json').read_bytes());manifest={r['path']:r for r in inputs};commands=[]
def source_check():
    for r in inputs:
        p=root/r['path'];assert p.resolve().is_relative_to(root) and not p.is_symlink() and p.stat().st_size==r['bytes'] and sha(p)==r['sha256']
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
def dependencies(name):
    words=shlex.split((out/name).read_text().replace('\\\n',' '));assert words[0].endswith(':');found={}
    for word in words[1:]:
        p=Path(word);p=p if p.is_absolute() else root/p;relative=p.resolve().relative_to(root).as_posix()
        assert relative in manifest and sha(p)==manifest[relative]['sha256'];found[relative]=manifest[relative]
    return [found[k] for k in sorted(found)]
passed=False;error=None;stacks=[]
try:
    source_check();snapshot('before')
    for mode,extra in [('normal',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
        run(['xcrun','clang++','-O2','-g','-std=c++17','-Wall','-Wextra','-Werror',*extra,'-MMD','-MF',str(out/(mode+'.d')),'runtime-tests.cpp','-o',str(out/('runtime-'+mode))],'compile-'+mode+'.log')
        raw=out/('raw-'+mode);raw.mkdir();run([str(out/('runtime-'+mode)),str(root),str(raw)],mode+'.json')
        r=json.loads((out/(mode+'.json')).read_bytes());assert r['passed'] and r==json.loads((root/'windows-reference.json').read_bytes())
        (out/('oracle-'+mode+'.json')).write_text(json.dumps(verify(root,raw),indent=2)+'\n')
    assert json.loads((out/'normal.json').read_bytes())==json.loads((out/'asan.json').read_bytes())
    for p in (out/'raw-normal').iterdir():assert p.read_bytes()==(out/'raw-asan'/p.name).read_bytes()
    sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();version=subprocess.check_output(['xcrun','--show-sdk-version'],text=True).strip()
    run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin',
         '-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,
         '-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wframe-larger-than=4096','-fstack-usage','-MMD','-MF',str(out/'kernel.d'),
         '-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers','-c','runtime-kernel-smoke.cpp','-o',str(out/'runtime-kernel.o')],'compile-kernel.log')
    for p in out.glob('*.su'):
        for line in p.read_text().splitlines():
            fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();stacks.append(int(fields[1]))
    assert stacks and max(stacks)<=4096
    closure={name:dependencies(name+'.d') for name in ('normal','asan','kernel')}
    (out/'compiled-inputs.json').write_text(json.dumps(closure,indent=2)+'\n')
    run(['xcrun','clang++','--version'],'compiler.txt');(out/'sdk.txt').write_text(version+'\n')
    snapshot('after');assert (out/'boot-before.txt').read_bytes()==(out/'boot-after.txt').read_bytes();source_check();passed=True
except Exception as ex:error=repr(ex)
finally:
    r=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,inputs=inputs,
           maximum_kernel_stack_frame=max(stacks) if stacks else None,native_mmio_adapter_implemented=True,native_reusable_dispatch_adapter_compiled=bool(stacks),kernel_template_compiled=bool(stacks),
           kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    (out/'verification.json').write_text(json.dumps(r,indent=2)+'\n');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    rows=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()]
    (out/'artifact-manifest.json').write_text(json.dumps(rows,indent=2)+'\n');archive=root.parent/'runtime-mac-v1.tar.gz';assert not archive.exists()
    with tarfile.open(archive,'w:gz') as tar:
        for p in sorted(out.rglob('*')):
            if p.is_file():tar.add(p,arcname='runtime-mac-v1/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(passed=passed,error=error,artifacts=len(rows),archive=str(archive),sha256=sha(archive))),flush=True)
raise SystemExit(0 if passed else 1)
