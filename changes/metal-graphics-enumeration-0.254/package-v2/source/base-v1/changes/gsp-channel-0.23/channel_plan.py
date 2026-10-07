"""Offline 0.23 channel/context candidate. No backend, GPU I/O, or kext loading."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
import gsp_rpc
import gsp_compute_prep_codec as prep

BAR1_BYTES=0x4000000
CHANNEL_PHYS_START=0x01100000
CHANNEL_PHYS_END=0x01107000
CONTEXT_PHYS_START=0x01200000
CHANNEL_VA=0x1020000000
CONTEXT_VA=0x1020200000
CHANNEL_OBJECT=0xcf000004
CLASSES=dict(channel=0xc56f,compute=0xc7c0,copy=0xc7b5)


def align(value,alignment):
    if type(value) is not int or type(alignment) is not int or not 0<=value<1<<49 or not 0<alignment<=1<<21 or alignment&(alignment-1):
        raise ValueError('Invalid bounded size/alignment')
    result=(value+alignment-1)&-alignment
    if result>=1<<49:raise ValueError('Aligned size overflow')
    return result


def channel_parameters():
    out=bytearray(368)
    struct.pack_into('<QII',out,8,CHANNEL_VA,32,0x200320)
    struct.pack_into('<I',out,28,prep.VASPACE)
    struct.pack_into('<Q',out,64,0x100)
    struct.pack_into('<II',out,128,1,3)
    for offset,base,size in ((144,0x01101000,0x1000),(168,0x01100100,0x20),(192,0x01101000,0x200),(216,0x01102000,0x5000)):
        struct.pack_into('<QQII',out,offset,base,size,2,0)
    struct.pack_into('<I',out,244,0x1a)
    return bytes(out)


def channel_request():
    args=struct.pack('<8I',prep.CLIENT,prep.OBJECTS[1],CHANNEL_OBJECT,CLASSES['channel'],0,368,0,0)
    return gsp_rpc.encode_record(103,args+channel_parameters(),transport_sequence=9)


def context_plan(gr_raw):
    if type(gr_raw) is not bytes or len(gr_raw)!=1664:raise ValueError('Wrong eight-engine GR context size table')
    def size(kind,extra=0,override=None):
        value,alignment=struct.unpack_from('<II',gr_raw,kind*8)
        if value in (0,0xffffffff) or alignment==0xffffffff:raise ValueError('Unavailable GR buffer kind '+str(kind))
        # Validate the reported alignment even when this caller needs a stronger one.
        align(value,alignment)
        value=align(value+extra,max(alignment,override or 1))
        if value>16<<20:raise ValueError('GR buffer exceeds fixed experiment budget')
        return value,max(4096,alignment,override or 1)
    specs=[(0,0,0x40000,None,True,True),(2,16,0,None,True,True),
           (3,17,0,None,False,True),(4,18,0,None,False,True),
           (5,19,0,2<<20,False,True),(6,20,0,None,False,True),
           (9,23,0,None,True,True),(10,24,0,None,True,False),(11,24,0,None,True,True)]
    phys=CONTEXT_PHYS_START;virtual=CONTEXT_VA;buffers=[]
    for buffer_id,kind,extra,override,use_phys,use_virt in specs:
        amount,alignment=size(kind,extra,override)
        allocated=align(amount,4096);phys=align(phys,alignment);virtual=align(virtual,alignment)
        if phys+allocated>BAR1_BYTES or virtual+allocated>=1<<49:raise ValueError('Context backing exceeds fixed aperture/VA budget')
        buffers.append(dict(buffer_id=buffer_id,gr_kind=kind,size=amount,allocated_bytes=allocated,alignment=alignment,
                            physical=phys,virtual=virtual,use_physical=use_phys,use_virtual=use_virt))
        phys+=allocated;virtual+=allocated
    return dict(buffers=buffers,physical_start=CONTEXT_PHYS_START,physical_end=phys,virtual_start=CONTEXT_VA,virtual_end=virtual,
                total_backing_bytes=sum(b['allocated_bytes'] for b in buffers),physical_span_bytes=phys-CONTEXT_PHYS_START,
                local_pm_buffer_deferred=True,requires_live_post_channel_gr_query=True,
                hardware_accessed=False,gpu_allocations_verified=False,compute_verified=False,metal_verified=False)


def promote_parameters(plan):
    entries=plan['buffers']
    if len(entries)!=9 or [b['buffer_id'] for b in entries]!=[0,2,3,4,5,6,9,10,11]:raise ValueError('Wrong fixed promotion set')
    out=bytearray(560)
    struct.pack_into('<6I',out,0,1,0,0,prep.CLIENT,CHANNEL_OBJECT,0)
    struct.pack_into('<I',out,40,len(entries))
    phys_end=CONTEXT_PHYS_START;virtual_end=CONTEXT_VA
    for i,b in enumerate(entries):
        use_phys,use_virt=b['use_physical'],b['use_virtual']
        if type(use_phys) is not bool or type(use_virt) is not bool or not use_phys and not use_virt:raise ValueError('Context description flags')
        amount=b['size'];allocated=b['allocated_bytes'];alignment=b['alignment'];phys=b['physical'];virtual=b['virtual']
        if not 0<amount<=16<<20 or allocated!=align(amount,4096) or alignment<4096:raise ValueError('Context allocation size')
        if align(phys,alignment)!=phys or align(virtual,alignment)!=virtual or phys<phys_end or virtual<virtual_end or phys+allocated>BAR1_BYTES or virtual+allocated>=1<<49:
            raise ValueError('Context allocation overlaps or exceeds bounds')
        phys_end=phys+allocated;virtual_end=virtual+allocated
        struct.pack_into('<QQQI HBB',out,48+32*i,b['physical'] if use_phys else 0,b['virtual'] if use_virt else 0,
                         b['size'] if use_phys else 0,4 if use_phys else 0,b['buffer_id'],int(use_phys),int(use_phys and not use_virt))
    return bytes(out)


def load_record(path,step,sequence):
    raw=path.read_bytes();decoded=prep.reply(raw,step,sequence)
    if decoded.get('parameter_decode_error'):raise ValueError(decoded['parameter_decode_error'])
    return raw,gsp_rpc.decode_record(raw,expected_sequence=sequence).rpc.payload[24:]


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--gr-record',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();raw,params=load_record(args.gr_record,5,11);plan=context_plan(params)
    args.out.mkdir(exist_ok=False)
    artifacts={'channel-parameters.bin':channel_parameters(),'channel-request.bin':channel_request(),'promote-parameters.bin':promote_parameters(plan)}
    for name,data in artifacts.items():(args.out/name).write_bytes(data)
    plan.update(status='offline-candidate-only',source_gr_sha256=hashlib.sha256(raw).hexdigest(),classes=CLASSES,
                channel_physical_start=CHANNEL_PHYS_START,channel_physical_end=CHANNEL_PHYS_END,channel_virtual=CHANNEL_VA,
                requires_022_live_success=True,requires_channel_leaf_mappings=True,requires_mmu_invalidation=True,
                artifact_sha256={name:hashlib.sha256(data).hexdigest() for name,data in artifacts.items()})
    (args.out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    print(json.dumps({k:v for k,v in plan.items() if k not in ('buffers','artifact_sha256')},indent=2))


if __name__=='__main__':main()
