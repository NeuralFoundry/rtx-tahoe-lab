"""Combine the pinned real first packet with independent Python test records."""
import hashlib
from pathlib import Path
import sys
import gsp_rpc


def generate(output):
    root = Path(__file__).resolve().parent
    first = (root/'results/gsp-first-boot-fix-20260906T172355Z/first-status.bin').read_bytes()
    if hashlib.sha256(first).hexdigest() != '18f195493d3068be1c0ce542899740c2408010fc4ea8f7bcbd1dbd13d50de2e0':
        raise ValueError('Recorded first packet differs from its pin')
    parsed = gsp_rpc.decode_record(first, expected_sequence=0)
    if parsed.rpc.function != 0x1020:
        raise ValueError('Wrong recorded event')
    data = first + gsp_rpc.encode_record(0x101e, bytes(5000), transport_sequence=1, result=0)
    data += gsp_rpc.encode_record(0x1001, bytes(4), transport_sequence=2, result=0)
    output.mkdir(parents=True, exist_ok=True)
    (output/'events.bin').write_bytes(data)


if __name__ == '__main__': generate(Path(sys.argv[1]))
