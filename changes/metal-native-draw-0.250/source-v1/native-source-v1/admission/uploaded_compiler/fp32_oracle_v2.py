"""Independent exact-rational binary32 oracle. No host floating-point arithmetic.

Models round-to-nearest ties-to-even add/sub/mul, optional signed FTZ on input
and the exact result before rounding, and noncontracted multiply/add. NaN payload/sign are not compared.
This is a CPU diagnostic, not evidence that NVIDIA hardware executed a shader.
"""
from fractions import Fraction

SIGN=0x80000000
INF=0x7f800000
QNAN=0x7fc00000

def category(bits):
    assert isinstance(bits,int) and 0<=bits<=0xffffffff
    e=(bits>>23)&255;m=bits&0x7fffff
    return ('nan' if m else 'inf') if e==255 else ('normal' if e else 'subnormal' if m else 'zero')

def flush(bits):
    return bits&SIGN if category(bits)=='subnormal' else bits

def rational(bits):
    c=category(bits);assert c not in ('nan','inf')
    e=(bits>>23)&255;m=bits&0x7fffff
    if e:m|=1<<23
    exponent=e-150 if e else -149
    n=Fraction(m<<exponent,1) if exponent>=0 else Fraction(m,1<<-exponent)
    return -n if bits&SIGN else n

def power2(exponent):
    return Fraction(1<<exponent,1) if exponent>=0 else Fraction(1,1<<-exponent)

def rounded_integer(value):
    assert value>=0
    whole,remainder=divmod(value.numerator,value.denominator)
    twice=remainder*2
    return whole+int(twice>value.denominator or (twice==value.denominator and whole&1))

def encode(value,*,zero_sign=0,ftz=True):
    assert isinstance(value,Fraction)
    if not value:return zero_sign&SIGN
    sign=SIGN if value<0 else 0;value=abs(value)
    e=value.numerator.bit_length()-value.denominator.bit_length()
    if value<power2(e):e-=1
    if e<-126:
        if ftz:return sign
        significand=rounded_integer(value/power2(-149))
        assert 0<=significand<=1<<23
        if significand==1<<23:return sign|0x00800000
        return sign if ftz else sign|significand
    if e>127:return sign|INF
    significand=rounded_integer(value/power2(e-23))
    assert 1<<23<=significand<=1<<24
    if significand==1<<24:e+=1;significand>>=1
    if e>127:return sign|INF
    return sign|((e+127)<<23)|(significand-(1<<23))

def operation(op,a,b,*,ftz=True):
    assert op in ('add','sub','mul')
    if ftz:a=flush(a);b=flush(b)
    if op=='sub':b^=SIGN;op='add'
    ca,cb=category(a),category(b)
    if ca=='nan' or cb=='nan':return QNAN
    if op=='mul':
        sign=(a^b)&SIGN
        if (ca=='inf' and cb=='zero') or (cb=='inf' and ca=='zero'):return QNAN
        if ca=='inf' or cb=='inf':return sign|INF
        return encode(rational(a)*rational(b),zero_sign=sign,ftz=ftz)
    if ca=='inf' or cb=='inf':
        if ca==cb=='inf' and (a^b)&SIGN:return QNAN
        return a if ca=='inf' else b
    return encode(rational(a)+rational(b),zero_sign=a&b&SIGN,ftz=ftz)

def program(name,a,b,*,ftz=True):
    if name=='chain':return operation('add',operation('mul',a,b,ftz=ftz),a,ftz=ftz)
    return operation({'fadd':'add','fsub':'sub','fmul':'mul'}[name],a,b,ftz=ftz)

def equivalent(a,b):
    # Require a quiet NaN for the arithmetic result; payload and sign may vary.
    if category(a)=='nan':return category(b)=='nan' and bool(b&0x00400000)
    return a==b

def self_check():
    cases=[('add',0x3f800000,0x33800000,0x3f800000),
           ('add',0x3f800001,0x33800000,0x3f800002),
           ('add',0x3f800000,0xbf800000,0),('add',SIGN,SIGN,SIGN),
           ('sub',SIGN,0,SIGN),('mul',0,0xbf800000,SIGN),
           ('mul',0x00800000,0x3f000000,0),('mul',0x80800000,0x3f000000,SIGN),
           ('mul',0x7f7fffff,0x40000000,INF),('add',1,0,0),
           ('mul',INF,0,QNAN),('add',INF,SIGN|INF,QNAN)]
    for op,a,b,want in cases:assert operation(op,a,b)==want,(op,a,b)
    assert operation('mul',0x00800000,0x3f000000,ftz=False)==0x00400000
    assert operation('add',1,0,ftz=False)==1
    # Exact subnormal/normal and overflow halfway boundaries.
    assert encode(power2(-149)/2,ftz=False)==0
    assert encode(power2(-149)*Fraction(3,2),ftz=False)==2
    assert encode(power2(-126)-power2(-150),ftz=False)==0x00800000
    assert encode(power2(-126)-power2(-150),ftz=True)==0
    assert encode(power2(128)-power2(103),ftz=False)==INF
    return len(cases)+7

if __name__=='__main__':print('oracle known-answer checks:',self_check())
