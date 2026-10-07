"""Independent finite CPU interpreter for the emitted PTX instruction subset.

Diagnostic oracle, not an NVIDIA GPU, PTX conformance suite or SASS emulator.
Sparse memory permits explicit >4GiB-address and high-index checks without
allocating enormous buffers. Every read/write is traced and bounds checked.
"""
import re


def evaluate(text, gid, local, inputs, addresses):
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
        return int(x, 0)

    instructions = 0
    for line in text.splitlines():
        line = line.strip()
        if not line.endswith(';') or line.startswith('.'):
            continue
        if line == 'ret;':
            break
        op, args = line[:-1].split(None, 1)
        a = [x.strip() for x in args.split(',')]
        instructions += 1
        if op == 'ld.param.u64':
            match = re.fullmatch(r'\[arg(\d+)\]', a[1]); assert match
            result = addresses[int(match[1])]
        elif op == 'ld.global.u32':
            address = value(a[1][1:-1]); assert a[1].startswith('[') and address in memory
            trace.append(('read', address)); result = memory[address]
        elif op == 'st.global.u32':
            address = value(a[0][1:-1]); assert a[0].startswith('[') and address in memory
            trace.append(('write', address)); memory[address] = value(a[1]) & 0xffffffff; continue
        elif op in ('mov.u32', 'mov.b32', 'mov.b64', 'cvta.to.global.u64', 'cvt.u32.u64', 'cvt.u64.u32'):
            result = value(a[1])
            # cvt's destination width, not its source width.
            bits = int(op.split('.')[1][1:]) if op.startswith('cvt.') else int(op[-2:])
            result &= (1 << bits) - 1
            regs[a[0]] = result; continue
        elif op == 'setp.lt.u32' or op == 'setp.lt.u64':
            result = value(a[1]) < value(a[2]); regs[a[0]] = result; continue
        elif op.startswith('selp.u'):
            result = value(a[1]) if value(a[3]) else value(a[2])
        elif op == 'mad.lo.u32':
            result = value(a[1])*value(a[2])+value(a[3])
        else:
            x, y = value(a[1]), value(a[2])
            bits = int(op[-2:])
            if op.startswith(('shl.', 'shr.')):
                if not 0 <= y < bits: raise ValueError('undefined SPIR-V shift count')
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
