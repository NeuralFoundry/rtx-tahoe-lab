"""Bind a tested diagnostic client to the unchanged, previously built kext."""
import datetime
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'results/gsp-execution-development'
BINARY='98c9c566e7e34be6524175222054ca28a005a427972eb2d2b8924839527ba4c7'
PLIST='04cfb580b87b8ffa27d69b2d39c8c89cecb6428912f7a16b5f8120179a7d610d'
CLIENT_CHANGES={'gsp_execution_client.py','gsp_execution_native.py'}

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text())
def path(name):
    p=ROOT/name
    if p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(ROOT):raise ValueError('Source path')
    return p
def write(name,value):(OUT/name).write_text(json.dumps(value,indent=2)+'\n')

def verify_inputs():
    rows=read(ROOT/'diagnostics-source-manifest.json')
    assert len(rows)==706 and len({r['path'] for r in rows})==706
    for row in rows:assert digest(path(row['path']))==row['sha256'],row['path']
    original=read(ROOT/'baseline/build-verification.json');prior_test=read(ROOT/'baseline/test-verification.json')
    assert original['passed'] and original['probe_version']=='0.25.0' and original['status']=='built-not-loaded' and not original['kext_loaded']
    assert prior_test['passed'] and prior_test['total_python_tests']==553 and prior_test['total_cpp_programs']==51 and not prior_test['hardware_accessed']
    assert digest(ROOT/'baseline/test-verification.json')==original['test_verification_sha256']
    assert digest(ROOT/'baseline/source-manifest.json')==prior_test['source_manifest_sha256']
    assert len(original['sources'])==85
    for row in original['sources']:assert digest(path(row['path']))==row['sha256'],'Native input changed: '+row['path']
    delta=read(ROOT/'diagnostics-derivation.json')
    assert delta['baseline_inputs']==702 and {r['path'] for r in delta['changed']}==CLIENT_CHANGES and len(delta['changed'])==2
    changes={r['path']:r for r in delta['changed']};observed=set()
    for row in read(ROOT/'baseline/source-manifest.json')['files']:
        actual=digest(path(row['path']))
        if actual!=row['sha256']:
            assert row['path'] in changes and changes[row['path']]['before']==row['sha256'] and changes[row['path']]['after']==actual
            observed.add(row['path'])
    assert observed==CLIENT_CHANGES
    assert digest(ROOT/'build/RTXProbe-0.25.0.kext/Contents/MacOS/RTXProbe')==BINARY==original['binary_sha256']
    assert digest(ROOT/'build/RTXProbe-0.25.0.kext/Contents/Info.plist')==PLIST==original['plist_sha256']
    return rows,original

def main():
    assert len(sys.argv)==2 and sys.argv[1] in ('before','after')
    rows,original=verify_inputs();stamp=datetime.datetime.now(datetime.timezone.utc).isoformat()
    manifest=OUT/'source-manifest.json'
    if sys.argv[1]=='before':
        OUT.mkdir(parents=True,exist_ok=True)
        write('test-verification.json',dict(passed=False,status='client-tests-running',recorded_utc=stamp,hardware_accessed=False))
        write('build-verification.json',dict(passed=False,status='native-reuse-not-yet-validated',kext_loaded=False))
        write('source-manifest.json',dict(recorded_utc=stamp,purpose='Current diagnostic client plus exact approved native inputs',
            files=rows+[dict(path='diagnostics-source-manifest.json',sha256=digest(ROOT/'diagnostics-source-manifest.json'))]))
        print('Verified client inputs and all 85 unchanged native inputs; no rebuild')
        return
    for row in read(manifest)['files']:assert digest(path(row['path']))==row['sha256'],row['path']
    tests=read(OUT/'python-test-result.json');assert tests['passed'] and tests['tests_run']==491 and not any(tests[k] for k in ('failures','errors','skipped'))
    result=dict(passed=True,probe_version='0.25.0',client_revision='diagnostics-v1',status='client-validation-passed',recorded_utc=stamp,
        source_manifest_sha256=digest(manifest),python=tests,hardware_accessed=False,kext_loaded=False,compute_verified=False,metal_verified=False,
        prior_native_cpp_programs=51,native_cpp_repeated=False,prior_full_test_sha256=digest(ROOT/'baseline/test-verification.json'))
    write('test-verification.json',result)
    reused=dict(original);reused.update(status='reused-approved-native-not-rebuilt',recorded_utc=stamp,build_performed=False,
        original_build_verification_sha256=digest(ROOT/'baseline/build-verification.json'),original_full_test_sha256=digest(ROOT/'baseline/test-verification.json'),
        test_verification_sha256=digest(OUT/'test-verification.json'),client_revision='diagnostics-v1')
    write('build-verification.json',reused)
    print(json.dumps(dict(passed=True,python_tests=491,native_inputs_unchanged=85,binary_sha256=BINARY,kext_rebuilt=False,hardware_accessed=False)))

if __name__=='__main__':main()
