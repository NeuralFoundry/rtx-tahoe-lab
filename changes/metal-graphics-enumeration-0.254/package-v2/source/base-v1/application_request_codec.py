"""OS-independent little-endian application ABI; no GPU or driver access."""
import struct
MAGIC=0x5254584A4F423332
SIZE=576
ELEMENTS=64
SLOTS=4

def encode(generation,request_id,a,b):
    if type(generation) is not int or not 0<generation<2**64:
        raise ValueError('generation')
    if type(request_id) is not int or not 1<=request_id<=SLOTS:
        raise ValueError('request id')
    if len(a)!=len(b) or len(a)>ELEMENTS:
        raise ValueError('element count')
    if any(type(x) is not int or not 0<=x<2**32 for x in list(a)+list(b)):
        raise ValueError('uint32 inputs')
    return (struct.pack('<QIIQQII24x',MAGIC,1,SIZE,generation,request_id,len(a),1)+
            struct.pack('<64I',*(list(a)+[0]*(ELEMENTS-len(a))))+
            struct.pack('<64I',*(list(b)+[0]*(ELEMENTS-len(b)))))

def decode(data):
    if type(data) is not bytes or len(data)!=SIZE:
        raise ValueError('size or type')
    magic,version,size,generation,request_id,count,operation=struct.unpack_from('<QIIQQII',data)
    if (magic,version,size,operation)!=(MAGIC,1,SIZE,1) or any(data[40:64]):
        raise ValueError('header')
    if not generation or not 1<=request_id<=SLOTS or count>ELEMENTS:
        raise ValueError('identity or count')
    a=struct.unpack_from('<64I',data,64);b=struct.unpack_from('<64I',data,320)
    if any(a[count:]) or any(b[count:]):
        raise ValueError('noncanonical unused inputs')
    return dict(generation=generation,request_id=request_id,a=list(a[:count]),b=list(b[:count]))
