"""CPU ABI and proposal checks. Does not import IOKit or access the GPU."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import unittest

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'out'
OUT.mkdir(exist_ok=False)


def run(args, log):
    with (OUT / log).open('w') as stream:
        subprocess.run(args, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, check=True)


common = ['clang++', '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
          '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
run(common + ['-isystem', 'sdk', 'sdk_oracle.cpp', '-o', 'out/sdk-oracle'], 'sdk-build.log')
run(['./out/sdk-oracle', 'out/sdk-parameters.bin'], 'sdk.json')
run(common + ['test_context_share.cpp', '-o', 'out/test-context-share'], 'test-build.log')
run(['./out/test-context-share', 'out/sdk-parameters.bin', 'evidence/requests.bin',
     'evidence/records.bin', 'out/parameters.bin'], 'test.json')


def u32(data, off):
    return struct.unpack_from('<I', data, off)[0]


def put(data, off, value):
    struct.pack_into('<I', data, off, value)


def checksum(data, size):
    value = 0
    for word in struct.unpack('<' + 'I' * (size // 4), data[:size]):
        value ^= word
    return value


def expected_parameters():
    group = struct.pack('<4IB3x', 0, 0, 0, 1, 0)
    share = struct.pack('<3I', 0xcf000003, 1, 0)
    channel = bytearray(368)
    struct.pack_into('<Q6I', channel, 8, 0x1020003000, 32, 0x200420,
                     0xcf00000b, 0, 0, 0)
    struct.pack_into('<Q', channel, 64, 0x800)
    struct.pack_into('<2I', channel, 128, 1, 4)
    put(channel, 244, 0x16)
    for offset, address, size in [(144, 0x03401000, 4096), (168, 0x03400800, 512),
                                  (192, 0x03401000, 512), (216, 0x03404000, 0x5000)]:
        struct.pack_into('<QQII', channel, offset, address, size, 2, 0)
    return [group, share, bytes(channel), bytes([1, 0])]


# Only these four request shapes are proposed here. They are not a complete
# executable RM transcript. Schedule would be step 11 of a future 13-step flow,
# after compute/context/copy setup, and before the final independent token query.
OPS = [(14, 103, 0xcf000001, 0xcf00000a, 0xa06c),
       (15, 103, 0xcf00000a, 0xcf00000b, 0x9067),
       (16, 103, 0xcf00000a, 0xcf000007, 0xc56f),
       (25, 76, 0, 0xcf00000a, 0xa06c0101)]


def frame(op, payload):
    sequence, function, parent, obj, command = op
    header_size = 32 if function == 103 else 24
    raw = bytearray(4096)
    for off, value in [(36, sequence), (40, 1), (48, 0x03000000), (52, 0x43505256),
                       (56, 32 + header_size + len(payload)), (60, function),
                       (64, 0xffffffff), (68, 0xffffffff), (80, 0xc1e00004)]:
        put(raw, off, value)
    if function == 103:
        struct.pack_into('<7I', raw, 84, parent, obj, command, 0, len(payload), 0, 0)
    else:
        struct.pack_into('<5I', raw, 84, obj, command, 0, len(payload), 0)
    raw[80 + header_size:80 + header_size + len(payload)] = payload
    put(raw, 32, checksum(raw, (80 + header_size + len(payload) + 7) & ~7))
    return bytes(raw)


class ProtocolTests(unittest.TestCase):
    def test_three_independent_parameter_encodings(self):
        expected = b''.join(expected_parameters())
        self.assertEqual(len(expected), 402)
        self.assertEqual(expected, (OUT / 'sdk-parameters.bin').read_bytes())
        self.assertEqual(expected, (OUT / 'parameters.bin').read_bytes())

    def test_actual_channel_input_uses_share_instead_of_direct_vaspace(self):
        actual = (ROOT / 'evidence/requests.bin').read_bytes()
        self.assertEqual(hashlib.sha256(actual).hexdigest(),
                         '96080e57002b226da1427eb349b7114c79c6edae251bce1a32d883440a088462')
        reply = (ROOT / 'evidence/records.bin').read_bytes()
        self.assertEqual(hashlib.sha256(reply).hexdigest(),
                         '2a9f84e95fe5a404cab4dbf4119c1697a53e29c2de975011ba2d629fa103687b')
        payload = bytearray(actual[112:480])
        self.assertEqual(u32(payload, 24), 0)
        put(payload, 24, 0xcf00000b)
        put(payload, 28, 0)
        self.assertEqual(payload, expected_parameters()[2])
        self.assertEqual(u32(reply, 244), 5)
        self.assertEqual((u32(payload, 20) >> 8) & 7, 4)

    def test_explicit_object_graph(self):
        self.assertEqual(OPS[0][2], 0xcf000001)
        self.assertEqual(OPS[1][2], OPS[0][3])
        self.assertEqual(OPS[2][2], OPS[0][3])
        self.assertEqual(u32(expected_parameters()[2], 24), OPS[1][3])
        self.assertEqual(OPS[3][3], OPS[0][3])
        self.assertEqual(len({row[3] for row in OPS[:3]}), 3)

    def test_proposed_request_envelopes(self):
        pages = [frame(op, p) for op, p in zip(OPS, expected_parameters())]
        for op, page in zip(OPS, pages):
            self.assertEqual(len(page), 4096)
            self.assertEqual(checksum(page, (48 + u32(page, 56) + 7) & ~7), 0)
            self.assertEqual(u32(page, 72), 0)  # 570.144 PF inner sequence
            self.assertEqual(u32(page, 36), op[0])
            self.assertEqual(u32(page, 60), op[1])
        actual = bytearray((ROOT / 'evidence/requests.bin').read_bytes()[:4096])
        put(actual, 36, 16)
        put(actual, 84, 0xcf00000a)
        put(actual, 136, 0xcf00000b)
        put(actual, 32, 0)
        put(actual, 32, checksum(actual, 480))
        self.assertEqual(actual, pages[2])


result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ProtocolTests))
assert result.wasSuccessful()
pages = [frame(op, payload) for op, payload in zip(OPS, expected_parameters())]
(OUT / 'proposal-requests.bin').write_bytes(b''.join(pages))
(OUT / 'python.json').write_text(json.dumps(dict(passed=True, tests=result.testsRun,
    source='independent struct encoding plus real 0.25.1 evidence',
    request_shapes=4, complete_rm_transcript=False, hardware_accessed=False,
    native_integrated=False, compute_verified=False, metal_verified=False), indent=2) + '\n')
