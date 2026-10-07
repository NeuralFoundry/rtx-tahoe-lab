from pathlib import Path
import subprocess,json,hashlib,datetime,re,unittest,sys
root=Path.cwd();out=root/'results/gsp-execution-development/application';assert not out.exists();out.mkdir()
def run(args,name):
    with (out/name).open('w') as log:subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)
programs=[('entry','entry/test_runtime_entry.cpp'),('runtime','runtime/test_runtime_native.cpp'),('owner','runtime/test_runtime_owner.cpp')]
prefix='changes/gsp-application-runtime-0.32/';fixture='changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/'
for name,source in programs:
    run(['xcrun','clang++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',prefix+source,'-o',str(out/name)],name+'-compile.log')
    dest=out/(name+'-data');dest.mkdir()
    args=[str(out/name)] if name=='owner' else [str(out/name),fixture+'record-011.bin',fixture+'record-010.bin',str(dest)]
    run(args,name+('.txt' if name=='owner' else '.json'))
    component='entry' if name=='entry' else 'runtime'
    expected=(root/prefix/component/'windows'/('owner.txt' if name=='owner' else 'native.json')).read_text()
    actual=(out/(name+('.txt' if name=='owner' else '.json'))).read_text()
    assert actual.strip()==expected.strip(),name
    for p in dest.glob('*.bin'):assert p.read_bytes()==(root/prefix/component/'windows'/p.name).read_bytes(),p.name
import test_gsp_application_entry as entry_tests
import test_gsp_application_run as runner_tests
entry_tests.FIXTURE=out/'entry-data'
suite=unittest.TestSuite([unittest.defaultTestLoader.loadTestsFromModule(m) for m in (entry_tests,runner_tests)])
with (out/'mac-native-client-tests.log').open('w') as log:result=unittest.TextTestRunner(stream=log,verbosity=2).run(suite)
assert result.wasSuccessful() and not result.skipped and result.testsRun==21
record=dict(passed=True,cpp_programs=3,python_tests=0,replayed_python_tests=result.testsRun,cpp_sanitizers=['address','undefined'],hardware_accessed=False,kext_built=False,metal_verified=False,entry=json.loads((out/'entry.json').read_text()),runtime=json.loads((out/'runtime.json').read_text()))
(out.parent/'application-extra-tests.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))