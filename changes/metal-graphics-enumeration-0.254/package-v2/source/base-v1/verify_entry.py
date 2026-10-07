"""Independent entry ABI/capture oracle using frozen NVIDIA schemas and GPU plans."""
from pathlib import Path
import json,struct
from verify_backing import fixtures,step,CB,QMD
SERIALS=(1,32,33,65)
def verify(root,raw):
    root,raw=Path(root),Path(raw);device,fields,release=fixtures(root)
    assert (raw/'initial-device.bin').read_bytes()==device
    library=(root/'library.bin').read_bytes();files=1
    for serial in range(1,66):
        wire=step(root,device,fields,release,serial)
        if serial not in SERIALS:continue
        program=(serial-1)%3;entry=64+program*112
        offset,length=struct.unpack_from('<2I',library,entry+4);local=struct.unpack_from('<I',library,entry+16)[0];parameters=struct.unpack_from('<I',library,entry+28)[0]
        plan=bytearray(4096)
        struct.pack_into('<QIIQQ8I',plan,0,0x52545852504c3335,1,4096,0x30603501,serial,program,1,serial&31,((serial&31)+1)&31,offset,length,local,parameters)
        plan[64:2112]=wire[64:];plan[2112:3136]=device[CB:CB+1024]
        qmd=bytearray(device[QMD:QMD+256]);qmd[:4]=(root/('golden-plan-%d.bin'%program)).read_bytes()[3328:3332]
        plan[3136:3392]=qmd;plan[3392:3448]=device[4160:4216];plan[3448:3456]=device[(serial&31)*8:(serial&31)*8+8]
        expected=dict(request=wire,plan=bytes(plan),root=(root/'bootstrap-root.bin').read_bytes(),children=(root/'bootstrap-children.bin').read_bytes(),device=bytes(device))
        for name,b in expected.items():assert (raw/('job-%d-%s.bin'%(serial,name))).read_bytes()==b,(serial,name);files+=1
        info=struct.unpack('<64Q',(raw/('job-%d-info.bin'%serial)).read_bytes());job=struct.unpack('<128Q',(raw/('job-%d-job.bin'%serial)).read_bytes());files+=2
        assert info[:7]==(0x5254585254493335,1,0x30603501,1,serial,32,2112)
        assert info[7:19]==(1,1,1,18,1,1,6,1,1,serial,0,0)
        assert info[19:29]==(3,512,4096,24576,4096,0,1,1,22,90112)
        assert info[31:36]==(1,4,serial,1,40960) and not any(info[36:])
        assert job[:13]==(0x52545852544a3335,1,0x30603501,serial,1,serial,serial,0,1,1,0,7,1)
        assert job[17]==1 and job[20:30]==(1,1,1,1,1,1,1,0x12345678,0,0x12345678)
        assert job[40:47]==(4,serial,1,1,22,90112,0x0340e000) and job[50:53]==(1,1,1)
        assert job[60:64]==(program,1,serial&31,((serial&31)+1)&31)
        used=set(range(18))|set(range(20,34))|set(range(40,49))|{50,51,52,60,61,62,63}
        assert all(not v for i,v in enumerate(job) if i not in used)
    failed=struct.unpack('<128Q',(raw/'failed-before-window-job.bin').read_bytes());files+=1
    assert failed[:11]==(0x52545852544a3335,1,0x30603501,2,3,1,1,1,1,0,5)
    assert not any(failed[20:34]) and not any(failed[40:53]) and failed[60:64]==(1,1,2,3)
    assert len(list(raw.glob('*.bin')))==files==30
    return dict(passed=True,raw_files=30,captured_serials=list(SERIALS),results_checked=256,plan_bytes=4096,
        stale_failed_capture_rejected=True,full_device_guards_verified=True,root_children_unchanged=True,
        cpu_simulated=True,gpu_commands_submitted=False,metal_verified=False)
if __name__=='__main__':
    import sys
    print(json.dumps(verify(Path(__file__).resolve().parent,Path(sys.argv[1]))))
