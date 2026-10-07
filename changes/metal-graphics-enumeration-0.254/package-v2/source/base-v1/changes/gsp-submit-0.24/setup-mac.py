import hashlib,json,pathlib,sys,tarfile
archive=pathlib.Path('/Users/DEVELOPER/rtx-submit024-sources.tar');target=pathlib.Path('/Users/DEVELOPER/rtx-submit024-offline')
assert len(sys.argv)==2 and hashlib.sha256(archive.read_bytes()).hexdigest()==sys.argv[1]
with tarfile.open(archive) as t:
    for m in t.getmembers():
        assert m.isfile() or m.isdir()
        p=pathlib.PurePosixPath(m.name)
        assert not p.is_absolute() and '..' not in p.parts and str((target/m.name).resolve()).startswith(str(target)+'/')
    target.mkdir(exist_ok=True);assert target.resolve()==target and not target.is_symlink()
    t.extractall(target)
sources=json.loads((target/'changes/gsp-submit-0.24/source-manifest.json').read_text())
for row in sources:assert hashlib.sha256((target/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
print('Verified standalone CPU-only source inputs:',len(sources))
