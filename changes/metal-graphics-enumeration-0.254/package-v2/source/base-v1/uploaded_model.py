"""Plan/backing oracle bound to one immutable, compiler-reviewed catalog."""
import struct
import program_model as baseline
import uploaded_request as request
from uploaded_library import Catalog,need
DEVICE_BYTES=36864
DATA,CB,QMD,FENCE=16384,20480,24576,32768
def put(q,lo,width,v):
    need(0<=v<1<<width,'QMD field range');x=int.from_bytes(q,'little');mask=((1<<width)-1)<<lo;q[:]=((x&~mask)|(v<<lo)).to_bytes(256,'little')

class Model:
    def __init__(self,catalog):need(type(catalog) is Catalog,'catalog');self.catalog=catalog
    def verify_library(self,library,code):self.catalog.verify(library,code)
    def initial(self):
        image=bytearray(baseline.guard(i) for i in range(24576));image[:4096]=self.catalog.code;image[8192:16384]=bytes(8192)
        for j in range(4):image[20480+j*256:20484+j*256]=bytes(4)
        image[20480:20512]=bytes(32);return bytes(image)
    def plan(self,wire):
        r=request.decode(self.catalog,wire);serial=r['serial'];p=self.catalog.record(r['program'])
        profile=baseline._profile.expected(p,0,r['groups']);q=bytearray(profile[:256])
        put(q,829,1,1);put(q,830,2,2);put(q,832,32,serial&0xffffffff);put(q,864,32,serial>>32)
        command=profile[4352:4384]+struct.pack('<6I',0x20050017,0x20009010,0x10,serial&0xffffffff,serial>>32,0x01100001)
        out=bytearray(4096);struct.pack_into('<QIIQQ8I',out,0,0x52545852504c3335,1,4096,r['generation'],serial,r['program'],r['groups'],serial&31,((serial&31)+1)&31,p['offset'],p['code_bytes'],r['invocations'],r['parameters'])
        out[64:2112]=r['data'];out[2112:3136]=profile[256:1280];out[3136:3392]=q;out[3392:3448]=command
        struct.pack_into('<Q',out,3448,0x1020001040|(1<<41)|(14<<42));return bytes(out)
    def bootstrap(self,root,children,device,before_root,before_children,before_device):
        import gsp_application_execution_native as execution
        need(all(type(b) is bytes for b in (root,children,device,before_root,before_children,before_device)),'immutable captures')
        need(len(root)==12288 and root==before_root and len(device)==DEVICE_BYTES and len(before_device)==12288 and 8192<=len(children)<=45056 and len(children)%4096==0 and len(children)==len(before_children),'bootstrap dimensions/root')
        expected=bytearray(before_children)
        for i in range(6):
            off=4096+(i+4)*8;need(struct.unpack_from('<Q',expected,off)[0]==0,'occupied PTE');struct.pack_into('<Q',expected,off,(6<<56)|((0x03409000+i*4096)>>4)|1)
        need(children==expected,'bootstrap PTEs')
        for i in range(6):
            for edge in (0,4095):need(execution.tables.p.g.walk(root,children,0x1020004000+i*4096+edge)==0x03409000+i*4096+edge,'bootstrap translation')
        need(device==before_device+self.initial(),'initial HOST/image bytes')
        need(struct.unpack_from('<Q',device)[0]==(0x1020001000|(1<<41)|(5<<42)) and not any(device[8:256]),'initial ring')
        need(struct.unpack_from('<2I',device,0x840)==(0x20001014,0x20001014) and struct.unpack_from('<2I',device,0x888)==(1,1),'initial USERD')
        need(device[4096:8192]==struct.pack('<5I',0x20040004,0x10,0x20002000,0x30602401,0x01000002)+bytes(4076) and device[8192:12288]==struct.pack('<I',0x30602401)+bytes(4092),'initial HOST command/fence')
        return dict(passed=True,pte_entries=6,translation_edges=12,initial_fence_bytes=32,hardware_accessed=False,metal_verified=False)
    def completed(self,previous,wire,actual):
        need(type(previous) is bytes and type(actual) is bytes and len(previous)==len(actual)==DEVICE_BYTES,'complete backing')
        r=request.decode(self.catalog,wire);serial=r['serial'];p=self.plan(wire);prior=serial-1
        need(struct.unpack_from('<2I',previous,0x888)==(serial&31,serial&31) and struct.unpack_from('<Q',previous,FENCE)[0]==prior and struct.unpack_from('<Q',previous,FENCE+16)[0]==prior,'prior retirement order')
        output,reference=request.evaluate(self.catalog,wire)
        expected=bytearray(previous);expected[DATA:DATA+2048]=output;expected[CB:CB+1024]=p[2112:3136];expected[QMD:QMD+256]=p[3136:3392];expected[4160:4216]=p[3392:3448]
        expected[(serial&31)*8:(serial&31)*8+8]=p[3448:3456];struct.pack_into('<2I',expected,0x840,0x20001078,0x20001078)
        index=((serial&31)+1)&31;struct.pack_into('<2I',expected,0x888,index,index);struct.pack_into('<Q',expected,FENCE,serial);struct.pack_into('<Q',expected,FENCE+16,serial)
        expected[QMD:QMD+256]=actual[QMD:QMD+256] # Freeze actual completed QMD for the next request.
        need(actual==expected,'shader output, input, queue, timeline or protected backing changed')
        return dict(passed=True,serial=serial,program=r['program'],results_checked=reference['output_words'],ring_index=serial&31,ring_wraps=serial//32,
                    qmd_serial=serial,host_timeline_serial=serial,reference=reference,hardware_accessed=False,metal_verified=False)
