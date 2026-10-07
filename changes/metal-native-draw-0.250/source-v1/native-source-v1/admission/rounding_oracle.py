"""Exact rational reference for scalar binary32 integral rounding."""
from fractions import Fraction
from fp32_oracle_v2 import SIGN,QNAN,category,flush,rational,encode

def rounding(name,bits,*,ftz=False):
    if name not in ('Trunc','Floor','Ceil','RoundEven') or type(bits) is not int or not 0<=bits<=0xffffffff or type(ftz) is not bool:
        raise ValueError('Rounding oracle input')
    if ftz:bits=flush(bits)
    kind=category(bits)
    if kind=='nan':return QNAN
    if kind in ('inf','zero'):return bits
    x=rational(bits);n,d=x.numerator,x.denominator
    if name=='Floor':whole=n//d
    elif name=='Ceil':whole=-((-n)//d)
    else:
        q,remainder=divmod(abs(n),d)
        if name=='RoundEven':q+=int(2*remainder>d or (2*remainder==d and q%2))
        whole=-q if n<0 else q
    return encode(Fraction(whole),zero_sign=bits&SIGN,ftz=False)
