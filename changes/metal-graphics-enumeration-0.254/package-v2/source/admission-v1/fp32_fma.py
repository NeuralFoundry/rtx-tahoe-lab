"""One-round exact-rational binary32 FMA reference, independent of lowering."""
from fp32_oracle_v2 import SIGN,INF,QNAN,category,flush,rational,encode

def fma(a,b,c,*,ftz=True):
 if ftz:a,b,c=flush(a),flush(b),flush(c)
 ca,cb,cc=category(a),category(b),category(c)
 if 'nan' in (ca,cb,cc):return QNAN
 sign=(a^b)&SIGN
 if (ca=='inf' and cb=='zero') or (cb=='inf' and ca=='zero'):return QNAN
 if ca=='inf' or cb=='inf':
  if cc=='inf' and (sign^c)&SIGN:return QNAN
  return sign|INF
 if cc=='inf':return c
 return encode(rational(a)*rational(b)+rational(c),zero_sign=sign&c&SIGN,ftz=ftz)
