import hashlib,json,pathlib,shutil,sys,tarfile
archive=pathlib.Path('/Users/DEVELOPER/rtx-execution-native024-transfer-v2/release-sources.tar')
target=pathlib.Path('/Users/DEVELOPER/rtx-execution-native024')
assert len(sys.argv)==2 and hashlib.sha256(archive.read_bytes()).hexdigest()==sys.argv[1]
assert not target.exists(), 'A fresh standalone directory is required'
with tarfile.open(archive) as t:
    names=set()
    for member in t.getmembers():
        assert member.isfile() or member.isdir()
        p=pathlib.PurePosixPath(member.name)
        assert not p.is_absolute() and '..' not in p.parts and str((target/member.name).resolve()).startswith(str(target)+'/')
        assert member.name not in names;names.add(member.name)
    target.mkdir();assert target.resolve()==target and not target.is_symlink()
    t.extractall(target)
sources=json.loads((target/'changes/gsp-submit-0.24/native/release-source-manifest.json').read_text())
for item in sources:assert hashlib.sha256((target/item['path']).read_bytes()).hexdigest()==item['sha256'],item['path']
print('Separate 0.24 client/release inputs verified:',len(sources))
origin=pathlib.Path('/Users/DEVELOPER/rtx-tahoe-lab')
firmware=origin/'firmware/570.144'
files=[str(p.relative_to(origin)) for p in firmware.rglob('*') if p.is_file()]
files+=['results/probe-20260906T092946Z/snapshot.plist','results/probe-20260906T100715Z/vbios-shadow.bin',
        'results/gsp-first-boot-fix-20260906T172355Z/first-status.bin','results/gsp-preflight-20260906T122322Z/snapshot.plist',
        'results/gsp-events-20260906T181605Z/event-001.bin','results/gsp-sequencer-20260906T192106Z/after-sequence/event-005.bin']
files+=['results/gsp-bar1-20260906T212057Z/rm/record-%03d.bin'%i for i in range(6,12)]
supplement=[]
for name in files:
    source=origin/name;destination=target/name
    assert source.is_file() and not source.is_symlink() and str(source.resolve()).startswith(str(origin)+'/')
    assert not destination.exists() and str(destination.resolve()).startswith(str(target)+'/')
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    digest=hashlib.sha256(source.read_bytes()).hexdigest();assert hashlib.sha256(destination.read_bytes()).hexdigest()==digest
    supplement.append(dict(path=name,sha256=digest))
(target/'supplement-inputs.json').write_text(json.dumps(supplement,indent=2)+'\n')
print('Copied isolated firmware/evidence files with matching hashes:',len(supplement))
