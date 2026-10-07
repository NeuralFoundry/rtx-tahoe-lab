"""Bounded REQ35 dispatches selected by immutable compiler reflection."""
import struct
from uploaded_library import Catalog,integer,need
from uploaded_compiler import checked_ptx_model
MAGIC=0x5254585245513335;WIRE_BYTES=2112;MAX_SERIAL=2**64-1

def encode(catalog,generation,serial,program,groups,data):
    need(type(catalog) is Catalog,'catalog');integer(generation,'generation',1,MAX_SERIAL);integer(serial,'serial',1,MAX_SERIAL)
    integer(program,'program',0,len(catalog.programs)-1);p=catalog.programs[program];integer(groups,'groups',1,64//p.local_size[0])
    need(type(data) is bytes and len(data)==2048 and not any(data[len(p.bindings)*256:]),'request buffer regions')
    return struct.pack('<QIIQQII',MAGIC,1,2112,generation,serial,program,groups)+bytes(24)+data

def decode(catalog,wire):
    need(type(wire) is bytes and len(wire)==2112,'request bytes');magic,abi,size,generation,serial,program,groups=struct.unpack_from('<QIIQQII',wire)
    need((magic,abi,size)==(MAGIC,1,2112) and not any(wire[40:64]),'request header');data=wire[64:]
    need(encode(catalog,generation,serial,program,groups,data)==wire,'request canonical encoding')
    p=catalog.programs[program]
    return dict(generation=generation,serial=serial,program=program,groups=groups,invocations=groups*p.local_size[0],parameters=len(p.bindings),data=data)

def evaluate(catalog,wire):
    """CPU reference and per-dispatch range/race rejection before submission.

    Every shader instruction is interpreted; names and program numbers do not
    select arithmetic. This is not GPU execution or SASS emulation.
    """
    r=decode(catalog,wire);p=catalog.programs[r['program']];data=r['data'];n=len(p.bindings)
    addresses=[0x1020005000+i*256 for i in range(n)]
    inputs=[dict(enumerate(struct.unpack_from('<64I',data,i*256))) for i in range(n)]
    original={addresses[i]+j*4:v for i,row in enumerate(inputs) for j,v in row.items()}
    writes={};reads={};instructions=0
    for gid in range(r['invocations']):
        memory,trace,count=checked_ptx_model.evaluate(p.ptx.decode(),(gid,0,0),p.local_size,inputs,addresses);instructions+=count
        local_reads=set();local_writes=set()
        for op,address in trace:
            need(address in original and address%4==0,'shader access outside bound buffer')
            parameter=(address-addresses[0])//256;offset=(address-addresses[0])%256;binding=p.bindings[parameter]
            if op=='read':need(binding in p.reads,'shader read reflection');local_reads.add(address)
            elif op=='write':
                need(binding in p.writes and offset<r['invocations']*4,'shader write reflection/range');local_writes.add(address)
            else:raise ValueError('unrecognized memory operation')
        for address in local_reads:
            need(address not in writes,'inter-invocation read/write hazard');reads.setdefault(address,set()).add(gid)
        for address in local_writes:
            need(address not in writes and reads.get(address,set())<={gid},'inter-invocation output hazard');writes[address]=(gid,memory[address])
    expected=bytearray(data)
    for address,(_,value) in writes.items():struct.pack_into('<I',expected,address-addresses[0],value)
    need(writes,'dispatch has no writes')
    return bytes(expected),dict(invocations=r['invocations'],output_words=len(writes),read_words=len(reads),instructions=instructions,range_and_race_checked=True,cpu_reference=True)

def requests(catalog,generation,count=65):
    integer(count,'job count',1,65);result=[]
    for serial in range(1,count+1):
        program=(serial-1)%len(catalog.programs);p=catalog.programs[program];data=bytearray(2048);seed=(serial^0x36bacc)&0xffffffff
        for parameter,binding in enumerate(p.bindings):
            for i in range(64):
                seed=(seed*1664525+1013904223)&0xffffffff
                value=seed if binding in p.reads else (0xcafe0000+parameter*256+i)
                struct.pack_into('<I',data,parameter*256+i*4,value)
        # Exercise partial/full dispatches using only whole compiled workgroups.
        groups=1 if ((serial-1)//len(catalog.programs))%2==0 else 64//p.local_size[0]
        wire=encode(catalog,generation,serial,program,groups,bytes(data));evaluate(catalog,wire);result.append(wire)
    return result
