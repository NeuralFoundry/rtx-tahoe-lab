"""Compiler specialization shape. This is not a resource/mapping admission."""
import math
def validate_local(value):
 if type(value) not in (list,tuple) or len(value)!=3 or any(type(x)is not int for x in value):raise ValueError('three integer dimensions required')
 if not(1<=value[0]<=1024 and 1<=value[1]<=1024 and 1<=value[2]<=64 and math.prod(value)<=1024):raise ValueError('sm_86 local dimensions or block product')
 return list(value)
def validate_dispatch(groups,local):
 local=validate_local(local)
 if type(groups) not in (list,tuple)or len(groups)!=3 or any(type(x)is not int for x in groups):raise ValueError('three integer group dimensions required')
 if not(1<=groups[0]<=0x7fffffff and 1<=groups[1]<=0xffff and 1<=groups[2]<=0xffff):raise ValueError('sm_86 grid dimensions')
 grid=[x*y for x,y in zip(groups,local)]
 if any(n>0xffffffff for n in grid)or math.prod(grid)>0xffffffffffffffff:raise ValueError('global index or invocation overflow')
 return dict(local_size=local,threadgroups=list(groups),grid=grid,invocations=math.prod(grid))
