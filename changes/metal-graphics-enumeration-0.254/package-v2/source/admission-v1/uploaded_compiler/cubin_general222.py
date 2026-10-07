"""Byte-level CUDA ELF resource/parameter audit; not a runtime GPU loader."""
import hashlib, re, struct


def audit(raw, parameter_count, threads):
    assert type(raw) is bytes and 64 <= len(raw) <= 131072
    from geometry164 import validate_local
    threads=validate_local(threads)
    assert type(parameter_count)is int and 1<=parameter_count<=8

    def region(off, size):
        assert 0 <= off <= len(raw) and 0 <= size <= len(raw)-off
        return raw[off:off+size]

    def string(table, off):
        assert 0 <= off < len(table)
        end = table.find(b'\0', off); assert end >= 0
        return table[off:end].decode('ascii')

    def attrs(data, repeated=frozenset({0x17})):
        at, result = 0, {}
        while at < len(data):
            assert at+4 <= len(data)
            typ, key, size = struct.unpack_from('<BBH', data, at); at += 4
            assert typ in (1, 3, 4) and (key not in result or key in repeated)
            assert typ != 4 or size <= len(data)-at
            val = data[at:at+size] if typ == 4 else size
            if typ == 4: at += size
            result.setdefault(key, []).append((typ, val))
        assert at == len(data)
        return result

    h = struct.unpack('<16sHHIQQQIHHHHHH', region(0, 64))
    assert h[0] == b'\x7fELF\x02\x01\x01A\x08'+bytes(7)
    assert h[1:5] == (2, 190, 1, 0) and h[7] == 0x06005604
    assert h[8:10] == (64, 56) and 0 < h[10] <= 8 and h[11] == 64 and 0 < h[13] < h[12] <= 64
    for p in struct.iter_unpack('<IIQQQQQQ', region(h[5], h[10]*56)):
        region(p[2], p[5]); assert p[5] <= p[6]
    rows = list(struct.iter_unpack('<IIQQQQIIQQ', region(h[6], h[12]*64)))
    names = region(rows[h[13]][4], rows[h[13]][5]); sections = {}
    for i, row in enumerate(rows):
        name = string(names, row[0]); assert name not in sections
        data = b'' if row[1] == 8 else region(row[4], row[5])
        sections[name] = (i, row, data)
    ti, text, code = sections['.text.rtx_entry']
    ci, constant, cb = sections['.nv.constant0.rtx_entry']
    assert text[1:3] == (1, 6) and 0 < text[5] and text[5] % 128 == 0 and text[8] == 128
    if text[5] > 4096:
        raise ValueError('Compiled shader exceeds current GPU code allocation: %d > 4096 bytes' % text[5])
    registers = text[7] >> 24; assert 1 <= registers <= 255
    assert constant[1] == 1 and constant[5] == 0x160+8*parameter_count and constant[7] == ti and not any(cb)
    _, symbols, symbolbytes = sections['.symtab']; _, _, strings = sections['.strtab']
    assert symbols[9] == 24 and len(symbolbytes) % 24 == 0 and symbols[6] == sections['.strtab'][0]
    entries = list(struct.iter_unpack('<IBBHQQ', symbolbytes))
    matches = [(i, s) for i, s in enumerate(entries) if string(strings, s[0]) == 'rtx_entry']
    assert len(matches) == 1
    si, sym = matches[0]; assert sym[1:] == (0x12, 0x10, ti, 0, len(code))
    # PTXAS embeds the correctly rounded division slow path in this same
    # executable section. It requires no separately loaded function or stack.
    helpers = []
    for index, symbol in enumerate(entries):
        if symbol[1] & 0xf != 2 or index == si:
            continue
        name = string(strings, symbol[0])
        assert re.fullmatch(r'\$__internal_[0-9]+_\$__cuda_sm3x_div_rn_ftz_f32_slowpath', name), 'unsupported helper function'
        assert symbol[1:4] == (2, 0, ti), 'helper must be local and defined in kernel text'
        offset, size = symbol[4:6]
        assert 0 < offset < len(code) and 0 < size <= len(code)-offset and offset % 16 == size % 16 == 0, 'helper extent'
        helpers.append(dict(symbol=index, name=name, offset=offset, bytes=size))
    assert len(helpers) <= 8 and len({v['name'] for v in helpers}) == len(helpers)
    intervals = sorted((v['offset'], v['offset']+v['bytes']) for v in helpers)
    assert all(left[1] <= right[0] for left, right in zip(intervals, intervals[1:])), 'overlapping helpers'
    for row in rows:
        if row[1] in (4, 9):
            assert row[7] < len(rows) and not rows[row[7]][2] & 2, 'runtime relocation'
        if row[2] & 2:
            assert row is text or row is constant or not row[5], 'additional allocated section'
    info = attrs(sections['.nv.info'][2], frozenset({0x11})); params = attrs(sections['.nv.info.rtx_entry'][2])
    assert info == {0x2f: [(4, struct.pack('<II', si, registers))],
                    0x11: [(4, struct.pack('<II', index, 0)) for index in sorted([si]+[v['symbol'] for v in helpers])],
                    0x12: [(4, struct.pack('<II', si, 0))]}, 'nonzero or unknown function stack metadata'
    required = {0x66, 0x37, 0x35, 0xa, 0x19, 0x17, 0x1b, 0x5f, 0x1c, 0x10}
    # PTXAS also emits EIATTR_CRS_STACK_SIZE for reconverging branch code
    # without helper functions. Preserve the zero-stack requirement.
    assert required <= set(params) <= required | {0x1e,0x04}
    # NVIDIA nvdisasm identifies this exact zero-payload boolean attribute
    # as EIATTR_CTAIDZ_USED when generated code reads the Z block index.
    if 0x04 in params:
        assert params[0x04] == [(1,0)], 'malformed CTAIDZ_USED attribute'
    assert not helpers or 0x1e in params
    if 0x1e in params:
        assert params[0x1e] == [(4, bytes(4))], 'call/return stack must be zero'
    if helpers:
        graph = sections['.nv.callgraph']; action = sections['.nv.rel.action']
        assert graph[1][1:3] == (0x70000001, 0) and graph[1][9] == 8
        assert graph[2] == b''.join(struct.pack('<II', 0, v) for v in (0xffffffff, 0xfffffffe, 0xfffffffd, 0xfffffffc)), 'unsupported callgraph metadata'
        assert action[1][1:3] == (0x7000000b, 0) and action[1][9] == 8
        assert action[2] == bytes.fromhex('73000000000000000000001125000536'), 'unsupported linker action metadata'
    assert params[0x66] == [(4, struct.pack('<I', 3))] and params[0x37] == [(4, struct.pack('<I', 0x84))]
    assert params[0x35] == [(1, 0)] and params[0x5f] == [(3, 0)]
    # PTXAS lowers MAXREG_COUNT for large required blocks (1024 -> 64).
    # Accept only a typed cap within the architecture's 64Ki register budget;
    # the actual QMD register count must fit it, including warp allocation.
    block_threads=threads[0]*threads[1]*threads[2]
    allocated_threads=(block_threads+31)//32*32
    assert len(params[0x1b])==1 and params[0x1b][0][0]==3
    max_registers=params[0x1b][0][1]
    assert registers<=max_registers<=min(255,65536//allocated_threads)
    assert ((registers+7)//8*8)*allocated_threads<=65536
    assert len(params[0xa]) == 1 and params[0xa][0][0] == 4
    cbsym, base, size = struct.unpack('<IHH', params[0xa][0][1])
    assert cbsym < len(entries) and entries[cbsym][3] == ci and base == 0x160 and size == 8*parameter_count
    assert params[0x19] == [(3, size)]
    expected = [(4, struct.pack('<IHHI', 0, n, 8*n, 0x21f000)) for n in reversed(range(parameter_count))]
    assert params[0x17] == expected and params[0x10] == [(4, struct.pack('<III', *threads))]
    assert len(params[0x1c]) == 1 and params[0x1c][0][0] == 4 and len(params[0x1c][0][1]) % 4 == 0
    exits = [x[0] for x in struct.iter_unpack('<I', params[0x1c][0][1])]
    assert exits and len(set(exits)) == len(exits) and all(0 <= x < len(code) and x % 16 == 0 for x in exits)
    result = dict(target='sm_86', code_bytes=len(code), registers=registers, constant_bytes=len(cb),
        stack_bytes=0, shared_bytes=0, runtime_relocations=False, parameter_offset=base,
        parameter_offsets=list(range(0, size, 8)), parameter_sizes=[8]*parameter_count,
        required_threads=threads, max_registers=max_registers, uses_ctaid_z=0x04 in params, exit_offsets=exits, code_sha256=hashlib.sha256(code).hexdigest(),
        cubin_sha256=hashlib.sha256(raw).hexdigest(), gpu_commands_submitted=False)
    if helpers:
        result['embedded_helpers'] = helpers
    if 0x1e in params:
        result['call_return_stack_bytes'] = 0
    return code, result
