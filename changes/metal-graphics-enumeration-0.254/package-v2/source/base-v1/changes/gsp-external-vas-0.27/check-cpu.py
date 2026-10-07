"""Build and compare the external-VA protocol on CPU; no hardware entrypoint."""
from pathlib import Path
import datetime,hashlib,json,re,subprocess,tarfile
ROOT=Path(__file__).resolve().parent
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args,name):
    with (OUT/(name+'.log')).open('w') as log:
        p=subprocess.run([str(a) for a in args],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
    if p.returncode:raise RuntimeError(name+' failed: '+(OUT/(name+'.log')).read_text()[-4000:])
inputs=json.loads((ROOT/'input-manifest.json').read_text(encoding='utf-8-sig'))
for f in inputs['files']:
    p=ROOT/f['path'];assert not p.is_symlink() and p.stat().st_size==f['bytes'] and digest(p)==f['sha256'],f['path']
for folder in ('reference','sdk-extra'):
    rows=json.loads((ROOT/folder/'manifest.json').read_text())
    for row in rows:
        b=(ROOT/folder/row['path']).read_bytes()
        assert hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()==row['git_blob_sha1']
OUT=ROOT/'cpu-review';assert not OUT.exists();OUT.mkdir()
native=ROOT/'native';assert not native.exists();native.mkdir()
flags=['-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
run(['xcrun','clang++',*flags,'test.cpp','-o',native/'test'],'compile')
run([native/'test',native],'native')
run(['xcrun','clang++',*flags,'-isystem',ROOT/'sdk','sdk-oracle.cpp','-o',native/'sdk-oracle'],'sdk-compile')
run([native/'sdk-oracle',native/'sdk-parameters.bin'],'sdk')
for name in ('requests.bin','replies.bin','sdk-parameters.bin'):
    assert (native/name).read_bytes()==(ROOT/'windows'/name).read_bytes(),name
    (OUT/name).write_bytes((native/name).read_bytes())
run(['python3','-m','unittest','-v'],'python')
count=int(re.findall(r'Ran (\d+) tests?',(OUT/'python.log').read_text())[-1]);assert count==6
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
version=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-version'],text=True).strip()
headers=Path(sdk)/'System/Library/Frameworks/Kernel.framework/Headers'
run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION',
     '-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone',
     '-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wframe-larger-than=4096',
     '-fstack-usage','-Wno-unused-parameter','-Wno-deprecated-declarations','-isysroot',sdk,
     '-isystem',headers,'-c','kernel-probe.cpp','-o',OUT/'kernel-probe.o'],'kernel-compile')
frames=[int(line.split('\t')[1]) for line in (OUT/'kernel-probe.su').read_text().splitlines()];assert frames and max(frames)<=4096
native_result=json.loads((OUT/'native.log').read_text())
assert native_result['passed'] and native_result['checks']==41479
for f in inputs['files']:assert digest(ROOT/f['path'])==f['sha256'],f['path']
summary=dict(passed=True,recorded_utc=datetime.datetime.utcnow().isoformat()+'Z',input_files=len(inputs['files']),
    cpp_checks=native_result['checks'],cpp_programs=2,python_tests=count,request_pages=5,
    kernel_max_stack=max(frames),input_manifest_sha256=digest(ROOT/'input-manifest.json'),
    hardware_accessed=False,kext_linked=False,compute_verified=False,metal_verified=False)
(OUT/'qualification.json').write_text(json.dumps(summary,indent=2)+'\n')
files=[dict(path=p.name,bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(OUT.iterdir()) if p.is_file()]
(OUT/'artifacts.json').write_text(json.dumps(dict(files=files),indent=2)+'\n')
archive=ROOT/'cpu-review-v1.tar.gz';assert not archive.exists()
with tarfile.open(archive,'w:gz') as tar:
    for p in sorted(OUT.iterdir()):tar.add(p,arcname='cpu-review/'+p.name,recursive=False)
print(json.dumps(dict(summary=summary,archive=str(archive),files=len(files)+1,sha256=digest(archive))))
