"""Source-level bounded loop outcomes, using exact-rational binary32 arithmetic.

Read order describes the inspected Apple frontend's legal immutable-load
placement. Stores describe every source-visible write, including repetitions.
"""
from fp32_division import operation
from selection_oracle import compare

def authored(name,index,a,b,flush):
    n=index&7
    x=a
    writes=[]
    iterations=0
    if name=='loop_write':
        reads=[1]+([0] if n else [])
        for _ in range(n):
            if compare('lt',x,0,flush):writes.append(x)
            x=operation('add',x,b,ftz=flush)
            iterations+=1
    else:
        reads=[0]+([1] if name!='while_read' or n else [])
        if name in ('counted','while_read'):
            for _ in range(n):
                x=operation('add',x,b,ftz=flush);iterations+=1
        elif name=='break_loop':
            for _ in range(8):
                if compare('lt',x,0,flush):break
                x=operation('sub',x,b,ftz=flush);iterations+=1
        elif name=='continue_loop':
            for i in range(n):
                iterations+=1
                if i<2:continue
                x=operation('add',x,b,ftz=flush)
        elif name=='nested_loop':
            for _ in range(index&3):
                for _ in range(3):
                    x=operation('add',x,b,ftz=flush);iterations+=1
        else:raise ValueError(name)
        writes.append(x)
    return writes,reads,iterations
