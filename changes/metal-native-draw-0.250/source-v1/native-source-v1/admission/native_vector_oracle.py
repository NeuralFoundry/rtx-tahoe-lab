"""Source references for vector programs compiled with native contraction.

This module is only test data generation, never a driver execution backend.
"""
from fp32_division import operation
from fp32_fma import fma
from vector_oracle import authored as separate

def authored(name,index,a,b,flush):
 op=lambda which,x,y:operation(which,x,y,ftz=flush)
 if name=='vec4_shuffle':
  p=(a,b,op('add',a,0x3f800000),op('sub',b,0x40000000))
  q=tuple(op('add',x,y) for x,y in zip(p,p[::-1]))
  return [fma(q[0],q[2],op('mul',q[1],q[3]),ftz=flush)],[0,1],0
 if name=='vec_loop':
  x,y=a,b
  for _ in range(index&3):x,y=fma(x,0x3f000000,y,ftz=flush),fma(y,0x40000000,x,ftz=flush)
  return [op('sub',x,y)],[0,1],index&3
 if name=='vec_bits':return separate(name,index,a,b,flush)
 raise ValueError(name)
