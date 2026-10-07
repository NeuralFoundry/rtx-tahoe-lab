"""Immutable vector data validation. Does not establish hardware provenance."""
from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
import vector_profile as v
HOST=struct.pack('<5I',0x20040004,0x10,0x20002000,0x30602401,0x1000002)
COMMAND=struct.pack('<8I',0x20012000,0xc7c0,0x200125a6,0x1011,0x200120ad,0x10200070,0x200120b0,9)
ENTRIES=struct.pack('<QQ',0x1020001000|(1<<41)|(5<<42),0x1020001040|(1<<41)|(8<<42))

def validate(queue,backing,initial):
    if type(queue) is not bytes or len(queue)!=12288:raise ValueError('Three captured queue pages required')
    if queue[:16]!=ENTRIES or any(queue[16:256]) or struct.unpack_from('<II',queue,0x888)!=(2,2):raise ValueError('GPFIFO/USERD mismatch')
    if queue[4096:8192]!=HOST+bytes(44)+COMMAND+bytes(4000) or queue[8192:]!=struct.pack('<I',0x30602401)+bytes(4092):raise ValueError('HOST/vector command and fence mismatch')
    v.validate_capture(backing,initial,61)
    return dict(bytes_valid=True,active_elements=61,inactive_elements=3,completion=v.COMPLETION,
                hardware_accessed=False,compute_verified=False,metal_verified=False)
