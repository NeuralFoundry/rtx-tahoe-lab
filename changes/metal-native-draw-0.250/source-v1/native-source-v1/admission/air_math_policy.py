"""Do not promote unreviewed remainder or signed-zero frontend lowerings.

The pinned frontend lowers air.fmod to rounded division/truncation/product/
subtraction. Enabling Trunc makes that sequence compilable, but does not prove
the 0-ulp strict fmod contract. Keep that family rejected until separately fixed
and reviewed. This is about intrinsic semantics, never source or entry names.
"""
import re

def require_reviewed_math(text):
    if type(text) is not str or not 0<len(text.encode('utf-8'))<=131072:
        raise ValueError('AIR math input bounds')
    for quoted,plain in re.findall(r'@(?:"([^"\r\n]*)"|([A-Za-z0-9_.$-]+))',text):
        name=re.sub(r'\\([0-9a-fA-F]{2})',lambda m:chr(int(m[1],16)),quoted) if quoted else plain
        if name.startswith(('air.round.','air.fast_round.')):
            raise ValueError('Unreviewed AIR round lowering; negative-zero preservation not established')
        if name.startswith(('air.fmod.','air.fast_fmod.')):
            raise ValueError('Unreviewed AIR remainder lowering; strict fmod accuracy not established')
