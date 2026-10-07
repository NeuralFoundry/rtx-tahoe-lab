"""CPU-only proposal for a future execution channel; no I/O on import."""
from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
import gsp_rpc as rpc
CLIENT=0xc1e00004
CHANNEL=0xcf000007
CONTROLS=(0xa06f0104,0xa06f0103,0xc36f0108)
PARAMS=(struct.pack('<I',1),b'\x01\x00',b'\xff'*4)
COMMAND_VA=0x1020001000
FENCE_VA=0x1020002000
FENCE_VALUE=0x30602401
def integer(v,limit):
    if type(v) is not int or not 0<=v<limit:raise ValueError('Bounded unsigned integer')
    return v
def request(step,sequence):
    integer(step,3);integer(sequence,0xffffffff)
    return rpc.encode_record(76,struct.pack('<6I',CLIENT,CHANNEL,CONTROLS[step],0,len(PARAMS[step]),0)+PARAMS[step],transport_sequence=sequence)
def reply(raw,step,sequence):
    integer(step,3);integer(sequence,0xffffffff)
    r=rpc.decode_record(raw,expected_sequence=sequence)
    if r.rpc.function!=76 or r.rpc.result or r.rpc.private_result or r.rpc.sequence:raise ValueError('Control envelope')
    expected=rpc.decode_record(request(step,sequence)).rpc.payload
    if len(r.rpc.payload)!=len(expected) or r.rpc.payload[:24]!=expected[:24]:raise ValueError('Control identity/status/size')
    params=r.rpc.payload[24:]
    result=dict(accepted=True,hardware_accessed=False,doorbell_ready=False,compute_verified=False)
    if step<2:
        if params!=PARAMS[step]:raise ValueError('Control parameter echo')
    else:
        result['raw_token']=struct.unpack('<I',params)[0]
        if result['raw_token']==0xffffffff:raise ValueError('Unchanged token sentinel')
    return result
def userd(offset,declared,backing):
    for v in (offset,declared,backing):integer(v,1<<32)
    return offset%4==0 and declared>=512 and offset<=backing and declared<=backing-offset
def entry(address,bytes_count):
    integer(address,1<<40);integer(bytes_count,4097)
    if not address or address%4 or not bytes_count or bytes_count%4 or address+bytes_count>1<<40:raise ValueError('GPFIFO address/size')
    return struct.pack('<Q',address|(1<<41)|((bytes_count//4)<<42))
def increment(method,subchannel,count):
    integer(method,0x4000);integer(subchannel,8);integer(count,8192)
    if method%4 or not count or method+count*4>0x4000:raise ValueError('Increment method range')
    return (1<<29)|(count<<16)|(subchannel<<13)|(method//4)
def fence():
    return struct.pack('<5I',increment(0x10,0,4),FENCE_VA>>32,FENCE_VA&0xffffffff,FENCE_VALUE,0x01000002)
