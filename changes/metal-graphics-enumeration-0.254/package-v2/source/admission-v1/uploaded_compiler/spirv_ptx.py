"""Structured-loop integers and local float32/predicate vectors SPIR-V to sm_86 PTX.

Input must be produced by spirv-dis from a spirv-val validated binary. This
module additionally rejects unsupported types, instructions and decorations.
No source/entry names select behavior. No GPU execution or resource allocation.
Whole workgroups and sufficient non-aliasing buffer ranges are caller contracts.
"""
import shlex
import math
import struct
if __package__:
    from .fp32_remainder223 import emit as emit_remainder
    from .sampler_ptx227 import SamplerEmitter
    from .texture_ptx225 import TextureEmitter
    from .texture_ptx226 import TextureEmitter as FloatTextureEmitter, FLOAT_FORMATS
else:
    from fp32_remainder223 import emit as emit_remainder
    from sampler_ptx227 import SamplerEmitter
    from texture_ptx225 import TextureEmitter
    from texture_ptx226 import TextureEmitter as FloatTextureEmitter, FLOAT_FORMATS

# Scalar IEEE comparisons: ordered tests are false on either NaN; unordered
# tests are true on either NaN. In particular, SPIR-V unordered != is PTX neu,
# not ne. Keep selection separate from arithmetic and comparison flushing.
FLOAT_COMPARE = {
    'OpFOrdEqual': 'eq', 'OpFUnordEqual': 'equ',
    'OpFOrdNotEqual': 'ne', 'OpFUnordNotEqual': 'neu',
    'OpFOrdLessThan': 'lt', 'OpFUnordLessThan': 'ltu',
    'OpFOrdLessThanEqual': 'le', 'OpFUnordLessThanEqual': 'leu',
    'OpFOrdGreaterThan': 'gt', 'OpFUnordGreaterThan': 'gtu',
    'OpFOrdGreaterThanEqual': 'ge', 'OpFUnordGreaterThanEqual': 'geu',
}


ROUNDING = {'Trunc':'rzi', 'Floor':'rmi', 'Ceil':'rpi', 'RoundEven':'rni'}


class Unsupported(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise Unsupported(message)



def flow_plan(body, types, constants, variables, extended_sets=()):
    blocks, current = {}, None
    terminators = ('OpReturn', 'OpBranch', 'OpBranchConditional')
    for row in body:
        dest, op, a = row
        if op == 'OpLabel':
            require(dest is not None and not a and dest not in blocks, 'block label')
            current = dest
            blocks[current] = []
        else:
            require(current is not None, 'instruction before block')
            require(not blocks[current] or blocks[current][-1][1] not in terminators, 'instruction after terminator')
            blocks[current].append(row)
    require(1 <= len(blocks) <= 128, 'block count')
    first = next(iter(blocks))
    edges, predecessors, phis, merges, loops, locations = {}, {b:set() for b in blocks}, {}, {}, {}, {}
    for label, rows in blocks.items():
        require(rows and rows[-1][1] in terminators, 'block terminator')
        dest, op, a = rows[-1]
        require(dest is None, 'terminator result')
        if op == 'OpReturn':
            require(not a, 'return operands'); targets = []
        elif op == 'OpBranch':
            require(len(a) == 1, 'branch operands'); targets = a
        else:
            require(len(a) == 3, 'conditional branch operands'); targets = a[1:]
            require(targets[0] != targets[1], 'identical branch targets')
        require(all(t in blocks and t != first for t in targets), 'branch target')
        edges[label] = targets
        for target in targets:
            predecessors[target].add(label)
        phis[label] = []
        non_phi = False
        for index, (dest, opcode, operands) in enumerate(rows):
            if dest is not None:
                require(dest not in locations, 'duplicate body value')
                locations[dest] = (label, index)
            require(opcode not in ('OpSwitch', 'OpUnreachable'), 'unsupported control instruction')
            if opcode == 'OpPhi':
                require(not non_phi and dest is not None and len(operands) >= 3 and len(operands)%2 == 1, 'phi position/shape')
                require(types.get(operands[0], ('',))[0] in ('uint','float','bool','vector'), 'phi value type')
                phis[label].append((dest, operands))
            else:
                non_phi = True
            if opcode == 'OpSelectionMerge':
                require(dest is None and len(operands) == 2 and operands[1] == 'None', 'selection merge operands')
                require(index == len(rows)-2 and rows[-1][1] == 'OpBranchConditional', 'selection merge adjacency')
                require(operands[0] in blocks and operands[0] != label, 'selection merge target')
                merges[label] = operands[0]
            elif opcode == 'OpLoopMerge':
                require(dest is None and len(operands) == 3 and operands[2] in ('None','Unroll','DontUnroll'), 'loop merge operands')
                require(index == len(rows)-2 and rows[-1][1] in ('OpBranch','OpBranchConditional'), 'loop merge adjacency')
                merge, continuation = operands[:2]
                require(merge in blocks and continuation in blocks and merge not in (label, continuation), 'loop merge/continue target')
                loops[label] = (merge, continuation)

    reachable, pending = set(), [first]
    while pending:
        node = pending.pop()
        if node in reachable: continue
        reachable.add(node); pending.extend(edges[node])
    require(reachable == set(blocks), 'unreachable block')

    # Greatest fixed point gives dominance on cyclic graphs. Entry is fixed;
    # every other reachable node intersects all incoming predecessor sets.
    dominators = {b:({first} if b == first else set(blocks)) for b in blocks}
    for iteration in range(len(blocks)+1):
        changed = False
        for b in blocks:
            if b == first: continue
            new = {b} | set.intersection(*(dominators[p] for p in predecessors[b]))
            if new != dominators[b]: dominators[b] = new; changed = True
        if not changed: break
    require(not changed, 'dominance convergence')

    back_edges = {(source,target) for source in blocks for target in edges[source] if target in dominators[source]}
    require(all(target in loops for _,target in back_edges), 'undeclared loop back edge')
    loop_regions = {}
    for header, (merge, continuation) in loops.items():
        incoming_back = [source for source,target in back_edges if target == header]
        require(len(incoming_back) == 1, 'loop back edge count')
        require(header in dominators[merge] and header in dominators[continuation], 'loop header dominance')
        require(continuation in dominators[incoming_back[0]], 'continue does not dominate back edge')
        region, pending = set(), [header]
        while pending:
            node = pending.pop()
            if node == merge or node in region: continue
            require(header in dominators[node], 'loop exits outside merge')
            region.add(node); pending.extend(edges[node])
        require(continuation in region and incoming_back[0] in region, 'continue outside loop')
        require(all(b == header or predecessors[b] <= region for b in region), 'loop entry outside header')
        loop_regions[header] = region
    regions = list(loop_regions.values())
    for i, one in enumerate(regions):
        for two in regions[i+1:]:
            require(not (one & two) or one < two or two < one, 'overlapping loop regions')

    # Removing only validated natural back edges must produce a DAG. Keep the
    # prior stable topological order for every previously accepted DAG input.
    forward_predecessors = {b:{p for p in predecessors[b] if (p,b) not in back_edges} for b in blocks}
    order, done = [], set()
    while len(order) < len(blocks):
        available_blocks = [b for b in blocks if b not in done and forward_predecessors[b] <= done]
        require(available_blocks, 'irreducible cyclic control flow')
        for b in available_blocks: order.append(b); done.add(b)
    require(order[0] == first, 'entry block')
    for b, merge in merges.items():
        require(b in dominators[merge], 'selection header dominance')
    merge_targets = set(merges.values()) | {m for m,c in loops.values()} | {c for m,c in loops.values()}
    for b in blocks:
        if blocks[b][-1][1] == 'OpBranchConditional' and b not in merges and b not in loops:
            require(any(t in merge_targets for t in edges[b]), 'unstructured conditional branch')

    def available(value, block, position):
        if value in constants or value in variables: return
        require(value in locations, 'undefined block operand')
        defined, index = locations[value]
        require(defined in dominators[block] and (defined != block or index < position), 'value dominance')

    for b, rows in blocks.items():
        for index, (dest, op, a) in enumerate(rows):
            if op == 'OpPhi':
                parents = a[2::2]
                require(len(parents) == len(set(parents)) and set(parents) == predecessors[b], 'phi predecessors')
                for value, parent in zip(a[1::2], parents): available(value, parent, len(blocks[parent]))
            elif op == 'OpBranchConditional':
                available(a[0], b, index)
            elif op == 'OpExtInst':
                require(dest is not None and len(a) >= 3 and a[1] in extended_sets and ((a[2] == 'Fma' and len(a) == 6) or (a[2] in ROUNDING and len(a) == 4)), 'extended instruction')
                for operand in a[3:]:available(operand, b, index)
            elif op not in ('OpReturn','OpBranch','OpSelectionMerge','OpLoopMerge'):
                for operand in (a[1:] if dest is not None else a):
                    if operand.startswith('%'): available(operand, b, index)
    return dict(blocks=blocks, order=order, edges=edges, phis=phis, loops=loops, back_edges=back_edges)


def translate(text, *, fp32_denorm="preserve", texture_policy="r32"):
    require(texture_policy in ('r32','float226','sampler227'),"texture format policy")
    require(fp32_denorm in ("preserve", "flush"), "float32 denormal policy")
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
    extensions = set()
    image_module = any(op == 'OpTypeImage' for _,op,_ in rows)
    memory_models = 0
    extended_sets = set()
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
            require(dest is None and len(a) == 1 and a[0] in (('Shader', 'Int64', 'FloatControls2', 'Int8', *(['StorageImageReadWithoutFormat','StorageImageWriteWithoutFormat'] if texture_policy in ('float226','sampler227') else [])) if image_module else ('Shader', 'Int64', 'FloatControls2')), 'capability')
            require(a[0] not in capabilities, 'duplicate capability')
            capabilities.add(a[0])
        elif op == 'OpExtension':
            require(dest is None and a == ['SPV_KHR_float_controls2'] and a[0] not in extensions, 'extension')
            extensions.add(a[0])
        elif op == 'OpExtInstImport':
            require(dest is not None and a == ['GLSL.std.450'] and not extended_sets, 'extended instruction set')
            extended_sets.add(dest)
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
        elif op in ('OpTypeVoid', 'OpTypeBool', 'OpTypeInt', 'OpTypeFloat', 'OpTypeRuntimeArray',
                    'OpTypeStruct', 'OpTypePointer', 'OpTypeVector', 'OpTypeFunction', 'OpTypeImage', 'OpTypeSampler', 'OpTypeSampledImage'):
            require(dest is not None, 'type result')
            if op == 'OpTypeVoid':
                require(not a, 'void'); t = ('void',)
            elif op == 'OpTypeBool':
                require(not a, 'bool'); t = ('bool',)
            elif op == 'OpTypeInt':
                require(len(a) == 2 and a[0] in (('8','32','64') if image_module else ('32','64')) and a[1] == '0', 'unsigned width')
                t = ('uint', int(a[0]))
            elif op == 'OpTypeFloat':
                require(a == ['32'], 'float width'); t = ('float', 32)
            elif op == 'OpTypeVector':
                require(len(a) == 2 and a[1] in ('2','3','4'), 'vector width')
                if types.get(a[0]) == ('uint',32) and a[1] == '3':
                    t = ('gid3', a[0])
                elif image_module and types.get(a[0]) == ('uint',32) and a[1] == '2':
                    t = ('coord2', a[0])
                else:
                    require(types.get(a[0]) in (('float',32),('bool',)), 'local vector element')
                    t = ('vector', a[0], int(a[1]))
            elif op == 'OpTypeRuntimeArray':
                require(len(a) == 1 and types.get(a[0]) in (('uint', 32), ('float', 32)), 'array element')
                t = ('array', a[0])
            elif op == 'OpTypeStruct':
                if image_module and len(a)==2 and types.get(a[0],('',))[0]=='vector' and types[types[a[0]][1]]==('float',32) and types[a[0]][2]==4 and types.get(a[1])==('uint',8):
                    t=('image_result',*a)
                else:
                    require(len(a) == 1 and types.get(a[0], ('',))[0] == 'array', 'struct type')
                    t = ('struct', a[0])
            elif op == 'OpTypePointer':
                require(len(a) == 2 and a[1] in types and a[0] in (('Input','StorageBuffer','UniformConstant') if image_module else ('Input','StorageBuffer')), 'pointer type')
                t = ('pointer', a[0], a[1])
            elif op == 'OpTypeSampler':
                require(texture_policy=='sampler227' and not a,'sampler type/policy');t=('sampler',)
            elif op == 'OpTypeSampledImage':
                require(texture_policy=='sampler227' and len(a)==1 and types.get(a[0])==('image',1,'Unknown'),'sampled image type/policy');t=('sampled_image',a[0])
            elif op == 'OpTypeImage':
                require(len(a)==7 and types.get(a[0])==('float',32) and a[1:5]==['2D','0','0','0'], 'texture shape/component')
                require((a[5],a[6]) in (('1','Unknown'),('2','R32f'), *([('2','Unknown')] if texture_policy in ('float226','sampler227') else [])), 'texture format/access')
                t=('image', int(a[5]), a[6])
            else:
                require(len(a) == 1 and types.get(a[0]) == ('void',), 'function type')
                t = ('function', a[0])
            types[dest] = t
        elif op in ('OpConstantTrue', 'OpConstantFalse'):
            require(dest is not None and len(a) == 1 and types.get(a[0]) == ('bool',), 'boolean constant type')
            constants[dest] = (a[0], op == 'OpConstantTrue')
        elif op == 'OpConstant':
            require(dest is not None and len(a) == 2 and types.get(a[0], ('',))[0] in ('uint','float'), 'constant type')
            ty = types[a[0]]
            try:
                if ty[0] == 'float':
                    value = float.fromhex(a[1]) if 'p' in a[1].lower() else float(a[1])
                    require(math.isfinite(value), 'nonfinite float constant')
                    value = struct.unpack('<I', struct.pack('<f', value))[0]
                else:
                    value = int(a[1], 0)
                    require(0 <= value < 1 << ty[1], 'constant range')
            except (ValueError, OverflowError) as e:
                raise Unsupported('constant literal') from e
            constants[dest] = (a[0], value)
        elif op == 'OpUndef':
            require(dest is not None and len(a) == 1 and (types.get(a[0],('',))[0] == 'vector' or (image_module and (types.get(a[0],('',))[0] in ('coord2','gid3') or types.get(a[0])==('uint',8)))), 'undefined local value')
            constants[dest] = (a[0], None)
        elif op == 'OpConstantComposite':
            require(dest is not None and a and types.get(a[0],('',))[0] == 'vector', 'vector constant type')
            vector_type = types[a[0]]
            require(len(a) == vector_type[2]+1, 'vector constant width')
            require(all(k in constants and constants[k][0] == vector_type[1] for k in a[1:]), 'vector constant elements')
            constants[dest] = (a[0], tuple(constants[k][1] for k in a[1:]))
        elif op == 'OpVariable':
            require(dest is not None and len(a) == 2 and types.get(a[0], ('',))[0] == 'pointer', 'global variable')
            require(types[a[0]][1] == a[1], 'storage class')
            variables[dest] = (a[0], a[1])
        else:
            raise Unsupported('header instruction ' + op)
    require(('FloatControls2' in capabilities) == ('SPV_KHR_float_controls2' in extensions), 'float controls extension linkage')
    require(phase == 'done' and memory_models == 1 and 'Shader' in capabilities, 'module layout')
    require(entry and mode and function and entry[1] == mode[0] == function[0], 'entry linkage')
    require(types.get(function[1][0]) == ('void',) and
            types.get(function[1][2]) == ('function', function[1][0]), 'entry signature')
    try:
        local_size = [int(n) for n in mode[2:]]
    except ValueError as e:
        raise Unsupported('local size') from e
    require(1 <= local_size[0] <= 1024 and 1 <= local_size[1] <= 1024 and 1 <= local_size[2] <= 64 and math.prod(local_size) <= 1024, 'sm_86 local size')
    require(len(entry[3:]) == len(variables) and set(entry[3:]) == set(variables), 'entry interface')
    bindings, builtin = {}, None
    image_variables, sampler_variables, resource_bindings = {}, {}, []
    for var, (ty, storage) in variables.items():
        t = types[ty]; d = decorations.get(var, {})
        if storage == 'Input':
            require(builtin is None and types[t[2]][0] == 'gid3' and d == {'BuiltIn': ['GlobalInvocationId']}, 'builtin')
            builtin = var
        elif storage == 'UniformConstant':
            require(types[t[2]][0] in ('image','sampler') and set(d)<= {'DescriptorSet','Binding','NonWritable','NonReadable'},'image descriptor layout')
            require(d.get('DescriptorSet')==['0'] and len(d.get('Binding',[]))==1,'image descriptor')
            require(all(k not in d or d[k]==[] for k in ('NonWritable','NonReadable')),'image access decoration')
            try: raw=int(d['Binding'][0])
            except ValueError as e: raise Unsupported('image binding number') from e
            if types[t[2]][0]=='sampler':
                index=raw-160;require(texture_policy=='sampler227' and 0<=index<16 and index not in sampler_variables.values() and set(d)=={'DescriptorSet','Binding'},'sampler namespace/range/duplicate')
                sampler_variables[var]=index;continue
            sampled=types[t[2]][1]==1; index=raw-(32 if sampled else 480)
            require(0<=index<128 and index not in [v[0] for v in image_variables.values()],'image namespace/range/duplicate')
            image_variables[var]=(index,types[t[2]])
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
    # Opaque declarations can survive frontend DCE. Only samplers used by an
    # actual sampling instruction are physical application bindings.
    sampler_loads={d:a[1] for d,op,a in rows if op=='OpLoad' and len(a)==2 and a[1] in sampler_variables}
    pairs={d:a[2] for d,op,a in rows if op=='OpSampledImage' and len(a)==3}
    sampling={a[1] for _,op,a in rows if op=='OpImageSampleExplicitLod' and len(a)>=2}
    active_samplers={sampler_loads.get(pairs.get(pair)) for pair in sampling}
    require(None not in active_samplers and (not sampler_variables or bool(active_samplers)), 'sampler use/pair mapping')
    require(builtin is not None and 1 <= len(bindings)+len(image_variables)+len(active_samplers)+(1 if image_variables else 0) <= 8, 'interface bounds')
    require(image_module == bool(image_variables),'unused image type')
    for tid, t in types.items():
        d = decorations.get(tid, {})
        if t[0] == 'array':
            require(d == {'ArrayStride': ['4']}, 'array stride')
        elif t[0] == 'struct':
            require(d == {'Block': []} and tid in members, 'block layout')
        else:
            require(not d, 'type decoration')
    require(set(members) <= {tid for tid, t in types.items() if t[0] == 'struct'}, 'member target')
    fused = {d for d,op,a in body if op == 'OpExtInst' and len(a) == 6 and a[1] in extended_sets and a[2] == 'Fma'}
    rounded = {d for d,op,a in body if op == 'OpExtInst' and len(a) == 4 and a[1] in extended_sets and a[2] in ROUNDING}
    arithmetic = {d for d,op,a in body if op in ('OpFAdd','OpFSub','OpFMul','OpFDiv','OpFRem')} | fused | rounded
    for key in set(decorations) - set(types) - set(variables):
        d = decorations[key]
        require(key in arithmetic and set(d) <= {'FPFastMathMode','NoContraction'}, 'arithmetic decoration target')
        require('FPFastMathMode' not in d or ('FloatControls2' in capabilities and d['FPFastMathMode'] == ['None']), 'float fast math mode')
        require('NoContraction' not in d or d['NoContraction'] == [], 'no contraction decoration')
    plan = flow_plan(body, types, constants, variables, extended_sets)
    branching = len(plan['blocks']) > 1
    instructions = body[1:-1]
    if branching:
        instructions = []
        for label in plan['order']:
            instructions.append((label, 'OpLabel', []))
            instructions.extend(plan['blocks'][label])
    labels = {label: 'BB' + str(i) for i, label in enumerate(plan['order'])}
    def edge_label(source, target): return labels[source] + '_to_' + labels[target]


    code, regs, values, value_types, pointers = [], [], {}, {}, {}
    read_bindings, written_bindings = set(), set()

    def register(t):
        if t[0] == 'vector':return tuple(register(types[t[1]]) for _ in range(t[2]))
        require(t[0] in ('uint', 'bool', 'float'), 'register type')
        prefix, decl = ('p', '.pred') if t[0] == 'bool' else ('r', '.b' + str(t[1]))
        if t[0] == 'float': prefix, decl = 'f', '.f32'
        name = '%' + prefix + str(len(regs))
        regs.append((decl, name))
        return name

    def scalar(key):
        require(key in values and types[value_types[key]][0] in ('uint', 'bool', 'float'), 'scalar operand')
        return values[key], types[value_types[key]]


    def vector_value(key, defined=True):
        require(key in values and key in value_types, 'vector operand')
        t = types[value_types[key]]
        require(t[0] == 'vector', 'vector operand type')
        v = values[key]
        require(type(v) is tuple and len(v) == t[2], 'vector operand width')
        require(not defined or all(x is not None for x in v), 'undefined vector component')
        return v, t

    def incoming_value(key):
        if key in value_types and types[value_types[key]][0] == 'vector':
            return vector_value(key)
        return scalar(key)

    def select_component(result, condition, yes, no, element):
        if element == ('bool',):
            selected_yes = register(element)
            inverse = register(element)
            selected_no = register(element)
            code.extend([f'and.pred {selected_yes}, {condition}, {yes};',
                         f'not.pred {inverse}, {condition};',
                         f'and.pred {selected_no}, {inverse}, {no};',
                         f'or.pred {result}, {selected_yes}, {selected_no};'])
        else:
            require(element == ('float', 32), 'vector selection element')
            code.append(f'selp.f32 {result}, {yes}, {no}, {condition};')

    def vector_instruction(dest, op, a, ty, t):
        if op == 'OpCompositeExtract' and len(a) >= 2 and a[1] in value_types and types[value_types[a[1]]][0] == 'vector':
            require(len(a) == 3, 'vector extract shape')
            source, source_type = vector_value(a[1], defined=False)
            require(types[source_type[1]] == t, 'vector extract element type')
            require(a[2].isdigit() and 0 <= int(a[2]) < len(source), 'vector extract index')
            r = source[int(a[2])]
            require(r is not None, 'undefined extracted component')
        elif op == 'OpVectorExtractDynamic':
            require(len(a) == 3, 'dynamic extract shape')
            source, source_type = vector_value(a[1])
            require(types[source_type[1]] == t, 'dynamic extract element type')
            index, index_type = scalar(a[2])
            require(index_type[0] == 'uint', 'dynamic extract index type')
            r = register(t)
            suffix = 'pred' if t == ('bool',) else 'f32'
            code.append(f'mov.{suffix} {r}, {source[0]};')
            # Source indices outside the vector width are undefined in SPIR-V.
            # Valid indices select exact component bits without FP arithmetic.
            for lane in range(1, len(source)):
                predicate = register(('bool',))
                code.append(f'setp.eq.u{index_type[1]} {predicate}, {index}, {lane};')
                select_component(r, predicate, source[lane], r, t)
        elif t[0] != 'vector':
            return False
        elif op == 'OpCompositeInsert':
            require(len(a) == 4, 'vector insert shape')
            value, element = scalar(a[1])
            source, source_type = vector_value(a[2], defined=False)
            require(source_type == t and element == types[t[1]], 'vector insert types')
            require(a[3].isdigit() and 0 <= int(a[3]) < t[2], 'vector insert index')
            parts = list(source); parts[int(a[3])] = value; r = tuple(parts)
        elif op == 'OpCompositeConstruct':
            require(len(a) >= 2, 'vector construct shape')
            parts = []
            for key in a[1:]:
                require(key in value_types, 'vector construct operand')
                if types[value_types[key]][0] == 'vector':
                    chunk, chunk_type = vector_value(key)
                    require(types[chunk_type[1]] == types[t[1]], 'vector construct element')
                    parts.extend(chunk)
                else:
                    value, element = scalar(key)
                    require(element == types[t[1]], 'vector construct element')
                    parts.append(value)
            require(len(parts) == t[2], 'vector construct width')
            r = tuple(parts)
        elif op == 'OpVectorShuffle':
            require(len(a) == t[2]+3, 'vector shuffle shape')
            x, xt = vector_value(a[1], defined=False)
            y, yt = vector_value(a[2], defined=False)
            require(types[xt[1]] == types[yt[1]] == types[t[1]], 'vector shuffle elements')
            source = x+y; parts = []
            for literal in a[3:]:
                require(literal.isdigit(), 'vector shuffle index')
                index = int(literal)
                require(index == 0xffffffff or 0 <= index < len(source), 'vector shuffle range')
                parts.append(None if index == 0xffffffff else source[index])
            r = tuple(parts)
        elif op == 'OpCopyObject':
            require(len(a) == 2, 'vector copy shape')
            r, source_type = vector_value(a[1], defined=False)
            require(source_type == t, 'vector copy type')
        elif op in ('OpFAdd','OpFSub','OpFMul','OpFDiv','OpFRem'):
            require(len(a) == 3 and types[t[1]] == ('float',32), 'vector float arithmetic shape')
            x, xt = vector_value(a[1]); y, yt = vector_value(a[2])
            require(xt == yt == t, 'vector float arithmetic types')
            r = register(t); operation = {'OpFAdd':'add','OpFSub':'sub','OpFMul':'mul','OpFDiv':'div'}.get(op)
            ftz = '.ftz' if fp32_denorm == 'flush' else ''
            for result, first, second in zip(r,x,y):
                if op == 'OpFRem':code.extend(emit_remainder(result,first,second,register,flush=fp32_denorm=='flush'))
                else:code.append(f'{operation}.rn{ftz}.f32 {result}, {first}, {second};')
        elif op in FLOAT_COMPARE:
            require(len(a) == 3 and types[t[1]] == ('bool',), 'vector comparison shape')
            x, xt = vector_value(a[1]); y, yt = vector_value(a[2])
            require(xt == yt and xt[2] == t[2] and types[xt[1]] == ('float',32), 'vector comparison types')
            r = register(t); ftz = '.ftz' if fp32_denorm == 'flush' else ''
            for result, first, second in zip(r,x,y):
                code.append(f'setp.{FLOAT_COMPARE[op]}{ftz}.f32 {result}, {first}, {second};')
        elif op == 'OpSelect':
            require(len(a) == 4, 'vector select shape')
            x, xt = vector_value(a[2]); y, yt = vector_value(a[3])
            require(xt == yt == t, 'vector select values')
            require(a[1] in value_types, 'vector select predicate')
            if types[value_types[a[1]]] == ('bool',):
                predicate, _ = scalar(a[1]); predicates = (predicate,)*t[2]
            else:
                predicates, pt = vector_value(a[1])
                require(types[pt[1]] == ('bool',) and pt[2] == t[2], 'vector select predicate width')
            r = register(t)
            for result, predicate, yes, no in zip(r,predicates,x,y):
                select_component(result,predicate,yes,no,types[t[1]])
        elif op in ('OpLogicalAnd','OpLogicalOr','OpLogicalNot','OpLogicalEqual','OpLogicalNotEqual'):
            require(types[t[1]] == ('bool',) and len(a) == (2 if op == 'OpLogicalNot' else 3), 'vector logical shape')
            x, xt = vector_value(a[1]); require(xt == t, 'vector logical first type')
            if op != 'OpLogicalNot':
                y, yt = vector_value(a[2]); require(yt == t, 'vector logical second type')
            r = register(t)
            for index,result in enumerate(r):
                if op == 'OpLogicalNot':code.append(f'not.pred {result}, {x[index]};')
                elif op == 'OpLogicalEqual':
                    different = register(('bool',))
                    code.extend([f'xor.pred {different}, {x[index]}, {y[index]};',f'not.pred {result}, {different};'])
                else:
                    operation = {'OpLogicalAnd':'and','OpLogicalOr':'or','OpLogicalNotEqual':'xor'}[op]
                    code.append(f'{operation}.pred {result}, {x[index]}, {y[index]};')
        else:
            return False
        values[dest] = r
        return True

    for key, (ty, value) in constants.items():
        value_types[key] = ty
        if types[ty][0] == 'vector':
            element = types[types[ty][1]]
            if value is None:
                values[key] = (None,)*types[ty][2]
            elif element == ('float',32):
                values[key] = tuple('0f%08x' % part for part in value)
            else:
                require(element == ('bool',), 'vector constant element')
                parts = []
                for part in value:
                    r = register(element); parts.append(r)
                    code.append(f'setp.eq.u32 {r}, 0, {0 if part else 1};')
                values[key] = tuple(parts)
        elif image_module and (types[ty][0] in ('coord2','gid3') or types[ty]==('uint',8)) and value is None:
            values[key]=(None,)*(2 if types[ty][0]=='coord2' else 3) if types[ty][0] in ('coord2','gid3') else None
        elif types[ty] == ('bool',):
            r = register(('bool',)); values[key] = r
            code.append(f'setp.eq.u32 {r}, 0, {0 if value else 1};')
        else:
            values[key] = ('0f%08x' % value) if types[ty][0] == 'float' else str(value)
    ordered = sorted(bindings, key=bindings.get)
    if image_variables:
        resource_bindings=[dict(kind='buffer',index=bindings[v],descriptor=0,format=0) for v in ordered]
        for ordinal,var in enumerate(sorted(image_variables,key=lambda v:image_variables[v][0])):
            ordered.append(var);resource_bindings.append(dict(kind='texture',index=image_variables[var][0],descriptor=ordinal,format=FLOAT_FORMATS if texture_policy in ('float226','sampler227') and image_variables[var][1][2]=='Unknown' else 3))
        for ordinal,var in enumerate(sorted(active_samplers,key=sampler_variables.get)):
            ordered.append(var);resource_bindings.append(dict(kind='sampler',index=sampler_variables[var],descriptor=len(image_variables)+ordinal,format=0))
        bindings={v:i for i,v in enumerate(ordered)}
        resource_bindings.append(dict(kind='texture_descriptors',index=0,descriptor=0,format=0))
    image_pointers={};sampler_pointers={};sampler_values={};sampled_values={}
    for index, var in enumerate(ordered):
        r = register(('uint', 64)); base = register(('uint', 64))
        code += [f'ld.param.u64 {r}, [arg{index}];', f'cvta.to.global.u64 {base}, {r};']
        if var in image_variables:
            image_pointers[var]=(base,resource_bindings[index]['descriptor'],index)+((resource_bindings[index]['format'],) if texture_policy in ('float226','sampler227') else ())
        elif var in sampler_variables:
            sampler_pointers[var]=(base,index)
        else:
            element = types[types[types[variables[var][0]][2]][1]][1]
            pointers[var] = ('root', bindings[var], base, element)
    image_values={}
    if image_variables:
        descriptor_index=len(ordered);dr=register(('uint',64));db=register(('uint',64))
        code.extend([f'ld.param.u64 {dr}, [arg{descriptor_index}];',f'cvta.to.global.u64 {db}, {dr};'])
        texture_emitter=(FloatTextureEmitter if texture_policy in ('float226','sampler227') else TextureEmitter)(code,register,require,image_pointers,db)
        read_bindings.add(descriptor_index)
        sampler_emitter=SamplerEmitter(code,register,require,texture_emitter,sampler_pointers)
    gid = []
    for axis in 'xyz':
        cta = register(('uint', 32)); tid = register(('uint', 32)); r = register(('uint', 32))
        code += [f'mov.u32 {cta}, %ctaid.{axis};', f'mov.u32 {tid}, %tid.{axis};',
                 f'mad.lo.u32 {r}, {cta}, {local_size["xyz".index(axis)]}, {tid};']
        gid.append(r)
    # Reserve every phi result before its incoming edges are emitted. Copy
    # sources into fresh temporaries before assigning destinations, preserving
    # simultaneous phi semantics and predicate/floating bit representations.
    for phi_rows in plan['phis'].values():
        for dest, a in phi_rows:
            value_types[dest] = a[0]; values[dest] = register(types[a[0]])
    current_block = None
    for dest, op, a in instructions:
        if op == 'OpLabel':
            current_block = dest; code.append(labels[dest] + ':'); continue
        if op in ('OpPhi', 'OpSelectionMerge', 'OpLoopMerge'): continue
        if op == 'OpReturn': code.append('ret;'); continue
        if op == 'OpBranch':
            code.append('bra ' + edge_label(current_block, a[0]) + ';'); continue
        if op == 'OpBranchConditional':
            predicate, predicate_type = scalar(a[0])
            require(predicate_type == ('bool',), 'branch condition type')
            code += [f'@{predicate} bra {edge_label(current_block, a[1])};',
                     f'bra {edge_label(current_block, a[2])};']
            continue
        if op == 'OpImageWrite':
            require(dest is None and len(a)==3 and a[0] in image_values,'image write operands')
            var=image_values[a[0]]
            require(image_variables[var][1][1]==2 and 'NonWritable' not in decorations[var],'image write access')
            require(image_variables[var][1][2]!='Unknown' or 'StorageImageWriteWithoutFormat' in capabilities,'formatless image write capability')
            require(a[1] in values and types[value_types[a[1]]][0]=='coord2' and all(v is not None for v in values[a[1]]),'image coordinates')
            source,vt=vector_value(a[2]);require(types[vt[1]]==('float',32) and vt[2]==4,'image write texel')
            written_bindings.add(texture_emitter.access(var,values[a[1]],source=source));continue
        if op == 'OpStore':
            require(dest is None and len(a) == 2 and a[0] in pointers, 'store')
            p = pointers[a[0]]; r, t = scalar(a[1])
            require(p[0] == 'element' and t in (('uint', 32), ('float', 32)) and t == types[p[3]], 'store element')
            var = next(k for k, v in bindings.items() if v == p[1])
            require('NonWritable' not in decorations[var], 'readonly store')
            suffix = 'f32' if t[0] == 'float' else 'u32'
            code.append(f'st.global.{suffix} [{p[2]}], {r};'); written_bindings.add(p[1])
            continue
        require(dest is not None and a and a[0] in types, 'typed result')
        ty = a[0]; t = types[ty]; value_types[dest] = ty
        if image_module:
            if op=='OpLoad' and len(a)==2 and a[1] in sampler_variables:
                require(t==('sampler',),'sampler load type');sampler_values[dest]=a[1];continue
            if op=='OpSampledImage':
                require(texture_policy=='sampler227' and len(a)==3 and t[0]=='sampled_image' and a[1] in image_values and a[2] in sampler_values,'sampled image pair')
                require(types[t[1]]==image_variables[image_values[a[1]]][1],'sampled image result type');sampled_values[dest]=(image_values[a[1]],sampler_values[a[2]]);continue
            if op=='OpImageSampleExplicitLod':
                require(texture_policy=='sampler227' and len(a)==5 and a[1] in sampled_values and a[3]=='Lod' and a[4] in constants and types[constants[a[4]][0]]==('float',32) and constants[a[4]][1] in (0,0x80000000),'sample level/operands')
                image,sampler=sampled_values[a[1]];require('NonReadable'not in decorations[image],'sample read access')
                require(t[0]=='vector' and types[t[1]]==('float',32) and t[2]==4,'sample result')
                require(a[2] in values and types[value_types[a[2]]][0]=='vector' and types[types[value_types[a[2]]][1]]==('float',32) and len(values[a[2]])==2 and all(v is not None for v in values[a[2]]),'sample coordinates')
                result=register(t);values[dest]=result;read_bindings.update(sampler_emitter.sample(image,sampler,values[a[2]],result));continue
            if op=='OpLoad'  and len(a)==2 and a[1] in image_variables:
                require(t==image_variables[a[1]][1],'image load type');image_values[dest]=a[1];continue
            if op=='OpUndef' and not a[1:] and (t==('uint',8) or t[0] in ('gid3','coord2')):
                values[dest]=None if t==('uint',8) else (None,)*(3 if t[0]=='gid3' else 2);continue
            if op=='OpCompositeConstruct' and t[0]=='image_result':
                require(len(a)==3 and value_types.get(a[1])==t[1] and value_types.get(a[2])==t[2],'image result wrapper')
                values[dest]=(values[a[1]],values[a[2]]);continue
            if op=='OpCompositeExtract' and len(a)==3 and types.get(value_types.get(a[1]),('',))[0]=='image_result':
                st=types[value_types[a[1]]];require(a[2]=='0' and ty==st[1],'unsupported sparse residency result')
                values[dest]=values[a[1]][0];continue
            if op=='OpVectorShuffle' and t[0]=='coord2':
                require(len(a)==5 and all(k in values and types[value_types[k]][0] in ('gid3','coord2') for k in a[1:3]),'coordinate shuffle')
                joined=values[a[1]]+values[a[2]]
                require(all(v.isdigit() and int(v)<len(joined) for v in a[3:]),'coordinate shuffle indices')
                result=tuple(joined[int(v)] for v in a[3:]);require(all(v is not None for v in result),'undefined coordinate')
                values[dest]=result;continue
            if op=='OpCompositeConstruct' and t[0]=='coord2':
                require(len(a)==3 and all(k in values and types[value_types[k]]==('uint',32) for k in a[1:]),'coordinate construction')
                values[dest]=tuple(values[k] for k in a[1:]);continue
            if op in ('OpImageFetch','OpImageRead'):
                require((op=='OpImageRead' and len(a)==3) or (op=='OpImageFetch' and len(a)==5 and a[3]=='Lod' and a[4] in constants and types[constants[a[4]][0]]==('uint',32) and constants[a[4]][1]==0),'image operands/lod')
                require(a[1] in image_values,'image value');var=image_values[a[1]]
                require(image_variables[var][1][1]==(1 if op=='OpImageFetch' else 2) and 'NonReadable' not in decorations[var],'image read access')
                require(op!='OpImageRead' or image_variables[var][1][2]!='Unknown' or 'StorageImageReadWithoutFormat' in capabilities,'formatless image read capability')
                require(t[0]=='vector' and types[t[1]]==('float',32) and t[2]==4,'image read result')
                require(a[2] in values and types[value_types[a[2]]][0]=='coord2' and all(v is not None for v in values[a[2]]),'image read coordinates')
                result=register(t);values[dest]=result
                read_bindings.add(texture_emitter.access(var,values[a[2]],result=result));continue
        if op == 'OpExtInst' and len(a) >= 3 and a[2] in ROUNDING:
            require(len(a) == 4 and a[1] in extended_sets, 'rounding operand count/set')
            require(t == ('float',32) or (t[0] == 'vector' and types[t[1]] == ('float',32)), 'rounding result type')
            value, operand_type = incoming_value(a[3]); require(operand_type == t, 'rounding operand type')
            result = register(t); values[dest] = result
            ftz = '.ftz' if fp32_denorm == 'flush' else ''
            for target, source in zip(result if t[0] == 'vector' else (result,), value if t[0] == 'vector' else (value,)):
                code.append(f'cvt.{ROUNDING[a[2]]}{ftz}.f32.f32 {target}, {source};')
            continue
        if op == 'OpExtInst':
            require(len(a) == 6 and a[1] in extended_sets and a[2] == 'Fma', 'extended instruction')
            require(t == ('float',32) or (t[0] == 'vector' and types[t[1]] == ('float',32)), 'fma result type')
            operands = [incoming_value(key) for key in a[3:]]
            require(all(operand_type == t for _,operand_type in operands), 'fma operand types')
            r = register(t); values[dest] = r
            vectors = [value if t[0] == 'vector' else (value,) for value,_ in operands]
            ftz = '.ftz' if fp32_denorm == 'flush' else ''
            for target,x,y,z in zip(r if t[0] == 'vector' else (r,),*vectors):
                code.append(f'fma.rn{ftz}.f32 {target}, {x}, {y}, {z};')
            continue
        if vector_instruction(dest,op,a,ty,t):continue
        if op == 'OpLoad':
            require(len(a) == 2, 'memory operands unsupported')
            if a[1] == builtin:
                require(t[0] == 'gid3', 'builtin load'); values[dest] = tuple(gid); continue
            require(a[1] in pointers and pointers[a[1]][0] == 'element' and t in (('uint', 32), ('float', 32)) and t == types[pointers[a[1]][3]], 'buffer load')
            p = pointers[a[1]]; var = next(k for k, v in bindings.items() if v == p[1])
            require('NonReadable' not in decorations[var], 'writeonly load')
            suffix = 'f32' if t[0] == 'float' else 'u32'
            r = register(t); code.append(f'ld.global.{suffix} {r}, [{p[2]}];'); read_bindings.add(p[1])
        elif op == 'OpCompositeExtract':
            require(len(a) == 3 and a[1] in values and type(values[a[1]]) is tuple and a[2] in ('0', '1', '2') and t == ('uint', 32), 'composite extract')
            r = values[a[1]][int(a[2])]
        elif op in ('OpAccessChain', 'OpInBoundsAccessChain'):
            require(len(a) == 4 and a[1] in pointers and pointers[a[1]][0] == 'root' and
                    a[2] in constants and constants[a[2]][1] == 0 and t[0:2] == ('pointer', 'StorageBuffer') and types[t[2]] in (('uint', 32), ('float', 32)) and t[2] == pointers[a[1]][3], 'access chain')
            idx, it = scalar(a[3]); require(it[0] == 'uint', 'index type')
            index = register(('uint', 64)); offset = register(('uint', 64)); r = register(('uint', 64))
            if it[1] == 32:
                code.append(f'cvt.u64.u32 {index}, {idx};')
            else:
                code.append(f'mov.b64 {index}, {idx};')
            p = pointers[a[1]]
            code += [f'mul.lo.u64 {offset}, {index}, 4;', f'add.u64 {r}, {p[2]}, {offset};']
            pointers[dest] = ('element', p[1], r, p[3])
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
            require(ct == ('bool',) and t == xt == yt and t[0] in ('uint', 'float', 'bool'), 'select types')
            # A selection copies the chosen value, including its NaN payload,
            # zero sign and subnormal bits. Do not attach .ftz to selp.
            r = register(t)
            if t == ('bool',):
                # PTX selp does not have a predicate destination type.
                yes = register(t); inverse = register(t); no = register(t)
                code += [f'and.pred {yes}, {cond}, {x};', f'not.pred {inverse}, {cond};',
                         f'and.pred {no}, {inverse}, {y};', f'or.pred {r}, {yes}, {no};']
            else:
                suffix = 'f32' if t == ('float', 32) else 'u' + str(t[1])
                code.append(f'selp.{suffix} {r}, {x}, {y}, {cond};')
        elif op in FLOAT_COMPARE:
            require(len(a) == 3 and t == ('bool',), 'float comparison shape')
            x, xt = scalar(a[1]); y, yt = scalar(a[2])
            require(xt == yt == ('float', 32), 'float comparison operands')
            r = register(t); ftz = '.ftz' if fp32_denorm == 'flush' else ''
            code.append(f'setp.{FLOAT_COMPARE[op]}{ftz}.f32 {r}, {x}, {y};')
        elif op in ('OpLogicalAnd', 'OpLogicalOr', 'OpLogicalNot', 'OpLogicalEqual', 'OpLogicalNotEqual'):
            require(t == ('bool',) and len(a) == (2 if op == 'OpLogicalNot' else 3), 'logical shape')
            x, xt = scalar(a[1]); require(xt == ('bool',), 'logical first operand')
            r = register(t)
            if op == 'OpLogicalNot':
                code.append(f'not.pred {r}, {x};')
            else:
                y, yt = scalar(a[2]); require(yt == ('bool',), 'logical second operand')
                operation = {'OpLogicalAnd': 'and', 'OpLogicalOr': 'or',
                             'OpLogicalEqual': 'xor', 'OpLogicalNotEqual': 'xor'}[op]
                if op == 'OpLogicalEqual':
                    different = register(('bool',))
                    code += [f'xor.pred {different}, {x}, {y};', f'not.pred {r}, {different};']
                else:
                    code.append(f'{operation}.pred {r}, {x}, {y};')
        elif op in ('OpFAdd', 'OpFSub', 'OpFMul', 'OpFDiv', 'OpFRem'):
            require(len(a) == 3 and t == ('float', 32), 'float arithmetic shape')
            x, xt = scalar(a[1]); y, yt = scalar(a[2]); require(xt == yt == t, 'float arithmetic types')
            r = register(t); operation = {'OpFAdd':'add','OpFSub':'sub','OpFMul':'mul','OpFDiv':'div'}.get(op)
            # Explicit round-to-nearest-even prevents multiply/add contraction.
            ftz = '.ftz' if fp32_denorm == 'flush' else ''
            if op == 'OpFRem':code.extend(emit_remainder(r,x,y,register,flush=fp32_denorm=='flush'))
            else:code.append(f'{operation}.rn{ftz}.f32 {r}, {x}, {y};')
        elif op in ('OpIAdd', 'OpISub', 'OpIMul', 'OpBitwiseAnd', 'OpBitwiseOr', 'OpBitwiseXor',
                    'OpShiftLeftLogical', 'OpShiftRightLogical', 'OpULessThan', 'OpULessThanEqual',
                    'OpUGreaterThan', 'OpUGreaterThanEqual', 'OpIEqual', 'OpINotEqual'):
            require(len(a) == 3, 'binary shape'); x, xt = scalar(a[1]); y, yt = scalar(a[2])
            require(xt[0] == yt[0] == 'uint', 'unsigned operands')
            r = register(t)
            if op in ('OpULessThan', 'OpULessThanEqual', 'OpUGreaterThan', 'OpUGreaterThanEqual', 'OpIEqual', 'OpINotEqual'):
                require(t == ('bool',) and xt == yt, 'comparison types')
                comparison = {'OpULessThan':'lt', 'OpULessThanEqual':'le', 'OpUGreaterThan':'gt', 'OpUGreaterThanEqual':'ge', 'OpIEqual':'eq', 'OpINotEqual':'ne'}[op]
                code.append(f'setp.{comparison}.u{xt[1]} {r}, {x}, {y};')
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
    if branching:
        for source in plan['order']:
            for target in plan['edges'][source]:
                code.append(edge_label(source, target) + ':')
                copies = []
                for dest, a in plan['phis'][target]:
                    incoming = dict(zip(a[2::2], a[1::2]))[source]
                    value, t = incoming_value(incoming)
                    require(t == types[a[0]], 'phi incoming type')
                    temporary = register(t)
                    if t[0] == 'vector':
                        suffix = 'pred' if types[t[1]] == ('bool',) else 'f32'
                        for destination, saved, component in zip(values[dest],temporary,value):
                            code.append(f'mov.{suffix} {saved}, {component};')
                            copies.append((destination,saved,suffix))
                    else:
                        suffix = 'pred' if t == ('bool',) else ('f32' if t == ('float', 32) else 'b' + str(t[1]))
                        code.append(f'mov.{suffix} {temporary}, {value};')
                        copies.append((values[dest], temporary, suffix))
                for destination, temporary, suffix in copies:
                    code.append(f'mov.{suffix} {destination}, {temporary};')
                code.append('bra ' + labels[target] + ';')
    require(written_bindings, 'no output')
    params = ',\n'.join(f'    .param .u64 arg{i}' for i in range(len(ordered)+(1 if image_variables else 0)))
    ptx = '.version 7.1\n.target sm_86\n.address_size 64\n\n.visible .entry rtx_entry(\n' + params + '\n)\n'
    ptx += '.reqntid ' + ', '.join(map(str, local_size)) + '\n{\n'
    ptx += ''.join(f'    .reg {t} {r};\n' for t, r in regs)
    ptx += ''.join('    ' + line + '\n' for line in code) + '    ret;\n}\n'
    return ptx, dict(entry='rtx_entry', target='sm_86', local_size=local_size,
        parameter_bindings=[bindings[v] for v in ordered]+([len(ordered)] if image_variables else []), parameter_bytes=8*(len(ordered)+(1 if image_variables else 0)),
        **(dict(resource_bindings=resource_bindings,texture_abi=227 if sampler_variables else 226 if texture_policy in ('float226','sampler227') else 225,texture_formats=['R32Float','RGBA8Unorm','BGRA8Unorm','RGBA32Float'] if texture_policy in ('float226','sampler227') else ['R32Float'],texture_descriptor_bytes=256) if image_variables else {}),
        read_bindings=sorted(read_bindings), written_bindings=sorted(written_bindings),
        required_dispatch='whole-workgroups', required_buffer_contract='caller-validated ranges and aliasing',
        **(dict(sampler_operations=True,sampler_lod=0,sampler_filters=['nearest','linear'],sampler_address_modes=['clamp_to_edge','repeat','mirrored_repeat','clamp_to_zero'],sampler_coordinate_modes=['normalized','pixel'],sampler_descriptor_shared=True,sampler_execution='gpu-linear-loads') if sampler_variables else {}),
        source_instructions=len(rows), float32_arithmetic=bool(arithmetic), float32_denorm=fp32_denorm, gpu_commands_submitted=False, metal_verified=False,
        **({'float32_comparisons': True} if any(op in FLOAT_COMPARE for _, op, _ in body) else {}),
        **({'float32_selection': True} if any(op in ('OpSelect', 'OpPhi') and a and (types.get(a[0]) == ('float',32) or (types.get(a[0],('',))[0] == 'vector' and types[types[a[0]][1]] == ('float',32))) for _, op, a in body) else {}),
        **(dict(float32_remainder='exact_significand_reduction223') if any(op=='OpFRem'for _,op,_ in body) else {}),
        **(dict(float32_fma=True) if fused else {}),
        **(dict(float32_rounding=True) if rounded else {}),
        **(dict(local_vector_values=True) if any(t[0] == 'vector' for t in types.values()) else {}),
        **(dict(control_flow_blocks=len(plan['blocks']),
                control_flow_edges=sum(map(len, plan['edges'].values())), phi_nodes=sum(map(len, plan['phis'].values())),
                **(dict(loop_control_flow=True, loop_headers=len(plan['loops']), loop_back_edges=len(plan['back_edges']))
                   if plan['loops'] else dict(acyclic_control_flow=True))) if branching else {}))
