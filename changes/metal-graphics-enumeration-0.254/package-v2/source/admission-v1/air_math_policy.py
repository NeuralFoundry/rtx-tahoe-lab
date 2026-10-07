"""Retain rejections for unreviewed fast/width/rounding intrinsics.

The223 frontend preserves strict binary32 air.fmod as OpFRem, and the PTX
backend reduces significands exactly. Fast/other-width remainder and round
lowerings remain unreviewed. Entry/source names never select behavior.
"""
import re

def require_reviewed_math(text):
    if type(text) is not str or not 0<len(text.encode('utf-8'))<=131072:
        raise ValueError('AIR math input bounds')
    for quoted,plain in re.findall(r'@(?:"([^"\r\n]*)"|([A-Za-z0-9_.$-]+))',text):
        name=re.sub(r'\\([0-9a-fA-F]{2})',lambda m:chr(int(m[1],16)),quoted) if quoted else plain
        if name.startswith(('air.round.','air.fast_round.')):
            raise ValueError('Unreviewed AIR round lowering; negative-zero preservation not established')
        if name.startswith(('air.fmod.','air.fast_fmod.')) and not re.fullmatch(r'air\.fmod\.(?:v[234])?f32',name):
            raise ValueError('Unreviewed AIR remainder lowering; strict fmod accuracy not established')
