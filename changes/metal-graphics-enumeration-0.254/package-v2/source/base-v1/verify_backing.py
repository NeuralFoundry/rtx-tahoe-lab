"""Independent CPU capture oracle; imports no production driver modules."""
from pathlib import Path
import hashlib, json, re, struct

SERIALS = (1, 4, 31, 32, 33, 64, 65, 256)
IMAGE, DATA, CB, QMD, FENCE = 12288, 16384, 20480, 24576, 32768

def fixtures(root):
    for row in json.loads((root / 'bootstrap-provenance.json').read_bytes()):
        b = (root / row['path']).read_bytes()
        assert len(b) == row['bytes'] and hashlib.sha256(b).hexdigest() == row['sha256']
    device = bytearray((root / 'bootstrap-device.bin').read_bytes())
    code = (root / 'code.bin').read_bytes()
    expected = bytearray(24576)
    for i in range(24576):
        expected[i] = code[i] if i < 4096 else 0 if 8192 <= i < 16384 or (20480 <= i < 21504 and (i - 20480) % 256 < 4) else (0x5a if i >= 20480 else 0xa5) ^ ((i * 13 + 7) & 255)
    assert device[IMAGE:] == expected
    device[FENCE:FENCE+32] = bytes(32)
    assert struct.unpack_from('<Q', device)[0] == 0x1020001000 | (1 << 41) | (5 << 42)
    assert device[8:256] == bytes(248)
    assert struct.unpack_from('<2I', device, 0x840) == (0x20001014, 0x20001014)
    assert struct.unpack_from('<2I', device, 0x888) == (1, 1)
    definitions = {}
    for path in ('changes/gsp-compute-0.25/qmd3/reference/clc6c0qmd.h', 'changes/gsp-compute-0.25/submit/reference/clc56f.h'):
        for line in (root / path).read_text().splitlines():
            m = re.match(r'#define\s+(\w+)\s+(\S+)', line)
            if m: definitions[m[1]] = m[2]
    def field(name):
        high, low = map(int, re.fullmatch(r'MW\((\d+):(\d+)\)', definitions['NVC6C0_QMDV03_00_' + name]).groups())
        return low, high-low+1
    fields = {name: field(name) for name in ('RELEASE0_ADDRESS_LOWER', 'RELEASE0_ADDRESS_UPPER', 'RELEASE0_PAYLOAD64B', 'RELEASE0_STRUCTURE_SIZE', 'RELEASE0_PAYLOAD_LOWER', 'RELEASE0_PAYLOAD_UPPER')}
    assert fields['RELEASE0_PAYLOAD64B'] == (829, 1)
    def enum(name): return int(definitions[name].strip('()'), 0)
    release = enum('NVC56F_SEM_EXECUTE_OPERATION_RELEASE') | (enum('NVC56F_SEM_EXECUTE_RELEASE_WFI_EN') << 20) | (enum('NVC56F_SEM_EXECUTE_PAYLOAD_SIZE_64BIT') << 24)
    assert release == 0x01100001 and enum('NVC56F_SEM_EXECUTE_RELEASE_TIMESTAMP_DIS') == 0
    assert enum('NVC6C0_QMDV03_00_RELEASE0_STRUCTURE_SIZE_SEMAPHORE_TWO_WORDS') == 2
    return device, fields, release

def put_field(q, low, width, value):
    assert 0 <= value < (1 << width)
    x = int.from_bytes(q, 'little'); mask = ((1 << width)-1) << low
    q[:] = ((x & ~mask) | (value << low)).to_bytes(256, 'little')

def step(root, device, fields, release, serial):
    program = (serial-1) % 3
    gold = (root / ('golden-plan-%d.bin' % program)).read_bytes()
    qmd = bytearray(gold[3328:3584])
    values = dict(RELEASE0_ADDRESS_LOWER=0x20009000, RELEASE0_ADDRESS_UPPER=0x10, RELEASE0_PAYLOAD64B=1,
                  RELEASE0_STRUCTURE_SIZE=2, RELEASE0_PAYLOAD_LOWER=serial & 0xffffffff, RELEASE0_PAYLOAD_UPPER=serial >> 32)
    for name, value in values.items(): put_field(qmd, *fields[name], value)
    put_field(qmd, 1024, 32, 0x20006000); put_field(qmd, 1056, 17, 0x10)
    # The CPU transport models one explicit QMD writeback word. This is not
    # a hardware QMD-writeback oracle or proof of GPU retirement.
    struct.pack_into('<I', qmd, 0, serial & 0xffffffff)
    cb = bytearray(gold[2304:3328])
    for j in range(3): struct.pack_into('<Q', cb, 0x160+j*8, 0x1020005000+j*256)
    command = bytearray(gold[3584:3616]); struct.pack_into('<I', command, 20, 0x10200070)
    command += struct.pack('<6I', 0x20050017, 0x20009010, 0x10, serial & 0xffffffff, serial >> 32, release)
    seed = (serial & 0xffffffff) ^ 0x35bacc; values = []
    for _ in range(128):
        seed = (seed * 1664525 + 1013904223) & 0xffffffff; values.append(seed)
    payload = struct.pack('<128I', *values) + struct.pack('<64I', *[0xcafe0000+i for i in range(64)]) + bytes(1280)
    wire = struct.pack('<QIIQQII', 0x5254585245513335, 1, 2112, 0x30603501, serial, program, 1) + bytes(24) + payload
    results = [((a+b) if program == 0 else (a*b) if program == 1 else (a^b)) & 0xffffffff for a, b in zip(values[:64], values[64:])]
    device[DATA:DATA+2048] = payload[:512] + struct.pack('<64I', *results) + payload[768:]
    device[CB:CB+1024] = cb; device[QMD:QMD+256] = qmd; device[4160:4216] = command
    struct.pack_into('<Q', device, (serial & 31)*8, 0x1020001040 | (1 << 41) | (14 << 42))
    struct.pack_into('<2I', device, 0x840, 0x20001078, 0x20001078)
    put = ((serial & 31)+1) & 31
    struct.pack_into('<2I', device, 0x888, put, put)
    struct.pack_into('<Q', device, FENCE, serial); struct.pack_into('<Q', device, FENCE+16, serial)
    return wire

def verify(root, raw):
    root, raw = Path(root), Path(raw)
    device, fields, release = fixtures(root)
    assert (raw / 'initial-device.bin').read_bytes() == device
    files = 1; expected_first = None
    for serial in range(1, 257):
        wire = step(root, device, fields, release, serial)
        if serial == 1: expected_first = bytes(device)
        if serial not in SERIALS: continue
        items = {'request': wire, 'device': bytes(device), 'root': (root / 'bootstrap-root.bin').read_bytes(),
                 'children': (root / 'bootstrap-children.bin').read_bytes()}
        for name, b in items.items():
            actual = (raw / ('job-%d-%s.bin' % (serial, name))).read_bytes()
            assert actual == b, (serial, name)
            files += 1
    bad = (raw / 'wrong-arithmetic-allowed-location.bin').read_bytes()
    assert len(bad) == 36864 and bad[DATA+512:DATA+768] != expected_first[DATA+512:DATA+768]
    assert struct.unpack_from('<Q', bad, FENCE)[0] == struct.unpack_from('<Q', bad, FENCE+16)[0] == 1
    assert bad != expected_first  # Gate allows output locations; independent arithmetic rejects this.
    files += 1
    assert len(list(raw.glob('*.bin'))) == files == 34
    return dict(passed=True, raw_files=files, captured_serials=list(SERIALS), results_checked=512,
                full_device_bytes_per_capture=36864, root_bytes=12288, children_bytes=40960,
                preserved_actual_host_entry_until_wrap=True, rejected_wrong_arithmetic=True,
                cpu_simulated=True, gpu_commands_submitted=False, metal_verified=False)

if __name__ == '__main__':
    import sys
    print(json.dumps(verify(Path(__file__).resolve().parent, Path(sys.argv[1]))))
