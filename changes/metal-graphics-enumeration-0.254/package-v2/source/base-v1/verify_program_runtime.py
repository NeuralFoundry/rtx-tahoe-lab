"""Independent lifecycle/capture oracle, reusing the verified queue byte model."""
from pathlib import Path
import json,struct,sys
import verify_program_queue as queue_oracle
RAW=('bootstrap.bin',)+tuple(f'{kind}-{j}.bin' for j in range(4) for kind in ('canonical','request','root','children','device'))+(
    'outcomes.bin','events.bin','tokens.bin','wrong-arithmetic-allowed-location.bin','failed-stage-device.bin')
NATIVE=dict(passed=True,scenarios=5629,checks=5931322,dispatch_rejections=4931,prepare_open_rejections=689,argument_rejections=6,
            requests=4,programs=3,io_per_request=827,clocks_per_request=1635,timer_origins_per_request=5,prepare_reads=96,prepare_clocks=194,
            state_bytes=44632,full_capture_slots=4,close_retains=True,arithmetic_location_separation_verified=True,
            cpu_simulation_only=True,gpu_commands_submitted=False)


def verify(root,data):
    root=Path(root);reference=root/'program-queue-windows-v1'
    previous={name:(reference/name).read_bytes() for name in queue_oracle.RAW+('native.json',)}
    queue_oracle.verify(root,previous) # Recompute every predecessor byte; hashes alone are insufficient.
    sys.path.insert(0,str(root/'changes/gsp-program-library-0.33'))
    from image_profile import models,data_offset
    req,plans,canonical,states=models();expected={'bootstrap.bin':states[0]};events=[];outcomes=[]
    for j in range(4):
        for kind in ('canonical','root','children','device'):expected[f'{kind}-{j}.bin']=previous[f'{kind}-{j}.bin']
        expected[f'request-{j}.bin']=req[j]['wire']
        first=lambda off:struct.unpack_from('<I',plans[j],off)[0]
        events.extend((j,0x1704,4,0,j,0x03409000+data_offset(j),2048,first(256),
                       j,0x0340b000+j*1024,1024,first(2304),j,0x0340c000+j*256,256,first(3328),
                       j,0x03402040+j*64,32,first(3584),j,0x03400008+j*8,8,first(3616),
                       j,0x0340088c,4,j+2,j,0x1704,4,0x80173d90))
        outcomes.extend((j,j+1,j+1,1,1,0x80173d90,0x80173d90,3,3,2,22,1,0x306033f0+j,3,827,1635))
    expected['events.bin']=struct.pack('<128I',*events);expected['outcomes.bin']=struct.pack('<64Q',*outcomes)
    expected['tokens.bin']=previous['tokens.bin'];expected['wrong-arithmetic-allowed-location.bin']=previous['wrong-arithmetic-allowed-location.bin']
    failed=bytearray(previous['device-0.bin'])
    failed[8:16]=bytes(8);struct.pack_into('<II',failed,0x888,1,1);failed[4096+64:4096+96]=bytes(32)
    failed[12288:]=states[0];failed[12288+data_offset(0):12288+data_offset(0)+2048]=plans[0][256:2304]
    expected['failed-stage-device.bin']=bytes(failed)
    assert set(expected)==set(RAW) and len(RAW)==26
    for name,value in expected.items():assert data[name]==value,name
    assert json.loads(data['native.json'])==NATIVE
    return dict(passed=True,raw_files=26,requests=4,programs=3,simulated_results=256,window_writes=8,
                stage_writes=12,queue_writes=12,notifications=4,full_captures=4,prepare_bytes=24576,
                failed_stage_capture_preserved=True,canonical_not_committed_on_failed_stage=True,
                application_rejects_wrong_arithmetic=True,close_retains=True,
                cpu_simulation_only=True,gpu_commands_submitted=False,metal_verified=False,native=NATIVE)


def verify_directory(root,out):
    out=Path(out);return verify(root,{name:(out/name).read_bytes() for name in RAW+('native.json',)})


if __name__=='__main__':
    assert len(sys.argv)==2
    print(json.dumps(verify_directory(Path(__file__).resolve().parent,Path(sys.argv[1]))))
