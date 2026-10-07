"""Read-only decoders/capture for the 0.22 native ABI; no device opens on import."""
import hashlib
import struct
import gsp_rpc
import gsp_compute_prep_codec as prep
import gsp_page_tables_codec as wire

FIELDS=('magic abi generation validated attempted staged failure inspected written checked captured reads writes ticks '
        'window_before window_after last_address last_value expected failed_word start_ns elapsed_ns cleanup_ns window_saved modified '
        'window_restored claimed lease_owned mapped bar1_base mapping_physical start end bytes root_bytes va_start va_end owner_phase '
        'pinned pci_command owned bar1_passed prep_passed post_passed post_words post_bytes post_failure post_elapsed_ns budget_ns '
        'cleanup_budget_ns max_ticks rm_claimed rm_passed post_attempted passed').split()+['reserved'+str(i) for i in range(55,64)]
BOOLS=('validated attempted staged window_saved modified window_restored claimed lease_owned mapped pinned owned '
       'bar1_passed prep_passed post_passed rm_claimed rm_passed post_attempted passed').split()
RM_FIELDS=prep.FIELDS[:]
RM_FIELDS[38:41]=['vaspace','control','params_bytes']


def decode(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or not generation: raise ValueError('Page-table ABI size/generation')
    r=dict(zip(FIELDS,struct.unpack('<64Q',raw)))
    if (r['magic'],r['abi'],r['generation'])!=(0x5254585047543232,1,generation): raise ValueError('Page-table ABI identity')
    if any(r[k] not in (0,1) for k in BOOLS) or any(r[k] for k in FIELDS[55:]): raise ValueError('Page-table booleans/reserved')
    if (r['start'],r['end'],r['bytes'],r['root_bytes'],r['va_start'],r['va_end'],r['budget_ns'],r['cleanup_budget_ns'],r['max_ticks'])!=(
            wire.START,wire.START+wire.SIZE,wire.SIZE,32,wire.VA_START,wire.VA_END,15_000_000_000,5_000_000_000,60000):
        raise ValueError('Page-table fixed profile differs')
    if (r['failure']>10 or r['post_failure']>5 or any(r[k]>3072 for k in ('inspected','written','checked','post_words')) or
            r['captured']>wire.SIZE or r['post_bytes']>wire.SIZE or r['captured']%4 or r['post_bytes']%4 or r['ticks']>60001):
        raise ValueError('Page-table progress bounds')
    if r['attempted'] and (not all(r[k] for k in ('validated','claimed','lease_owned','mapped','owned','pinned','bar1_passed','prep_passed')) or
            r['owner_phase'] not in (7,17) or r['pci_command']!=6 or not 0<r['bar1_base']<1<<40 or r['bar1_base']%0x4000000 or
            r['mapping_physical']!=r['bar1_base']+wire.START): raise ValueError('Page-table retained owner/mapping')
    if r['staged'] and (r['failure'] or any(r[k]!=3072 for k in ('inspected','written','checked')) or r['captured']!=wire.SIZE or
            not all(r[k] for k in ('attempted','window_saved','modified','window_restored')) or r['window_before']!=r['window_after'] or
            r['failed_word']!=0xffffffff or r['elapsed_ns']>=r['budget_ns'] or r['cleanup_ns']>=r['cleanup_budget_ns']):
        raise ValueError('Incomplete page-table staging/restoration')
    if r['post_passed'] and (not r['post_attempted'] or not r['rm_passed'] or r['post_failure'] or r['post_words']!=3072 or
            r['post_bytes']!=wire.SIZE or r['post_elapsed_ns']>=r['budget_ns']): raise ValueError('Incomplete post-control capture')
    if r['passed'] and (not all(r[k] for k in ('staged','rm_claimed','rm_passed','post_passed')) or r['owner_phase']!=17):
        raise ValueError('Incomplete whole page-table experiment')
    return r


def decode_rm(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or not generation: raise ValueError('Page-table RM ABI size/generation')
    r=dict(zip(RM_FIELDS,struct.unpack('<64Q',raw)))
    if (r['magic'],r['abi'],r['generation'])!=(0x5254585044523232,1,generation): raise ValueError('Page-table RM ABI identity')
    if any(r[k] not in (0,1) for k in prep.BOOLS) or any(r[k] for k in RM_FIELDS[50:]): raise ValueError('Page-table RM booleans/reserved')
    if (r['client'],r['vaspace'],r['control'],r['params_bytes'],r['request_bytes'],r['max_records'],r['max_pages'],r['budget_ns'],r['max_ticks'])!=(
            wire.CLIENT,wire.VASPACE,wire.CONTROL,184,4096,16,32,15_000_000_000,150000): raise ValueError('Page-table RM profile')
    if (r['failure']>20 or r['step']!=0 or not r['completed']<=r['sent']<=1 or r['doorbells']>r['sent'] or
            not r['count']<=r['pages']<=32 or r['count']>16 or r['bytes']!=r['pages']*4096 or r['rx_reader']>=63 or r['rx_producer']>=63 or
            r['ticks']>150001 or r['polls']>r['ticks'] or r['consumer_writes']>r['count']): raise ValueError('Page-table RM bounds')
    if r['validated'] and (not 8<=r['tx_reader']<=r['tx_writer']<=9 or r['tx_writer']!=8+r['sent']): raise ValueError('Page-table command origin')
    if r['attempted'] and (not all(r[k] for k in ('validated','claimed','owned','workspace','pinned','prefix_consumed')) or r['pci_command']!=6):
        raise ValueError('Page-table RM retained owner')
    if r['count'] and r['rx_sequence']!=r['initial_sequence']+r['count']: raise ValueError('Page-table RM sequence')
    if r['passed'] and (r['failure'] or not r['attempted'] or r['completed']!=1 or r['doorbells']!=1 or r['last_function']!=76 or
            r['last_result'] or r['last_param_status'] or r['tx_writer']!=9 or r['tx_reader']!=9 or r['rx_reader']!=r['rx_producer'] or
            r['consumer_writes']!=r['count'] or r['elapsed_ns']>=r['budget_ns']): raise ValueError('Incomplete reserved-PDE RM success')
    return r


def capture_tables(backend,summary,output):
    result={}
    for i,(name,count) in enumerate((('staged',summary['captured']),('post-control',summary['post_bytes']))):
        data=b''.join(backend.page_data(i,off,min(4096,count-off)) for off in range(0,count,4096))
        if len(data)!=count: raise ValueError('Short page-table capture')
        (output/('page-tables-'+name+'.bin')).write_bytes(data)
        result[name]=wire.decode_tables(data,require_initial=i==0 and bool(summary['staged'])) if count==wire.SIZE else dict(bytes=count,complete=False)
    result['captures_verified']=bool(summary['passed'] and all(result[name].get('bytes')==wire.SIZE for name in ('staged','post-control')))
    result['gpu_translation_verified']=result['compute_verified']=result['metal_verified']=False
    return result


def capture_rm(backend,summary,output):
    output.mkdir(exist_ok=False)
    index=backend.page_rm_index(0,summary['count']) if summary['count'] else b''
    if len(index)!=summary['count']*72: raise ValueError('Wrong page-table RM index length')
    (output/'index.bin').write_bytes(index)
    request=backend.page_rm_request()
    (output/'request.bin').write_bytes(request)
    if len(request)!=4096 or any(request) and request!=wire.request() or summary['passed'] and request!=wire.request():
        raise ValueError('Native reserved-PDE request differs')
    rows=[];offset=0;slot=summary['initial_reader'];successes=0;previous_us=0
    for i in range(summary['count']):
        row=dict(zip(prep.ROW_FIELDS,struct.unpack_from('<9Q',index,i*72)))
        if (row['offset']!=offset or row['slot']!=slot or row['step']!=0 or row['sequence']!=summary['initial_sequence']+i or
                not 4096<=row['bytes']<=65536 or row['bytes']%4096 or row['bytes']>summary['bytes']-offset or
                not previous_us<=row['elapsed_us']<=summary['elapsed_ns']//1000): raise ValueError('Page-table RM index geometry')
        data=b''.join(backend.page_rm_data(offset+j,min(4096,row['bytes']-j)) for j in range(0,row['bytes'],4096))
        name='record-%03d.bin'%row['sequence'];(output/name).write_bytes(data)
        packet=gsp_rpc.decode_record(data,expected_sequence=row['sequence'])
        if (packet.rpc.function,packet.rpc.result,len(packet.rpc.payload))!=(row['function'],row['result'],row['payload_bytes']):
            raise ValueError('Page-table RM record differs from index')
        item=dict(row,file=name,sha256=hashlib.sha256(data).hexdigest())
        if row['function']==76:
            try: item['exchange']=wire.decode_response(data,row['sequence']);successes+=1
            except ValueError as error:
                item['reply_error']=str(error)
                if summary['passed']: raise
        elif summary['passed'] and (row['result'] or row['function'] not in (0x100c,0x1020)):
            raise ValueError('Unsupported event during reserved-PDE control')
        offset+=row['bytes'];slot=(slot+row['bytes']//4096)%63;previous_us=row['elapsed_us'];rows.append(item)
    if offset!=summary['bytes'] or summary['passed'] and (successes!=1 or slot!=summary['rx_reader']):
        raise ValueError('Reserved-PDE response evidence incomplete')
    return dict(records=rows,exchanges_verified=bool(summary['passed'] and successes==1),gpu_translation_verified=False,compute_verified=False,metal_verified=False)
