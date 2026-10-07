"""Binary32 comparisons using integer bit ordering, with no host FP operations."""
RELATIONS=('eq','equ','ne','neu','lt','ltu','le','leu','gt','gtu','ge','geu','num','nan')

def compare(relation,a,b,flush=False):
    if relation not in RELATIONS or any(type(x) is not int or not 0<=x<=0xffffffff for x in (a,b)):
        raise ValueError('Comparison input')
    if flush:
        if a&0x7f800000==0:a &= 0x80000000
        if b&0x7f800000==0:b &= 0x80000000
    unordered=(a&0x7fffffff)>0x7f800000 or (b&0x7fffffff)>0x7f800000
    if relation=='num':return not unordered
    if relation=='nan':return unordered
    if unordered:return relation.endswith('u')
    if a&0x7fffffff==0 and b&0x7fffffff==0:a=b=0
    # Negative encodings descend as numerical values rise; positives ascend.
    ka=(~a&0xffffffff) if a>>31 else a|0x80000000
    kb=(~b&0xffffffff) if b>>31 else b|0x80000000
    op=relation.removesuffix('u')
    return {'eq':ka==kb,'ne':ka!=kb,'lt':ka<kb,'le':ka<=kb,'gt':ka>kb,'ge':ka>=kb}[op]

def authored(name,a,b,flush=False):
    # The source ternary copies original input bits even when the comparison
    # flushes subnormals. No min/max substitution or NaN canonicalization.
    if name=='range':condition=compare('ge',a,0,flush) and compare('lt',a,b,flush)
    elif name=='outside':condition=compare('lt',a,0,flush) or compare('gt',a,b,flush)
    elif name.startswith('not'):condition=not compare(name[3:],a,b,flush)
    else:condition=compare('neu' if name=='ne' else name,a,b,flush)
    return a if condition else b
