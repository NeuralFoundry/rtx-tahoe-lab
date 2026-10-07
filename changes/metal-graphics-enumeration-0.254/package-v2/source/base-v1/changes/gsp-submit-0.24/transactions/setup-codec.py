import hashlib,json,pathlib,sys,tarfile
archive=pathlib.Path('/Users/DEVELOPER/rtx-execution024-transfer-20260907T0130/sources.tar');target=pathlib.Path('/Users/DEVELOPER/rtx-execution024-offline')
assert len(sys.argv)==2 and hashlib.sha256(archive.read_bytes()).hexdigest()==sys.argv[1]
assert not target.exists(), 'Use a fresh standalone test directory'
with tarfile.open(archive) as t:
    for m in t.getmembers():
        assert m.isfile() or m.isdir()
        p=pathlib.PurePosixPath(m.name)
        assert not p.is_absolute() and '..' not in p.parts and str((target/m.name).resolve()).startswith(str(target)+'/')
    target.mkdir();assert target.resolve()==target and not target.is_symlink()
    t.extractall(target)
sources=json.loads((target/'changes/gsp-submit-0.24/transactions/source-manifest.json').read_text())
for s in sources:assert hashlib.sha256((target/s['path']).read_bytes()).hexdigest()==s['sha256'],s['path']
print('Verified standalone execution protocol CPU inputs:',len(sources))
