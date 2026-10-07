"""External MSL arithmetic reference, never a driver execution backend."""
from fp32_division import operation
from fp32_fma import fma
from rounding_oracle import rounding

OPERATIONS=('round_vec4','round_loop','round_select')
BODIES=('float4 p=float4(a[tid],b[tid],a[tid]+1.0f,b[tid]-2.0f); float4 q=floor(p.wzyx)+trunc(p); out[tid]=fma(q.x,q.z,q.y*q.w);', 'float2 p=float2(a[tid],b[tid]); uint n=tid&3u;\n#pragma clang loop unroll(disable)\nfor(uint i=0u;i<n;++i){p=ceil(p*float2(0.5f,2.0f))+rint(p.yx);} out[tid]=p.x-p.y;', 'float2 p=float2(a[tid],b[tid]); float2 q=rint(p); out[tid]=q[tid&1u];')

def authored(name,index,a,b,flush):
 op=lambda which,x,y:operation(which,x,y,ftz=flush)
 rnd=lambda which,x:rounding(which,x,ftz=flush)
 if name=='round_vec4':
  p=(a,b,op('add',a,0x3f800000),op('sub',b,0x40000000))
  q=tuple(op('add',rnd('Floor',x),rnd('Trunc',y)) for x,y in zip(p[::-1],p))
  return [fma(q[0],q[2],op('mul',q[1],q[3]),ftz=flush)],[0,1],0
 if name=='round_loop':
  x,y=a,b
  for _ in range(index&3):x,y=op('add',rnd('Ceil',op('mul',x,0x3f000000)),rnd('RoundEven',y)),op('add',rnd('Ceil',op('mul',y,0x40000000)),rnd('RoundEven',x))
  return [op('sub',x,y)],[0,1],index&3
 if name=='round_select':return [rnd('RoundEven',(a,b)[index&1])],[0,1],0
 raise ValueError(name)
