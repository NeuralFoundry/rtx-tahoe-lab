"""Independent named NVIDIA QMD fields and arithmetic oracle; CPU only."""
from pathlib import Path
import struct,sys
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'baseline'))
import qmd_profile as baseline
import shader_image
ELEMENTS=64;DEFAULT_COUNT=61;DEFAULT_SEED=0x30603001;COMPLETION=0x306030f0

def inputs(seed):
    if type(seed) is not int or not 0<=seed<=0xffffffff:raise ValueError('Seed')
    a=[];b=[]
    for _ in range(64):
        seed=(seed*1664525+1013904223)&0xffffffff;a.append(seed)
        seed=(seed*1664525+1013904223)&0xffffffff;b.append(seed)
    a[:8]=[0,0xffffffff,0x80000000,0x7fffffff,1,0xffffffff,0xaaaaaaaa,0x55555555]
    b[:8]=[0,1,0x80000000,1,0xffffffff,0xffffffff,0x55555555,0xaaaaaaaa]
    return a,b

def values():
    v=baseline.values()
    v.update(CTA_RASTER_WIDTH=2,CTA_THREAD_DIMENSION0=32,REGISTER_COUNT_V=12,RELEASE0_PAYLOAD_LOWER=COMPLETION,
             PROGRAM_PREFETCH_SIZE=2,**{'CONSTANT_BUFFER_SIZE_SHIFTED4[0]':24})
    return v

def build(code,a,b,count):
    if type(code) is not bytes or len(code)!=512:raise ValueError('Code size')
    if type(count) is not int or not 0<=count<=64:raise ValueError('Count')
    if type(a) not in (tuple,list) or type(b) not in (tuple,list) or len(a)!=64 or len(b)!=64 or any(type(x) is not int or not 0<=x<=0xffffffff for x in (*a,*b)):raise ValueError('Inputs')
    fields=baseline.schema();descriptor=0
    for name,value in values().items():
        low,width=fields[name]
        if not 0<=value<1<<width:raise ValueError('Field overflow')
        descriptor|=value<<low
    qmd=descriptor.to_bytes(256,'little');_,_,command=baseline.build()
    image=bytearray(24576);image[:512]=code;image[12288:12544]=qmd
    cb=8192;struct.pack_into('<Q',image,cb+0x28,0xfffdc0)
    struct.pack_into('<QQQI',image,cb+0x160,baseline.OUTPUT,baseline.CONSTANT+0x200,baseline.CONSTANT+0x300,count)
    struct.pack_into('<64I',image,cb+0x200,*a);struct.pack_into('<64I',image,cb+0x300,*b)
    sums=[(x+y)&0xffffffff for x,y in zip(a,b)]
    struct.pack_into('<64I',image,16384,*(x^0xffffffff for x in sums))
    image[16640:20480]=bytes(0xa5^((i*13+7)&255) for i in range(256,4096))
    image[20484:]=bytes(0x5a^((i*13+7)&255) for i in range(4,4096))
    expected=struct.pack('<64I',*(sums[i] if i<count else sums[i]^0xffffffff for i in range(64)))
    return bytes(image),command,expected

def validate_capture(actual,initial,count):
    if type(actual) is not bytes or type(initial) is not bytes or len(actual)!=24576 or len(initial)!=24576 or type(count) is not int or not 0<=count<=64:raise ValueError('Capture shape')
    if struct.unpack_from('<I',initial,8192+0x178)[0]!=count or any(initial[20480:20484]):raise ValueError('Initial profile')
    a=struct.unpack_from('<64I',initial,8192+0x200);b=struct.unpack_from('<64I',initial,8192+0x300)
    sums=[(x+y)&0xffffffff for x,y in zip(a,b)]
    if any(struct.unpack_from('<I',initial,16384+i*4)[0]!=(sums[i]^0xffffffff) for i in range(count)):raise ValueError('Initial poison')
    expected=bytearray(initial);expected[12288:12544]=actual[12288:12544]
    for i in range(count):struct.pack_into('<I',expected,16384+i*4,sums[i])
    struct.pack_into('<I',expected,20480,COMPLETION)
    if actual!=bytes(expected):raise ValueError('Arithmetic, completion or immutable memory mismatch')
    # A CPU helper cannot attest that its caller obtained bytes from hardware.
    return dict(bytes_valid=True,count=count,hardware_accessed=False,compute_verified=False,metal_verified=False)
