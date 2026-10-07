"""Independent compiler-profile construction for arbitrary application inputs."""
from pathlib import Path
from functools import lru_cache
import hashlib,importlib.util,struct,sys
import program_request_codec as request
ROOT=Path(__file__).resolve().parent
COMPONENT=ROOT/'changes/gsp-program-library-0.33'
sys.path.insert(0,str(COMPONENT))
_spec=importlib.util.spec_from_file_location('rtx_program_profile033',COMPONENT/'profile.py')
_profile=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_profile)
LIBRARY_SHA='cd23a9b3ad9303fa295d70299ec3959e82ce51e3915c51a0cb99468aa5d1d751'
CODE_SHA='f9bd1f6a3031cb389439d1aedbc60ed2cf6999b74af75fdfbd4843488f5c39f7'


@lru_cache(maxsize=1)
def sealed():
    wire,code,programs=_profile.library()
    if hashlib.sha256(wire).hexdigest()!=LIBRARY_SHA or hashlib.sha256(code).hexdigest()!=CODE_SHA:raise ValueError('sealed compiler profile changed')
    if len(programs)!=3 or any(p['local_size']!=[64,1,1] or p['bindings']!=[0,1,2] or p['read_mask']!=3 or p['write_mask']!=4 for p in programs):raise ValueError('request/compiler contract')
    return wire,code,programs


def verify_sealed(library,code):
    expected,sealed_code,_=sealed()
    if type(library) is not bytes or type(code) is not bytes or library!=expected or code!=sealed_code:raise ValueError('driver/compiler program identity')


def data_offset(slot):return (4096 if slot<2 else 16384)+(slot%2)*2048
def guard(offset):return (0x5a if offset>=20480 else 0xa5)^((offset*13+7)&255)


def initial():
    _,code,_=sealed();image=bytearray(guard(i) for i in range(24576));image[:4096]=code;image[8192:16384]=bytes(8192)
    for j in range(4):image[20480+j*256:20484+j*256]=bytes(4)
    return bytes(image)


def plan(wire):
    r=request.decode(wire);j=r['request_id']-1;_,_,programs=sealed()
    packet=_profile.expected(programs[r['program']],j,r['groups'])
    out=bytearray(4096);out[:256]=packet[4384:4640]
    data=bytearray(guard(data_offset(j)+i) for i in range(2048));data[:768]=r['data'][:768]
    out[256:2304]=data;out[2304:3328]=packet[256:1280];out[3328:3584]=packet[:256]
    out[3584:3616]=packet[4352:4384];out[3616:3624]=packet[4448:4456]
    return bytes(out)


def canonical(history,generation):
    request.integer(generation,'generation',1,2**64-1)
    if type(history) not in (list,tuple) or len(history)>4:raise ValueError('history capacity')
    image=bytearray(initial());plans=[]
    for j,wire in enumerate(history):
        r=request.decode(wire)
        if r['generation']!=generation or r['request_id']!=j+1:raise ValueError('history order/generation')
        p=plan(wire);plans.append(p);start=data_offset(j)
        image[start:start+2048]=p[256:2304];image[8192+j*1024:9216+j*1024]=p[2304:3328]
        image[12288+j*256:12544+j*256]=p[3328:3584]
    return bytes(image),plans


def verify_backing(actual,history,generation,arithmetic=True):
    if type(actual) is not bytes or len(actual)!=24576:raise ValueError('program backing size/type')
    image,plans=canonical(history,generation);image=bytearray(image)
    for j,wire in enumerate(history):
        r=request.decode(wire);start=data_offset(j)+512
        if arithmetic:struct.pack_into('<64I',image,start,*(request.answer(r['program'],a,b) for a,b in zip(r['a'],r['b'])))
        else:image[start:start+256]=actual[start:start+256]
        struct.pack_into('<I',image,20480+j*256,0x306033f0+j)
        image[12288+j*256:12544+j*256]=actual[12288+j*256:12544+j*256] # Completed QMD writeback.
    if actual!=image:raise ValueError('program arithmetic/input/code/guard evidence')
    return plans
