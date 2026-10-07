"""All inherited/new CPU tests and a preserved actual-native-upload rehearsal."""
from pathlib import Path
import datetime,json,os,sys,unittest
root=Path(__file__).resolve().parent;assert len(sys.argv)==6
out=Path(sys.argv[1]).resolve();out.mkdir()
for name,value in zip(('RTX_PROGRAM_FIXTURES','RTX_REUSABLE_FIXTURES','RTX_UPLOADED_NATIVE','RTX_UPLOADED_BRIDGE'),sys.argv[2:]):os.environ[name]=str(Path(value).resolve())
os.chdir(root)
suite=unittest.defaultTestLoader.discover(str(root),pattern='test_*.py')
with (out/'tests.log').open('w') as log:result=unittest.TextTestRunner(stream=log,verbosity=2).run(suite)
r=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=result.wasSuccessful() and not result.skipped,
       tests=result.testsRun,errors=len(result.errors),failures=len(result.failures),skipped=len(result.skipped),hardware_accessed=False,metal_verified=False)
(out/'test-result.json').write_text(json.dumps(r,indent=2)+'\n')
if not r['passed']:print((out/'tests.log').read_text()[-15000:]);raise SystemExit(1)
from test_uploaded_client import Backend,setup,GEN
import gsp_uploaded_run as work
import gsp_uploaded as runner
import uploaded_request as request
b=Backend();folder=out/'rehearsal';folder.mkdir()
try:
    initial,prior,before,bootstrap=setup(b,folder);completed=work.dispatch(b,GEN,folder/'program',initial,prior,before,bootstrap)
    assert completed['passed'] and completed['results_checked']==3872 and completed['active_elements']==3872 and completed['ring_wraps']==2
    wires=request.requests(b.catalog,GEN);parsed=[request.decode(b.catalog,w) for w in wires]
    result=dict(passed=True,connection_closed=False,hardware_backend=False,generation=GEN,catalog_sha256=b.catalog.digest,
        expected_programs=[p['program'] for p in parsed],expected_invocations=sum(p['invocations'] for p in parsed),expected_outputs=3872,
        shader_upload=json.loads((folder/'shader-upload/decoded.json').read_bytes()),library_consumed=runner.uploaded_transport.codec.decode_info((folder/'consumed.bin').read_bytes()),
        init_done_observed=True,rm_exchange=dict(exchanges_verified=True),bar1=dict(passed=True),bar1_readback=dict(readback_verified=True),
        page_tables=dict(passed=True),page_rm=dict(passed=True),page_rm_exchange=dict(exchanges_verified=True),page_table_captures=dict(captures_verified=True),
        channel=dict(passed=True),execution=prior,runtime_bootstrap=initial,program=completed)
finally:b.close()
result['connection_closed']=b.closed;final=runner.finalize_result(result)
assert final['passed'] and not final['compute_verified'] and not final['metal_verified']
(out/'rehearsal-final.json').write_text(json.dumps(final,indent=2)+'\n');(out/'rehearsal-calls.json').write_text(json.dumps(b.calls,indent=2)+'\n')
print(json.dumps(r))
