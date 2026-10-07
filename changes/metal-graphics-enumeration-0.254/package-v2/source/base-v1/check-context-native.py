"""CPU integration check only. Does not load a kext, contact a GPU, or reboot."""
import datetime, hashlib, json, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'context-native-reports'
GR = 'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/record-011.bin'
FIFO = GR.replace('011', '010')
SUBMIT = 'changes/gsp-submit-0.24/'
COMPUTE = 'changes/gsp-compute-0.25/'
COMPONENTS = [
    ('layout', SUBMIT+'layout/test_layout.cpp', SUBMIT+'layout/windows', [GR]),
    ('codec', SUBMIT+'transactions/test_execution_codec.cpp', SUBMIT+'transactions/windows', [GR,FIFO]),
    ('runtime', SUBMIT+'runtime/test_execution_runtime.cpp', SUBMIT+'runtime/windows', [GR,FIFO]),
    ('fence', SUBMIT+'fence/test_host_fence.cpp', SUBMIT+'fence/windows', [GR,FIFO]),
    ('capture', SUBMIT+'native/test_execution_capture.cpp', SUBMIT+'native/windows', [GR,FIFO]),
    ('client', SUBMIT+'native/test_execution_client.cpp', SUBMIT+'native/windows-client', [GR,FIFO]),
    ('compute-memory', COMPUTE+'memory/test_compute_memory.cpp', COMPUTE+'memory/windows', [GR,FIFO]),
    ('compute-submit', COMPUTE+'submit/test_compute_submit.cpp', COMPUTE+'submit/windows', [GR,FIFO]),
    ('compute-entry', COMPUTE+'entry/test_compute_capture.cpp', COMPUTE+'entry/windows', [GR,FIFO]),
    ('identity', 'test_execution_identity.cpp', 'windows/identity', [GR,FIFO,'evidence/execution-identity/records.bin']),
]

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def run(args, name, cwd=ROOT):
    with (OUT/(name+'.log')).open('w') as log:
        result = subprocess.run([str(x) for x in args],cwd=cwd,stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(name+' failed; see '+str(OUT/(name+'.log')))

def main():
    os.chdir(ROOT)
    OUT.mkdir(exist_ok=True)
    manifest = json.loads((ROOT/'context-native-inputs.json').read_text())
    for row in manifest['files']:
        assert digest(ROOT/row['path'])==row['sha256'],row['path']
    original=(ROOT/(SUBMIT+'runtime/test_execution_runtime.cpp')).read_text()
    prefix=original[:original.index('int main(')].replace('#include "MemorySimulation.hpp"','#include "../runtime/MemorySimulation.hpp"').replace('#include "ExecutionTransactions.hpp"','#include "../runtime/ExecutionTransactions.hpp"')
    assert (ROOT/(SUBMIT+'fence/RuntimeSimulation.hpp')).read_text()==prefix
    record=dict(passed=False,hardware_accessed=False,compute_verified=False,metal_verified=False,
                input_manifest_sha256=digest(ROOT/'context-native-inputs.json'),input_count=len(manifest['files']),components=[],python=[])
    (OUT/'verification.json').write_text(json.dumps(record,indent=2)+'\n')
    for name,source,windows,args in COMPONENTS:
        folder=OUT/name;folder.mkdir(exist_ok=True)
        executable=folder/name
        run(['xcrun','clang++','-O2','-std=c++17','-Wall','-Wextra','-Werror',
             '-fsanitize=address,undefined','-fno-sanitize-recover=all',
             '-I',ROOT/'changes/gsp-channel-0.23/reference',ROOT/source,'-o',executable],name+'-compile')
        run([executable,*args,folder],name+'-run')
        result=json.loads((OUT/(name+'-run.log')).read_text())
        assert result['passed'] and not result['hardware_accessed'],name
        compared=[]
        for artifact in sorted(folder.glob('*.bin')):
            assert artifact.read_bytes()==(ROOT/windows/artifact.name).read_bytes(),name+'/'+artifact.name
            compared.append(dict(path=artifact.relative_to(ROOT).as_posix(),sha256=digest(artifact)))
        record['components'].append(dict(name=name,result=result,binary_comparisons=compared))
        print(name+': passed, '+str(len(compared))+' Windows/Mac binary comparisons',flush=True)
    suites=[('root',ROOT),('layout-python',ROOT/(SUBMIT+'layout')),
            ('codec-python',ROOT/(SUBMIT+'transactions')),
            ('compute-memory-python',ROOT/(COMPUTE+'memory')),
            ('compute-submit-python',ROOT/(COMPUTE+'submit'))]
    for name,cwd in suites:
        run([sys.executable,'-m','unittest','discover','-v'],name,cwd)
        match=re.search(r'Ran (\d+) tests? in ',(OUT/(name+'.log')).read_text())
        assert match,name
        record['python'].append(dict(name=name,tests=int(match.group(1))))
        print(name+': '+match.group(1)+' tests passed',flush=True)
    sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
    version=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-version'],text=True).strip()
    headers=Path(sdk)/'System/Library/Frameworks/Kernel.framework/Headers'
    run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION',
         '-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone',
         '-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wframe-larger-than=4096',
         '-fstack-usage','-Wno-unused-parameter','-Wno-deprecated-declarations','-isysroot',sdk,
         '-isystem',headers,'-c','driver/GSPExecutionProbe.cpp','-o',OUT/'GSPExecutionProbe.o'],'kernel-compile')
    stack=(OUT/'GSPExecutionProbe.su').read_text()
    frames=[int(line.split('\t')[1]) for line in stack.splitlines()]
    assert frames and max(frames)<=4096
    for row in manifest['files']:
        assert digest(ROOT/row['path'])==row['sha256'],row['path']
    record.update(passed=True,recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  kernel_object_sha256=digest(OUT/'GSPExecutionProbe.o'),maximum_stack_frame_bytes=max(frames),
                  kext_linked=False,kext_loaded=False)
    (OUT/'verification.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(dict(passed=True,cpp_programs=len(record['components']),python_tests=sum(x['tests'] for x in record['python']),maximum_stack_frame_bytes=max(frames),hardware_accessed=False)),flush=True)

if __name__=='__main__': main()
