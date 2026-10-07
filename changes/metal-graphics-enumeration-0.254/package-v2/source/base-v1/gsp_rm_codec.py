"""Independent fixed-request serializer and captured RM evidence decoder."""
import hashlib
import struct
import gsp_rpc

FIELDS=('magic abi generation validated attempted passed failure step completed sent doorbells count pages bytes '
        'tx_writer tx_reader rx_reader rx_producer rx_sequence ticks polls imports reads writes publishes '
        'elapsed_ns budget_ns max_ticks initial_reader initial_sequence consumer_writes prefix_consumed '
        'last_function last_result last_param_status last_address last_value client root_object device subdevice '
        'request_bytes max_records max_pages claimed owned workspace pinned pci_command start_ns').split()
FIELDS += ['reserved'+str(i) for i in range(50,64)]
ROW_FIELDS='offset bytes function result sequence payload_bytes step slot elapsed_us'.split()
BOOLS='validated attempted passed prefix_consumed claimed owned workspace pinned'.split()
CLIENT=0xc1e00004
OBJECTS=(0xcf000000,0xcf000001,0xcf000002)
PARENTS=(0,CLIENT,OBJECTS[1])
CLASSES=(0,0x80,0x2080)
SIZES=(120,56,4)


def request(step):
    if type(step) is not int or not 0<=step<3:
        raise ValueError('Expected one of three fixed RM allocation steps')
    params=bytearray(SIZES[step])
    if step==1: struct.pack_into('<I',params,4,CLIENT)
    payload=struct.pack('<8I',CLIENT,PARENTS[step],OBJECTS[step],CLASSES[step],0,SIZES[step],0,0)+bytes(params)
    return gsp_rpc.encode_record(103,payload,transport_sequence=step+2)


def decode(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or not generation:
        raise ValueError('RM info requires exactly64 immutable u64 fields and generation')
    r=dict(zip(FIELDS,struct.unpack('<64Q',raw)))
    if (r['magic'],r['abi'],r['generation'])!=(0x525458524d303138,1,generation):
        raise ValueError('Wrong RM identity/ABI')
    if any(r[k] not in (0,1) for k in BOOLS) or any(r[k] for k in FIELDS[50:]):
        raise ValueError('Invalid RM boolean or reserved word')
    if (r['client'],r['root_object'],r['device'],r['subdevice'],r['request_bytes'],r['max_records'],r['max_pages'],
            r['budget_ns'],r['max_ticks'])!=(CLIENT,*OBJECTS,12288,16,32,15_000_000_000,150000):
        raise ValueError('Wrong fixed RM profile/limits')
    if (r['failure']>20 or r['step']>2 or not r['completed']<=r['sent']<=3 or r['doorbells']>r['sent'] or
            not r['count']<=r['pages']<=32 or r['count']>16 or r['bytes']!=r['pages']*4096 or
            not 2<=r['tx_reader']<=r['tx_writer']<=5 or r['tx_writer']!=2+r['sent'] or
            r['rx_reader']>=63 or r['rx_producer']>=63 or r['ticks']>150001 or
            r['polls']>r['ticks'] or r['consumer_writes']>r['count']+1):
        raise ValueError('RM limits or queue geometry violated')
    if r['attempted'] and (not all(r[k] for k in ('validated','claimed','owned','workspace','pinned')) or r['pci_command']!=6):
        raise ValueError('RM execution without current pinned native owner')
    if r['prefix_consumed'] and not r['attempted']:
        raise ValueError('RM consumption without validated attempt')
    if r['count'] and r['rx_sequence']!=r['initial_sequence']+r['count']:
        raise ValueError('RM response transport sequence mismatch')
    if r['passed'] and (r['failure'] or not r['prefix_consumed'] or r['completed']!=3 or r['doorbells']!=3 or
            r['last_function']!=103 or r['last_result'] or r['last_param_status'] or
            r['tx_writer']!=5 or r['tx_reader']!=5 or r['rx_reader']!=r['rx_producer'] or
            r['consumer_writes']!=r['count']+1 or r['elapsed_ns']>=r['budget_ns']):
        raise ValueError('Incomplete RM allocation success evidence')
    return r


def reply(raw,step,sequence):
    p=gsp_rpc.decode_record(raw,expected_sequence=sequence).rpc
    if p.function!=103 or p.result or p.private_result or p.sequence or len(p.payload)!=32+SIZES[step]:
        raise ValueError('Wrong RM reply header/envelope')
    fields=struct.unpack_from('<8I',p.payload)
    if fields!=(CLIENT,PARENTS[step],OBJECTS[step],CLASSES[step],0,SIZES[step],0,0):
        raise ValueError('Wrong RM reply object/status/parameters')
    return dict(step=step,client=CLIENT,object=OBJECTS[step],parent=PARENTS[step],hclass=CLASSES[step],
                rpc_result=0,param_status=0,params_bytes=SIZES[step])


def capture(backend,summary,after,output):
    if summary['attempted'] and (not after['init_done'] or
            (summary['initial_reader'],summary['initial_sequence'])!=((3+after['pages'])%63,2+after['count'])):
        raise ValueError('RM origin differs from verified INIT_DONE prefix')
    index=backend.rm_index(0,summary['count']) if summary['count'] else b''
    (output/'index.bin').write_bytes(index)
    if len(index)!=summary['count']*72: raise ValueError('Wrong RM index size')
    requests=b''.join(backend.rm_requests(i*4096,4096) for i in range(3))
    (output/'requests.bin').write_bytes(requests)
    generated=[]
    for i in range(3):
        raw=requests[i*4096:(i+1)*4096]
        if any(raw):
            if raw!=request(i): raise ValueError('Native request differs from independent canonical serialization')
            generated.append(i)
    if summary['passed'] and generated!=[0,1,2]: raise ValueError('Missing actual requests')
    rows=[];offset=0;previous_us=0;successes=[];slot=summary['initial_reader']
    for i in range(summary['count']):
        row=dict(zip(ROW_FIELDS,struct.unpack_from('<9Q',index,i*72)))
        if (row['offset']!=offset or row['slot']!=slot or row['step']>2 or
                row['sequence']!=summary['initial_sequence']+i or not 4096<=row['bytes']<=65536 or
                row['bytes']%4096 or row['bytes']>summary['bytes']-offset or
                not previous_us<=row['elapsed_us']<=summary['elapsed_ns']//1000):
            raise ValueError('Invalid RM capture index row')
        raw=b''.join(backend.rm_data(offset+j,min(4096,row['bytes']-j)) for j in range(0,row['bytes'],4096))
        name='record-%03d.bin'%row['sequence'];(output/name).write_bytes(raw)
        packet=gsp_rpc.decode_record(raw,expected_sequence=row['sequence'])
        if (packet.rpc.function,packet.rpc.result,len(packet.rpc.payload))!=(row['function'],row['result'],row['payload_bytes']):
            raise ValueError('Independent RM packet decoder differs from index')
        item=dict(row,file=name,sha256=hashlib.sha256(raw).hexdigest(),framing_verified=True)
        if row['function']==103:
            try:
                item['allocation']=reply(raw,row['step'],row['sequence']);successes.append(row['step'])
            except ValueError as error:
                item['reply_error']=str(error)
                if summary['passed']: raise
        elif row['function'] not in (0x100c,0x1020) and summary['passed']:
            raise ValueError('Unsupported event in successful RM run')
        offset+=row['bytes'];slot=(slot+row['bytes']//4096)%63;previous_us=row['elapsed_us'];rows.append(item)
    if offset!=summary['bytes'] or summary['passed'] and (successes!=[0,1,2] or slot!=summary['rx_reader']):
        raise ValueError('RM captured exchanges do not prove all3 allocations')
    return dict(request_steps_generated=generated,records=rows,successful_reply_steps=successes,
                exchanges_verified=bool(summary['passed'] and successes==[0,1,2]),compute_verified=False,metal_verified=False)
