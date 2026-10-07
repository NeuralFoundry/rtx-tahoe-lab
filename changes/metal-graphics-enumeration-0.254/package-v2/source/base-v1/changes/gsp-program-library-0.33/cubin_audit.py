"""Byte-level CUDA ELF resource/parameter audit; not a runtime GPU loader."""
import hashlib, struct


def audit(raw, parameter_count, threads):
    assert type(raw) is bytes and 64 <= len(raw) <= 131072

    def region(off, size):
        assert 0 <= off <= len(raw) and 0 <= size <= len(raw)-off
        return raw[off:off+size]

    def string(table, off):
        assert 0 <= off < len(table)
        end = table.find(b'\0', off); assert end >= 0
        return table[off:end].decode('ascii')

    def attrs(data):
        at, result = 0, {}
        while at < len(data):
            assert at+4 <= len(data)
            typ, key, size = struct.unpack_from('<BBH', data, at); at += 4
            assert typ in (1, 3, 4) and (key not in result or key == 0x17)
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
    assert text[1:3] == (1, 6) and 0 < text[5] <= 4096 and text[5] % 128 == 0 and text[8] == 128
    registers = text[7] >> 24; assert 1 <= registers <= 255
    assert constant[1] == 1 and constant[5] == 0x160+8*parameter_count and constant[7] == ti and not any(cb)
    _, symbols, symbolbytes = sections['.symtab']; _, _, strings = sections['.strtab']
    assert symbols[9] == 24 and len(symbolbytes) % 24 == 0 and symbols[6] == sections['.strtab'][0]
    entries = list(struct.iter_unpack('<IBBHQQ', symbolbytes))
    matches = [(i, s) for i, s in enumerate(entries) if string(strings, s[0]) == 'rtx_entry']
    assert len(matches) == 1
    si, sym = matches[0]; assert sym[1:] == (0x12, 0x10, ti, 0, len(code))
    for row in rows:
        if row[1] in (4, 9):
            assert row[7] < len(rows) and not rows[row[7]][2] & 2, 'runtime relocation'
        if row[2] & 2:
            assert row is text or row is constant or not row[5], 'additional allocated section'
    info = attrs(sections['.nv.info'][2]); params = attrs(sections['.nv.info.rtx_entry'][2])
    assert info == {0x2f: [(4, struct.pack('<II', si, registers))],
                    0x11: [(4, struct.pack('<II', si, 0))], 0x12: [(4, struct.pack('<II', si, 0))]}
    assert set(params) == {0x66, 0x37, 0x35, 0xa, 0x19, 0x17, 0x1b, 0x5f, 0x1c, 0x10}
    assert params[0x66] == [(4, struct.pack('<I', 3))] and params[0x37] == [(4, struct.pack('<I', 0x84))]
    assert params[0x35] == [(1, 0)] and params[0x1b] == [(3, 255)] and params[0x5f] == [(3, 0)]
    assert len(params[0xa]) == 1 and params[0xa][0][0] == 4
    cbsym, base, size = struct.unpack('<IHH', params[0xa][0][1])
    assert cbsym < len(entries) and entries[cbsym][3] == ci and base == 0x160 and size == 8*parameter_count
    assert params[0x19] == [(3, size)]
    expected = [(4, struct.pack('<IHHI', 0, n, 8*n, 0x21f000)) for n in reversed(range(parameter_count))]
    assert params[0x17] == expected and params[0x10] == [(4, struct.pack('<III', *threads))]
    assert len(params[0x1c]) == 1 and params[0x1c][0][0] == 4 and len(params[0x1c][0][1]) % 4 == 0
    exits = [x[0] for x in struct.iter_unpack('<I', params[0x1c][0][1])]
    assert exits and len(set(exits)) == len(exits) and all(0 <= x < len(code) and x % 16 == 0 for x in exits)
    return code, dict(target='sm_86', code_bytes=len(code), registers=registers, constant_bytes=len(cb),
        stack_bytes=0, shared_bytes=0, runtime_relocations=False, parameter_offset=base,
        parameter_offsets=list(range(0, size, 8)), parameter_sizes=[8]*parameter_count,
        required_threads=threads, exit_offsets=exits, code_sha256=hashlib.sha256(code).hexdigest(),
        cubin_sha256=hashlib.sha256(raw).hexdigest(), gpu_commands_submitted=False)
