"""Bit-field PTX conversion model, independent of rational source oracle."""
def convert(mode,bits,ftz=False):
    assert mode in ('rzi','rmi','rpi','rni') and type(bits) is int and 0<=bits<=0xffffffff and type(ftz) is bool
    negative=bits>>31;sign=bits&0x80000000;exp=(bits>>23)&255;fraction=bits&0x7fffff
    if exp==255:return bits|0x400000 if fraction else bits
    if ftz and exp==0:return sign
    if bits&0x7fffffff==0 or exp>=150:return bits
    if exp<127:
        bump=(mode=='rmi' and negative) or (mode=='rpi' and not negative) or (mode=='rni' and exp==126 and fraction!=0)
        return sign|(0x3f800000 if bump else 0)
    shift=150-exp;mask=(1<<shift)-1;rest=bits&mask;base=bits&~mask
    increment=bool(rest) and ((mode=='rmi' and negative) or (mode=='rpi' and not negative))
    if mode=='rni':increment=rest>(1<<(shift-1)) or (rest==(1<<(shift-1)) and (base>>shift)&1)
    return base+((1<<shift) if increment else 0)
