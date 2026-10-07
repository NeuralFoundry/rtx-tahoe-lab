"""Independent finite CPU interpreter for the emitted PTX instruction subset.

Diagnostic oracle, not an NVIDIA GPU, PTX conformance suite or SASS emulator.
Sparse memory permits explicit >4GiB-address and high-index checks without
allocating enormous buffers. Every read/write is traced and bounds checked.
"""
import re
from fp32_division import operation
from fp32_fma import fma
from rounding_bits import convert
from selection_oracle import compare


def evaluate(text, gid, local, inputs, addresses, flow=None, stores=None):
    assert len(gid) == len(local) == 3 and len(addresses) == len(inputs)
    regs = {}
    memory = {addresses[b] + 4*i: n for b, data in enumerate(inputs) for i, n in data.items()}
    assert len(memory) == sum(len(data) for data in inputs), 'aliased test allocations'
    trace = []

    def value(x):
        if x.startswith('%ctaid.'):
            axis = 'xyz'.index(x[-1]); return gid[axis] // local[axis]
        if x.startswith('%tid.'):
            axis = 'xyz'.index(x[-1]); return gid[axis] % local[axis]
        if x.startswith('%'):
            return regs[x]
        if re.fullmatch(r'0f[0-9a-fA-F]{8}',x):return int(x[2:],16)
        return int(x, 0)

    stream = [line.strip() for line in text.splitlines() if line.strip() and not line.strip().startswith('.')]
    labels = {line[:-1]: i for i, line in enumerate(stream) if line.endswith(':')}
    assert len(labels) == sum(line.endswith(':') for line in stream)
    instructions = pc = 0
    while pc < len(stream):
        line = stream[pc]; pc += 1
        if line.endswith(':'):
            if flow is not None: flow.append(line[:-1])
            continue
        if not line.endswith(';'): continue
        if line == 'ret;': break
        if line.startswith('@'):
            predicate, line = line.split(None, 1)
            assert line.startswith('bra ') and predicate.startswith('@%p')
            flag = value(predicate[1:]); assert type(flag) is bool
            instructions += 1; assert instructions <= 10000
            if not flag: continue
        if line.startswith('bra '):
            target = line[4:-1]; assert target in labels
            pc = labels[target]; instructions += 1; assert instructions <= 10000
            continue
        op, args = line[:-1].split(None, 1)
        a = [x.strip() for x in args.split(',')]
        instructions += 1; assert instructions <= 10000
        if op == 'ld.param.u64':
            match = re.fullmatch(r'\[arg(\d+)\]', a[1]); assert match
            result = addresses[int(match[1])]
        elif op in ('ld.global.u32','ld.global.f32'):
            address = value(a[1][1:-1]); assert a[1].startswith('[') and address in memory
            trace.append(('read', address)); result = memory[address]
        elif op in ('st.global.u32','st.global.f32'):
            address = value(a[0][1:-1]); assert a[0].startswith('[') and address in memory
            trace.append(('write', address)); memory[address] = value(a[1]) & 0xffffffff
            if stores is not None: stores.append((address,memory[address]))
            continue
        elif op in ('mov.f32', 'mov.u32', 'mov.b32', 'mov.b64', 'cvta.to.global.u64', 'cvt.u32.u64', 'cvt.u64.u32'):
            result = value(a[1])
            # cvt's destination width, not its source width.
            bits = int(op.split('.')[1][1:]) if op.startswith('cvt.') else int(op[-2:])
            result &= (1 << bits) - 1
            regs[a[0]] = result; continue
        elif re.fullmatch(r'cvt\.(rzi|rmi|rpi|rni)(?:\.ftz)?\.f32\.f32',op):
            assert len(a)==2
            result=convert(op.split('.')[1],value(a[1]),ftz='.ftz.' in op)
        elif re.fullmatch(r'fma\.rn(?:\.ftz)?\.f32',op):
            assert len(a)==4
            result=fma(value(a[1]),value(a[2]),value(a[3]),ftz='.ftz.' in op)
        elif re.fullmatch(r'(add|sub|mul|div)\.rn(?:\.ftz)?\.f32',op):
            result=operation(op.split('.')[0],value(a[1]),value(a[2]),ftz='.ftz.' in op)
        elif re.fullmatch(r'setp\.(?:eq|ne|lt|le|gt|ge|equ|neu|ltu|leu|gtu|geu|num|nan)(?:\.ftz)?\.f32',op):
            regs[a[0]]=compare(op.split('.')[1],value(a[1]),value(a[2]),flush='.ftz.' in op);continue
        elif re.fullmatch(r'setp\.(eq|ne|lt|le|gt|ge)\.u(32|64)',op):
            x,y=value(a[1]),value(a[2]);mask=(1<<int(op[-2:]))-1
            assert type(x) is int and type(y) is int and 0<=x<=mask and 0<=y<=mask
            regs[a[0]]={'eq':x==y,'ne':x!=y,'lt':x<y,'le':x<=y,'gt':x>y,'ge':x>=y}[op.split('.')[1]];continue
        elif op == 'mov.pred':
            result = value(a[1]); assert type(result) is bool; regs[a[0]] = result; continue
        elif op in ('and.pred','or.pred','xor.pred','not.pred'):
            x=value(a[1]);assert type(x) is bool
            if op=='not.pred':result=not x
            else:
                y=value(a[2]);assert type(y) is bool
                result={'and.pred':x and y,'or.pred':x or y,'xor.pred':x!=y}[op]
            regs[a[0]]=result;continue
        elif op=='selp.f32':
            regs[a[0]]=value(a[1]) if value(a[3]) else value(a[2]);continue
        elif op.startswith('selp.u'):
            result = value(a[1]) if value(a[3]) else value(a[2])
        elif op == 'mad.lo.u32':
            result = value(a[1])*value(a[2])+value(a[3])
        else:
            x, y = value(a[1]), value(a[2])
            bits = int(op[-2:])
            if op in ('add.u32', 'add.u64'): result = x+y
            elif op in ('sub.u32', 'sub.u64'): result = x-y
            elif op in ('mul.lo.u32', 'mul.lo.u64'): result = x*y
            elif op in ('and.b32', 'and.b64'): result = x&y
            elif op in ('or.b32', 'or.b64'): result = x|y
            elif op in ('xor.b32', 'xor.b64'): result = x^y
            elif op in ('shl.b32', 'shl.b64'): result = x << y if y < bits else 0
            elif op in ('shr.u32', 'shr.u64'): result = x >> y if y < bits else 0
            else: raise AssertionError('Unmodeled PTX ' + op)
        regs[a[0]] = result & ((1 << int(op[-2:]))-1)
    return memory, trace, instructions
