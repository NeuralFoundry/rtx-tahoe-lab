"""Independent CPU queue/capture oracle using the frozen compiler profile."""
from pathlib import Path
import hashlib,json,struct,sys
RAW=tuple(f'{kind}-{j}.bin' for j in range(4) for kind in ('canonical','root','children','device'))+(
    'outcomes.bin','writes.bin','tokens.bin','wrong-arithmetic-allowed-location.bin','allowed-qmd-writeback.bin')
NATIVE=dict(passed=True,scenarios=4371,checks=5123406,rejected=4368,gate_denials=8896,requests=4,programs=3,
            io_per_request=724,clocks_per_request=1450,result_bytes=128,queue_bytes=712,
            arithmetic_location_separation_verified=True,cpu_simulation_only=True,gpu_commands_submitted=False)


def verify(root,data):
    root=Path(root);sys.path.insert(0,str(root/'changes/gsp-program-library-0.33'))
    from image_profile import models,data_offset
    req,plans,canonical,states=models()
    reference=root/'program-memory-windows-v2'
    root_bytes=(reference/'root.bin').read_bytes();child=(reference/'children-after.bin').read_bytes()
    assert hashlib.sha256(root_bytes).hexdigest()=='f0a33ea320ffbb8748e8a4460b40c02ae31d104c6edae1fdafc1dae689de56e7'
    assert hashlib.sha256(child).hexdigest()=='305d47956ffc065ab7613a3e12ecafa58043084cd61edf89b3a700f6096f7e4e'
    expected={};trace=[];outcomes=[]
    for j in range(4):
        ring=bytearray(4096);command=bytearray(4096);fence=bytearray(4096)
        struct.pack_into('<Q',ring,0,0x1020001000|(1<<41)|(5<<42))
        struct.pack_into('<II',ring,0x888,j+2,j+2)
        struct.pack_into('<5I',command,0,0x20040004,0x10,0x20002000,0x30602401,0x01000002)
        struct.pack_into('<I',fence,0,0x30602401)
        for prior in range(j+1):
            command[64+prior*64:96+prior*64]=plans[prior][3584:3616]
            ring[8+prior*8:16+prior*8]=plans[prior][3616:3624]
        expected[f'canonical-{j}.bin']=canonical[j];expected[f'root-{j}.bin']=root_bytes;expected[f'children-{j}.bin']=child
        expected[f'device-{j}.bin']=bytes(ring+command+fence)+states[j+1]
        trace.extend((j,0x03402040+j*64,32,j,0x03400008+j*8,8,j,0x0340088c,4))
        outcomes.extend((j,j+1,j+1,0,j+2,j+2,0x30602401,0x306033f0+j,3,2,720,1449))
    expected['writes.bin']=struct.pack('<36I',*trace);expected['tokens.bin']=struct.pack('<4I',4,4,4,4)
    expected['outcomes.bin']=struct.pack('<48Q',*outcomes)
    wrong=bytearray(expected['device-0.bin']);wrong[12288+data_offset(0)+512]^=1
    expected['wrong-arithmetic-allowed-location.bin']=bytes(wrong)
    assert wrong[12288+data_offset(0)+512:12288+data_offset(0)+768]!=req[0]['answer']
    qmd=bytearray(expected['device-0.bin']);qmd[12288+12288]^=1;expected['allowed-qmd-writeback.bin']=bytes(qmd)
    assert set(expected)==set(RAW) and len(RAW)==21
    for name,value in expected.items():assert data[name]==value,name
    assert json.loads(data['native.json'])==NATIVE
    return dict(passed=True,raw_files=21,requests=4,programs=3,simulated_results=256,queue_writes=12,notifications=4,
                root_children_unchanged=True,application_rejects_wrong_arithmetic=True,submitted_qmd_writeback_allowed=True,
                cpu_simulation_only=True,gpu_commands_submitted=False,metal_verified=False,native=NATIVE)


def verify_directory(root,out):
    out=Path(out);return verify(root,{name:(out/name).read_bytes() for name in RAW+('native.json',)})


if __name__=='__main__':
    assert len(sys.argv)==2
    print(json.dumps(verify_directory(Path(__file__).resolve().parent,Path(sys.argv[1]))))
