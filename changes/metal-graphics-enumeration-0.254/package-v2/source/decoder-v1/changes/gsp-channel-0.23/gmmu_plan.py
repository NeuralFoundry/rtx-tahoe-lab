"""CPU-only MMUv2 small-page candidate. No MMIO, mapping, publication or GPU backend."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import channel_plan as channel

PAGE=4096
OLD_BASE=0x01002000
OLD_BYTES=3*PAGE
NEW_BASE=0x01005000
LEASE_END=0x01010000
VA_BASE=0x1020000000
VA_END=VA_BASE+(1<<29)
PARENT_OFFSET=2*PAGE+129*8
PARENT_VALUE=(NEW_BASE>>4)|0x22
KIND=6


def integer(value):
    if type(value) is not int or not 0<=value<1<<49:raise ValueError('Invalid bounded unsigned integer')
    return value


def mappings(plan):
    channel.promote_parameters(plan)  # Recheck the original bounded context plan.
    return [(VA_BASE,channel.CHANNEL_PHYS_START,PAGE)]+[(b['virtual'],b['physical'],b['allocated_bytes']) for b in plan['buffers']]


def validate_ranges(ranges):
    if type(ranges) not in (list,tuple) or not 1<=len(ranges)<=10:raise ValueError('Mapping count')
    checked=[]
    for item in ranges:
        if len(item)!=3:raise ValueError('Mapping tuple')
        va,pa,size=map(integer,item)
        if va%PAGE or pa%PAGE or size%PAGE or not 0<size<=16<<20:raise ValueError('Mapping alignment/size')
        if not VA_BASE<=va<va+size<=VA_END:raise ValueError('Outside fixed channel VA section')
        # Instance/RAMFC/method descriptors have separate physical-only backing.
        if not (pa==channel.CHANNEL_PHYS_START and va==VA_BASE and size==PAGE or
                channel.CONTEXT_PHYS_START<=pa<pa+size<=channel.BAR1_BYTES):
            raise ValueError('Outside fixed channel/context physical backing')
        for old_va,old_pa,old_size in checked:
            if va<old_va+old_size and old_va<va+size or pa<old_pa+old_size and old_pa<pa+size:
                raise ValueError('Aliased or overlapping mapping')
        checked.append((va,pa,size))
    return sorted(checked)


def old_image_checked(old):
    if type(old) is not bytes or len(old)!=OLD_BYTES:raise ValueError('Three captured old table pages required')
    if struct.unpack_from('<Q',old,0)[0]!=0x100322 or struct.unpack_from('<Q',old,PAGE)[0]!=0x100422:
        raise ValueError('Captured root chain does not match fixed VRAM tables')
    if struct.unpack_from('<Q',old,PARENT_OFFSET)[0]!=0:raise ValueError('Channel PDE1 entry already occupied')


def build(old,ranges):
    old_image_checked(old)
    ranges=validate_ranges(ranges)
    groups=sorted({(va+off-VA_BASE)>>21 for va,_,size in ranges for off in range(0,size,PAGE)})
    if (len(groups)+1)*PAGE>LEASE_END-NEW_BASE:raise ValueError('Insufficient table lease')
    image=bytearray((len(groups)+1)*PAGE)
    page_for={group:i+1 for i,group in enumerate(groups)}
    for group,page in page_for.items():
        struct.pack_into('<QQ',image,group*16,0x20,((NEW_BASE+page*PAGE)>>4)|2)
    for va,pa,size in ranges:
        for off in range(0,size,PAGE):
            index=((va+off)>>12)&511;page=page_for[(va+off-VA_BASE)>>21]
            struct.pack_into('<Q',image,page*PAGE+index*8,(KIND<<56)|((pa+off)>>4)|1)
    # The existing table image is never modified. Publication is a separate final step.
    return dict(children=bytes(image),parent_offset=PARENT_OFFSET,parent_expected=0,parent_value=PARENT_VALUE,
                mapped_pages=sum(size//PAGE for _,_,size in ranges),leaf_tables=len(groups),ranges=ranges,
                old_sha256=hashlib.sha256(old).hexdigest(),hardware_accessed=False,gpu_translation_verified=False,
                requires_022_live_success=True,requires_live_table_lease_check=True,requires_mmu_invalidation=True)


def walk(published_old,children,va):
    """Strict software walker for this candidate section; never walks GSP-owned VA."""
    integer(va)
    if not VA_BASE<=va<VA_END:raise ValueError('Walker outside candidate section')
    if type(published_old) is not bytes or len(published_old)!=OLD_BYTES:raise ValueError('Old table size')
    if type(children) is not bytes or len(children)%PAGE or not 2*PAGE<=len(children)<=LEASE_END-NEW_BASE:raise ValueError('Child table size')
    for off,expected in ((0,0x100322),(PAGE,0x100422),(PARENT_OFFSET,PARENT_VALUE)):
        if struct.unpack_from('<Q',published_old,off)[0]!=expected:raise ValueError('Parent chain mismatch')
    group=(va-VA_BASE)>>21;low,high=struct.unpack_from('<QQ',children,group*16)
    if low==high==0:return None
    if low!=0x20 or high&255!=2 or high>>33:raise ValueError('Unsupported/corrupt dual PDE')
    physical=(high&0x1ffffff00)<<4
    offset=physical-NEW_BASE
    if offset<PAGE or offset%PAGE or offset+PAGE>len(children):raise ValueError('PTE pointer outside captured lease')
    pte=struct.unpack_from('<Q',children,offset+((va>>12)&511)*8)[0]
    if pte==0:return None
    allowed=(KIND<<56)|0x1ffffff00|1
    if pte&~allowed or pte>>56!=KIND or pte&255!=1:raise ValueError('Unsupported/corrupt PTE')
    result=((pte&0x1ffffff00)<<4)|(va&(PAGE-1))
    if not (channel.CHANNEL_PHYS_START<=result<channel.CHANNEL_PHYS_START+PAGE or
            channel.CONTEXT_PHYS_START<=result<channel.BAR1_BYTES):raise ValueError('Leaf address outside backing')
    return result


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--plan',type=Path,required=True)
    parser.add_argument('--old-tables',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();old=args.old_tables.read_bytes()
    result=build(old,mappings(json.loads(args.plan.read_text())))
    args.out.mkdir(exist_ok=False)
    children=result.pop('children')
    (args.out/'children.bin').write_bytes(children)
    # Diagnostic simulation only. This file is never sent to a hardware backend.
    simulated=bytearray(old);struct.pack_into('<Q',simulated,PARENT_OFFSET,PARENT_VALUE)
    (args.out/'simulated-published-old.bin').write_bytes(simulated)
    (args.out/'ranges.bin').write_bytes(b''.join(struct.pack('<3Q',*r) for r in result['ranges']))
    result.update(status='offline-candidate-only',child_bytes=len(children),child_sha256=hashlib.sha256(children).hexdigest(),
                  compute_verified=False,metal_verified=False)
    (args.out/'plan.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='ranges'},indent=2))


if __name__=='__main__':main()
