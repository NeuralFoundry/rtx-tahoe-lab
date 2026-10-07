"""Deterministic 65-job FP32 hardware workload; no shader-name arithmetic."""
import struct
import uploaded_request as request
from uploaded_library import need

# The 0.52 reference's boundary pool, with signaling NaNs changed to quiet NaNs.
# Signaling NaNs are outside the Metal language contract.
POOL=(0,0x80000000,1,0x007fffff,0x00800000,0x00800001,0x00800002,0x807fffff,0x80800000,0x80000001,
      0x3f800000,0xbf800000,0x40000000,0xc0000000,0x3f000000,0xbf000000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,
      0x7fc00000,0xffc00000,0x7fc00001,0xffc00001,0x3f7fffff,0x3f800001,0xbf7fffff,0xbf800001,0x3f800002,0xbf800002,
      0x33800000,0xb3800000,0x33000000,0xb3000000,0x4b800000,0xcb800000,0x4b7fffff,0x4b800001,
      0x00800003,0x01000000,0x7effffff,0x7f000000,0x3eaaaaab,0xbeaaaaab,0x3dcccccd,0xbdcccccd,0x00200000,0x80200000)

def pool():
    result=list(POOL);seed=0x5211aa77
    while len(result)<64:
        seed=(1664525*seed+1013904223)&0xffffffff
        if seed&0x7f800000!=0x7f800000 and seed not in result:result.append(seed)
    return tuple(result)

def requests(catalog,generation):
    need(len(catalog.programs)==1,'one native runtime pipeline')
    for p in catalog.programs:
        need(p.air and p.local_size==(64,1,1) and p.bindings==(0,1,2) and p.reads==(0,1) and p.writes==(2,),'FP32 workload buffer profile')
    values=pool();wires=[]
    for serial in range(1,66):
        program=0;iteration=serial-1;data=bytearray(2048)
        # A coprime stride samples both extremes and rounding/cancellation rows.
        a=values[(iteration*17)%64]
        struct.pack_into('<64I',data,0,*([a]*64));struct.pack_into('<64I',data,256,*values)
        poison=[(0x4a530000^(serial*997+i*17)) for i in range(64)]
        struct.pack_into('<64I',data,512,*poison)
        wire=request.encode(catalog,generation,serial,program,1,bytes(data));expected,meta=request.evaluate(catalog,wire)
        need(meta['output_words']==64 and all(x!=y for x,y in zip(poison,struct.unpack_from('<64I',expected,512))),'output poison must differ')
        wires.append(wire)
    return wires
