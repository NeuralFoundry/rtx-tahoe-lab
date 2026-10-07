from pathlib import Path
import sys,datetime,hashlib,json,os,plistlib,shlex,subprocess,tarfile,traceback
root=Path(__file__).resolve().parent;out=root.parent/'mac';out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=json.loads((root/'source-manifest.json').read_bytes());byname={r['path']:r for r in inputs};commands=[];closures={};bundles=[];passed=False;error=None;mounted=False
env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def run(argv,name,timeout=120):
    p=subprocess.run([str(x) for x in argv],cwd=root,capture_output=True,timeout=timeout,env=env)
    (out/name).write_bytes(p.stdout);(out/(name+'.stderr')).write_bytes(p.stderr);commands.append(dict(argv=[str(x) for x in argv],log=name,returncode=p.returncode))
    assert p.returncode==0,(name,p.returncode,(p.stdout+p.stderr).decode(errors='replace')[-2500:]);return p.stdout
def check():
    for r in inputs:assert sha(root/r['path'])==r['sha256'] and (root/r['path']).stat().st_size==r['bytes']
def snapshot(phase):
    run(['/usr/sbin/sysctl','kern.bootsessionuuid','kern.boottime'],'boot-'+phase+'.txt')
    b=run(['/usr/bin/kmutil','showloaded','--list-only'],'loaded-'+phase+'.txt');assert b'RTXProbe' not in b and b'AMDRadeon' in b
    b=run(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c','RTXProbe'],'rtx-'+phase+'.plist');assert not (plistlib.loads(b) if b.strip() else [])
try:
    assert os.geteuid()==501;check();snapshot('before');run(['/usr/bin/sw_vers'],'os.txt');run(['xcrun','clang++','--version'],'compiler.txt')
    reference=json.loads((root/'toolchain-reference.json').read_bytes());dmg=Path(reference['dmg_path']);assert sha(dmg)==reference['dmg_sha256'] and dmg.stat().st_size==reference['dmg_bytes']
    mount=root.parent/'mount';mount.mkdir();run(['/usr/bin/hdiutil','attach','-readonly','-nobrowse','-noautoopen','-plist','-mountpoint',mount,dmg],'mount.plist');mounted=True;chain=mount/'Metal.xctoolchain'
    for i,r in enumerate(reference['binaries']):
        p=(chain/r['relative']).resolve();assert p.is_relative_to(chain.resolve()) and sha(p)==r['sha256'];run(['/usr/bin/codesign','--verify','--strict','-R','=anchor apple',p],'signature-metal-'+str(i)+'.txt')
    endpoint=json.loads(Path(sys.argv[1]).read_bytes());metal=(chain/'usr/bin/metal').resolve();endpoint['metal']=dict(path=str(metal),sha256=sha(metal));configuration=root.parent/'compiler-config.json';configuration.write_text(json.dumps(endpoint));configuration.chmod(0o600)
    helper=root/'backend_client.py'
    sources=['RTXMetalLibrary.mm','RTXHostBuffer.mm','RTXMetalCommand.mm','RTXDeviceLimits.mm','RTXDeviceFeatures.mm','RTXApplicationClient.mm','RTXApplicationXPC.mm','RTXApplicationBundleEntry.mm','RTXDeviceConstruction.mm','RTXApplicationPort.mm','RTXDeviceContract.mm','RTXDeviceIdentity.mm','RTXDeviceWrapper.mm','RTXResourceFactories.mm','RTXDeviceRegistration.mm','RTXDeviceCompiler.mm','RTXNativeFunction.mm','RTXBackendCompiler.mm','bundle-load-test.mm','contract-test.mm','source-test.mm','pipeline-test.mm']
    frameworks=['-framework','Foundation','-framework','Metal','-framework','IOKit','-framework','IOSurface']
    for mode,flags in [('normal',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        opts=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fno-objc-arc','-fobjc-weak','-fblocks',*flags];objects=[]
        for name in sources:
            key=Path(name).stem+'-'+mode;obj=out/(key+'.o');dep=out/(key+'.d');objects.append(obj)
            run([*opts,'-MMD','-MF',dep,'-c',name,'-o',obj],'compile-'+key+'.txt');rows=[]
            for token in shlex.split(dep.read_text().replace(chr(92)+'\n',' '))[1:]:
                p=(root/token).resolve();assert p.is_relative_to(root);r=byname[p.relative_to(root).as_posix()];assert sha(p)==r['sha256'];rows.append(r)
            closures[key]=rows
        bundle=out/('RTXMetalApplication056-'+mode+'.bundle');contents=bundle/'Contents';(contents/'MacOS').mkdir(parents=True)
        info=dict(CFBundleIdentifier='local.emre.RTXMetalApplication056.'+mode,CFBundleName='RTXMetalApplication056',CFBundleExecutable='RTXMetalApplication',CFBundlePackageType='BNDL',CFBundleVersion='0.56.0',CFBundleShortVersionString='0.56.0',NSPrincipalClass='RTXMetalApplicationDevice056')
        (contents/'Resources').mkdir();(contents/'Resources/selected.rtxlib').write_bytes((root/'selected.rtxlib').read_bytes())
        (contents/'Info.plist').write_bytes(plistlib.dumps(info,sort_keys=True));executable=contents/'MacOS/RTXMetalApplication'
        run([*opts,'-bundle','-Wl,-U,_OBJC_CLASS_$__MTLDevice',*objects[:18],*frameworks,'-o',executable],'link-bundle-'+mode+'.txt')
        host=out/('bundle-load-test-'+mode);run([*opts,objects[18],*frameworks,'-o',host],'link-test-'+mode+'.txt')
        for kind,path in [('bundle',bundle),('host',host)]:
            run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',path],'sign-'+kind+'-'+mode+'.txt')
            run(['/usr/bin/codesign','--verify','--strict','--verbose=2',path],'signature-'+kind+'-'+mode+'.txt')
        run(['/usr/bin/otool','-L',executable],'bundle-linkage-'+mode+'.txt');run(['/usr/bin/nm','-gU',executable],'bundle-exports-'+mode+'.txt');run(['/usr/bin/otool','-L',host],'host-linkage-'+mode+'.txt')
        for test in ('explicit','named','principal'):
            name=mode+'-'+test;run([host,bundle,test,root/'selected.rtxlib',out/(name+'-result.json')],name+'.log',30);r=json.loads((out/(name+'-result.json')).read_bytes());assert r['passed'] and r['uid']==501 and r['class']=='RTXMetalApplicationDevice056' and r['entry_count']==1 and not r['gpu_commands_submitted']
        contract=out/('contract-test-'+mode);source_test=out/('source-test-'+mode)
        for name,host_test,obj in [('contract',contract,objects[19]),('source',source_test,objects[20])]:
            run([*opts,obj,*frameworks,'-o',host_test],'link-'+name+'-'+mode+'.txt')
            run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',host_test],'sign-'+name+'-'+mode+'.txt')
            run(['/usr/bin/codesign','--verify','--strict','--verbose=2',host_test],'signature-'+name+'-'+mode+'.txt')
        run([contract,bundle,root/'selected.rtxlib',root/'selection.json',out/('contract-'+mode+'.json')],'contract-'+mode+'.log',60)
        case=out/('source-'+mode);case.mkdir();run([source_test,bundle,root/'selected.rtxlib',case],'source-'+mode+'.log',90)
        assert json.loads((case/'result.json').read_bytes())['passed']
        host_pipeline=out/('pipeline-test-'+mode);run([*opts,objects[21],*frameworks,'-o',host_pipeline],'link-pipeline-'+mode+'.txt')
        run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',host_pipeline],'sign-pipeline-'+mode+'.txt')
        case=out/('pipeline-'+mode);case.mkdir();diagnostics=out/('compiler-'+mode);diagnostics.mkdir()
        run([host_pipeline,bundle,root/'selected.rtxlib',case,helper,configuration,diagnostics],'pipeline-'+mode+'.log',300)
        assert json.loads((case/'result.json').read_bytes())['passed']
        files=[dict(path=p.relative_to(bundle).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(bundle.rglob('*')) if p.is_file()];bundles.append(dict(mode=mode,path=str(bundle),files=files))
    check();snapshot('after');assert (out/'boot-before.txt').read_text().splitlines()[0]==(out/'boot-after.txt').read_text().splitlines()[0];passed=True
except Exception as e:error=repr(e);traceback.print_exc()
finally:
    if mounted:
        try:run(['/usr/bin/hdiutil','detach',root.parent/'mount'],'detach.txt');mounted=False
        except Exception as e:passed=False;error=str(error)+'; detach '+repr(e)
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'compiled-inputs.json').write_text(json.dumps(closures,indent=2)+'\n');(out/'bundle-manifest.json').write_text(json.dumps(bundles,indent=2)+'\n')
    (out/'verification.json').write_text(json.dumps(dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=passed,error=error,source_inputs=len(inputs),kext_loaded=False,gpu_commands_submitted=False,system_metal_registered=False,system_bundle_installed=False),indent=2)+'\n')
    rows=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()];(out/'artifact-manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
    archive=root.parent/'application-bundle-review.tar.gz'
    with tarfile.open(archive,'x:gz') as tar:
        for p in sorted(out.rglob('*')):
            if p.is_file():tar.add(p,arcname='mac/'+p.relative_to(out).as_posix(),recursive=False)
    print(json.dumps(dict(passed=passed,error=error,archive_sha256=sha(archive))),flush=True)
raise SystemExit(0 if passed else 1)
