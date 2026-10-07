"""Independent Python transport encoder supplies native parser test vectors."""
from pathlib import Path
import struct
import sys
import gsp_rpc


def prepare(output):
    output.mkdir(parents=True, exist_ok=True)
    for name, function, payload in (
        ('init-done', 0x1001, struct.pack('<I', 0x12345678)),
        ('sequencer', 0x1002, bytes(16)),
        ('multi-page', 0x1002, bytes(5000)),
    ):
        packet = gsp_rpc.encode_record(function, payload, result=0)
        (output / (name + '.bin')).write_bytes(packet)


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
