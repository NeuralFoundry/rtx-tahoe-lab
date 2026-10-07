from pathlib import Path
import datetime,hashlib,json,os,subprocess,tarfile
from verify_profile import verify
root=Path(__file__).resolve().parent;out=root/'mac';assert not out.exists();out.mkdir()
inputs=json.loads((root/'source-manifest.json').read_text())


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def sources():
    for r in inputs:
        p=root/r['path'];assert p.resolve().is_relative_to(root.resolve()) and not p.is_symlink()
        assert p.stat().st_size==r['bytes'] and sha(p)==r['sha256']


def run(argv,name):
    with (out/name).open('w') as f:
        p=subprocess.run(argv,cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=90,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
    if p.returncode:raise RuntimeError(name+': '+str(p.returncode))


passed=False;error=None
try:
    sources()
    run(['xcrun','clang++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
         '-fno-sanitize-recover=all','test_program.cpp','-o',str(out/'program')],'compile.log')
    run([str(out/'program'),'fixtures',str(out)],'native.json')
    sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    version=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-version'],text=True).strip()
    run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin',
         '-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,
         '-Wall','-Wextra','-Werror','-Wframe-larger-than=4096','-fstack-usage','-isysroot',sdk,
         '-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers','-c','kernel-smoke.cpp','-o',str(out/'program-kernel.o')],'kernel-compile.log')
    stacks=[]
    for p in out.glob('*.su'):
        for line in p.read_text().splitlines():
            fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();stacks.append(int(fields[1]))
    assert stacks and max(stacks)<=4096
    report=verify(root,out);report.update(maximum_kernel_stack_frame=max(stacks),sdk_version=version)
    (out/'profile-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    run(['sysctl','kern.bootsessionuuid','kern.boottime'],'boot.txt')
    run(['kmutil','showloaded','--list-only'],'loaded.txt')
    run(['xcrun','clang++','--version'],'compiler.txt')
    sources();passed=True
except Exception as ex:error=repr(ex)
finally:
    r=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,
        source_inputs=len(inputs),kernel_open_attempted=False,gpu_commands_submitted=False,metal_verified=False)
    (out/'verification.json').write_text(json.dumps(r,indent=2)+'\n')
    artifacts=[dict(path=p.name,bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.iterdir()) if p.is_file()]
    (out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
    archive=root.parent/'program-cpu-v2.tar.gz';assert not archive.exists()
    with tarfile.open(archive,'w:gz') as t:
        for p in sorted(out.iterdir()):
            if p.is_file():t.add(p,arcname='mac/'+p.name,recursive=False)
    print(json.dumps(dict(passed=passed,error=error,artifacts=len(artifacts),archive=str(archive),sha256=sha(archive))))
raise SystemExit(0 if passed else 1)
