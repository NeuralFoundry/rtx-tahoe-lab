"""Compile/test 0.37 and link an uninstalled KEXT; never open or load it."""
from pathlib import Path
import datetime,hashlib,json,os,plistlib,shlex,struct,subprocess,tarfile,traceback
root=Path(__file__).resolve().parent;out=root.parent/'build-v1';out.mkdir();commands=[];closures={};frames={};passed=False;error=None
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=json.loads((root/'source-manifest.json').read_bytes());manifest={r['path']:r for r in inputs}
reference=json.loads((root/'completion-windows-reference.json').read_bytes())
def run(argv,name,timeout=120,extra=None):
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    if extra:env.update(extra)
    with (out/name).open('xb') as f:p=subprocess.run(argv,cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=timeout,env=env)
    commands.append(dict(argv=argv,returncode=p.returncode,log=name));assert not p.returncode,(name,(out/name).read_text(errors='replace')[-5000:])
def source_check():
    for r in inputs:
        p=root/r['path'];assert p.resolve().is_relative_to(root) and not p.is_symlink() and p.stat().st_size==r['bytes'] and sha(p)==r['sha256'],r['path']
def snapshot(phase):
    run(['/usr/sbin/sysctl','kern.bootsessionuuid','kern.boottime'],'boot-'+phase+'.txt');run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-'+phase+'.txt');run(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c','RTXProbe'],'rtx-'+phase+'.plist')
    raw=(out/('rtx-'+phase+'.plist')).read_bytes();assert not (plistlib.loads(raw) if raw.strip() else [])
    raw=(out/('loaded-'+phase+'.txt')).read_bytes();assert b'RTXProbe' not in raw and b'AMDRadeon' in raw
def closure(tag):
    words=shlex.split((out/(tag+'.d')).read_text().replace('\\\n',' '));assert words[0].endswith(':');names=set()
    for word in words[1:]:
        p=Path(word);p=p if p.is_absolute() else root/p;n=p.resolve().relative_to(root).as_posix();assert n in manifest and sha(p)==manifest[n]['sha256'];names.add(n)
    closures[tag]=[manifest[n] for n in sorted(names)]
try:
    assert os.geteuid()!=0;source_check();snapshot('before')
    for mode,extra in [('normal',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
        for variant,source,define in [('legacy','completion-tests.cpp',['-DRTX_TEST_LEGACY_OBSERVATION']),('ordered','completion-tests.cpp',[]),('entry','entry-tests.cpp',[]),('uploaded','uploaded-client-fixture.cpp',[])]:
            tag=variant+'-'+mode;raw=out/tag;raw.mkdir();exe=out/(tag+'-test')
            run(['xcrun','clang++','-O1','-g','-std=c++17','-Wall','-Wextra','-Werror',*extra,*define,'-MMD','-MF',str(out/(tag+'.d')),source,'-o',str(exe)],'compile-'+tag+'.log');closure(tag)
            run([str(exe),str(root/'selected-fixtures' if variant=='uploaded' else root),str(raw)],tag+'.json')
            assert json.loads((out/(tag+'.json')).read_bytes())==reference['tests'][variant]
            for r in reference['raw'][variant]:assert (raw/r['path']).stat().st_size==r['bytes'] and sha(raw/r['path'])==r['sha256']
        run(['/usr/bin/python3','-B','-m','unittest','-v','test_completion_observation'],'python-'+mode+'.log',extra={'RTX_OBSERVATION_FIXTURES':str(out),'RTX_OBSERVATION_SUFFIX':'-'+mode})
        assert b'Ran 13 tests' in (out/('python-'+mode+'.log')).read_bytes()
    sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip();version=subprocess.check_output(['xcrun','--show-sdk-version'],text=True).strip()
    for tag,source in [('completion-kernel','completion-smoke.cpp'),('kernel-entry','driver/GSPLibraryUploadProbe.cpp')]:
        run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone',
            '-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wno-unused-parameter','-Wframe-larger-than=4096','-fstack-usage','-MMD','-MF',str(out/(tag+'.d')),
            '-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers','-c',source,'-o',str(out/(tag+'.o'))],'compile-'+tag+'.log');closure(tag)
        usage=[]
        for line in (out/(tag+'.su')).read_text().splitlines():
            fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();usage.append(dict(function=fields[0],bytes=int(fields[1])))
        assert usage and max(r['bytes'] for r in usage)<=4096;frames[tag]=usage
    # An uninstalled, signed bundle is made only after all CPU/entry checks.
    bundle=out/'RTXProbe-0.37.0.kext';(bundle/'Contents/MacOS').mkdir(parents=True)
    info=plistlib.loads((root/'driver/Info.plist').read_bytes());assert info['CFBundleVersion']==info['CFBundleShortVersionString']=='0.37.0';(bundle/'Contents/Info.plist').write_bytes(plistlib.dumps(info));binary=bundle/'Contents/MacOS/RTXProbe'
    run(['xcrun','clang++','-arch','x86_64','-nostdlib','-isysroot',sdk,'-mmacosx-version-min='+version,'-Wl,-kext','-Wl,-undefined,dynamic_lookup','-Wl,-no_fixup_chains',str(out/'kernel-entry.o'),'-lkmod','-lkmodc++','-lcc_kext','-o',str(binary)],'link-kext.log')
    run(['plutil','-lint',str(bundle/'Contents/Info.plist')],'plist.log');run(['codesign','--force','--sign','-','--timestamp=none',str(bundle)],'sign.log');run(['codesign','--verify','--verbose=2',str(bundle)],'signature.log')
    h=struct.unpack_from('<4I',binary.read_bytes());assert h[0]==0xfeedfacf and h[1]==0x1000007 and h[3]==11 and b'0.37.0' in binary.read_bytes()
    snapshot('after');assert (out/'boot-before.txt').read_bytes()==(out/'boot-after.txt').read_bytes();source_check();passed=True
except Exception as ex:error=repr(ex);traceback.print_exc()
finally:
    bundle=out/'RTXProbe-0.37.0.kext';binary=bundle/'Contents/MacOS/RTXProbe'
    record=dict(passed=passed,error=error,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),source_inputs=len(inputs),native_entry_compiled='kernel-entry' in frames,kext_linked=binary.exists(),kext_loaded=False,native_io_opens=0,gpu_submissions=0,
        binary_sha256=sha(binary) if binary.exists() else None,plist_sha256=sha(bundle/'Contents/Info.plist') if (bundle/'Contents/Info.plist').exists() else None)
    for name,data in [('verification.json',record),('commands.json',commands),('compiled-inputs.json',closures),('stack-frames.json',frames)]: (out/name).write_text(json.dumps(data,indent=2)+'\n')
    rows=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()];(out/'artifact-manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
    archive=root.parent/'build-v1.tar.gz'
    with tarfile.open(archive,'w:gz') as tar:
        for p in sorted(out.rglob('*')):
            if p.is_file():tar.add(p,arcname='build-v1/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(record,archive_sha256=sha(archive),artifacts=len(rows))),flush=True)
raise SystemExit(0 if passed else 1)
