"""Build a bounded program container from audited compiler output; CPU only."""
from pathlib import Path
import hashlib,json,struct,sys
from cubin_audit import audit
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'baseline'))
import qmd_profile as Q


def library():
    wire=bytearray(512);code=bytearray(4096);cursor=0;programs=[]
    struct.pack_into('<QIIIII',wire,0,0x5254584c49423333,1,512,3,0,0x86)
    for index,name in enumerate(('add','multiply','xor')):
        folder=ROOT/'compiler'/name
        meta=json.loads((folder/'lowering.json').read_text())
        body,abi=audit((folder/'shader.cubin').read_bytes(),len(meta['parameter_bindings']),meta['local_size'])
        assert meta['entry']=='rtx_entry' and meta['target']=='sm_86' and meta['required_dispatch']=='whole-workgroups'
        bindings=meta['parameter_bindings'];assert 1<=len(bindings)<=8 and sorted(set(bindings))==bindings
        read=sum(1<<v for v in meta['read_bindings']);write=sum(1<<v for v in meta['written_bindings'])
        assert write and set(meta['read_bindings']+meta['written_bindings'])<=set(bindings)
        assert cursor%256==0 and len(body)%128==0 and cursor+len(body)<=4096
        at=64+index*112
        struct.pack_into('<11I',wire,at,index+1,cursor,len(body),abi['registers'],*meta['local_size'],len(bindings),read,write,abi['constant_bytes'])
        struct.pack_into('<'+'I'*len(bindings),wire,at+48,*bindings)
        code[cursor:cursor+len(body)]=body
        programs.append(dict(name=name,index=index,offset=cursor,code_bytes=len(body),registers=abi['registers'],
            local_size=meta['local_size'],bindings=bindings,read_mask=read,write_mask=write,
            constant_bytes=abi['constant_bytes'],code_sha256=abi['code_sha256'],cubin_sha256=abi['cubin_sha256']))
        cursor=(cursor+len(body)+255)&~255
    struct.pack_into('<I',wire,20,cursor)
    return bytes(wire),bytes(code),programs


def expected(program,slot,groups=1):
    assert 0<=slot<4 and groups>0 and groups*program['local_size'][0]<=64
    pv=Q.PROGRAM+program['offset'];cv=Q.CONSTANT+slot*1024;fv=Q.FENCE+slot*256;qv=Q.QMD+slot*256
    buffers=[(Q.PROGRAM+4096 if slot<2 else Q.OUTPUT)+(slot%2)*2048+n*256 for n in range(len(program['bindings']))]
    values=Q.values()
    values.update(PROGRAM_PREFETCH_ADDR_LOWER_SHIFTED=(pv>>8)&0xffffffff,PROGRAM_PREFETCH_ADDR_UPPER_SHIFTED=pv>>40,
        PROGRAM_ADDRESS_LOWER=pv&0xffffffff,PROGRAM_ADDRESS_UPPER=pv>>32,PROGRAM_PREFETCH_SIZE=(program['code_bytes']+255)//256,
        CTA_RASTER_WIDTH=groups,CTA_THREAD_DIMENSION0=program['local_size'][0],REGISTER_COUNT_V=program['registers'],
        RELEASE0_ADDRESS_LOWER=fv&0xffffffff,RELEASE0_ADDRESS_UPPER=fv>>32,RELEASE0_PAYLOAD_LOWER=0x306033f0+slot,
        **{'CONSTANT_BUFFER_ADDR_LOWER[0]':cv&0xffffffff,'CONSTANT_BUFFER_ADDR_UPPER[0]':cv>>32,
           'CONSTANT_BUFFER_SIZE_SHIFTED4[0]':(program['constant_bytes']+15)//16})
    schema=Q.schema();qmd=0
    for name,value in values.items():
        lo,width=schema[name];assert 0<=value<1<<width;qmd|=value<<lo
    constant=bytearray(4096);struct.pack_into('<Q',constant,0x28,0xfffdc0)
    for n,va in enumerate(buffers):struct.pack_into('<Q',constant,0x160+n*8,va)
    _,_,base_command=Q.build();command=bytearray(base_command);struct.pack_into('<I',command,20,qv>>8)
    launch=bytearray(256)
    struct.pack_into('<7I',launch,0,program['index'],slot,groups*program['local_size'][0],program['offset'],program['code_bytes'],program['registers'],len(buffers))
    struct.pack_into('<5Q',launch,32,pv,cv,qv,fv,(0x1020001040+slot*64)|(1<<41)|(8<<42))
    struct.pack_into('<'+'Q'*len(buffers),launch,72,*buffers)
    return qmd.to_bytes(256,'little')+bytes(constant)+bytes(command)+bytes(launch)


def prepare():
    out=ROOT/'fixtures';assert not out.exists();out.mkdir()
    wire,code,programs=library()
    (out/'library.bin').write_bytes(wire);(out/'code.bin').write_bytes(code)
    (out/'programs.json').write_text(json.dumps(programs,indent=2)+'\n')
    print(json.dumps(dict(programs=len(programs),used_code=struct.unpack_from('<I',wire,20)[0],wire_sha256=hashlib.sha256(wire).hexdigest(),code_sha256=hashlib.sha256(code).hexdigest(),gpu_commands_submitted=False)))


if __name__=='__main__':prepare()
