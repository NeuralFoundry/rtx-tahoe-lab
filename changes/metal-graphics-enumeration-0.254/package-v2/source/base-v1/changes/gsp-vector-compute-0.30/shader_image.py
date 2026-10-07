"""Strict extraction of one pinned compiler profile, not a general CUDA loader."""
import hashlib,struct
NAME='rtx_probe_vectoradd'
CUBIN_SHA='f53a64e63a9aa349fb4144de75be782fba0f6fbeef50140e389f0679947b966a'
CODE_SHA='72df57907aa481126f3e96617d56d87c6d26bab24b3250ef62ba3643b3efef47'

def region(raw,off,size):
    if not 0<=off<=len(raw) or not 0<=size<=len(raw)-off:raise ValueError('ELF range')
    return raw[off:off+size]

def info(raw):
    pos=0;result={}
    while pos<len(raw):
        typ,attr,size=struct.unpack('<BBH',region(raw,pos,4));pos+=4
        if typ not in (1,3,4) or (attr in result and attr!=0x17):raise ValueError('CUDA attribute')
        value=region(raw,pos,size) if typ==4 else size
        if typ==4:pos+=size
        result.setdefault(attr,[]).append((typ,value))
    return result

def extract(raw):
    if type(raw) is not bytes or len(raw)>65536:raise ValueError('Bounded immutable cubin')
    h=struct.unpack('<16sHHIQQQIHHHHHH',region(raw,0,64))
    if h[0]!=b'\x7fELF\x02\x01\x01A\x08'+bytes(7) or h[1:4]!=(2,190,1) or h[4]!=0 or h[7]!=0x06005604 or h[8:]!=(64,56,3,64,14,1):raise ValueError('sm_86 ELF header')
    for ph in struct.iter_unpack('<IIQQQQQQ',region(raw,h[5],h[10]*56)):
        region(raw,ph[2],ph[5])
        if ph[5]>ph[6]:raise ValueError('Segment memory size')
    rows=list(struct.iter_unpack('<IIQQQQIIQQ',region(raw,h[6],h[12]*64)))
    strings=region(raw,rows[h[13]][4],rows[h[13]][5]);sections={}
    def string(table,off):
        if not 0<=off<len(table):raise ValueError('String offset')
        end=table.find(b'\0',off)
        if end<0:raise ValueError('Unterminated string')
        try:return table[off:end].decode('ascii')
        except UnicodeDecodeError as e:raise ValueError('Non-ASCII symbol') from e
    for i,row in enumerate(rows):
        name=string(strings,row[0]);data=b'' if row[1]==8 else region(raw,row[4],row[5])
        if name in sections:raise ValueError('Duplicate section')
        sections[name]=(i,row,data)
    try:
        ti,text,code=sections['.text.'+NAME];_,cb,constant=sections['.nv.constant0.'+NAME]
        _,symbols,symbytes=sections['.symtab'];_,_,strbytes=sections['.strtab']
        attrs=info(sections['.nv.info'][2]);params=info(sections['.nv.info.'+NAME][2])
    except KeyError as e:raise ValueError('Missing kernel section') from e
    if text[1]!=1 or text[2]!=6 or text[5]!=512 or text[8]!=128 or text[7]>>24!=12 or cb[1]!=1 or cb[5]!=380 or cb[7]!=ti or any(constant):raise ValueError('Kernel code/resources/constants')
    if symbols[9]!=24 or len(symbytes)%24 or symbols[6]!=sections['.strtab'][0]:raise ValueError('Symbol table')
    entries=list(struct.iter_unpack('<IBBHQQ',symbytes));matches=[(i,s) for i,s in enumerate(entries) if string(strbytes,s[0])==NAME]
    if len(matches)!=1:raise ValueError('Kernel entry symbol')
    si,symbol=matches[0]
    if symbol[1:]!=(0x12,0x10,ti,0,512):raise ValueError('Kernel entry shape')
    for row in rows:
        if row[1] in (4,9) and (row[7]>=len(rows) or rows[row[7]][2]&2):raise ValueError('Runtime relocation requires loader')
        if row[2]&2 and row[1]==8 and row[5]:raise ValueError('Unexpected shader BSS/shared')
    expected_attrs={0x2f:[(4,struct.pack('<II',si,12))],0x11:[(4,struct.pack('<II',si,0))],0x12:[(4,struct.pack('<II',si,0))]}
    if attrs!=expected_attrs:raise ValueError('Register/stack metadata')
    expected_params={0x66:[(4,struct.pack('<I',3))],0x37:[(4,struct.pack('<I',0x84))],0x35:[(1,0)],
        0xa:[(4,struct.pack('<IHH',4,0x160,28))],0x19:[(3,28)],0x1b:[(3,255)],0x5f:[(3,0)],
        0x1c:[(4,struct.pack('<II',0x50,0xf0))],0x10:[(4,struct.pack('<III',32,1,1))],
        0x17:[(4,struct.pack('<IHHI',0,ordinal,offset,flags)) for ordinal,offset,flags in
              ((3,24,0x11f000),(2,16,0x21f000),(1,8,0x21f000),(0,0,0x21f000))]}
    if params!=expected_params:raise ValueError('Parameter/exit/required-thread ABI')
    if hashlib.sha256(code).hexdigest()!=CODE_SHA:raise ValueError('Pinned shader instructions')
    report=dict(target='sm_86',code_bytes=512,registers=12,stack_bytes=0,shared_bytes=0,
        constant_section_bytes=380,parameter_offset=0x160,parameter_bytes=28,parameter_offsets=[0,8,16,24],parameter_sizes=[8,8,8,4],
        required_threads=[32,1,1],exit_offsets=[0x50,0xf0],code_sha256=CODE_SHA,cubin_sha256=hashlib.sha256(raw).hexdigest(),
        runtime_relocations=False,hardware_accessed=False,compute_verified=False,metal_verified=False)
    return code,report
