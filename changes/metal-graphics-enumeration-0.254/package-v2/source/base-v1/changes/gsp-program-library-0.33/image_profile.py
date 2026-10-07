"""Independent request/image oracle using the verified named-field QMD model."""
from pathlib import Path
import json,struct
from profile import library,expected
ROOT=Path(__file__).resolve().parent
GENERATION=0x30603301
OPERATIONS=[lambda a,b:(a+b)&0xffffffff,lambda a,b:(a*b)&0xffffffff,lambda a,b:a^b]


def guard(offset):return (0x5a if offset>=20480 else 0xa5)^((offset*13+7)&255)
def data_offset(slot):return (4096 if slot<2 else 16384)+(slot%2)*2048


def requests(programs):
    result=[];seed=0x33001000
    for slot,index in enumerate((0,1,2,0)):
        a=[];b=[]
        for i in range(64):
            seed=(seed*1664525+1013904223)&0xffffffff;a.append(seed)
            seed=(seed*1664525+1013904223)&0xffffffff;b.append(seed)
        a[:4]=[0,0xffffffff,0x80000000,0x7fffffff];b[:4]=[0xffffffff,1,0x80000000,1]
        answer=[OPERATIONS[index](x,y) for x,y in zip(a,b)]
        data=struct.pack('<192I',*a,*b,*(x^0xffffffff for x in answer))+bytes(5*256)
        wire=struct.pack('<QIIQQII',0x5254585245513333,1,2112,GENERATION,slot+1,index,1)+bytes(24)+data
        assert len(wire)==2112
        result.append(dict(slot=slot,program=index,wire=wire,data=data,answer=struct.pack('<64I',*answer)))
    return result


def models():
    wire,code,programs=library();req=requests(programs)
    image=bytearray(guard(i) for i in range(24576));image[:4096]=code;image[8192:16384]=bytes(8192)
    for slot in range(4):image[20480+slot*256:20484+slot*256]=bytes(4)
    states=[bytes(image)];plans=[];canonicals=[]
    for r in req:
        slot=r['slot'];packet=expected(programs[r['program']],slot)
        plan=bytearray(4096);plan[:256]=packet[4384:4640]
        start=data_offset(slot);data=bytearray(guard(start+i) for i in range(2048));data[:768]=r['data'][:768]
        plan[256:2304]=data;plan[2304:3328]=packet[256:1280];plan[3328:3584]=packet[:256]
        plan[3584:3616]=packet[4352:4384];plan[3616:3624]=packet[4448:4456]
        plans.append(bytes(plan))
        image[start:start+2048]=data;image[8192+slot*1024:9216+slot*1024]=plan[2304:3328]
        image[12288+slot*256:12544+slot*256]=plan[3328:3584]
        canonicals.append(bytes(image))
        # Keep the expected CPU image unchanged; mutate a separate simulation.
        simulated=bytearray(image)
        for done in req[:slot+1]:
            at=data_offset(done['slot'])+512;simulated[at:at+256]=done['answer']
            struct.pack_into('<I',simulated,20480+done['slot']*256,0x306033f0+done['slot'])
        states.append(bytes(simulated))
    return req,plans,canonicals,states


def prepare():
    out=ROOT/'image-fixtures';assert not out.exists();out.mkdir()
    req,_,_,_=models()
    for r in req:(out/f'request-{r["slot"]}.bin').write_bytes(r['wire'])
    print(json.dumps(dict(requests=len(req),bytes_each=2112,simulated_only=True,gpu_commands_submitted=False)))


if __name__=='__main__':prepare()
