"""Independent extension of a captured golden tree; immutable CPU bytes only."""
import struct
import execution_plan as p
def merge(root,child,golden,execution):
    p.validate(execution,golden)
    if type(root) is not bytes or len(root)!=12288 or type(child) is not bytes or not 8192<=len(child)<=45056 or len(child)%4096:raise ValueError('Captured tree sizes')
    if struct.unpack_from('<Q',root,p.g.PARENT_OFFSET)[0]!=p.g.PARENT_VALUE:raise ValueError('Published golden parent required')
    scratch=bytearray(root);struct.pack_into('<Q',scratch,p.g.PARENT_OFFSET,0)
    old_ranges=p.g.mappings(golden);baseline=p.g.build(bytes(scratch),old_ranges)
    if child!=baseline['children']:raise ValueError('Captured golden tree differs from its verified plan')
    ranges=p.mappings(execution,golden)
    for va,pa,size in ranges:
        for old_va,old_pa,old_size in old_ranges:
            if va<old_va+old_size and old_va<va+size or pa<old_pa+old_size and old_pa<pa+size:raise ValueError('Execution aliases golden mapping')
    table={group:(((struct.unpack_from('<Q',child,group*16+8)[0]&0x1ffffff00)<<4)-p.g.NEW_BASE)//4096
           for group in range(256) if struct.unpack_from('<Q',child,group*16+8)[0]}
    old_pages=len(child)//4096;new_groups=sorted({(va+off-p.g.VA_BASE)>>21 for va,_,size in ranges for off in range(0,size,4096)}-set(table))
    if (old_pages+len(new_groups))*4096>45056:raise ValueError('Insufficient retained page-table lease')
    table.update({group:old_pages+i for i,group in enumerate(new_groups)})
    existing=0;count=0
    for va,_,size in ranges:
        for off in range(0,size,4096):
            page=table[(va+off-p.g.VA_BASE)>>21];index=((va+off)>>12)&511
            if page<old_pages:
                if struct.unpack_from('<Q',child,page*4096+index*8)[0]:raise ValueError('Occupied execution leaf')
                existing+=1
            count+=1
    out=bytearray(child+bytes(len(new_groups)*4096))
    for group in new_groups:struct.pack_into('<QQ',out,group*16,0x20,((p.g.NEW_BASE+table[group]*4096)>>4)|2)
    for va,pa,size in ranges:
        for off in range(0,size,4096):
            index=table[(va+off-p.g.VA_BASE)>>21]*4096+(((va+off)>>12)&511)*8
            struct.pack_into('<Q',out,index,(p.g.KIND<<56)|((pa+off)>>4)|1)
    return dict(children=bytes(out),old_bytes=len(child),child_bytes=len(out),added_leaf_pages=len(new_groups),added_ptes=count,
                existing_table_ptes=existing,hardware_accessed=False,gpu_translation_verified=False)
