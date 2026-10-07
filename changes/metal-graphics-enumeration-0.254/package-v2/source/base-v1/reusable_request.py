"""Fixed three-program request ABI for the reusable 0.35 runtime. No I/O."""
import struct
MAGIC=0x5254585245513335
WIRE_BYTES=2112
MAX_SERIAL=2**64-1
def integer(value,name,low,high):
    if type(value) is not int or not low<=value<=high:raise ValueError('Invalid '+name)
    return value
def encode(generation,serial,program,a,b):
    integer(generation,'generation',1,MAX_SERIAL);integer(serial,'serial',1,MAX_SERIAL);integer(program,'program',0,2)
    if type(a) not in (list,tuple) or type(b) not in (list,tuple) or len(a)!=64 or len(b)!=64:raise ValueError('Exactly 64 values per input')
    values=[integer(v,'input',0,0xffffffff) for v in [*a,*b]]
    payload=struct.pack('<128I',*values)+struct.pack('<64I',*[0xcafe0000+i for i in range(64)])+bytes(1280)
    return struct.pack('<QIIQQII',MAGIC,1,WIRE_BYTES,generation,serial,program,1)+bytes(24)+payload
def decode(wire):
    if type(wire) is not bytes or len(wire)!=WIRE_BYTES:raise ValueError('Request size/type')
    magic,abi,size,generation,serial,program,groups=struct.unpack_from('<QIIQQII',wire)
    if (magic,abi,size)!=(MAGIC,1,WIRE_BYTES) or any(wire[40:64]) or any(wire[832:]):raise ValueError('Request ABI/reserved bytes')
    integer(generation,'generation',1,MAX_SERIAL);integer(serial,'serial',1,MAX_SERIAL);integer(program,'program',0,2)
    if groups!=1:raise ValueError('Sealed programs require one group of 64')
    a=struct.unpack_from('<64I',wire,64);b=struct.unpack_from('<64I',wire,320)
    return dict(generation=generation,serial=serial,program=program,groups=groups,a=a,b=b,data=wire[64:])
def answer(program,a,b):return ((a+b) if program==0 else (a*b) if program==1 else a^b)&0xffffffff
def requests(generation,count=65):
    integer(count,'job count',1,65);result=[]
    for serial in range(1,count+1):
        seed=serial^0x35bacc;values=[]
        for _ in range(128):seed=(seed*1664525+1013904223)&0xffffffff;values.append(seed)
        result.append(encode(generation,serial,(serial-1)%3,values[:64],values[64:]))
    return result
