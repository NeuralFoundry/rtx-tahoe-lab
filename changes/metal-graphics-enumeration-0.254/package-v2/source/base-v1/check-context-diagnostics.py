"""CPU-only qualification of a diagnostic decoder using captured GPU files."""
from pathlib import Path
import datetime,hashlib,json,re,subprocess,tarfile
import gsp_execution_native as n
from test_gsp_context_diagnostics import Saved,GEN
root=Path(__file__).resolve().parent
inputs=json.loads((root/'diagnostics-inputs.json').read_text(encoding='utf-8-sig'))
for f in inputs['files']:
    p=root/f['path'];assert not p.is_symlink()
    assert p.stat().st_size==f['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==f['sha256'],f['path']
out=root/'cpu-review';assert not out.exists();out.mkdir()
with (out/'python.log').open('w') as log:
    run=subprocess.run(['python3','-m','unittest','discover','-v'],cwd=root,stdout=log,stderr=subprocess.STDOUT)
assert run.returncode==0,'CPU tests failed; inspect cpu-review/python.log'
log=(out/'python.log').read_text();count=int(re.findall(r'Ran (\d+) tests? in',log)[-1])
assert count==508,(count,'expected previous504 plus4 actual diagnostic regressions')
result=n.capture(Saved(),GEN,out/'redecoded')
assert not result['passed'] and result['native_stop']['failure']==17
assert len(result['records'])==16 and len(result['firmware_assertions'])==13
assert not any(result[k] for k in ('host_command_verified','compute_verified','metal_verified','device_bytes_verified'))
summary=dict(passed=True,recorded_utc=datetime.datetime.utcnow().isoformat()+'Z',python_tests=count,
    input_files=len(inputs['files']),input_manifest_sha256=hashlib.sha256((root/'diagnostics-inputs.json').read_bytes()).hexdigest(),
    actual_records=16,assert_records=13,native_failure=17,kext_changed=False,hardware_accessed=False,compute_verified=False,metal_verified=False)
(out/'qualification.json').write_text(json.dumps(summary,indent=2)+'\n')
files=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(out.rglob('*')) if p.is_file()]
(out/'artifacts.json').write_text(json.dumps(dict(files=files),indent=2)+'\n')
archive=root/'cpu-review-v1.tar.gz';assert not archive.exists()
with tarfile.open(archive,'w:gz') as tar:
    for p in sorted(out.rglob('*')):
        if p.is_file():tar.add(p,arcname='cpu-review/'+p.relative_to(out).as_posix(),recursive=False)
print(json.dumps(dict(summary=summary,archive=str(archive),files=len(files)+1,sha256=hashlib.sha256(archive.read_bytes()).hexdigest())))
