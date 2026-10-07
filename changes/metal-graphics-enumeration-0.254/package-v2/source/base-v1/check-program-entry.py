"""Build/sanitize the program ABI and compile the complete driver entry, no IOKit open."""
from pathlib import Path
import datetime,hashlib,json,os,shlex,subprocess,tarfile
from verify_program_entry import RAW,verify_directory

root=Path(__file__).resolve().parent
out=root/'mac-program-entry';assert not out.exists();out.mkdir()
rows=json.loads((root/'program-entry-inputs.json').read_text())
manifest={r['path']:r for r in rows};assert len(manifest)==len(rows)


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def sources():
    for r in rows:
        p=root/r['path']
        assert p.resolve().is_relative_to(root.resolve()) and not p.is_symlink()
        assert p.stat().st_size==r['bytes'] and sha(p)==r['sha256'],r['path']


def run(argv,name,timeout=120):
    with (out/name).open('w') as f:
        p=subprocess.run(argv,cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=timeout,
                         env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
    if p.returncode:raise RuntimeError(name+': '+str(p.returncode))


def dependencies(name):
    words=shlex.split((out/name).read_text().replace('\\\n',' '));assert words[0].endswith(':')
    result={}
    for word in words[1:]:
        p=Path(word);p=p if p.is_absolute() else root/p
        relative=p.resolve().relative_to(root.resolve()).as_posix()
        assert relative in manifest,relative
        row=manifest[relative];assert sha(p)==row['sha256']
        result[relative]=row
    assert result
    return list(sorted(result.values(),key=lambda r:r['path']))


passed=False;error=None
try:
    sources()
    run(['sysctl','kern.bootsessionuuid','kern.boottime'],'boot-before.txt')
    run(['kmutil','showloaded','--list-only'],'loaded-before.txt')
    component='changes/gsp-program-library-0.33/'
    run(['xcrun','clang++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
         '-fno-sanitize-recover=all','-MMD','-MF',str(out/'cpu.d'),component+'entry/test_entry.cpp','-o',str(out/'entry')],'compile.log')
    evidence='changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/'
    run([str(out/'entry'),evidence+'record-011.bin',evidence+'record-010.bin',component,str(out)],'native.json')
    sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    version=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-version'],text=True).strip()
    run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin',
         '-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,
         '-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter','-Wframe-larger-than=4096','-fstack-usage',
         '-MMD','-MF',str(out/'kernel.d'),'-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers',
         '-c','driver/GSPProgramProbe.cpp','-o',str(out/'program-entry-kernel.o')],'kernel-compile.log')
    stacks=[]
    for p in out.glob('*.su'):
        for line in p.read_text().splitlines():
            fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();stacks.append(int(fields[1]))
    assert stacks and max(stacks)<=4096
    closure=dict(cpu=dependencies('cpu.d'),kernel=dependencies('kernel.d'))
    (out/'compiled-inputs.json').write_text(json.dumps(closure,indent=2)+'\n')
    report=verify_directory(root,out)
    for name in RAW:assert (out/name).read_bytes()==(root/'program-entry-windows-v2'/name).read_bytes(),name
    assert json.loads((out/'native.json').read_text())==json.loads((root/'program-entry-windows-v2/native.json').read_text())
    report.update(maximum_kernel_stack_frame=max(stacks),sdk_version=version,
                  compiled_cpu_inputs=len(closure['cpu']),compiled_kernel_inputs=len(closure['kernel']))
    (out/'entry-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    run(['sysctl','kern.bootsessionuuid','kern.boottime'],'boot-after.txt')
    run(['kmutil','showloaded','--list-only'],'loaded-after.txt')
    assert (out/'boot-before.txt').read_bytes()==(out/'boot-after.txt').read_bytes()
    for name in ('loaded-before.txt','loaded-after.txt'):
        text=(out/name).read_text();assert 'RTXProbe' not in text and 'AMDRadeon' in text
    run(['xcrun','clang++','--version'],'compiler.txt')
    sources();passed=True
except Exception as ex:error=repr(ex)
finally:
    result=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,
                source_inputs=len(rows),native_adapter_compiled=passed,driver_entry_integrated=passed,kext_built=False,
                kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    artifacts=[dict(path=p.name,bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.iterdir()) if p.is_file()]
    (out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
    archive=root.parent/'program-entry-cpu-v1.tar.gz';assert not archive.exists()
    with tarfile.open(archive,'w:gz') as t:
        for p in sorted(out.iterdir()):
            if p.is_file():t.add(p,arcname='mac-program-entry/'+p.name,recursive=False)
    print(json.dumps(dict(passed=passed,error=error,artifacts=len(artifacts),archive=str(archive),sha256=sha(archive))))
raise SystemExit(0 if passed else 1)
