"""ABI semantic audit plus independently reconstructed plans/captured device bytes."""
from pathlib import Path
import json,re,struct,sys
import verify_program_runtime as runtime
RAW=('info-empty.bin','info-ready.bin','memory.bin')+tuple(f'{kind}-{j}.bin' for j in range(4) for kind in ('info','job','submit','plan','device'))+('info-closed.bin','capture-partial.bin','job-invalid.bin')
NATIVE=dict(passed=True,checks=24541,requests=4,programs=3,chunks=104,span_rejections=217,raw_files=26,cpu_simulation_only=True,gpu_commands_submitted=False)


def verify(root,data):
    root=Path(root);reference=root/'program-runtime-windows-v1'
    previous={n:(reference/n).read_bytes() for n in runtime.RAW+('native.json',)}
    runtime.verify(root,previous)
    sys.path.insert(0,str(root/'changes/gsp-program-library-0.33'))
    from image_profile import models,data_offset
    req,plans,canonical,states=models()
    sealed=(root/'changes/gsp-program-library-0.33/entry/SealedPrograms.hpp').read_text()
    for name,filename,count in [('Library','library.bin',512),('Code','code.bin',4096)]:
        text=re.search(r'\b'+name+r'\['+str(count)+r'\]=\{(.*?)\};',sealed,re.S).group(1)
        values=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',text))
        assert values==(root/'changes/gsp-program-library-0.33/fixtures'/filename).read_bytes()

    def record(name,magic,count):
        b=data[name];assert len(b)==count*8,name
        w=struct.unpack('<%dQ'%count,b);assert w[:3]==(magic,1,0x30603301),name
        return w

    def fields(w,expected):
        for i,v in expected.items():assert w[i]==v,(i,w[i],v)

    def info(name,completed,phase,empty=False,closed=False):
        w=record(name,0x5254585254493333,64)
        fields(w,{3:phase,4:completed,5:4,6:2112,7:not empty,8:not empty,9:not empty,10:7 if closed else 18,
                  11:1,12:1,13:6,14:1,15:phase==1,16:completed,17:phase==5,18:0,19:0 if empty else 3,
                  20:512,21:4096,22:24576,23:4096,24:closed})
        assert not any(w[36:])
        if empty:assert not any(w[25:])
        else:
            fields(w,{25:1,26:1,27:0,28:96,29:24576,32:1,33:1,34:0,35:0x80173d90})
            assert w[30]>0 and 0<w[31]<5000000000

    info('info-empty.bin',0,0,empty=True);info('info-ready.bin',0,1);info('info-closed.bin',4,6,closed=True)
    for j in range(4):
        assert data[f'plan-{j}.bin']==plans[j]
        assert data[f'device-{j}.bin']==previous[f'device-{j}.bin']
        info(f'info-{j}.bin',j+1,5 if j==3 else 1)
        w=record(f'job-{j}.bin',0x52545852544a3333,128)
        fields(w,{3:j,4:4,5:5 if j==3 else 1,6:j+1,7:1,8:1,9:1,10:1,11:1,12:1,13:1,14:0,15:0,
                  16:0x80173d90,17:0,18:0x80173d90,19:0x30603301,20:j+1,25:1,26:3,27:1,28:1,
                  29:1,30:1,31:1,32:1,33:1,34:1,35:1,36:1,37:0,38:j,40:73,41:3,42:3,43:0x0340e000+j*256,
                  46:1,47:1,48:3,49:j+1,50:1,51:1,52:0,53:12288,54:40960,55:36864,56:40960,57:22,
                  58:0x0340e000,59:0,61:1,62:0x306033f0+j,63:[0,1,2,0][j],64:j+1,65:0x30603301})
        assert not any(w[66:])
        for start,elapsed in [(21,22),(23,24),(44,45)]:assert w[start]>0 and 0<w[elapsed]<5000000000
        assert 0<w[39]<256 and 0<w[60]<5000000000
        w=record(f'submit-{j}.bin',0x52545842534d3333,64)
        fields(w,{3:j,4:1,5:0,6:1,7:1,8:1,9:1,10:1,11:1,12:1,14:720,15:3,16:2,17:4,
                  18:0x0340e000+j*256,19:j+1,20:j+1,21:0,22:j+2,23:j+2,24:0x30602401,25:0x306033f0+j,26:1,27:1,
                  30:0x03402040+j*64,31:0x03400008+j*8,32:0x03400888,33:0x0340088c,34:0x03409000+data_offset(j),
                  35:0x0340e000+j*256,36:0x1020001040+j*64,37:0x1020004000+data_offset(j),38:0x1020009000+j*256,
                  39:0x1020006000+j*1024,40:0x1020007000+j*256,41:0x00bb0090,42:5000000000,43:65536,
                  44:18,45:1,46:1,47:6,48:1,49:1,50:3,51:j+1,52:4,62:1,63:1})
        assert tuple(w[53:61])==struct.unpack('<8I',plans[j][3584:3616])
        assert w[61]==struct.unpack('<Q',plans[j][3616:3624])[0]
        assert 0<w[13]<65536 and w[28]>0 and 0<w[29]<5000000000
    w=record('memory.bin',0x525458424d4d3333,64)
    fields(w,{3:2,4:1,5:1,6:0,7:1,8:1,9:1,10:1,11:1,14:64,15:18,16:24576,18:24576,19:40960,
              20:40960,21:6,29:1,30:1,31:1,34:6,35:3,41:1,45:18,46:1,47:1,48:6,57:1,58:1,59:1,60:18,61:3,62:0x03409000,63:24576})
    w=record('capture-partial.bin',0x5254584243503333,64)
    fields(w,{3:1,4:0,5:5,6:12288,7:40960,8:4096,9:40960,10:22,11:0x0340e000,12:0,14:7,15:1,16:1,17:6,
              18:0x01002000,19:0x01005000,20:12288,21:45056,22:36864,23:5000000000})
    assert w[24:33]==(0x03400000,0x03402000,0x03403000)+tuple(0x03409000+j*4096 for j in range(6)) and not any(w[33:])
    w=record('job-invalid.bin',0x52545852544a3333,128);assert w[3:5]==(4,4) and not any(w[5:])
    assert json.loads(data['native.json'])==NATIVE
    return dict(passed=True,raw_files=26,abi_records=18,independently_reconstructed_plans=4,
                independently_reconstructed_captures=4,native=NATIVE,driver_entry_integrated=True,
                kext_built=False,cpu_simulation_only=True,gpu_commands_submitted=False,metal_verified=False)


def verify_directory(root,out):
    out=Path(out);return verify(root,{n:(out/n).read_bytes() for n in RAW+('native.json',)})


if __name__=='__main__':
    assert len(sys.argv)==2
    print(json.dumps(verify_directory(Path(__file__).resolve().parent,Path(sys.argv[1]))))
