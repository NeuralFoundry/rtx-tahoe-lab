"""Independent byte oracle for per-program staging; no GPU-result assertion."""
from pathlib import Path
import json,struct,sys
RAW=('initial.bin',)+tuple(f'{kind}-{i}.bin' for i in range(4) for kind in ('request','plan','canonical','staged'))+('writes.bin',)
NATIVE=dict(passed=True,scenarios=555,checks=30379,rejected=554,gate_rejections=871,requests=4,
            operations_per_request=76,clocks_per_request=152,reads_per_request=73,writes_per_request=3,
            result_bytes=5672,state_bytes=43232,session_completion_simulated=True,cpu_simulation_only=True,gpu_commands_submitted=False)


def verify(root,data):
    root=Path(root);sys.path.insert(0,str(root/'changes/gsp-program-library-0.33'))
    from image_profile import models,data_offset
    req,plans,canonical,states=models();expected={'initial.bin':states[0]};trace=[]
    for j in range(4):
        expected[f'request-{j}.bin']=req[j]['wire'];expected[f'plan-{j}.bin']=plans[j]
        expected[f'canonical-{j}.bin']=canonical[j]
        # Only previous synthetic completion markers differ. No CPU-calculated
        # shader output is inserted; staging writes precisely the caller input.
        staged=bytearray(canonical[j])
        for prior in range(j):struct.pack_into('<I',staged,20480+prior*256,0x306033f0+prior)
        expected[f'staged-{j}.bin']=bytes(staged)
        for offset,size in ((data_offset(j),2048),(8192+j*1024,1024),(12288+j*256,256)):
            trace.extend((j,0x03409000+offset,size))
        assert staged[:4096]==states[0][:4096]
        assert staged[20480+j*256:20484+j*256]==bytes(4)
    expected['writes.bin']=struct.pack('<36I',*trace)
    assert set(expected)==set(RAW) and len(RAW)==18
    for name,value in expected.items():assert data[name]==value,name
    assert json.loads(data['native.json'])==NATIVE
    return dict(passed=True,raw_files=18,requests=4,programs=3,staging_writes=12,
                code_immutable=True,current_completion_zero=True,request_plan_matches_compiler_oracle=True,
                session_completion_simulated=True,cpu_simulation_only=True,gpu_commands_submitted=False,metal_verified=False,native=NATIVE)


def verify_directory(root,out):
    out=Path(out);return verify(root,{name:(out/name).read_bytes() for name in RAW+('native.json',)})


if __name__=='__main__':
    assert len(sys.argv)==2
    print(json.dumps(verify_directory(Path(__file__).resolve().parent,Path(sys.argv[1]))))
