"""Independent raw artifact oracle. Does not import native/host upload code."""
from pathlib import Path
import hashlib,json,struct
GEN=0x130603601
def sha(b):return hashlib.sha256(b).hexdigest()
def info(raw,phase,written=4608,chunks=5,sealed=True,error=0,payload=None):
    assert len(raw)==256
    assert struct.unpack_from('<QIIQ',raw)==(0x52545855494e3336,1,256,GEN)
    assert struct.unpack_from('<10I',raw,24)==(phase,written,chunks,error,4608,1024,5,64,int(sealed),0)
    assert raw[136:]==bytes(120)
    if payload is not None:
        expected=hashlib.sha256(payload).digest();assert raw[64:96]==expected
        assert raw[96:128]==(expected if sealed else bytes(32))
    if sealed:
        count,used=struct.unpack_from('<II',payload,16)
        assert struct.unpack_from('<II',raw,128)==(count,used)
    else:assert raw[128:136]==bytes(8)

def bits(raw,start,width,value):
    assert 0<=value<1<<width
    old=int.from_bytes(raw,'little');mask=((1<<width)-1)<<start
    return ((old&~mask)|(value<<start)).to_bytes(len(raw),'little')

def verify(root,raw,baseline_qmd):
    root,raw=Path(root),Path(raw);payload=(root/'library.bin').read_bytes()+(root/'code.bin').read_bytes()
    names=set();read=lambda name:(names.add(name),(raw/name).read_bytes())[1]
    info(read('begin-info.bin'),1,0,0,False,payload=payload)
    for i in range(5):info(read('chunk-%d-info.bin'%i),1,min((i+1)*1024,4608),i+1,False,payload=payload)
    for phase,name in ((2,'ready'),(3,'consumed'),(5,'closed')):info(read(name+'-info.bin'),phase,payload=payload)
    b=read('bad-digest-info.bin');info(b,4,sealed=False,error=7)
    changed=bytearray(payload);changed[512]^=1
    assert b[64:96]==hashlib.sha256(payload).digest() and b[96:128]==hashlib.sha256(changed).digest()
    for count in range(6):
        b=read('unsealed-close-%d.bin'%count);info(b,5,min(count*1024,4608),count,False);assert b[64:128]==bytes(64)
    provenance=json.loads((root/'fixtures/provenance.json').read_bytes());programs=0
    for i,case in enumerate(provenance['cases']):
        p=(root/'fixtures'/case['path']).read_bytes();assert sha(p)==case['sha256'] and len(p)==case['bytes']==4608
        order=case['original_program_indices'];assert len(order)==struct.unpack_from('<I',p,16)[0]
        info(read('package-%d-info.bin'%i),3,payload=p)
        end=0
        for n,origin in enumerate(order):
            old=bytearray(payload[64+origin*112:64+(origin+1)*112]);_,oldoff,size,regs,local=struct.unpack_from('<5I',old)
            struct.pack_into('<II',old,0,n+1,end);assert p[64+n*112:64+(n+1)*112]==old
            assert p[512+end:512+end+size]==payload[512+oldoff:512+oldoff+size]
            assert not any(p[512+end+size:512+((end+size+255)&~255)])
            # Full plan compared with an already reviewed baseline plus only
            # descriptor-dependent fields, all at independently fixed bit positions.
            q=baseline_qmd;va=0x1020004000+end
            for start,width,value in ((256,32,va>>8),(1536,32,va&0xffffffff),(1568,17,va>>32),(1632,9,va>>40),(1641,9,(size+255)//256),
                                      (592,16,local),(648,9,regs),(829,1,1),(830,2,2),(832,32,n+1),(864,32,0)):
                q=bits(q,start,width,value)
            prefix='package-%d-program-%d'%(i,n)
            assert read(prefix+'-qmd.bin')==q
            cb=bytearray(4096);struct.pack_into('<Q',cb,40,0xfffdc0)
            for parameter in range(3):struct.pack_into('<Q',cb,0x160+parameter*8,0x1020005000+parameter*256)
            assert read(prefix+'-cb.bin')==bytes(cb)
            cmd=struct.pack('<14I',0x20012000,0xc7c0,0x200125a6,0x1011,0x200120ad,0x10200070,0x200120b0,9,0x20050017,0x20009010,0x10,n+1,0,0x01100001)
            assert read(prefix+'-command.bin')==cmd
            end=(end+size+255)&~255;programs+=1
        assert end==struct.unpack_from('<I',p,20)[0] and not any(p[512+end:])
    assert names=={p.name for p in raw.iterdir() if p.is_file()}
    return dict(passed=True,raw_files=len(names),packages=4,plans=programs,source_and_opcode_bytes_unchanged=True,gpu_commands_submitted=False)

if __name__=='__main__':
    import sys
    root=Path(__file__).resolve().parent
    print(json.dumps(verify(root,sys.argv[1],(root/'fixtures/baseline-qmd.bin').read_bytes())))
