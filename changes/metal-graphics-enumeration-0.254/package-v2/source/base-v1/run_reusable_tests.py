"""Run all baseline and reusable CPU tests, preserving both rehearsals."""
from pathlib import Path
import datetime,json,os,sys,unittest
root=Path(__file__).resolve().parent
assert len(sys.argv)==4
out=Path(sys.argv[1]).resolve();assert not out.exists();out.mkdir()
fixture=Path(sys.argv[2]).resolve();assert fixture.is_dir()
reusable=Path(sys.argv[3]).resolve();assert reusable.is_dir()
os.environ['RTX_PROGRAM_FIXTURES']=str(fixture)
os.environ['RTX_REUSABLE_FIXTURES']=str(reusable)
suite=unittest.defaultTestLoader.discover(str(root),pattern='test_*.py')
with (out/'tests.log').open('w') as log:result=unittest.TextTestRunner(stream=log,verbosity=2).run(suite)
record=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=result.wasSuccessful() and not result.skipped,
            tests=result.testsRun,errors=len(result.errors),failures=len(result.failures),skipped=len(result.skipped),hardware_accessed=False,metal_verified=False)
(out/'test-result.json').write_text(json.dumps(record,indent=2)+'\n')
if not record['passed']:print((out/'tests.log').read_text()[-12000:]);raise SystemExit(1)
from test_program_run import Backend,Run
import gsp_program_run as old
from test_program_client import GEN as OLD_GEN
backend=Backend();rehearsal=out/'baseline-rehearsal';rehearsal.mkdir()
initial,prior,before=Run().setup_run(backend,rehearsal);assert initial['passed']
completed=old.dispatch(backend,OLD_GEN,rehearsal/'work',initial,prior,before)
assert completed['passed'] and completed['active_elements']==256 and completed['programs']==[0,1,2,0]
assert not completed['hardware_accessed'] and not completed['metal_verified']
(out/'baseline-calls.json').write_text(json.dumps(backend.calls,indent=2)+'\n')
from test_reusable_client import Backend as NewBackend,setup,binding,GEN
import gsp_reusable_run as work
import gsp_reusable as runner
backend=NewBackend();rehearsal=out/'reusable-rehearsal';rehearsal.mkdir()
initial,prior,before,bootstrap=setup(backend,rehearsal);assert initial['passed']
completed=work.dispatch(backend,GEN,rehearsal/'program',initial,prior,before,bootstrap)
assert completed['passed'] and completed['active_elements']==4160 and completed['ring_wraps']==2
assert not completed['hardware_accessed'] and not completed['metal_verified']
backend.close();final=runner.finalize_result(binding(completed))
assert final['passed'] and not final['compute_verified'] and not final['metal_verified']
(out/'reusable-calls.json').write_text(json.dumps(backend.calls,indent=2)+'\n')
(out/'reusable-final.json').write_text(json.dumps(final,indent=2)+'\n')
print(json.dumps(record))
