"""External test references for three programs compiled during one GPU session.

This is a test oracle, never the application's or driver's execution backend.
"""
import hashlib,struct
from rounding_workload_oracle import authored

def equivalent(a,b):return a==b
from rounding_pool import pool

JOBS=65
OPERATIONS=('round_vec4', 'round_loop', 'round_select')
BODIES=('float4 p=float4(a[tid],b[tid],a[tid]+1.0f,b[tid]-2.0f); float4 q=floor(p.wzyx)+trunc(p); out[tid]=fma(q.x,q.z,q.y*q.w);', 'float2 p=float2(a[tid],b[tid]); uint n=tid&3u;\n#pragma clang loop unroll(disable)\nfor(uint i=0u;i<n;++i){p=ceil(p*float2(0.5f,2.0f))+rint(p.yx);} out[tid]=p.x-p.y;', 'float2 p=float2(a[tid],b[tid]); float2 q=rint(p); out[tid]=q[tid&1u];')


def effects(index,a,b,lane):
    if type(index) is not int or not 0<=index<3:raise ValueError('Workload program')
    if type(lane) is not int or not 0<=lane<64:raise ValueError('Workload lane')
    return authored(OPERATIONS[index],lane,a,b,flush=True)

def result(index,a,b,lane):
    writes,reads,iterations=effects(index,a,b,lane)
    return writes[-1] if writes else None

def accesses(index,a,b,lane):
    return effects(index,a,b,lane)[1]

def need(value,message):
    if not value:raise ValueError(message)

def selection(index):
    need(type(index) is int and 0<=index<JOBS,'Workload index')
    return min(index//22,2)

def requests(generation):
    need(type(generation) is int and 0<generation<1<<64,'Workload generation')
    values=pool();wires=[]
    for i in range(JOBS):
        serial=i+1;data=bytearray(2048)
        struct.pack_into('<64I',data,0,*([values[(i*17)%64]]*64))
        struct.pack_into('<64I',data,256,*values)
        struct.pack_into('<64I',data,512,*[(0x4a530000^(serial*997+k*17)) for k in range(64)])
        wire=struct.pack('<QIIQQII',0x5254585245513335,1,2112,generation,serial,0,1)+bytes(24)+data
        wires.append(wire)
    return wires

def references(wires):
    need(type(wires) is list and len(wires)==JOBS,'Workload extent')
    expected=[];records=[];generation=None
    for i,wire in enumerate(wires):
        need(type(wire) is bytes and len(wire)==2112,'Request extent')
        magic,abi,size,g,serial,program,groups=struct.unpack_from('<QIIQQII',wire)
        if generation is None:generation=g
        need((magic,abi,size,g,serial,program,groups)==(0x5254585245513335,1,2112,generation,i+1,0,1) and g>0 and not any(wire[40:64]) and not any(wire[832:]),'Request identity or reserved data')
        selected=selection(i);out=bytearray(wire[64:]);unchanged=written=0
        for lane in range(64):
            a,b=struct.unpack_from('<I',wire,64+lane*4)[0],struct.unpack_from('<I',wire,320+lane*4)[0]
            want=result(selected,a,b,lane)
            if want is None:unchanged+=1;continue
            written+=1
            poison=struct.unpack_from('<I',wire,576+lane*4)[0]
            need(not equivalent(poison,want),'Output poison equals result')
            struct.pack_into('<I',out,512+lane*4,want)
        out=bytes(out);expected.append(out)
        records.append(dict(serial=i+1,program=selected,operation=OPERATIONS[selected],compiled_after_jobs=selected*22,epoch=selected+2,request_sha256=hashlib.sha256(wire).hexdigest(),expected_sha256=hashlib.sha256(out).hexdigest(),output_words=64,unchanged_output_words=unchanged,written_output_words=written))
    return b''.join(expected),records
