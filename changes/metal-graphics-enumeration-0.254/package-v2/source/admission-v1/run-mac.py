"""Build isolated application and CPU provider; this step creates no root service."""
from pathlib import Path
import datetime,hashlib,json,os,plistlib,shlex,subprocess,tarfile,traceback
from BuildHostState import validate,identity

source=Path(__file__).resolve().parent;out=source.parent/'mac';out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
rows=json.loads((source/'source-manifest.json').read_bytes());lookup={r['path']:r for r in rows};objects=json.loads((source/'objects-manifest.json').read_bytes())
commands=[];closures={};passed=False;error=None
def run(argv,name):
    p=subprocess.run([str(a) for a in argv],cwd=source,capture_output=True,timeout=90)
    (out/name).write_bytes(p.stdout);(out/(name+'.stderr')).write_bytes(p.stderr)
    commands.append(dict(argv=[str(a) for a in argv],log=name,returncode=p.returncode))
    assert p.returncode==0,(name,p.returncode,p.stderr.decode(errors='replace')[-2000:]);return p.stdout
def snapshot(phase):
    boot=run(['/usr/sbin/sysctl','kern.bootsessionuuid','kern.boottime'],'boot-'+phase+'.txt')
    loaded=run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-'+phase+'.txt')
    raw=run(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c','RTXProbe'],'probe-'+phase+'.plist')
    return validate(boot,loaded,raw)

def closure(name,dep):
    names=set((source/w).resolve().relative_to(source).as_posix() for w in shlex.split(dep.read_text().replace('\\\n',' '))[1:]);closures[name]=[lookup[n] for n in sorted(names)]
try:
    assert os.geteuid()==501
    for row in rows:assert sha(source/row['path'])==row['sha256']
    before=snapshot('before')
    for name in ('test-admission.py','test-runtime-admission.py','test-workload.py','test-build-host.py'):
        report=json.loads(run(['/usr/bin/python3','-B',name],name+'.json'));assert report==json.loads((source/(name+'.windows.json')).read_bytes())
    for mode,flags in [('normal',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
        for row in objects[mode]:
            p=Path(row['path']);assert sha(p)==row['sha256'] and p.stat().st_size==row['bytes']
            for dependency in row['inputs']:assert lookup[dependency['path']]==dependency
        opts=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fno-objc-arc','-fobjc-weak','-fblocks',*flags]
        dep=out/(mode+'.d');obj=out/('runtime-application-'+mode+'.o');exe=out/('application-'+mode)
        run([*opts,'-MMD','-MF',dep,'-c','runtime-application-test.mm','-o',obj],'compile-'+mode+'.log');closure(mode,dep)
        run([*opts,'-Wl,-U,_OBJC_CLASS_$__MTLDevice',*[r['path'] for r in objects[mode]],obj,'-framework','Foundation','-framework','Metal','-framework','IOKit','-framework','IOSurface','-o',exe],'link-'+mode+'.log')
        run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',exe],'sign-'+mode+'.log');run(['/usr/bin/codesign','--verify','--strict','--verbose=2',exe],'verify-'+mode+'.log')
    provider=out/'FixtureProvider063.dylib';dep=out/'provider.d'
    run(['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fno-objc-arc','-fobjc-weak','-fblocks','-dynamiclib','-MMD','-MF',dep,'resident-fixture-server.mm','-framework','Foundation','-framework','Metal','-framework','IOKit','-o',provider],'compile-provider.log');closure('provider',dep)
    run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',provider],'sign-provider.log');run(['/usr/bin/codesign','--verify','--strict','--verbose=2',provider],'verify-provider.log')
    after=snapshot('after');assert identity(before)==identity(after);passed=True
except Exception as e:error=repr(e);traceback.print_exc()
finally:
    result=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,source_inputs=len(rows),production_objects_reused_per_mode=18,application_pipeline_tests_run=False,cpu_fixture_only=True,native_iokit_connected=False,gpu_commands_submitted=False)
    for name,value in [('verification.json',result),('commands.json',commands),('compiled-inputs.json',closures)]: (out/name).write_text(json.dumps(value,indent=2)+'\n')
    artifacts=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()];(out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
    archive=source.parent/'application-bundle-review.tar.gz'
    with tarfile.open(archive,'x:gz') as t:
        for p in sorted(out.rglob('*')):
            if p.is_file():t.add(p,arcname='mac/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(result,artifacts=len(artifacts),archive_sha256=sha(archive))),flush=True)
raise SystemExit(0 if passed else 1)
