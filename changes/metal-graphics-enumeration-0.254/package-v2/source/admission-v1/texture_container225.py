"""Explicit application container ABI3; native program payload remains ABI2.

ABI3 is single-entry only. The 512-byte name area contains its ordinary
128-byte name, eight 16-byte resource rows, then 256 zero bytes. ABI1/2 keep
their original four-name layout. Each row is kind, logical index, record, format.
"""
import struct


def resource_bytes(resources):
    if type(resources) is not list or not 2 <= len(resources) <= 8:
        raise ValueError('texture resource count')
    kinds={'buffer':1,'texture':2,'texture_descriptors':3,'sampler':4}
    result=bytearray(128);seen=set();textures=samplers=0;last_buffer=last_texture=last_sampler=-1
    for n,row in enumerate(resources):
        if type(row) is not dict or set(row)!={'kind','index','descriptor','format'}:
            raise ValueError('texture resource schema')
        kind=row['kind'];index=row['index'];record=row['descriptor'];fmt=row['format']
        if kind not in kinds or any(type(v) is not int for v in (index,record,fmt)):
            raise ValueError('texture resource type')
        if (kind,index) in seen:raise ValueError('duplicate logical resource')
        seen.add((kind,index))
        if kind=='buffer':
            if textures or n==len(resources)-1 or not last_buffer<index<32 or record or fmt:
                raise ValueError('buffer resource mapping')
            last_buffer=index
        elif kind=='texture':
            if samplers or n==len(resources)-1 or not last_texture<index<128 or record!=textures or fmt not in (3,0x80000498):
                raise ValueError('texture resource mapping')
            textures+=1;last_texture=index
        elif kind=='sampler':
            if not textures or n==len(resources)-1 or not last_sampler<index<16 or record!=textures+samplers or fmt:
                raise ValueError('sampler resource mapping')
            samplers+=1;last_sampler=index
        elif n!=len(resources)-1 or not textures or index or record or fmt:
            raise ValueError('texture descriptor mapping')
        struct.pack_into('<4I',result,n*16,kinds[kind],index,record,fmt)
    if resources[-1]['kind']!='texture_descriptors':raise ValueError('missing descriptor parameter')
    return bytes(result)
