"""Compare actual compiler PTX with independent source arithmetic and memory effects."""
from pathlib import Path
import json,struct
import ondemand_workload as W
from ptx_rounding_model import evaluate
from fp32_oracle_v2 import equivalent
from native_vector_oracle import authored as old_vector
h=Path(__file__).resolve().parent;checks=0
def check(v):
 global checks
 checks+=1
 if not v:raise AssertionError(checks)
def rejects(fn):
 try:fn()
 except ValueError:check(True);return
 raise AssertionError('Invalid workload accepted')
wires=W.requests(37);expected,records=W.references(wires)
check(b''.join(wires)==(h/'requests-reference.bin').read_bytes());check(expected==(h/'expected-reference.bin').read_bytes());check(records==json.loads((h/'workload-reference.json').read_bytes()))
check([sum(W.selection(i)==p for i in range(65)) for p in range(3)]==[22,22,21])
for i in (-1,65,True,'0'):rejects(lambda:W.selection(i))
for g in (0,-1,1<<64,True,'37'):rejects(lambda:W.requests(g))
for lane in (-1,64,True,'0'):rejects(lambda:W.result(0,0,0,lane))
addresses=[0x1100000000,0x2200000000,0x3300000000];instructions=written=loads=iterations=0
paths=[set() for _ in range(3)];counts=[set() for _ in range(3)];differences=[0]*3;selected_zeros=set();ptx=[(h/('reference-%d.ptx'%i)).read_text() for i in range(3)]
for i,wire in enumerate(wires):
 program=W.selection(i);ref=expected[i*2048:(i+1)*2048];check(ref[:512]==wire[64:576] and not any(ref[768:]))
 for lane in range(64):
  a,b,poison=[struct.unpack_from('<I',wire,64+binding*256+lane*4)[0] for binding in range(3)]
  writes,reads,count=W.effects(program,a,b,lane);check(len(writes)==1 and reads==[0,1]);want=writes[0]
  maps=[{-1:0xface0000+j,2048:0xbeef0000+j,lane:v} for j,v in enumerate((a,b,poison))];flow=[];stores=[]
  memory,trace,n=evaluate(ptx[program],(lane,0,0),(64,1,1),maps,addresses,flow,stores)
  check(equivalent(memory[addresses[2]+lane*4],want));check(want==struct.unpack_from('<I',ref,512+lane*4)[0])
  check(trace==[('read',addresses[0]+lane*4),('read',addresses[1]+lane*4),('write',addresses[2]+lane*4)])
  check(len(stores)==1 and stores[0][0]==addresses[2]+lane*4 and equivalent(stores[0][1],want))
  check(all(memory[addresses[j]+k*4]==v for j,m in enumerate(maps) for k,v in m.items() if (j,k)!=(2,lane)));check(want!=poison)
  baseline=old_vector(('vec4_shuffle','vec_loop','vec_bits')[program],lane,a,b,True)[0][-1];differences[program]+=not equivalent(baseline,want)
  if program==2 and want in (0,0x80000000):selected_zeros.add(want)
  instructions+=n;written+=1;loads+=2;iterations+=count;paths[program].add(tuple(flow));counts[program].add(count)
check(written==4160 and loads==8320 and iterations==2112);check([len(p) for p in paths]==[1,4,1]);check(counts==[{0},{0,1,2,3},{0}]);check(all(x>100 for x in differences));check(selected_zeros=={0,0x80000000})
for index,offset in ((0,0),(0,8),(0,12),(0,16),(1,16),(0,24),(1,24),(0,32),(0,36),(0,40),(0,832),(64,2111)):
 bad=list(wires);raw=bytearray(bad[index]);raw[offset]^=1;bad[index]=bytes(raw);rejects(lambda:W.references(bad))
rejects(lambda:W.references(wires[:-1]));rejects(lambda:W.references(tuple(wires)))
print(json.dumps(dict(passed=True,checks=checks,jobs=65,new_programs=3,replacements=3,fp32_values=written,ptx_instructions=instructions,conditional_loads=loads,unchanged_output_words=0,written_output_words=written,distinct_paths=[len(p) for p in paths],source_iteration_counts=[sorted(p) for p in counts],loop_iterations=iterations,differences_from_previous_vector_programs=differences,signed_zero_selection_checked=True,external_reference_only=True,gpu_commands_submitted=False)))
