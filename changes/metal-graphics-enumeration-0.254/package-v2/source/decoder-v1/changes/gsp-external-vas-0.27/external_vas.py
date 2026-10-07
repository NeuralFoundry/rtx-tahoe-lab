"""Independent CPU encoding of the five external-VA setup messages."""
import struct
import gsp_rpc
CLIENT=0xc1000000
OBJECTS=(0xcf000010,0xcf000011,0xcf000012,0xcf000013)
PARENTS=(0,CLIENT,OBJECTS[1],OBJECTS[1])
CLASSES=(0,0x80,0x2080,0x90f1)
SIZES=(120,56,4,48,48)
FIRST=14
STEPS=5
ROOT_PA=0x1002000
REQUIRED_LO=0x1000000000
REQUIRED_HI=0x1040000000

def directory(physical,flags):
    if type(physical)is not int or type(flags)is not int or flags!=9 or not 4096<=physical<=(1<<40)-4096 or physical%4096:
        raise ValueError('Owned SYS root address/flags')
    return physical,flags

def parameters(step,root=None):
    if type(step) is not int or not 0<=step<STEPS:raise ValueError('Five fixed external VA steps')
    p=bytearray(SIZES[step])
    if step==1:struct.pack_into('<I',p,4,CLIENT)
    if step==3:struct.pack_into('<IIQQQIIQ',p,0,0,0x48,0x1fffffb000000,0,0,0,0,0x1000)
    if step==4:
        physical,flags=(ROOT_PA,8)if root is None else directory(*root)
        struct.pack_into('<4IQ6I',p,0,CLIENT,OBJECTS[1],0xffffffff,0,physical,4,flags,OBJECTS[3],0,1,0xffffffff)
    return bytes(p)

def request(step,root=None):
    if root is not None:directory(*root)
    p=parameters(step,root)
    if step<4:p=struct.pack('<8I',CLIENT,PARENTS[step],OBJECTS[step],CLASSES[step],0,SIZES[step],0,0)+p
    return gsp_rpc.encode_record(103 if step<4 else 54,p,transport_sequence=FIRST+step)

def reply(step,sequence,raw):
    expected=parameters(step)
    if len(raw)!=4096 or type(sequence) is not int or not 0<=sequence<0xffffffff:raise ValueError('Reply bounds')
    p=gsp_rpc.decode_record(raw,expected_sequence=sequence).rpc
    if p.function!=(103 if step<4 else 54) or p.result or p.private_result or p.sequence:raise ValueError('Reply identity/status')
    if len(p.payload)!=(SIZES[step]+32 if step<4 else 0):raise ValueError('Reply payload size')
    if step==4:return dict(accepted=True,step=step)  # Exact status-only ACK; unused slot tail is not payload.
    if step<4:
        if struct.unpack_from('<8I',p.payload)!=(CLIENT,PARENTS[step],OBJECTS[step],CLASSES[step],0,SIZES[step],0,0):raise ValueError('Allocation ownership/status')
        params=p.payload[32:]
    else:params=p.payload
    result=dict(accepted=True,step=step)
    if step==0:expected=struct.pack('<I',CLIENT)+expected[4:]
    if step==3:
        index,flags,size,lo,hi,big,pad,base=struct.unpack('<IIQQQIIQ',params)
        if index or flags!=0x48 or pad or not 0x1000<=base<=REQUIRED_LO or base%4096 or not REQUIRED_HI<=size<=1<<49 or size%4096 or big not in (0,65536,131072):raise ValueError('Returned VA extent/flags')
        if (lo or hi) and (lo>hi or hi>=size or not(hi<REQUIRED_LO or lo>=REQUIRED_HI)):raise ValueError('Returned internal VA overlap')
        result.update(base=base,size=size,internal_lo=lo,internal_hi=hi,big_page=big)
    elif params!=expected:raise ValueError('Changed setup parameters')
    return result
