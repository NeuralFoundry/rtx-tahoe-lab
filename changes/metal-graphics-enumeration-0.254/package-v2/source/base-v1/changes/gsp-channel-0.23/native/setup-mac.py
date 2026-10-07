"""Stage authorized test sources in one separate, fixed macOS directory."""
import hashlib,json,pathlib,sys,tarfile
archive=pathlib.Path('/Users/DEVELOPER/rtx-channel-native-023-sources.tar')
target=pathlib.Path('/Users/DEVELOPER/rtx-channel-native-023')
assert len(sys.argv)==2 and hashlib.sha256(archive.read_bytes()).hexdigest()==sys.argv[1]
with tarfile.open(archive) as tar:
    members=tar.getmembers()
    for item in members:
        assert item.isfile() or item.isdir()
        p=pathlib.PurePosixPath(item.name)
        assert not p.is_absolute() and '..' not in p.parts
        assert str((target/item.name).resolve()).startswith(str(target)+'/')
    if not target.exists():
        target.mkdir()
        for name in ('results','firmware'):
            (target/name).symlink_to(pathlib.Path('/Users/DEVELOPER/rtx-tahoe-lab')/name,target_is_directory=True)
    assert target.resolve()==target and not target.is_symlink()
    tar.extractall(target)
for item in json.loads((target/'changes/gsp-channel-0.23/native/source-manifest.json').read_text()):
    assert hashlib.sha256((target/item['path']).read_bytes()).hexdigest()==item['sha256'],item['path']
print('Isolated source inputs verified:',len(members))
