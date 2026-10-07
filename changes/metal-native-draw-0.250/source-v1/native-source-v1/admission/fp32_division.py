"""Exact-rational CPU oracle for binary32 division, no host float operations."""
from fp32_oracle_v2 import SIGN, INF, QNAN, category, flush, rational, encode
from fp32_oracle_v2 import operation as basic_operation


def operation(op, a, b, *, ftz=True):
    if op != 'div':
        return basic_operation(op, a, b, ftz=ftz)
    if ftz:
        a, b = flush(a), flush(b)
    ca, cb = category(a), category(b)
    if ca == 'nan' or cb == 'nan':
        return QNAN
    sign = (a ^ b) & SIGN
    if (ca == cb == 'zero') or (ca == cb == 'inf'):
        return QNAN
    if ca == 'inf' or cb == 'zero':
        return sign | INF
    if ca == 'zero' or cb == 'inf':
        return sign
    return encode(rational(a) / rational(b), zero_sign=sign, ftz=ftz)


def self_check():
    from fp32_oracle_v2 import self_check as basic_checks
    total = basic_checks()
    cases = [
        (0x3f800000, 0x40400000, 0x3eaaaaab),
        (0x40000000, 0x40400000, 0x3f2aaaab),
        (0xbf800000, 0x40400000, 0xbeaaaaab),
        (0, 0xbf800000, SIGN), (SIGN, 0xbf800000, 0),
        (INF, 0xbf800000, SIGN | INF), (0x3f800000, SIGN | INF, SIGN),
        (0x3f800000, 0, INF), (0x3f800000, SIGN, SIGN | INF),
        (0, SIGN, QNAN), (INF, SIGN | INF, QNAN),
        (0x00800000, 0x40000000, 0), (0x80800000, 0x40000000, SIGN),
        (0x7f7fffff, 0x3f000000, INF), (0x7f800001, 0x3f800000, QNAN),
        (1, 1, QNAN), (0x3f800000, 1, INF),
    ]
    for a, b, expected in cases:
        assert operation('div', a, b) == expected, (a, b, expected)
    assert operation('div', 1, 1, ftz=False) == 0x3f800000
    assert operation('div', 0x00800000, 0x40000000, ftz=False) == 0x00400000
    assert operation('div', 3, 0x40000000, ftz=False) == 2
    assert operation('div', 1, 0x40000000, ftz=False) == 0
    return total + len(cases) + 4
