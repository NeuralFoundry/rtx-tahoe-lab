from pathlib import Path
import dataclasses,hashlib,json
import runtime_admission as R
from uploaded_library import Program
from uploaded_compiler.spirv_ptx import translate
s=Path(__file__).resolve().parent;d=s/'programs/integer210';checks=0
def check(value):
 global checks
 checks+=1;assert value,checks
def rejects(fn):
 global checks
 try:fn()
 except Exception:checks+=1
 else:raise AssertionError('invalid integer AIR transaction accepted')
manifest=(d/'runtime-pipeline.json').read_bytes();catalog,image,receipt=R.load(d,hashlib.sha256(manifest).hexdigest());p=catalog.programs[0]
check(image==(d/'compiled.rtxlib').read_bytes());check(p.name=='runtime_210_same'and p.bindings==(0,1,2)and p.local_size==(64,1,1));check(receipt['compiler_review_sha256']=='ff585b0c71c9b90054f0c37eec616eae0a5f5cc15f8e9eeb7db4a58c879e5176')
for air in (b'',b'\xff',p.air.replace(b'air.compile.fast_math_disable',b'air.compile.fast_math_enable'),p.air.replace(b'air.compile.denorms_disable',b'air.compile.denorms_enable'),p.air.replace(b'!air.compile_options =',b'!other.compile_options ='),p.air+b'\ndeclare float @air.fmod.f32(float,float)\n'):
 rejects(lambda air=air:dataclasses.replace(p,air=air))
metadata=json.loads(p.lowering)
for key,value in [('float32_arithmetic',True),('float32_denorm','preserve'),('local_size',[32,1,1]),('read_bindings',[0]),('target','sm_75')]:
 bad=dict(metadata);bad[key]=value;rejects(lambda bad=bad:dataclasses.replace(p,lowering=json.dumps(bad).encode()))
rejects(lambda:dataclasses.replace(p,ptx=p.ptx.replace(b'.u32',b'.s32',1)))
# The historical integer producer without AIR remains reproducible. It has a
# preserve lowering, not the current source producer's AIR/flush metadata.
ptx,meta=translate(p.assembly.decode(),fp32_denorm='preserve');legacy=Program(p.name,p.assembly,ptx.encode(),p.cubin,json.dumps(meta).encode());check(legacy.code==p.code and legacy.air==b'')
release=json.loads((s/'admission-release.json').read_bytes())
for row in release['programs']:
 old,container,result=R.load(s/row['path'],row['manifest_sha256']);check(hashlib.sha256(container).hexdigest()==row['container_sha256']);check(result['payload_sha256']==row['payload_sha256'])
print(json.dumps(dict(passed=True,checks=checks,actual_integer_transaction=True,legacy_integer_preserved=True,prior_float_transactions_preserved=True,gpu_executed=False)))
