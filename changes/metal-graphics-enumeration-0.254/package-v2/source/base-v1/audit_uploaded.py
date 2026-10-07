"""Saved raw native runtime checked against authored arithmetic and old layout.

Does not use the production uploaded request/model/native modules. Only the
byte-level prior descriptor/QMD oracle and saved library provenance are read.
"""
from pathlib import Path
import json,struct
import program_model
def bits(q,lo,width,value):
    assert 0<=value<1<<width;n=int.from_bytes(q,'little');mask=((1<<width)-1)<<lo;return ((n&~mask)|(value<<lo)).to_bytes(256,'little')
def verify(root,raw):
    root,raw=Path(root),Path(raw);fixtures=root/'selected-fixtures';library=(fixtures/'library.bin').read_bytes();code=(fixtures/'code.bin').read_bytes()
    meta=json.loads((root/'selected-library-provenance.json').read_bytes());assert len(meta['programs'])==4
    programs=meta['programs'];gen=0x130603601;images=[];checked=0
    initial=(raw/'initial-device.bin').read_bytes();assert len(initial)==36864
    expected=bytearray(program_model.guard(i) for i in range(24576));expected[:4096]=code;expected[8192:16384]=bytes(8192)
    for i in range(4):expected[20480+i*256:20484+i*256]=bytes(4)
    expected[20480:20512]=bytes(32);assert initial[12288:]==bytes(expected)
    before_root=(root/'reusable-fixture-baseline/before-root.bin').read_bytes();before_child=(root/'reusable-fixture-baseline/before-children.bin').read_bytes()
    assert (raw/'initial-root.bin').read_bytes()==before_root;children=bytearray(before_child)
    for i in range(6):
        off=4096+(i+4)*8;assert children[off:off+8]==bytes(8);struct.pack_into('<Q',children,off,(6<<56)|((0x03409000+i*4096)>>4)|1)
    assert (raw/'initial-children.bin').read_bytes()==children
    assert initial[:12288]==(root/'reusable-fixture-baseline/before-device.bin').read_bytes();previous=initial
    for serial in range(1,66):
        prefix=raw/('job-%d'%serial);read=lambda suffix:Path(str(prefix)+'-'+suffix+'.bin').read_bytes()
        wire=read('request');plan=read('plan');actual=read('device');job=struct.unpack('<128Q',read('job'));info=struct.unpack('<64Q',read('info'))
        assert wire==(fixtures/('request-%d.bin'%serial)).read_bytes()
        magic,abi,size,generation,number,index,groups=struct.unpack_from('<QIIQQII',wire);assert (magic,abi,size,generation,number)==(0x5254585245513335,1,2112,gen,serial)
        assert index==(serial-1)%4 and groups==(2 if index==0 and ((serial-1)//4)%2 else 1)
        p=programs[index];invocations=groups*p['local_size'][0];params=len(p['bindings']);data=bytearray(wire[64:]);assert not any(wire[40:64]) and not any(data[params*256:])
        a=struct.unpack_from('<64I',data);b=struct.unpack_from('<64I',data,256)
        for i in range(invocations):
            if index==0:v=(a[i]*b[i]+struct.unpack_from('<I',data,512+4*i)[0])^(i*0x9e3779b9);offset=768
            elif index==1:v=a[i]^b[i];offset=512
            elif index==2:v=a[i]+b[i];offset=512
            else:v=a[i]*b[i];offset=512
            struct.pack_into('<I',data,offset+4*i,v&0xffffffff)
        assert bytes(data)==(fixtures/('expected-%d.bin'%serial)).read_bytes()
        profile=program_model._profile.expected(p,0,groups);q=profile[:256]
        for lo,width,value in ((829,1,1),(830,2,2),(832,32,serial),(864,32,0)):q=bits(q,lo,width,value)
        assert struct.unpack_from('<QIIQQ8I',plan)==(0x52545852504c3335,1,4096,gen,serial,index,groups,serial&31,((serial&31)+1)&31,p['offset'],p['code_bytes'],invocations,params)
        assert plan[64:2112]==wire[64:] and plan[2112:3136]==profile[256:1280] and plan[3136:3392]==q
        command=profile[4352:4384]+struct.pack('<6I',0x20050017,0x20009010,0x10,serial,0,0x01100001)
        assert plan[3392:3448]==command and struct.unpack_from('<Q',plan,3448)[0]==(0x1020001040|(1<<41)|(14<<42)) and not any(plan[3456:])
        assert read('root')==before_root and read('children')==children
        next_index=((serial&31)+1)&31;expected=bytearray(previous);expected[16384:18432]=data;expected[20480:21504]=plan[2112:3136]
        expected[24576:24832]=actual[24576:24832];expected[4160:4216]=command;expected[(serial&31)*8:(serial&31)*8+8]=plan[3448:3456]
        struct.pack_into('<2I',expected,0x840,0x20001078,0x20001078);struct.pack_into('<2I',expected,0x888,next_index,next_index)
        struct.pack_into('<Q',expected,32768,serial);struct.pack_into('<Q',expected,32784,serial)
        assert actual==expected
        assert info[:3]==(0x5254585254493335,1,gen) and info[4]==info[16]==serial and info[15]==1 and info[19]==4 and info[18]==0
        assert job[:4]==(0x52545852544a3335,1,gen,serial) and job[9]==job[17]==1 and job[10]==0 and job[11:13]==(7,1) and job[40:46]==(4,serial,1,1,22,90112)
        previous=actual;checked+=invocations
    assert checked==3872
    return dict(passed=True,jobs=65,outputs=checked,ring_wraps=2,full_backing_states=66,compiler_programs=4,new_sparse_bindings=[0,3,7,11],new_group_counts=[1,2],gpu_commands_submitted=False)
if __name__=='__main__':
    import sys
    print(json.dumps(verify(Path(__file__).resolve().parent,sys.argv[1])))
