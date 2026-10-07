"""Pure layout/parameter proposal; no backend, allocation, or device access."""
from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'gsp-channel-0.23'))
import channel_plan as c
import gmmu_plan as g
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'gsp-external-vas-0.27'))
import external_vas as external
CLIENT=external.CLIENT
DEVICE,SUBDEVICE,VASPACE=external.OBJECTS[1:]
BASE=0x03400000
CONTEXT_BASE=BASE+0x10000
CONTEXT_VA=0x1022000000
RING_VA=0x1020003000
CHANNEL=0xcf000007
GROUP=0xcf00000a
SHARE=0xcf00000b
GRAPHICS=0xcf00000c
def golden_fits(golden):
    c.promote_parameters(golden)
    pa=c.CONTEXT_PHYS_START;va=c.CONTEXT_VA;total=0
    for i,(b,kind) in enumerate(zip(golden['buffers'],(0,16,17,18,19,20,23,24,24))):
        if any(type(b.get(k)) is not int or b[k]<0 for k in ('buffer_id','gr_kind','size','allocated_bytes','alignment','physical','virtual')):raise ValueError('Golden context integer')
        if b['gr_kind']!=kind or b['use_physical']!=(i<2 or i>=6) or b['use_virtual']!=(b['buffer_id']!=10):raise ValueError('Golden context identity/flags')
        if b['physical']!=c.align(pa,b['alignment']) or b['virtual']!=c.align(va,b['alignment']):raise ValueError('Golden context packing')
        pa=b['physical']+b['allocated_bytes'];va=b['virtual']+b['allocated_bytes'];total+=b['allocated_bytes']
    if any(type(golden.get(k)) is not int for k in ('physical_end','virtual_end','total_backing_bytes')):raise ValueError('Golden context totals type')
    if (golden['physical_end'],golden['virtual_end'],golden['total_backing_bytes'])!=(pa,va,total):raise ValueError('Golden context totals')
    if pa>BASE or va>CONTEXT_VA:raise ValueError('Golden backing overlaps execution reservation')
def make(gr,golden):
    golden_fits(golden)
    if type(gr) is not bytes or len(gr)!=1664:raise ValueError('Fresh GR context payload required')
    pa=CONTEXT_BASE;va=CONTEXT_VA;buffers=[]
    for index in range(3):
        kind=16 if index else 0;amount,alignment=struct.unpack_from('<II',gr,kind*8)
        if amount in (0,0xffffffff):raise ValueError('Unavailable private context size')
        amount=c.align(amount+(0 if index else 0x40000),alignment)
        if amount>0x1000000:raise ValueError('Private context too large')
        allocated=c.align(amount,4096);alignment=max(4096,alignment);pa=c.align(pa,alignment);va=c.align(va,alignment)
        if pa+allocated>0x4000000 or va+allocated>g.VA_END:raise ValueError('Execution backing exceeds reservation')
        buffers.append(dict(id=index,kind=kind,size=amount,allocated=allocated,alignment=alignment,physical=pa,va=va));pa+=allocated;va+=allocated
    result=dict(valid=True,buffers=buffers,physical_end=pa,virtual_end=va,backing_bytes=sum(b['allocated'] for b in buffers))
    validate(result,golden);return result
def validate(p,golden):
    golden_fits(golden)
    if p.get('valid') is not True or len(p.get('buffers',[]))!=3:raise ValueError('Private context plan state')
    pa=CONTEXT_BASE;va=CONTEXT_VA;total=0
    for i,b in enumerate(p['buffers']):
        if any(type(b.get(k)) is not int or b[k]<0 for k in ('id','kind','size','allocated','alignment','physical','va')):raise ValueError('Private context integer')
        if b['id']!=i or b['kind']!=(16 if i else 0) or not 0<b['size']<=0x1000000 or (i==0 and b['size']<=0x40000) or b['alignment']<4096:raise ValueError('Private context identity/size')
        if b['allocated']!=c.align(b['size'],4096) or b['physical']!=c.align(pa,b['alignment']) or b['va']!=c.align(va,b['alignment']):raise ValueError('Private context packing')
        pa=b['physical']+b['allocated'];va=b['va']+b['allocated'];total+=b['allocated']
        if pa>0x4000000 or va>g.VA_END:raise ValueError('Private context bounds')
    a,b=p['buffers'][1:]
    if (a['size'],a['alignment'])!=(b['size'],b['alignment']):raise ValueError('Private patch/PM shape differs')
    if any(type(p.get(k)) is not int for k in ('physical_end','virtual_end','backing_bytes')) or (p['physical_end'],p['virtual_end'],p['backing_bytes'])!=(pa,va,total):raise ValueError('Private context totals')
# cid is the RM-session identifier; USERD flags independently request ChID4.
HARDWARE_CHANNEL_ID=4
def session_id(cid,predecessor=3):
    return type(cid) is int and 0<cid<0xffffffff and cid!=predecessor

def channel_parameters(golden,golden_channel_id):
    golden_fits(golden)
    if type(golden_channel_id) is not int or golden_channel_id!=3:raise ValueError('Fixed ChID4 requires verified predecessor ChID3')
    out=bytearray(368);struct.pack_into('<QII',out,8,RING_VA,32,0x200420);struct.pack_into('<II',out,24,SHARE,0)
    struct.pack_into('<Q',out,64,0x800);struct.pack_into('<II',out,128,0,4);struct.pack_into('<I',out,244,0x14)
    for offset,base,size in ((144,BASE+0x1000,4096),(168,BASE+0x800,512),(192,BASE+0x1000,512),(216,BASE+0x4000,0x5000)):
        struct.pack_into('<QQII',out,offset,base,size,2,0)
    return bytes(out)
def promotion(p,golden,physical):
    validate(p,golden)
    if type(physical) is not bool:raise ValueError('Promotion mode')
    out=bytearray(560);struct.pack_into('<I',out,0,1);struct.pack_into('<II',out,12,CLIENT,CHANNEL);struct.pack_into('<I',out,40,3 if physical else 10)
    for b in p['buffers']:
        struct.pack_into('<QQQIBBBB',out,48+b['id']*32,b['physical'] if physical else 0,0 if physical else b['va'],b['size'] if physical else 0,
                         4 if physical else 0,b['id'],0,physical,physical)
    if not physical:
        for i,b in enumerate(golden['buffers'][2:]):
            struct.pack_into('<QQQI HBB',out,48+(i+3)*32,0,b['virtual'],0,0,b['buffer_id'],0,0)
    return bytes(out)
def mappings(p,golden):
    validate(p,golden)
    return g.validate_ranges([(0x1020001000,BASE+0x2000,4096),(0x1020002000,BASE+0x3000,4096),(RING_VA,BASE,4096)]+
        [(b['va'],b['physical'],b['allocated']) for b in p['buffers']])
