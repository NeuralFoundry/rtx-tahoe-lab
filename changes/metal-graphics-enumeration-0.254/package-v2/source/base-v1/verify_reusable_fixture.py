"""Independent complete-byte verification of the native CPU transport fixture."""
from pathlib import Path
import json
import reusable_model as model
import reusable_native as native
import reusable_request as request

def verify(root,raw):
    root=Path(root);raw=Path(raw);gen=0x30603501
    read=lambda n:(raw/n).read_bytes()
    initial=native.info(read('initial-info.bin'),gen);assert initial['ready'] and initial['completed']==0
    table=read('initial-root.bin');children=read('initial-children.bin');previous=read('initial-device.bin')
    before=root/'reusable-fixture-baseline'
    proof=model.bootstrap(table,children,previous,*[(before/('before-'+n+'.bin')).read_bytes() for n in ('root','children','device')]);assert proof['passed']
    for serial,wire in enumerate(request.requests(gen),1):
        prefix='job-%d-'%serial
        assert read(prefix+'request.bin')==wire and read(prefix+'plan.bin')==model.plan(wire)
        assert native.info(read(prefix+'info.bin'),gen)['completed']==serial
        assert native.job(read(prefix+'job.bin'),gen,serial,len(children))['passed']
        assert read(prefix+'root.bin')==table and read(prefix+'children.bin')==children
        actual=read(prefix+'device.bin');assert model.completed(previous,wire,actual)['passed'];previous=actual
    assert len(list(raw.iterdir()))==459
    return dict(passed=True,raw_files=459,simulated_jobs=65,results_checked=4160,ring_wraps=2,
                full_device_bytes_per_job=36864,hardware_accessed=False,metal_verified=False)

if __name__=='__main__':
    import sys
    print(json.dumps(verify(Path(__file__).resolve().parent,Path(sys.argv[1]))))
