import hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parents[3];p=pathlib.Path(__file__).resolve().parent;out=p/'reports'
rows=json.loads((p/'source-manifest.json').read_text())
for row in rows:assert hashlib.sha256((root/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
native=json.loads((out/'native.json').read_text());assert native['passed'] and not any(native[k] for k in ('hardware_accessed','compute_verified','metal_verified'))
assert native==json.loads((p/'windows/native.json').read_text())
for path in (p/'windows').glob('*.bin'):assert path.read_bytes()==(out/path.name).read_bytes(),path.name
frames=[int(line.split('\t')[1]) for line in (out/'kernel-stack.su').read_text().splitlines()]
assert frames and max(frames)<=4096
artifacts=[]
for path in sorted(out.iterdir()):
 if path.name in ('artifact-manifest.json','verification.json'):continue
 assert path.is_file() and not path.is_symlink()
 artifacts.append(dict(path=path.name,bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
(out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
result=dict(passed=True,hardware_accessed=False,compute_verified=False,metal_verified=False,inputs=len(rows),artifacts=len(artifacts),
 scenarios=native['scenarios'],checks=native['checks'],maximum_stack_frame_bytes=max(frames),
 kernel_object_sha256=hashlib.sha256((root/'build/compute-entry-kernel.o').read_bytes()).hexdigest(),
 source_manifest_sha256=hashlib.sha256((p/'source-manifest.json').read_bytes()).hexdigest())
(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
