"""Authored source behavior, independent of SPIR-V labels and PTX branches."""
from selection_oracle import compare
from fp32_division import operation

def authored(name, index, x, y, flush):
    negative = compare('lt', x, 0, flush)
    add = lambda a,b: operation('add',a,b,ftz=flush)
    mul = lambda a,b: operation('mul',a,b,ftz=flush)
    if name in ('guard','nested','early_exit') and index >= 37: return None, []
    reads = [0]
    if name in ('guard','early_exit'):
        if name == 'early_exit' and negative: return None, reads
        return add(x,y), reads+[1]
    if name == 'branch_load': return (mul(y,0x40000000), reads+[1]) if negative else (x,reads)
    if name == 'phi': return (add(mul(y,0x40000000),0x40400000),reads+[1]) if negative else (add(add(x,0x3f800000),0x40400000),reads)
    if name == 'nested': return (add(y,0x3f800000),reads+[1]) if negative else (mul(x,0x40000000),reads)
    if name == 'conditional_write': return (y,reads+[1]) if negative else (None,reads)
    raise AssertionError(name)
