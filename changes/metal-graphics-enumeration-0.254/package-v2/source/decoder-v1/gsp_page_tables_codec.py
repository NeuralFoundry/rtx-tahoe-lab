"""Independent fixed Ampere page-tree and reserved-PDE control serializers."""
import struct
import hashlib

START=0x01002000
SIZE=12288
VA_START=0x1000000000
VA_END=0x1020000000
CLIENT=0xc1e00004
VASPACE=0xcf000003
CONTROL=0x90f10106


def image():
    out=bytearray(SIZE)
    struct.pack_into('<Q',out,0,((START+4096)>>12)<<8|0x22)
    struct.pack_into('<Q',out,4096,((START+8192)>>12)<<8|0x22)
    return bytes(out)


def parameters():
    out=bytearray(184)
    struct.pack_into('<IIQQQI',out,0,0,0,VA_END-VA_START,VA_START,VA_END-1,3)
    for i,(size,shift) in enumerate(((32,47),(4096,38),(4096,29))):
        struct.pack_into('<QQIB',out,40+24*i,START+4096*i,size,1,shift)
    return bytes(out)


def checksum(data):
    end=48+struct.unpack_from('<I',data,56)[0]
    if end<80 or end>len(data) or (end+7)&~7>len(data): raise ValueError('Invalid RPC checksum span')
    result=0
    for off in range(0,(end+7)&~7,4): result^=struct.unpack_from('<I',data,off)[0]
    return result


def request():
    p=bytearray(4096)
    struct.pack_into('<4I',p,32,0,8,1,0)
    struct.pack_into('<8I',p,48,0x03000000,0x43505256,240,76,0xffffffff,0xffffffff,0,0)
    struct.pack_into('<6I',p,80,CLIENT,VASPACE,CONTROL,0,184,0)
    p[104:288]=parameters()
    struct.pack_into('<I',p,32,checksum(p))
    return bytes(p)


def decode_response(data,sequence):
    if type(data) is not bytes or len(data)!=4096 or type(sequence) is not int or not 0<=sequence<=0xffffffff:
        raise ValueError('Wrong reserved-PDE response bounds')
    if any(data[:32]) or struct.unpack_from('<III',data,36)!=(sequence,1,0): raise ValueError('Response transport identity')
    if struct.unpack_from('<8I',data,48)!=(0x03000000,0x43505256,240,76,0,0,0,0) or checksum(data):
        raise ValueError('Response RPC identity/status/checksum')
    if struct.unpack_from('<6I',data,80)!=(CLIENT,VASPACE,CONTROL,0,184,0) or data[104:288]!=parameters():
        raise ValueError('Response control identity or echoed inputs')
    return dict(sequence=sequence,function=76,rpc_result=0,param_status=0,parameters_verified=True,
                sha256=hashlib.sha256(data).hexdigest(),gpu_translation_verified=False,compute_verified=False,metal_verified=False)


def vaspace_range(params):
    if type(params) is not bytes or len(params)!=48: raise ValueError('Wrong VASPACE parameters')
    end=struct.unpack_from('<Q',params,8)[0]
    base=struct.unpack_from('<Q',params,40)[0]
    if not 0x4000000<=base<end<=1<<49 or not base<=VA_START<VA_END<=end: raise ValueError('Fixed VA outside current VASPACE')
    return dict(base=base,end=end,usable_bytes=end-base)


def decode_tables(data,require_initial=False):
    if type(data) is not bytes or len(data)!=SIZE: raise ValueError('Wrong page-table capture size')
    if require_initial and data!=image(): raise ValueError('Staged tree differs from fixed serializer')
    pages=[]
    for i,entries in enumerate((4,512,512)):
        values=struct.unpack_from('<'+str(entries)+'Q',data,4096*i)
        pages.append(dict(level=i,page_shift=(47,38,29)[i],physical=START+4096*i,
                          entries=[dict(index=j,raw=v) for j,v in enumerate(values) if v]))
    return dict(bytes=SIZE,sha256=hashlib.sha256(data).hexdigest(),initial_image_verified=data==image(),pages=pages,
                gpu_translation_verified=False,compute_verified=False,metal_verified=False)
