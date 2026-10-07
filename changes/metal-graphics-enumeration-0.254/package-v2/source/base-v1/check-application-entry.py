from pathlib import Path
import subprocess,json,hashlib,datetime,re,unittest,sys
root=Path.cwd();out=root/'application-entry-mac';assert not out.exists();out.mkdir()
inputs=json.loads((root/'application-entry-inputs.json').read_text())
def verify_inputs():
    seen=set()
    for row in inputs:
        p=root/row['path'];assert p.resolve().is_relative_to(root.resolve()) and p.is_file() and not p.is_symlink() and row['path'] not in seen
        seen.add(row['path']);assert p.stat().st_size==row['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256']
verify_inputs()
def run(args,name):
    with (out/name).open('w') as log:subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)
programs=[('entry','entry/test_runtime_entry.cpp'),('runtime','runtime/test_runtime_native.cpp'),('owner','runtime/test_runtime_owner.cpp')]
prefix='changes/gsp-application-runtime-0.32/';fixture='changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/'
for name,source in programs:
    run(['xcrun','clang++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',prefix+source,'-o',str(out/name)],name+'-compile.log')
    dest=out/(name+'-data');dest.mkdir()
    args=[str(out/name)] if name=='owner' else [str(out/name),fixture+'record-011.bin',fixture+'record-010.bin',str(dest)]
    run(args,name+('.txt' if name=='owner' else '.json'))
    component='entry' if name=='entry' else 'runtime'
    expected=(root/prefix/component/'windows'/('owner.txt' if name=='owner' else 'native.json')).read_text()
    actual=(out/(name+('.txt' if name=='owner' else '.json'))).read_text()
    assert actual.strip()==expected.strip(),name
    for p in dest.glob('*.bin'):assert p.read_bytes()==(root/prefix/component/'windows'/p.name).read_bytes(),p.name
run([sys.executable,'-m','unittest','test_gsp_application_entry'],'python-tests.log')
import test_gsp_application_entry as tests
suite=unittest.defaultTestLoader.loadTestsFromModule(tests);python_count=suite.countTestCases()
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();version=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-version'],text=True).strip()
run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-deprecated-declarations','-Wframe-larger-than=4096','-fstack-usage','-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers','-c','driver/GSPApplicationProbe.cpp','-o',str(out/'GSPApplicationProbe.o')],'kernel-compile.log')
frames=[]
for p in out.glob('*.su'):
    for line in p.read_text().splitlines():
        fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();frames.append(int(fields[1]))
assert frames and max(frames)<=4096
closure=set()
def include(p):
    p=p.resolve();assert p.is_relative_to(root.resolve()) and p.is_file() and not p.is_symlink()
    rel=p.relative_to(root).as_posix()
    if rel in closure:return
    closure.add(rel)
    for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"',p.read_text(),re.M):include(p.parent/name)
include(root/'driver/GSPApplicationProbe.cpp')
record=dict(passed=True,recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),source_files=len(inputs),native_includes=len(closure),maximum_kernel_stack_frame=max(frames),cpp_sanitizers=['address','undefined'],python_tests=python_count,driver_entry_compiled=True,kext_built=False,hardware_accessed=False,metal_verified=False,entry=json.loads((out/'entry.json').read_text()),runtime=json.loads((out/'runtime.json').read_text()))
(out/'native-includes.json').write_text(json.dumps([dict(path=p,sha256=hashlib.sha256((root/p).read_bytes()).hexdigest()) for p in sorted(closure)],indent=2)+'\n')
verify_inputs();(out/'verification.json').write_text(json.dumps(record,indent=2)+'\n')
artifacts=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(out.rglob('*')) if p.is_file()]
(out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n');print(json.dumps(record))
