import datetime,hashlib,json,pathlib
from test_compute_memory import expected_image,expected_tables
P=pathlib.Path(__file__).resolve().parent;R=P.parents[2];out=P/'reports'
rows=json.loads((P/'source-manifest.json').read_text())
for row in rows:assert hashlib.sha256((R/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
native=json.loads((out/'native.json').read_text())
assert native==json.loads((P/'windows/native.json').read_text()) and native['passed'] and native['scenarios']==216 and native['baseline_io_operations']==91
assert not native['hardware_accessed'] and not native['compute_verified'] and not native['metal_verified']
for name in ('image.bin','command.bin','children.bin','root.bin','write-trace.bin'):assert (out/name).read_bytes()==(P/'windows'/name).read_bytes(),name
assert (out/'image.bin').read_bytes()==expected_image() and (out/'children.bin').read_bytes()==expected_tables()
log=(out/'python.log').read_text();assert 'Ran 5 tests' in log and log.rstrip().endswith('OK')
frames=[int(line.split('\t')[-2]) for line in (out/'kernel-stack.su').read_text().splitlines() if line.strip()]
assert frames and max(frames)<=4096
record=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=True,input_files=len(rows),python_tests=5,cpp_programs=1,
 cpp_checks=native['checks'],cpp_scenarios=216,windows_mac_python_identical=True,added_pages=6,kernel_sdk_compile_passed=True,
 max_kernel_frame_bytes=max(frames),kernel_object_sha256=hashlib.sha256((R/'build/compute-memory-kernel.o').read_bytes()).hexdigest(),
 native_adapter_implemented=True,kext_integrated=False,kext_built=False,hardware_accessed=False,compute_verified=False,metal_verified=False)
(out/'verification.json').write_text(json.dumps(record,indent=2)+'\n')
artifacts=[dict(path=f.name,bytes=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in sorted(out.iterdir()) if f.is_file() and f.name!='artifact-manifest.json']
(out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
print(json.dumps(record,indent=2))
