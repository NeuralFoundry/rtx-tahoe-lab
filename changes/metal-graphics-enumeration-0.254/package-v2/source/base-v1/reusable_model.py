"""Independent sealed-profile plan and complete backing proof; no device access."""
import struct
import program_model as sealed_model
import reusable_request as request
sealed=sealed_model.sealed
verify_sealed=sealed_model.verify_sealed
DEVICE_BYTES=36864
DATA,CB,QMD,FENCE=16384,20480,24576,32768
def initial():
    image=bytearray(sealed_model.initial());image[20480:20512]=bytes(32);return bytes(image)
def put(q,lo,width,v):
    if not 0<=v<1<<width:raise ValueError('QMD field range')
    x=int.from_bytes(q,'little');mask=((1<<width)-1)<<lo;q[:]=((x&~mask)|(v<<lo)).to_bytes(256,'little')
def plan(wire):
    r=request.decode(wire);library,_,programs=sealed();program=r['program'];serial=r['serial']
    profile=sealed_model._profile.expected(programs[program],0,1)
    q=bytearray(profile[:256]);put(q,829,1,1);put(q,830,2,2);put(q,832,32,serial&0xffffffff);put(q,864,32,serial>>32)
    command=profile[4352:4384]+struct.pack('<6I',0x20050017,0x20009010,0x10,serial&0xffffffff,serial>>32,0x01100001)
    start=64+program*112;offset,length=struct.unpack_from('<2I',library,start+4);parameters=struct.unpack_from('<I',library,start+28)[0]
    out=bytearray(4096);struct.pack_into('<QIIQQ8I',out,0,0x52545852504c3335,1,4096,r['generation'],serial,program,1,serial&31,((serial&31)+1)&31,offset,length,64,parameters)
    out[64:2112]=r['data'];out[2112:3136]=profile[256:1280];out[3136:3392]=q;out[3392:3448]=command
    struct.pack_into('<Q',out,3448,0x1020001040|(1<<41)|(14<<42));return bytes(out)
def bootstrap(root,children,device,before_root,before_children,before_device):
    import gsp_application_execution_native as execution
    if any(type(b) is not bytes for b in (root,children,device,before_root,before_children,before_device)):raise ValueError('Immutable captures required')
    if len(root)!=12288 or root!=before_root or len(device)!=DEVICE_BYTES or len(before_device)!=12288 or not 8192<=len(children)<=45056 or len(children)%4096 or len(children)!=len(before_children):raise ValueError('Bootstrap dimensions/root')
    expected=bytearray(before_children)
    for i in range(6):
        off=4096+(i+4)*8
        if struct.unpack_from('<Q',expected,off)[0]:raise ValueError('Occupied bootstrap PTE')
        struct.pack_into('<Q',expected,off,(6<<56)|((0x03409000+i*4096)>>4)|1)
    if children!=expected:raise ValueError('Bootstrap child PTEs changed')
    for i in range(6):
        for edge in (0,4095):
            if execution.tables.p.g.walk(root,children,0x1020004000+i*4096+edge)!=0x03409000+i*4096+edge:raise ValueError('Bootstrap page translation')
    if device!=before_device+initial():raise ValueError('Initial HOST/image bytes')
    if struct.unpack_from('<Q',device)[0]!=(0x1020001000|(1<<41)|(5<<42)) or any(device[8:256]):raise ValueError('Initial HOST ring')
    if struct.unpack_from('<2I',device,0x840)!=(0x20001014,0x20001014) or struct.unpack_from('<2I',device,0x888)!=(1,1):raise ValueError('Initial USERD')
    if device[4096:8192]!=struct.pack('<5I',0x20040004,0x10,0x20002000,0x30602401,0x01000002)+bytes(4076) or device[8192:12288]!=struct.pack('<I',0x30602401)+bytes(4092):raise ValueError('Initial HOST command/fence')
    return dict(passed=True,pte_entries=6,translation_edges=12,initial_fence_bytes=32,hardware_accessed=False,metal_verified=False)
def completed(previous,wire,actual):
    if type(previous) is not bytes or type(actual) is not bytes or len(previous)!=DEVICE_BYTES or len(actual)!=DEVICE_BYTES:raise ValueError('Complete device capture required')
    r=request.decode(wire);serial=r['serial'];p=plan(wire);prior=serial-1
    if struct.unpack_from('<2I',previous,0x888)!=(serial&31,serial&31) or struct.unpack_from('<Q',previous,FENCE)[0]!=prior or struct.unpack_from('<Q',previous,FENCE+16)[0]!=prior:raise ValueError('Previous retirement ordering')
    expected=bytearray(previous);expected[DATA:DATA+2048]=r['data'];expected[CB:CB+1024]=p[2112:3136];expected[QMD:QMD+256]=p[3136:3392];expected[4160:4216]=p[3392:3448]
    expected[(serial&31)*8:(serial&31)*8+8]=p[3448:3456]
    struct.pack_into('<2I',expected,0x840,0x20001078,0x20001078);index=((serial&31)+1)&31;struct.pack_into('<2I',expected,0x888,index,index)
    struct.pack_into('<Q',expected,FENCE,serial);struct.pack_into('<Q',expected,FENCE+16,serial)
    struct.pack_into('<64I',expected,DATA+512,*(request.answer(r['program'],a,b) for a,b in zip(r['a'],r['b'])))
    expected[QMD:QMD+256]=actual[QMD:QMD+256] # Completed QMD writeback is frozen for the next request.
    if actual!=expected:raise ValueError('Arithmetic, input, queue, timeline or protected backing changed')
    return dict(passed=True,serial=serial,program=r['program'],results_checked=64,ring_index=serial&31,ring_wraps=serial//32,
                qmd_serial=serial,host_timeline_serial=serial,hardware_accessed=False,metal_verified=False)
