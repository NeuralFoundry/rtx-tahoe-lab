"""Replay the actual calendar-boundary failure and reject changed GPU state."""
from pathlib import Path
import base64,copy,json,plistlib,re
from BuildHostState import validate,identity
h=Path(__file__).resolve().parent
fixture=json.loads((h/'build-host-fixture.json').read_bytes())
boot,loaded,probe=[base64.b64decode(fixture[n],validate=True) for n in ('boot','loaded','probe')]
baseline=validate(boot,loaded,probe);checks=1;rejected=0;variants=0
assert baseline['boot_seconds']==1789011183 and baseline['boot_microseconds']==55975
for seconds,microseconds in [(1789011182,975697),(1789011183,55975),(1789014783,999999),(1789007583,0)]:
    changed=re.sub(rb'sec = [0-9]+, usec = [0-9]+',('sec = %d, usec = %d'%(seconds,microseconds)).encode(),boot)
    observed=validate(changed,loaded,probe)
    assert identity(observed)==identity(baseline) and observed['boot_seconds']==seconds and observed['boot_microseconds']==microseconds
    checks+=1;variants+=1
def reject(b=boot,l=loaded,p=probe):
    global checks,rejected
    try:validate(b,l,p)
    except ValueError:checks+=1;rejected+=1;return
    raise AssertionError('Accepted changed host')
reject(b=boot.replace(b'96A304DA',b'86A304DA'))
reject(b=boot+boot)
reject(b=boot.replace(b'kern.bootsessionuuid',b'kern.other'))
reject(b=boot.replace(b'usec = 55975',b'usec = 1000000'))
reject(b=boot.replace(b'sec = 1789011183',b'sec = 0'))
reject(l=loaded.replace(b'local.emre.RTXProbe (0.75.0)',b'local.emre.RTXProbe (0.75.1)'))
reject(l=loaded.replace(b'AMDRadeon',b'NoAMD'))
reject(l=loaded+b'RTXMetalAccelerator')
rows=plistlib.loads(probe)
for key,value in baseline['properties'].items():
    altered=copy.deepcopy(rows);altered[0][key]=not value if type(value) is bool else value+1 if type(value) is int else value+'-changed'
    reject(p=plistlib.dumps(altered))
    if type(value) is bool:
        altered=copy.deepcopy(rows);altered[0][key]=int(value);reject(p=plistlib.dumps(altered))
reject(p=plistlib.dumps([]));reject(p=plistlib.dumps(rows+rows))
assert baseline['prior_kernel_retained'] and not baseline['candidate_loaded']
print(json.dumps(dict(passed=True,checks=checks,calendar_variants=variants,rejected_host_mutations=rejected,boot_uuid_and_registry_state_required=True,calendar_time_not_reset_evidence=True,gpu_commands_submitted=False)))
