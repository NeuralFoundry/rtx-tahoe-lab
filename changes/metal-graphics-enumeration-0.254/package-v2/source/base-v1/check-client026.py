"""Check the client-only version fix; never open a real IOKit connection."""
import datetime,hashlib,json,pathlib,unittest
root=pathlib.Path(__file__).resolve().parent
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((root/'client-inputs.json').read_text())
for row in manifest['files']:assert digest(root/row['path'])==row['sha256'],row['path']
out=root/'client026-reports';out.mkdir(exist_ok=True)
record=dict(passed=False,hardware_accessed=False,input_count=len(manifest['files']),manifest_sha256=digest(root/'client-inputs.json'))
(out/'verification.json').write_text(json.dumps(record,indent=2)+'\n')
with (out/'python-tests.log').open('w') as log:
 result=unittest.TextTestRunner(stream=log,verbosity=2).run(unittest.defaultTestLoader.discover(str(root)))
assert result.wasSuccessful() and not result.skipped and result.testsRun==500
for row in manifest['files']:assert digest(root/row['path'])==row['sha256'],row['path']
record.update(passed=True,python_tests=result.testsRun,failures=len(result.failures),errors=len(result.errors),
              skipped=len(result.skipped),native_binary_changed=False,recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
(out/'verification.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
