"""Host codec for the proposed pre-bootstrap library-upload ABI; no I/O on import.

The body is the existing LIB33 descriptor followed by a padded 4096-byte code
image. The supplied bytes still need independent compiler/ELF/ABI review.
A matching upload digest does not authenticate code or validate SASS semantics.
"""
import hashlib,struct

HEADER_MAGIC=0x52545855504c3336
INFO_MAGIC=0x52545855494e3336
PAYLOAD_BYTES=4608
CHUNK_BYTES=1024

def integer(value,name,lo,hi):
    if type(value) is not int or not lo<=value<=hi:raise ValueError(name)
    return value

def bytes_exact(value,size,name):
    if type(value) is not bytes or len(value)!=size:raise ValueError(name)
    return value

def begin_header(generation,library,code):
    integer(generation,'generation',1,2**64-1)
    body=bytes_exact(library,512,'library')+bytes_exact(code,4096,'code')
    return struct.pack('<QIIQ4I',HEADER_MAGIC,1,128,generation,PAYLOAD_BYTES,CHUNK_BYTES,5,0x86)+hashlib.sha256(body).digest()+bytes(56)

def upload_chunks(library,code):
    body=bytes_exact(library,512,'library')+bytes_exact(code,4096,'code')
    return tuple((offset,body[offset:offset+CHUNK_BYTES]) for offset in range(0,PAYLOAD_BYTES,CHUNK_BYTES))

def decode_info(raw):
    bytes_exact(raw,256,'info');magic,version,size,generation=struct.unpack_from('<QIIQ',raw)
    phase,written,chunks,error,total,chunk_bytes,count,max_threads,sealed,reserved=struct.unpack_from('<10I',raw,24)
    programs,code_bytes=struct.unpack_from('<II',raw,128)
    if (magic,version,size)!=(INFO_MAGIC,1,256) or phase>5 or error>9 or reserved or any(raw[136:]):raise ValueError('info header')
    if (total,chunk_bytes,count,max_threads)!=(4608,1024,5,64) or sealed not in (0,1):raise ValueError('info limits')
    if chunks>5 or written!=min(chunks*1024,4608):raise ValueError('info progress')
    if phase==0 and (generation or written or error or sealed or any(raw[64:136])):raise ValueError('empty state')
    if phase and not generation:raise ValueError('owner generation')
    if phase in (2,3) and not sealed:raise ValueError('unsealed ready')
    if phase in (0,1,4) and sealed:raise ValueError('unexpected seal')
    if sealed and (written!=4608 or error or not 1<=programs<=4 or not 0<code_bytes<=4096 or code_bytes%256 or raw[64:96]!=raw[96:128]):raise ValueError('seal')
    if not sealed and (programs or code_bytes):raise ValueError('unsealed metadata')
    if (phase==4 and error not in (7,8,9)) or (phase not in (4,5) and error):raise ValueError('terminal error')
    return dict(generation=generation,phase=phase,written=written,chunks=chunks,error=error,sealed=bool(sealed),
                programs=programs,code_bytes=code_bytes,expected_sha256=raw[64:96].hex(),actual_sha256=raw[96:128].hex())
