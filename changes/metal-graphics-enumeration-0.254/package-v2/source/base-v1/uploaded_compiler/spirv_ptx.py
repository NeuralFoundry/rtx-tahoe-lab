"""Closed, straight-line integer SPIR-V assembly subset to sm_86 PTX.

Input must be produced by spirv-dis from a spirv-val validated binary. This
module additionally rejects unsupported types, instructions and decorations.
No source/entry names select behavior. No GPU execution or resource allocation.
Whole workgroups and sufficient non-aliasing buffer ranges are caller contracts.
"""
import shlex


class Unsupported(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise Unsupported(message)


def translate(text):
    require(type(text) is str and len(text.encode()) <= 131072, 'module limit')
    rows = []
    definitions = set()
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(';'):
            continue
        tokens = shlex.split(line)
        dest = None
        if len(tokens) >= 3 and tokens[1] == '=':
            dest, tokens = tokens[0], tokens[2:]
            require(dest.startswith('%') and dest not in definitions, 'duplicate result')
            definitions.add(dest)
        require(tokens, 'empty instruction')
        rows.append((dest, tokens[0], tokens[1:]))
    require(0 < len(rows) <= 2048, 'instruction limit')
    types, constants, variables, decorations, members = {}, {}, {}, {}, {}
    body = []
    entry = mode = function = None
    capabilities = set()
    memory_models = 0
    phase = 'header'
    for dest, op, a in rows:
        if op == 'OpFunction':
            require(phase == 'header' and len(a) == 3 and a[1] == 'None', 'function shape')
            function = (dest, a)
            phase = 'function'
            continue
        if op == 'OpFunctionEnd':
            require(phase == 'function' and not a and dest is None, 'function end')
            phase = 'done'
            continue
        if phase == 'function':
            body.append((dest, op, a))
            continue
        require(phase == 'header', 'trailing instruction')
        if op == 'OpCapability':
            require(dest is None and len(a) == 1 and a[0] in ('Shader', 'Int64'), 'capability')
            require(a[0] not in capabilities, 'duplicate capability')
            capabilities.add(a[0])
        elif op == 'OpMemoryModel':
            require(dest is None and a == ['Logical', 'GLSL450'], 'memory model')
            memory_models += 1
        elif op == 'OpEntryPoint':
            require(dest is None and entry is None and len(a) >= 4 and a[0] == 'GLCompute', 'entry')
            entry = a
        elif op == 'OpExecutionMode':
            require(dest is None and mode is None and len(a) == 5 and a[1] == 'LocalSize', 'mode')
            mode = a
        elif op == 'OpSource':
            require(dest is None and a == ['Unknown', '0'], 'source annotation')
        elif op == 'OpName':
            require(dest is None and len(a) == 2 and a[0] in definitions, 'name annotation')
        elif op == 'OpDecorate':
            require(dest is None and len(a) >= 2 and a[0] in definitions, 'decoration target')
            d = decorations.setdefault(a[0], {})
            require(a[1] not in d, 'duplicate decoration')
            d[a[1]] = a[2:]
        elif op == 'OpMemberDecorate':
            require(dest is None and len(a) == 4 and a[0] not in members and a[1:] == ['0', 'Offset', '0'], 'member layout')
            members[a[0]] = 0
        elif op in ('OpTypeVoid', 'OpTypeBool', 'OpTypeInt', 'OpTypeRuntimeArray',
                    'OpTypeStruct', 'OpTypePointer', 'OpTypeVector', 'OpTypeFunction'):
            require(dest is not None, 'type result')
            if op == 'OpTypeVoid':
                require(not a, 'void'); t = ('void',)
            elif op == 'OpTypeBool':
                require(not a, 'bool'); t = ('bool',)
            elif op == 'OpTypeInt':
                require(len(a) == 2 and a[0] in ('32', '64') and a[1] == '0', 'unsigned width')
                t = ('uint', int(a[0]))
            elif op == 'OpTypeVector':
                require(len(a) == 2 and types.get(a[0]) == ('uint', 32) and a[1] == '3', 'vector type')
                t = ('gid3', a[0])
            elif op == 'OpTypeRuntimeArray':
                require(len(a) == 1 and types.get(a[0]) == ('uint', 32), 'array element')
                t = ('array', a[0])
            elif op == 'OpTypeStruct':
                require(len(a) == 1 and types.get(a[0], ('',))[0] == 'array', 'struct type')
                t = ('struct', a[0])
            elif op == 'OpTypePointer':
                require(len(a) == 2 and a[1] in types and a[0] in ('Input', 'StorageBuffer'), 'pointer type')
                t = ('pointer', a[0], a[1])
            else:
                require(len(a) == 1 and types.get(a[0]) == ('void',), 'function type')
                t = ('function', a[0])
            types[dest] = t
        elif op == 'OpConstant':
            require(dest is not None and len(a) == 2 and types.get(a[0], ('',))[0] == 'uint', 'constant type')
            try:
                value = int(a[1], 0)
            except ValueError as e:
                raise Unsupported('constant literal') from e
            require(0 <= value < 1 << types[a[0]][1], 'constant range')
            constants[dest] = (a[0], value)
        elif op == 'OpVariable':
            require(dest is not None and len(a) == 2 and types.get(a[0], ('',))[0] == 'pointer', 'global variable')
            require(types[a[0]][1] == a[1], 'storage class')
            variables[dest] = (a[0], a[1])
        else:
            raise Unsupported('header instruction ' + op)
    require(phase == 'done' and memory_models == 1 and 'Shader' in capabilities, 'module layout')
    require(entry and mode and function and entry[1] == mode[0] == function[0], 'entry linkage')
    require(types.get(function[1][0]) == ('void',) and
            types.get(function[1][2]) == ('function', function[1][0]), 'entry signature')
    try:
        local_size = [int(n) for n in mode[2:]]
    except ValueError as e:
        raise Unsupported('local size') from e
    require(1 <= local_size[0] <= 1024 and local_size[1:] == [1, 1], 'one-dimensional local size')
    require(len(entry[3:]) == len(variables) and set(entry[3:]) == set(variables), 'entry interface')
    bindings, builtin = {}, None
    for var, (ty, storage) in variables.items():
        t = types[ty]; d = decorations.get(var, {})
        if storage == 'Input':
            require(builtin is None and types[t[2]][0] == 'gid3' and d == {'BuiltIn': ['GlobalInvocationId']}, 'builtin')
            builtin = var
        else:
            require(types[t[2]][0] == 'struct' and set(d) <= {'DescriptorSet', 'Binding', 'NonWritable', 'NonReadable'}, 'buffer layout')
            require(d.get('DescriptorSet') == ['0'] and len(d.get('Binding', [])) == 1, 'descriptor')
            for access in ('NonWritable', 'NonReadable'):
                require(access not in d or d[access] == [], 'access decoration')
            try:
                binding = int(d['Binding'][0])
            except ValueError as e:
                raise Unsupported('binding number') from e
            require(0 <= binding < 32 and binding not in bindings.values(), 'binding range/duplicate')
            bindings[var] = binding
    require(builtin is not None and 1 <= len(bindings) <= 8, 'interface bounds')
    for tid, t in types.items():
        d = decorations.get(tid, {})
        if t[0] == 'array':
            require(d == {'ArrayStride': ['4']}, 'array stride')
        elif t[0] == 'struct':
            require(d == {'Block': []} and tid in members, 'block layout')
        else:
            require(not d, 'type decoration')
    require(set(members) <= {tid for tid, t in types.items() if t[0] == 'struct'}, 'member target')
    require(set(decorations) <= set(types) | set(variables), 'unhandled decoration')
    require(body and body[0][1:] == ('OpLabel', []) and body[-1] == (None, 'OpReturn', []), 'single block/return')

    code, regs, values, value_types, pointers = [], [], {}, {}, {}
    read_bindings, written_bindings = set(), set()

    def register(t):
        require(t[0] in ('uint', 'bool'), 'register type')
        prefix, decl = ('p', '.pred') if t[0] == 'bool' else ('r', '.b' + str(t[1]))
        name = '%' + prefix + str(len(regs))
        regs.append((decl, name))
        return name

    def scalar(key):
        require(key in values and types[value_types[key]][0] in ('uint', 'bool'), 'scalar operand')
        return values[key], types[value_types[key]]

    for key, (ty, value) in constants.items():
        values[key] = str(value); value_types[key] = ty
    ordered = sorted(bindings, key=bindings.get)
    for index, var in enumerate(ordered):
        r = register(('uint', 64)); base = register(('uint', 64))
        code += [f'ld.param.u64 {r}, [arg{index}];', f'cvta.to.global.u64 {base}, {r};']
        pointers[var] = ('root', bindings[var], base)
    gid = []
    for axis in 'xyz':
        cta = register(('uint', 32)); tid = register(('uint', 32)); r = register(('uint', 32))
        code += [f'mov.u32 {cta}, %ctaid.{axis};', f'mov.u32 {tid}, %tid.{axis};',
                 f'mad.lo.u32 {r}, {cta}, {local_size["xyz".index(axis)]}, {tid};']
        gid.append(r)
    for dest, op, a in body[1:-1]:
        if op == 'OpStore':
            require(dest is None and len(a) == 2 and a[0] in pointers, 'store')
            p = pointers[a[0]]; r, t = scalar(a[1])
            require(p[0] == 'element' and t == ('uint', 32), 'store element')
            var = next(k for k, v in bindings.items() if v == p[1])
            require('NonWritable' not in decorations[var], 'readonly store')
            code.append(f'st.global.u32 [{p[2]}], {r};'); written_bindings.add(p[1])
            continue
        require(dest is not None and a and a[0] in types, 'typed result')
        ty = a[0]; t = types[ty]; value_types[dest] = ty
        if op == 'OpLoad':
            require(len(a) == 2, 'memory operands unsupported')
            if a[1] == builtin:
                require(t[0] == 'gid3', 'builtin load'); values[dest] = tuple(gid); continue
            require(a[1] in pointers and pointers[a[1]][0] == 'element' and t == ('uint', 32), 'buffer load')
            p = pointers[a[1]]; var = next(k for k, v in bindings.items() if v == p[1])
            require('NonReadable' not in decorations[var], 'writeonly load')
            r = register(t); code.append(f'ld.global.u32 {r}, [{p[2]}];'); read_bindings.add(p[1])
        elif op == 'OpCompositeExtract':
            require(len(a) == 3 and a[1] in values and type(values[a[1]]) is tuple and a[2] in ('0', '1', '2') and t == ('uint', 32), 'composite extract')
            r = values[a[1]][int(a[2])]
        elif op in ('OpAccessChain', 'OpInBoundsAccessChain'):
            require(len(a) == 4 and a[1] in pointers and pointers[a[1]][0] == 'root' and
                    a[2] in constants and constants[a[2]][1] == 0 and t[0:2] == ('pointer', 'StorageBuffer') and types[t[2]] == ('uint', 32), 'access chain')
            idx, it = scalar(a[3]); require(it[0] == 'uint', 'index type')
            index = register(('uint', 64)); offset = register(('uint', 64)); r = register(('uint', 64))
            if it[1] == 32:
                code.append(f'cvt.u64.u32 {index}, {idx};')
            else:
                code.append(f'mov.b64 {index}, {idx};')
            p = pointers[a[1]]
            code += [f'mul.lo.u64 {offset}, {index}, 4;', f'add.u64 {r}, {p[2]}, {offset};']
            pointers[dest] = ('element', p[1], r)
        elif op in ('OpCopyObject', 'OpUConvert'):
            require(len(a) == 2, 'unary shape'); src, st = scalar(a[1])
            if op == 'OpCopyObject':
                require(t == st, 'copy type'); r = src
            else:
                require(t[0] == st[0] == 'uint', 'unsigned conversion'); r = register(t)
                if t == st:
                    code.append(f'mov.b{t[1]} {r}, {src};')
                else:
                    code.append(f'cvt.u{t[1]}.u{st[1]} {r}, {src};')
        elif op == 'OpSelect':
            require(len(a) == 4, 'select shape')
            cond, ct = scalar(a[1]); x, xt = scalar(a[2]); y, yt = scalar(a[3])
            require(ct == ('bool',) and t == xt == yt and t[0] == 'uint', 'select types')
            r = register(t); code.append(f'selp.u{t[1]} {r}, {x}, {y}, {cond};')
        elif op in ('OpIAdd', 'OpISub', 'OpIMul', 'OpBitwiseAnd', 'OpBitwiseOr', 'OpBitwiseXor',
                    'OpShiftLeftLogical', 'OpShiftRightLogical', 'OpULessThan'):
            require(len(a) == 3, 'binary shape'); x, xt = scalar(a[1]); y, yt = scalar(a[2])
            require(xt[0] == yt[0] == 'uint', 'unsigned operands')
            r = register(t)
            if op == 'OpULessThan':
                require(t == ('bool',) and xt == yt, 'comparison types')
                code.append(f'setp.lt.u{xt[1]} {r}, {x}, {y};')
            else:
                require(t == xt, 'result type')
                if op in ('OpShiftLeftLogical', 'OpShiftRightLogical'):
                    # PTX shift count is b32, even for a b64 value. SPIR-V
                    # counts >= value width are undefined, not valid test inputs.
                    if yt[1] == 64:
                        yr = register(('uint', 32)); code.append(f'cvt.u32.u64 {yr}, {y};'); y = yr
                    instruction = ('shl.b' if op == 'OpShiftLeftLogical' else 'shr.u') + str(t[1])
                else:
                    require(t == yt, 'binary operand types')
                    instruction = {'OpIAdd': 'add.u', 'OpISub': 'sub.u', 'OpIMul': 'mul.lo.u',
                                   'OpBitwiseAnd': 'and.b', 'OpBitwiseOr': 'or.b', 'OpBitwiseXor': 'xor.b'}[op] + str(t[1])
                code.append(f'{instruction} {r}, {x}, {y};')
        else:
            raise Unsupported('executable instruction ' + op)
        values[dest] = r
    require(written_bindings, 'no output')
    params = ',\n'.join(f'    .param .u64 arg{i}' for i in range(len(ordered)))
    ptx = '.version 7.1\n.target sm_86\n.address_size 64\n\n.visible .entry rtx_entry(\n' + params + '\n)\n'
    ptx += '.reqntid ' + ', '.join(map(str, local_size)) + '\n{\n'
    ptx += ''.join(f'    .reg {t} {r};\n' for t, r in regs)
    ptx += ''.join('    ' + line + '\n' for line in code) + '    ret;\n}\n'
    return ptx, dict(entry='rtx_entry', target='sm_86', local_size=local_size,
        parameter_bindings=[bindings[v] for v in ordered], parameter_bytes=8*len(ordered),
        read_bindings=sorted(read_bindings), written_bindings=sorted(written_bindings),
        required_dispatch='whole-workgroups', required_buffer_contract='caller-validated ranges and aliasing',
        source_instructions=len(rows), gpu_commands_submitted=False, metal_verified=False)
