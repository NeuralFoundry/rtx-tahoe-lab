from pathlib import Path
import datetime,hashlib,json,os,subprocess,tarfile,traceback
root=Path(__file__).resolve().parent;out=root.parent/'cpu';out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();commands=[];passed=False;error=None;inputs=0;actual_cold_abi=False
def run(args,name):
    p=subprocess.run(args,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'));(out/name).write_bytes(p.stdout);commands.append(dict(argv=args,returncode=p.returncode,log=name))
    if p.returncode:raise RuntimeError(name+': '+p.stdout.decode(errors='replace')[-5000:])
def source_check():
    rows=json.loads((root/'source-manifest.json').read_bytes())
    for r in rows:assert sha(root/r['path'])==r['sha256'] and (root/r['path']).stat().st_size==r['bytes']
    origins=json.loads((root/'uploaded-origin-manifest.json').read_bytes());assert len(origins)==1934
    for r in origins:assert sha(root/r['path'])==r['sha256']
    return len(rows)
def snapshot(phase):
    run(['sysctl','kern.bootsessionuuid','kern.boottime'],'boot-'+phase+'.txt');run(['kmutil','showloaded'],'loaded-'+phase+'.txt');run(['ioreg','-r','-c','RTXProbe','-l'],'rtx-'+phase+'.txt')
    assert not (out/('rtx-'+phase+'.txt')).read_bytes().strip();loaded=(out/('loaded-'+phase+'.txt')).read_bytes();assert b'RTXProbe' not in loaded and b'AMDRadeon' in loaded
try:
    assert os.geteuid()!=0;inputs=source_check();snapshot('before')
    run(['/usr/bin/python3','-B','bridge-tests.py'],'bridge-tests.log');assert 'Ran 26 tests' in (out/'bridge-tests.log').read_text()
    run(['/usr/bin/python3','-B','actual-cold-abi.py'],'actual-cold.json');assert json.loads((out/'actual-cold.json').read_bytes())['passed']
    actual_cold_abi=True
    # Bootstrap/oracle bodies are identical to the frozen release; bridge-tests
    # verifies their AST equality and all 65 new application boundary calls.
    snapshot('after');assert (out/'boot-before.txt').read_bytes()==(out/'boot-after.txt').read_bytes();source_check();passed=True
except Exception as ex:error=repr(ex);traceback.print_exc()
finally:
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (out/'verification.json').write_text(json.dumps(dict(passed=passed,error=error,inputs=inputs,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),new_tests=26,actual_cold_abi=actual_cold_abi,native_opens=0,gpu_submissions=0),indent=2)+'\n')
    rows=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()];(out/'artifact-manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
    archive=root.parent/'cpu.tar.gz'
    with tarfile.open(archive,'w:gz') as tar:
        for p in sorted(out.rglob('*')):
            if p.is_file():tar.add(p,arcname='cpu/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(passed=passed,error=error,artifacts=len(rows),archive_sha256=sha(archive))),flush=True)
raise SystemExit(0 if passed else 1)
