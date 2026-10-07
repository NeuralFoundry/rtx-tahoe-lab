"""Independent QMD3 schema and candidate first-kernel packet; no hardware."""
import hashlib,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent
PROGRAM=0x1020004000;CONSTANT=0x1020006000;QMD=0x1020007000;OUTPUT=0x1020008000;FENCE=0x1020009000
OUTPUT_VALUE=0x30602501;FENCE_VALUE=0x306025f0

def schema():
    raw=(ROOT/'reference/clc6c0qmd.h').read_bytes()
    if hashlib.sha256(raw).hexdigest()!='9427646887d0620e5dbf0957b04d979413319c7dc37839267c05d7c6408cb004':raise ValueError('Pinned QMD header hash')
    fields={}
    def term(t,i):
        if t.isdecimal():return int(t)
        m=re.fullmatch(r'\((\d+)\+\(i\)\*(\d+)\)',t)
        if not m or i is None:raise ValueError('Unsupported source expression')
        return int(m[1])+i*int(m[2])
    for line in raw.decode().splitlines():
        if not line.startswith('#define NVC6C0_QMDV03_00_') or ' MW(' not in line:continue
        m=re.fullmatch(r'#define NVC6C0_QMDV03_00_([A-Z0-9_]+)(\(i\))?\s+MW\((.+):(.+)\)',line)
        if not m:raise ValueError('Unparsed source field')
        for i in range(8) if m[2] else (None,):
            high,low=term(m[3],i),term(m[4],i);name=m[1]+('['+str(i)+']' if i is not None else '')
            if not 0<=low<=high<2048 or high-low>=32 or name in fields:raise ValueError('Invalid field')
            fields[name]=(low,high-low+1)
    coverage=0
    for low,width in fields.values():
        bits=((1<<width)-1)<<low
        if coverage&bits:raise ValueError('Overlapping QMD fields')
        coverage|=bits
    if coverage.bit_length()!=2048:raise ValueError('Incomplete QMD schema')
    return fields

def values():
    return dict(QMD_GROUP_ID=0x3f,SM_GLOBAL_CACHING_ENABLE=1,
      INVALIDATE_TEXTURE_HEADER_CACHE=1,INVALIDATE_TEXTURE_SAMPLER_CACHE=1,INVALIDATE_TEXTURE_DATA_CACHE=1,
      INVALIDATE_SHADER_DATA_CACHE=1,INVALIDATE_INSTRUCTION_CACHE=1,INVALIDATE_SHADER_CONSTANT_CACHE=1,
      PROGRAM_PREFETCH_ADDR_LOWER_SHIFTED=(PROGRAM>>8)&0xffffffff,CWD_MEMBAR_TYPE=1,API_VISIBLE_CALL_LIMIT=1,SAMPLER_INDEX=1,
      CTA_RASTER_WIDTH=1,CTA_RASTER_HEIGHT=1,CTA_RASTER_DEPTH=1,SHARED_MEMORY_SIZE=0,MIN_SM_CONFIG_SHARED_MEM_SIZE=9,
      MAX_SM_CONFIG_SHARED_MEM_SIZE=0x1a,QMD_VERSION=0,QMD_MAJOR_VERSION=3,CTA_THREAD_DIMENSION0=1,CTA_THREAD_DIMENSION1=1,
      CTA_THREAD_DIMENSION2=1,REGISTER_COUNT_V=8,TARGET_SM_CONFIG_SHARED_MEM_SIZE=9,SHADER_LOCAL_MEMORY_LOW_SIZE=0,BARRIER_COUNT=0,
      RELEASE0_ADDRESS_LOWER=FENCE&0xffffffff,RELEASE0_ADDRESS_UPPER=FENCE>>32,RELEASE0_MEMBAR_TYPE=1,RELEASE0_ENABLE=1,
      RELEASE0_STRUCTURE_SIZE=1,RELEASE0_PAYLOAD_LOWER=FENCE_VALUE,PROGRAM_ADDRESS_LOWER=PROGRAM&0xffffffff,
      PROGRAM_ADDRESS_UPPER=PROGRAM>>32,SHADER_LOCAL_MEMORY_HIGH_SIZE=0,PROGRAM_PREFETCH_ADDR_UPPER_SHIFTED=PROGRAM>>40,
      PROGRAM_PREFETCH_SIZE=1,SASS_VERSION=0x86,**{'CONSTANT_BUFFER_VALID[0]':1,'CONSTANT_BUFFER_ADDR_LOWER[0]':CONSTANT&0xffffffff,
      'CONSTANT_BUFFER_ADDR_UPPER[0]':CONSTANT>>32,'CONSTANT_BUFFER_INVALIDATE[0]':1,'CONSTANT_BUFFER_SIZE_SHIFTED4[0]':23})

def build():
    fields=schema();qmd=0
    for name,value in values().items():
        low,width=fields[name]
        if not 0<=value<1<<width:raise ValueError('QMD value overflow')
        qmd|=value<<low
    cb=bytearray(4096);struct.pack_into('<Q',cb,0x28,0xfffdc0);struct.pack_into('<Q',cb,0x160,OUTPUT)
    raw=(ROOT/'reference/clc7c0.h').read_bytes()
    if hashlib.sha256(raw).hexdigest()!='ba8cf3280eaf9836e92400d063c7bc33f1e0bc1887ce69521a2ec530c2366a8a':raise ValueError('Pinned compute class header hash')
    text=raw.decode()
    def hexval(name):return int(re.search(r'^#define '+name+r'\s+(0x[0-9a-fA-F]+)\s*$',text,re.M)[1],16)
    def shift(name):return int(re.search(r'^#define '+name+r'\s+\d+:(\d+)\s*$',text,re.M)[1])
    invalid='NVC7C0_INVALIDATE_SHADER_CACHES_NO_WFI'
    masks=sum(hexval(invalid+'_'+field+'_TRUE')<<shift(invalid+'_'+field) for field in ('INSTRUCTION','GLOBAL_DATA','CONSTANT'))
    methods=((hexval('NVC7C0_SET_OBJECT'),0xc7c0),(hexval(invalid),masks),(hexval('NVC7C0_SEND_PCAS_A'),QMD>>8),
      (hexval('NVC7C0_SEND_SIGNALING_PCAS2_B'),hexval('NVC7C0_SEND_SIGNALING_PCAS2_B_PCAS_ACTION_PREFETCH_SCHEDULE')))
    command=b''.join(struct.pack('<II',0x20012000|(m>>2),v) for m,v in methods)
    return qmd.to_bytes(256,'little'),bytes(cb),command
