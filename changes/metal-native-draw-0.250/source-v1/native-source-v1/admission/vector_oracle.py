"""Source-level local float vector oracle with exact-rational scalar arithmetic."""
from fp32_division import operation
from selection_oracle import compare
def authored(name,index,a,b,flush):
 op=lambda which,x,y:operation(which,x,y,ftz=flush)
 iterations=0
 if name=='vec2_arithmetic':
  q0=op('add',op('mul',a,a),0x3f800000)
  q1=op('add',op('mul',b,b),0xc0000000)
  answer=op('sub',q0,q1)
 elif name=='vec4_shuffle':
  p=(a,b,op('add',a,0x3f800000),op('sub',b,0x40000000))
  q=tuple(op('add',x,y) for x,y in zip(p,p[::-1]))
  answer=op('add',op('mul',q[0],q[2]),op('mul',q[1],q[3]))
 elif name=='vec3_arithmetic':
  p=(a,b,op('sub',a,b))
  q=tuple(op('mul',x,y) for x,y in zip(p,(0x40000000,0x40400000,0xbf800000)))
  answer=op('add',op('add',q[0],q[1]),q[2])
 elif name=='vec_select':
  q0=b if compare('lt',a,0,flush) else a
  q1=a if compare('lt',b,0x3f800000,flush) else b
  answer=op('add',q0,q1)
 elif name=='vec_loop':
  x,y=a,b
  for _ in range(index&3):
   x,y=op('add',op('mul',x,0x3f000000),y),op('add',op('mul',y,0x40000000),x)
   iterations+=1
  answer=op('sub',x,y)
 elif name=='vec_dynamic':
  answer=(a,b,op('add',a,b),op('sub',a,b))[index&3]
 elif name=='vec_bits':
  answer=(a,b)[index&1]
 else:raise ValueError(name)
 return [answer],[0,1],iterations
