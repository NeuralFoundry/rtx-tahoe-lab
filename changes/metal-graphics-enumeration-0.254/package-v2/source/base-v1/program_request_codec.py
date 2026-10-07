"""Immutable 0.33 request wire for the three sealed sm_86 programs; no I/O."""
import struct
MAGIC=0x5254585245513333
SIZE=2112
PROGRAMS=('add','multiply','xor')


def integer(value,name,low,high):
    if type(value) is not int or not low<=value<=high:raise ValueError(name)
    return value


def answer(program,a,b):
    integer(program,'program',0,2)
    return (a+b)&0xffffffff if program==0 else (a*b)&0xffffffff if program==1 else a^b


def encode_data(generation,request_id,program,groups,data):
    integer(generation,'generation',1,2**64-1);integer(request_id,'request id',1,4)
    integer(program,'program',0,2);integer(groups,'whole workgroups',1,1)
    if type(data) is not bytes or len(data)!=2048 or any(data[768:]):raise ValueError('parameter data')
    return struct.pack('<QIIQQII24x',MAGIC,1,SIZE,generation,request_id,program,groups)+data


def encode(generation,request_id,program,a,b,initial=None):
    integer(program,'program',0,2)
    if type(a) not in (list,tuple) or type(b) not in (list,tuple) or len(a)!=64 or len(b)!=64:raise ValueError('64 input elements required')
    for value in list(a)+list(b):integer(value,'uint32 input',0,2**32-1)
    if initial is None:initial=[answer(program,x,y)^0xffffffff for x,y in zip(a,b)]
    if type(initial) not in (list,tuple) or len(initial)!=64:raise ValueError('initial output dimensions')
    for value in initial:integer(value,'uint32 output seed',0,2**32-1)
    return encode_data(generation,request_id,program,1,struct.pack('<192I',*a,*b,*initial)+bytes(1280))


def decode(wire):
    if type(wire) is not bytes or len(wire)!=SIZE:raise ValueError('request size/type')
    magic,version,size,generation,request_id,program,groups=struct.unpack_from('<QIIQQII',wire)
    if (magic,version,size)!=(MAGIC,1,SIZE) or any(wire[40:64]):raise ValueError('request header')
    encode_data(generation,request_id,program,groups,wire[64:])
    return dict(generation=generation,request_id=request_id,program=program,groups=groups,
                data=wire[64:],a=list(struct.unpack_from('<64I',wire,64)),b=list(struct.unpack_from('<64I',wire,320)),
                initial=list(struct.unpack_from('<64I',wire,576)))
