"""570.144 INIT_DONE: actual empty wire form and unused four-byte placeholder."""
import hashlib
import gsp_rpc


def parse(raw, *, expected_sequence):
    row = gsp_rpc.decode_record(raw, expected_sequence=expected_sequence)
    if row.rpc.function != 0x1001 or row.rpc.result or len(row.rpc.payload) not in (0, 4):
        raise ValueError('Invalid INIT_DONE function/result/envelope')
    return dict(function=0x1001, result=0, sequence=row.sequence,
                payload_bytes=len(row.rpc.payload), empty_payload=not row.rpc.payload,
                framing_verified=True, sha256=hashlib.sha256(raw).hexdigest(),
                native_ownership_verified=False, compute_verified=False, metal_verified=False)
