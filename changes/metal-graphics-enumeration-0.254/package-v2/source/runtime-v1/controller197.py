"""Exact-package, one-attempt host controller for the authorized197 experiment."""
from pathlib import Path,PurePosixPath
import argparse,ctypes,hashlib,json,os,subprocess,sys,time
ROOT=Path('/private/var/root/rtx-bootstrap197-v1')
def save(name,row):
    with(ROOT/name).open('x')as f:json.dump(row,f,indent=2);f.write('\n')
def verify(pin):
    if ROOT.resolve()!=ROOT or os.geteuid()!=0 or sys.platform!='darwin' or not __debug__:raise ValueError('Root macOS controller')
    raw=(ROOT/'package-manifest.json').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=pin:raise ValueError('Package manifest pin')
    rows=json.loads(raw);seen=set()
    for row in rows:
        rel=PurePosixPath(row['path'])
        if rel.is_absolute()or '..'in rel.parts or str(rel)!=row['path']or ':'in str(rel)or chr(92)in str(rel)or str(rel)in seen:raise ValueError('Manifest path')
        seen.add(str(rel));p=ROOT/str(rel)
        if not p.is_file()or p.is_symlink()or p.resolve()!=p or p.stat().st_size!=row['bytes']or hashlib.sha256(p.read_bytes()).hexdigest()!=row['sha256']:raise ValueError('Package file: '+str(rel))
    return len(rows)
def preflight(pin):
    count=verify(pin)
    from bootstrap_main197 import activate
    catalog,image,receipt=activate(ROOT/'source')
    import native_bootstrap197 as native
    from machine197 import observe
    for path,sha,kind in [(ROOT/'owner.bundle/Contents/MacOS/RTXMetalDriver',native.OWNER_SHA,8),(ROOT/'RTXProbe.kext/Contents/MacOS/RTXProbe',native.KERNEL_SHA,11)]:native.verified_file(path,sha,kind)
    for path in (ROOT/'owner.bundle',ROOT/'RTXProbe.kext'):
        subprocess.run(['/usr/bin/codesign','--verify','--strict',str(path)],check=True,timeout=30)
    owner=ctypes.CDLL(str(ROOT/'owner.bundle/Contents/MacOS/RTXMetalDriver'),mode=ctypes.RTLD_GLOBAL);native.bind(owner)
    row,_=native.read_info(owner,os.getpid())
    if row['state']!=0 or row['calls']or row['io_opens']:raise ValueError('Library import opened hardware')
    return dict(passed=True,verified_files=count,compiler=receipt,native_cold=row,machine=observe(),firmware_executed=False)
def worker(pin):
    verify(pin)
    from machine197 import observe,require_cold
    machine=observe();binding=require_cold(machine)
    save('worker-started.json',dict(pid=os.getpid(),binding=binding,package_manifest_sha256=pin,one_attempt=True))
    argv=['/usr/bin/python3','-B',str(ROOT/'source/runtime-v1/bootstrap_main197.py'),'--root',str(ROOT)]
    env={'PATH':'/usr/bin:/bin:/usr/sbin:/sbin','PYTHONDONTWRITEBYTECODE':'1','PYTHONUTF8':'1','HOME':'/var/root','TMPDIR':'/private/tmp'}
    result=dict(returncode=None)
    try:
        with(ROOT/'bootstrap.stdout').open('xb')as out,(ROOT/'bootstrap.stderr').open('xb')as err:
            p=subprocess.Popen(argv,cwd=str(ROOT/'source/runtime-v1'),env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=err)
            save('bootstrap-started.json',dict(pid=p.pid,parent_pid=os.getpid(),binding=binding,argv=argv))
            try:result['returncode']=p.wait(timeout=1200)
            except subprocess.TimeoutExpired:
                result['timeout']=True;p.terminate()
                try:result['returncode']=p.wait(timeout=15)
                except subprocess.TimeoutExpired:result['process_still_present']=True
    except Exception as error:result['error']=repr(error)
    save('worker-terminal.json',dict(result,pid=os.getpid(),consumed=True))
    return result
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=('preflight','load','start','worker','observe'));p.add_argument('--manifest',required=True);p.add_argument('--attempt',type=int,default=1);a=p.parse_args()
    count=verify(a.manifest)
    if a.action=='preflight':result=preflight(a.manifest);save('preflight.json',result)
    elif a.action=='load':
        from machine197 import observe
        before=observe()
        if before['loaded_rtx']or before['RTXProbe']:raise ValueError('Load requires RTX absent')
        if not 1<=a.attempt<=999:raise ValueError('Load attempt index')
        label='load-%03d'%a.attempt;save(label+'-attempt.json',dict(before=before,package_manifest_sha256=a.manifest))
        proc=subprocess.run(['/usr/bin/kmutil','load','-p',str(ROOT/'RTXProbe.kext')],capture_output=True,timeout=90)
        result=dict(returncode=proc.returncode,stdout=proc.stdout.decode(errors='replace'),stderr=proc.stderr.decode(errors='replace'),after=observe());save(label+'-result.json',result)
    elif a.action=='start':
        from machine197 import observe,require_cold
        binding=require_cold(observe());save('start-attempt.json',dict(binding=binding,package_manifest_sha256=a.manifest))
        with(ROOT/'worker.stdout').open('xb')as out,(ROOT/'worker.stderr').open('xb')as err:
            proc=subprocess.Popen(['/usr/bin/python3','-B',str(Path(__file__).resolve()),'worker','--manifest',a.manifest],cwd=str(ROOT),stdin=subprocess.DEVNULL,stdout=out,stderr=err,start_new_session=True)
        result=dict(worker_pid=proc.pid,binding=binding);save('start-returned.json',result)
    elif a.action=='worker':result=worker(a.manifest)
    else:
        from machine197 import observe
        result=dict(machine=observe())
        for name in ('preflight.json','load-001-result.json','start-returned.json','worker-started.json','bootstrap-started.json','worker-terminal.json','evidence/result.json'):
            path=ROOT/name
            if path.is_file():result[name]=json.loads(path.read_bytes())
        for name in ('bootstrap.stdout','bootstrap.stderr','worker.stdout','worker.stderr'):
            path=ROOT/name
            if path.is_file():result[name]=path.read_bytes()[-12000:].decode(errors='replace')
        for name in ('start-returned.json','bootstrap-started.json'):
            if name in result:
                pid=result[name].get('pid',result[name].get('worker_pid'))
                proc=subprocess.run(['/bin/ps','-p',str(pid),'-o','pid=,ppid=,stat=,etime=,command='],capture_output=True,timeout=10)
                result[name+'-process']=proc.stdout.decode(errors='replace').strip()
    print(json.dumps(result,sort_keys=True),flush=True)
if __name__=='__main__':main()
