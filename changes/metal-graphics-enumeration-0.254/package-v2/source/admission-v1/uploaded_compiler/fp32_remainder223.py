"""Inline exact binary32 remainder using integer significand reduction.

The loop reduces the dividend one exponent bit at a time. No rounded floating
division/product is used. Inputs/results optionally flush subnormals to signed
zero, matching the selected backend arithmetic mode. NaN payload is unspecified.
The maximum finite binary32 exponent gap bounds the reduction to 276 iterations.
"""

def _general(destination, first, second, register, *, flush):
    assert type(flush)is bool
    names=('x','y','ax','ay','sign','mx','my','ex','ey','shift','delta','result')
    r={n:register(('uint',32))for n in names};p=register(('bool',));q=register(('bool',))
    prefix='FREM_'+destination.removeprefix('%');label=lambda n:prefix+'_'+n
    lines=[]
    def add(text):lines.append(text.format(**r,p=p,q=q,destination=destination,first=first,second=second))
    def branch(name,pred=None):lines.append((('@'+pred+' ')if pred else'')+'bra '+label(name)+';')
    def mark(name):lines.append(label(name)+':')
    add('mov.b32 {x}, {first};');add('mov.b32 {y}, {second};')
    add('and.b32 {sign}, {x}, 0x80000000;')
    add('and.b32 {ax}, {x}, 0x7fffffff;');add('and.b32 {ay}, {y}, 0x7fffffff;')
    if flush:
        add('setp.lt.u32 {p}, {ax}, 0x00800000;');add('@{p} mov.b32 {ax}, 0;')
        add('setp.lt.u32 {p}, {ay}, 0x00800000;');add('@{p} mov.b32 {ay}, 0;')
    add('setp.ge.u32 {p}, {ax}, 0x7f800000;')
    add('setp.gt.u32 {q}, {ay}, 0x7f800000;');add('or.pred {p}, {p}, {q};')
    add('setp.eq.u32 {q}, {ay}, 0;');add('or.pred {p}, {p}, {q};');branch('nan',p)
    add('setp.lt.u32 {p}, {ax}, {ay};');branch('input',p)
    add('setp.eq.u32 {p}, {ax}, {ay};');branch('zero',p)
    for a,m,e,which in [('ax','mx','ex','x'),('ay','my','ey','y')]:
        add('shr.u32 {'+e+'}, {'+a+'}, 23;')
        add('and.b32 {'+m+'}, {'+a+'}, 0x007fffff;')
        add('setp.eq.u32 {p}, {'+e+'}, 0;');branch('subnormal_'+which,p)
        add('or.b32 {'+m+'}, {'+m+'}, 0x00800000;');branch('normalized_'+which)
        mark('subnormal_'+which)
        add('clz.b32 {shift}, {'+m+'};');add('sub.u32 {shift}, {shift}, 8;')
        add('shl.b32 {'+m+'}, {'+m+'}, {shift};');add('sub.s32 {'+e+'}, 1, {shift};')
        mark('normalized_'+which)
    add('sub.s32 {delta}, {ex}, {ey};')
    mark('loop');add('setp.eq.u32 {p}, {delta}, 0;');branch('last',p)
    add('setp.ge.u32 {p}, {mx}, {my};');add('@{p} sub.u32 {mx}, {mx}, {my};')
    add('shl.b32 {mx}, {mx}, 1;');add('sub.u32 {delta}, {delta}, 1;');branch('loop')
    mark('last');add('setp.ge.u32 {p}, {mx}, {my};');add('@{p} sub.u32 {mx}, {mx}, {my};')
    add('setp.eq.u32 {p}, {mx}, 0;');branch('zero',p)
    add('clz.b32 {shift}, {mx};');add('sub.u32 {shift}, {shift}, 8;')
    add('shl.b32 {mx}, {mx}, {shift};');add('sub.s32 {ey}, {ey}, {shift};')
    add('setp.le.s32 {p}, {ey}, 0;');branch('tiny',p)
    add('shl.b32 {result}, {ey}, 23;');add('and.b32 {mx}, {mx}, 0x007fffff;')
    add('or.b32 {result}, {result}, {mx};');branch('signed')
    mark('tiny')
    if flush:add('mov.b32 {result}, 0;')
    else:add('sub.s32 {shift}, 1, {ey};');add('shr.u32 {result}, {mx}, {shift};')
    branch('signed')
    mark('input');add('mov.b32 {result}, {ax};');branch('signed')
    mark('zero');add('mov.b32 {result}, 0;')
    mark('signed');add('or.b32 {result}, {result}, {sign};');branch('done')
    mark('nan');add('mov.b32 {result}, 0x7fc00000;')
    mark('done');add('mov.b32 {destination}, {result};')
    return lines


def emit(destination,first,second,register,*,flush):
    if not flush:return _general(destination,first,second,register,flush=False)
    # After input flushing/classification, every active operand is normal.
    # Use predicated selections for exceptional/inactive inputs; the sole branch
    # loop performs the exact significand reduction. No subnormal normalization
    # branches are necessary in the selected flush mode.
    names=('x','y','ax','ay','sign','mx','my','ex','ey','shift','delta','result','base')
    r={n:register(('uint',32))for n in names}
    invalid,active,p,zero=(register(('bool',))for _ in range(4))
    lines=[];prefix='FREM_'+destination.removeprefix('%')
    def add(text):lines.append(text.format(**r,p=p,invalid=invalid,active=active,zero=zero,destination=destination,first=first,second=second))
    add('mov.b32 {x}, {first};');add('mov.b32 {y}, {second};')
    add('and.b32 {sign}, {x}, 0x80000000;')
    add('and.b32 {ax}, {x}, 0x7fffffff;');add('and.b32 {ay}, {y}, 0x7fffffff;')
    add('setp.lt.u32 {p}, {ax}, 0x00800000;');add('@{p} mov.b32 {ax}, 0;')
    add('setp.lt.u32 {p}, {ay}, 0x00800000;');add('@{p} mov.b32 {ay}, 0;')
    add('setp.ge.u32 {invalid}, {ax}, 0x7f800000;')
    add('setp.gt.u32 {p}, {ay}, 0x7f800000;');add('or.pred {invalid}, {invalid}, {p};')
    add('setp.eq.u32 {p}, {ay}, 0;');add('or.pred {invalid}, {invalid}, {p};')
    add('setp.gt.u32 {active}, {ax}, {ay};');add('not.pred {p}, {invalid};');add('and.pred {active}, {active}, {p};')
    add('selp.u32 {x}, {ax}, 0x3f800000, {active};');add('selp.u32 {y}, {ay}, 0x3f800000, {active};')
    for a,m,e in [('x','mx','ex'),('y','my','ey')]:
        add('shr.u32 {'+e+'}, {'+a+'}, 23;');add('and.b32 {'+m+'}, {'+a+'}, 0x007fffff;');add('or.b32 {'+m+'}, {'+m+'}, 0x00800000;')
    add('sub.u32 {delta}, {ex}, {ey};')
    lines.append(prefix+'_loop:');add('setp.eq.u32 {p}, {delta}, 0;');lines.append('@'+p+' bra '+prefix+'_last;')
    add('setp.ge.u32 {p}, {mx}, {my};');add('@{p} sub.u32 {mx}, {mx}, {my};')
    add('shl.b32 {mx}, {mx}, 1;');add('sub.u32 {delta}, {delta}, 1;');lines.append('bra '+prefix+'_loop;')
    lines.append(prefix+'_last:');add('setp.ge.u32 {p}, {mx}, {my};');add('@{p} sub.u32 {mx}, {mx}, {my};')
    add('setp.eq.u32 {zero}, {mx}, 0;');add('clz.b32 {shift}, {mx};');add('sub.u32 {shift}, {shift}, 8;')
    add('shl.b32 {mx}, {mx}, {shift};');add('sub.s32 {ey}, {ey}, {shift};')
    add('shl.b32 {result}, {ey}, 23;');add('and.b32 {mx}, {mx}, 0x007fffff;');add('or.b32 {result}, {result}, {mx};')
    add('setp.le.s32 {p}, {ey}, 0;');add('or.pred {p}, {p}, {zero};');add('@{p} mov.b32 {result}, 0;')
    add('setp.eq.u32 {p}, {ax}, {ay};');add('selp.u32 {base}, 0, {ax}, {p};')
    add('selp.u32 {result}, {result}, {base}, {active};');add('or.b32 {result}, {result}, {sign};')
    add('selp.u32 {result}, 0x7fc00000, {result}, {invalid};');add('mov.b32 {destination}, {result};')
    return lines
