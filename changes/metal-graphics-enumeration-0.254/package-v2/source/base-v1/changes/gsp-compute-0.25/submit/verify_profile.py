import datetime,hashlib,json,pathlib
import source_oracle,snapshot
P=pathlib.Path(__file__).resolve().parent;R=P.parents[2];out=P/'reports'
rows=json.loads((P/'source-manifest.json').read_text())
for row in rows:assert hashlib.sha256((R/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
native=json.loads((out/'native.json').read_text())
assert native==json.loads((P/'windows/native.json').read_text()) and native['passed'] and native['scenarios']==1526 and native['baseline_io_operations']==714
assert not native['hardware_accessed'] and not native['compute_verified'] and not native['metal_verified']
for name in ('command.bin','entry.bin','write-trace.bin','simulated-queue.bin','simulated-backing.bin'):assert (out/name).read_bytes()==(P/'windows'/name).read_bytes(),name
args=[(p/name).read_bytes() for p,name in ((out,'simulated-queue.bin'),(out,'simulated-backing.bin'),(P/'reference','initial-image.bin'),
 (P/'reference','host-command.bin'),(P/'reference','host-entry.bin'),(out,'command.bin'),(out,'entry.bin'))]
decoded=snapshot.validate(*args);assert decoded['valid_snapshot']
log=(out/'python.log').read_text();assert 'Ran 6 tests' in log and log.rstrip().endswith('OK')
oracle=source_oracle.verify();assert (out/'entry.bin').read_bytes()==source_oracle.entry()
(out/'source-oracle.json').write_text(json.dumps(oracle,indent=2)+'\n')
frames=[int(line.split('\t')[-2]) for line in (out/'kernel-stack.su').read_text().splitlines() if line.strip()]
assert frames and max(frames)<=4096
record=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=True,input_files=len(rows),python_tests=6,cpp_programs=1,
 cpp_checks=native['checks'],cpp_scenarios=1526,windows_mac_identical=True,independent_snapshot_verified=True,source_oracle_verified=True,
 kernel_sdk_compile_passed=True,max_kernel_frame_bytes=max(frames),kernel_object_sha256=hashlib.sha256((R/'build/compute-submit-kernel.o').read_bytes()).hexdigest(),
 native_adapter_implemented=True,kext_integrated=False,kext_built=False,hardware_accessed=False,compute_verified=False,metal_verified=False)
(out/'verification.json').write_text(json.dumps(record,indent=2)+'\n')
artifacts=[dict(path=f.name,bytes=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in sorted(out.iterdir()) if f.is_file() and f.name!='artifact-manifest.json']
(out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
print(json.dumps(record,indent=2))
