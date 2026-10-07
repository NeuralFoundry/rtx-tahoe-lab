"""Bounded 0.23 diagnostic decoders. No backend or device is opened on import."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import gsp_rpc
import gsp_event_codec
import gsp_compute_prep_codec as prep
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'changes/gsp-channel-0.23/transactions'))
import channel_transactions as tx
import gmmu_plan as gmmu

MEMORY_FIELDS=('magic abi generation stage attempted passed failure modified parent_attempted backing_verified children_verified '
 'window_saved window_restored operations reads writes inspected_bytes zeroed_bytes verified_backing_bytes child_bytes verified_child_bytes '
 'links_published last_address last_value window_before window_after start_ns elapsed_ns cleanup_ns inv_passed inv_completed inv_command_attempted '
 'inv_failure inv_operations inv_reads inv_writes inv_last_address inv_last_value inv_start_ns inv_last_time_ns inv_elapsed_ns '
 'lease mapped bar1_base mapping_physical owner_phase pinned owned pci_command ring_claimed contexts_claimed window_observed physical_mode '
 'excluded_staging_ns budget_ns cleanup_budget_ns max_operations').split()+['reserved'+str(i) for i in range(57,64)]
MEMORY_BOOLS=('attempted passed modified parent_attempted backing_verified children_verified window_saved window_restored inv_passed inv_completed '
 'inv_command_attempted lease mapped pinned owned ring_claimed contexts_claimed window_observed physical_mode').split()
RM_FIELDS=prep.FIELDS[:50]
RM_FIELDS[38:42]=['channel','compute','copy','page_bytes']
RM_FIELDS+=('context_valid context_preparation_attempted context_prepared channel_id subdevice_mask excluded_staging_ns ring_passed context_passed '
 'snapshot_passed request_bytes owner_phase complete reserved62 reserved63').split()
SNAP_FIELDS=('magic abi generation attempted passed failure root_bytes child_bytes requested reads last_address last_value elapsed_ns '
 'owner_phase pinned owned pci_command').split()+['reserved'+str(i) for i in range(17,64)]


def words(raw,fields,magic,generation):
    if type(raw) is not bytes or len(raw)!=len(fields)*8 or type(generation) is not int or generation<=0:raise ValueError('Channel ABI size/generation')
    r=dict(zip(fields,struct.unpack('<'+str(len(fields))+'Q',raw)))
    if (r['magic'],r['abi'],r['generation'])!=(magic,1,generation):raise ValueError('Channel ABI identity')
    if any(v for k,v in r.items() if k.startswith('reserved')):raise ValueError('Channel ABI reserved field')
    return r


def retained(r):
    return r['owner_phase'] in (7,18) and r['pinned']==r['owned']==1 and r['pci_command']==6


def memory(raw,generation,stage):
    r=words(raw,MEMORY_FIELDS,0x5254584d454d3233,generation)
    if r['stage']!=stage or stage not in (0,1) or any(r[k] not in (0,1) for k in MEMORY_BOOLS):raise ValueError('Memory stage/boolean')
    if (r['budget_ns'],r['cleanup_budget_ns'],r['max_operations'])!=(90_000_000_000,5_000_000_000,100000):raise ValueError('Memory stage budgets')
    if r['failure']>13 or r['inv_failure']>8 or r['operations']>100000 or r['reads']+r['writes']!=r['operations']:raise ValueError('Memory stage operation bounds')
    if r['inv_operations']>4096 or r['inv_reads']+r['inv_writes']!=r['inv_operations']:raise ValueError('Invalidation operation bounds')
    if (r['child_bytes']>45056 or r['child_bytes']%4096 or r['verified_child_bytes']>r['child_bytes'] or r['verified_child_bytes']%4096 or
        r['links_published']>10 or r['zeroed_bytes']>48<<20 or r['verified_backing_bytes']>r['zeroed_bytes'] or r['inspected_bytes']>49<<20):raise ValueError('Memory byte bounds')
    if r['attempted'] and (not retained(r) or not all(r[k] for k in ('lease','mapped','ring_claimed')) or stage==1 and not r['contexts_claimed'] or
        not 0<r['bar1_base']<1<<40 or r['bar1_base']%0x4000000 or r['mapping_physical']!=r['bar1_base']+0x1002000):raise ValueError('Memory retained mapping/owner')
    if r['inv_passed'] and (r['inv_failure'] or not r['inv_completed'] or not r['inv_command_attempted'] or r['inv_writes']!=3 or
        r['inv_last_address']!=0x30b0 or r['inv_last_value']&0x80000000 or r['inv_elapsed_ns']>=2_000_000_000):raise ValueError('Incomplete invalidation')
    if r['passed']:
        if (r['failure'] or not all(r[k] for k in ('attempted','modified','parent_attempted','backing_verified','children_verified','inv_passed')) or
            r['verified_child_bytes']!=r['child_bytes'] or r['zeroed_bytes']!=r['verified_backing_bytes'] or r['elapsed_ns']>=r['budget_ns']):
            raise ValueError('Incomplete memory stage success')
        if stage==0 and (r['zeroed_bytes']!=0x7000 or r['child_bytes']!=8192 or r['links_published']!=1 or not r['window_saved']):raise ValueError('Ring staging dimensions')
        if stage==1 and (not r['zeroed_bytes'] or r['child_bytes']<12288 or r['links_published']!=r['child_bytes']//4096-2):raise ValueError('Context staging dimensions')
    if r['window_restored'] and (stage!=0 or not r['window_saved'] or r['window_after']!=r['window_before'] or r['cleanup_ns']>=r['cleanup_budget_ns']):raise ValueError('Memory window restore')
    return r


def plan(raw,generation):
    names=['magic','abi','generation','valid','physical_end','virtual_end','total_backing_bytes','count']+['entry'+str(i) for i in range(81)]+['reserved'+str(i) for i in range(89,128)]
    r=words(raw,names,0x525458504c4e3233,generation)
    if r['valid'] not in (0,1) or r['count']!=(9 if r['valid'] else 0):raise ValueError('Plan state/count')
    if not r['valid']:
        if any(r[k] for k in names[4:89]):raise ValueError('Invalid plan contains backing')
        return dict(valid=False,buffers=[])
    keys='buffer_id gr_kind physical virtual size allocated_bytes alignment use_physical use_virtual'.split()
    buffers=[]
    for i in range(9):
        b=dict(zip(keys,[r['entry'+str(i*9+j)] for j in range(9)]))
        if b['use_physical'] not in (0,1) or b['use_virtual'] not in (0,1):raise ValueError('Plan boolean')
        b['use_physical']=bool(b['use_physical']);b['use_virtual']=bool(b['use_virtual']);buffers.append(b)
    p=dict(valid=True,buffers=buffers,physical_end=r['physical_end'],virtual_end=r['virtual_end'],total_backing_bytes=r['total_backing_bytes'])
    tx.c.promote_parameters(p)
    if p['physical_end']!=buffers[-1]['physical']+buffers[-1]['allocated_bytes'] or p['virtual_end']!=buffers[-1]['virtual']+buffers[-1]['allocated_bytes'] or p['total_backing_bytes']!=sum(b['allocated_bytes'] for b in buffers):raise ValueError('Plan totals')
    gmmu.validate_ranges(gmmu.mappings(p))
    return p


def rm(raw,generation):
    r=words(raw,RM_FIELDS,0x52545843484e3233,generation)
    bools=prep.BOOLS+['context_valid','context_preparation_attempted','context_prepared','ring_passed','context_passed','snapshot_passed','complete']
    if any(r[k] not in (0,1) for k in bools):raise ValueError('Channel RM boolean')
    if (r['client'],r['channel'],r['compute'],r['copy'],r['page_bytes'],r['max_records'],r['max_pages'],r['request_bytes'],r['budget_ns'],r['max_ticks'])!=(
        prep.CLIENT,0xcf000004,0xcf000005,0xcf000006,4096,16,32,20480,15_000_000_000,150000):raise ValueError('Channel RM profile')
    if (r['failure']>20 or r['step']>4 or not r['completed']<=r['sent']<=5 or r['doorbells']>r['sent'] or
        not r['count']<=r['pages']<=32 or r['count']>16 or r['bytes']!=r['pages']*4096 or r['ticks']>150001 or r['polls']>r['ticks'] or
        r['consumer_writes']>r['count'] or r['rx_reader']>=63 or r['rx_producer']>=63):raise ValueError('Channel RM bounds')
    if r['validated'] and (not 9<=r['tx_reader']<=r['tx_writer']<=14 or r['tx_writer']!=9+r['sent']):raise ValueError('Channel command producer')
    if r['attempted'] and (not retained(r) or not all(r[k] for k in ('validated','claimed','workspace','prefix_consumed'))):raise ValueError('Channel RM owner')
    if r['count'] and r['rx_sequence']!=r['initial_sequence']+r['count']:raise ValueError('Channel response sequence')
    if r['passed'] and (r['failure'] or r['completed']!=5 or r['sent']!=5 or r['doorbells']!=5 or r['tx_writer']!=14 or r['tx_reader']!=14 or
        r['consumer_writes']!=r['count'] or r['last_function']!=103 or r['last_result'] or r['last_param_status'] or
        not all(r[k] for k in ('context_valid','context_preparation_attempted','context_prepared','context_passed')) or r['elapsed_ns']>=r['budget_ns']):raise ValueError('Incomplete five-RM success')
    if r['complete'] and (not all(r[k] for k in ('passed','ring_passed','context_passed','snapshot_passed')) or r['owner_phase']!=18):raise ValueError('Incomplete native overall result')
    return r


def snapshot(raw,generation):
    r=words(raw,SNAP_FIELDS,0x525458534e503233,generation)
    if any(r[k] not in (0,1) for k in ('attempted','passed','pinned','owned')) or r['failure']>6:raise ValueError('Snapshot state')
    if (r['root_bytes']>12288 or r['root_bytes']%4096 or r['child_bytes']>r['requested'] or r['child_bytes']%4096 or r['requested']>45056 or
        r['requested']%4096 or r['reads']>14):raise ValueError('Snapshot bounds')
    if r['attempted'] and (not retained(r) or r['requested']<8192):raise ValueError('Snapshot owner/request')
    if r['passed'] and (r['failure'] or not r['attempted'] or r['root_bytes']!=12288 or r['child_bytes']!=r['requested'] or
        r['reads']!=3+r['requested']//4096 or r['elapsed_ns']>=5_000_000_000):raise ValueError('Incomplete snapshot')
    return r


def capture(backend,generation,output):
    output.mkdir(exist_ok=False)
    result=dict(hardware_compute_verified=False,metal_verified=False)
    diagnostics=(
        ('ring',lambda:backend.channel_memory_info(0),lambda b:memory(b,generation,0)),
        ('contexts',lambda:backend.channel_memory_info(1),lambda b:memory(b,generation,1)),
        ('rm',backend.channel_rm_info,lambda b:rm(b,generation)),('plan',backend.channel_plan_info,lambda b:plan(b,generation)),
        ('snapshot',backend.channel_snapshot_info,lambda b:snapshot(b,generation)))
    # Save every obtainable raw summary before validating a possibly failed one.
    captured={}
    for name,getter,_ in diagnostics:
        raw=getter();(output/(name+'-info.bin')).write_bytes(raw);captured[name]=raw
    for name,_,decoder in diagnostics:result[name]=decoder(captured[name])
    summary=result['rm'];rows=[];offset=0;slot=summary['initial_reader'];step=0;fresh=None;previous=0
    index=backend.channel_rm_index(0,summary['count']) if summary['count'] else b''
    (output/'index.bin').write_bytes(index)
    # Preserve bounded sent requests and the complete journal while this original
    # connection is open. A failed reply or malformed index must not lose them.
    requests=[]
    for sent in range(summary['sent']):
        raw=backend.channel_request(sent);(output/('request-%d.bin'%sent)).write_bytes(raw);requests.append(raw)
    records=b''.join(backend.channel_rm_data(off,min(4096,summary['bytes']-off)) for off in range(0,summary['bytes'],4096))
    (output/'records.bin').write_bytes(records)
    if type(index) is not bytes or len(index)!=summary['count']*72:raise ValueError('Channel index length')
    if len(records)!=summary['bytes'] or any(len(raw)!=4096 for raw in requests):raise ValueError('Channel journal/request transport length')
    for i in range(summary['count']):
        row=dict(zip(prep.ROW_FIELDS,struct.unpack_from('<9Q',index,i*72)))
        if row['offset']!=offset or row['slot']!=slot or row['sequence']!=summary['initial_sequence']+i or row['step']!=step or not 4096<=row['bytes']<=65536 or row['bytes']%4096 or row['bytes']>summary['bytes']-offset or not previous<=row['elapsed_us']<=summary['elapsed_ns']//1000:raise ValueError('Channel index geometry')
        raw=records[offset:offset+row['bytes']]
        name='record-%03d.bin'%row['sequence'];(output/name).write_bytes(raw)
        packet=gsp_rpc.decode_record(raw,expected_sequence=row['sequence'])
        if (packet.rpc.function,packet.rpc.result,len(packet.rpc.payload))!=(row['function'],row['result'],row['payload_bytes']):raise ValueError('Channel packet/index disagreement')
        item=dict(row,file=name,sha256=hashlib.sha256(raw).hexdigest())
        if row['function']==0x1020:
            try:item['nocat']=gsp_event_codec.decode_nocat(packet.rpc.payload)
            except ValueError as error:item['nocat_error']=str(error)
        if step<5 and row['function']==tx.FUNCTIONS[step]:
            try:
                item['exchange']=tx.reply(raw,step,row['sequence'],fresh)
                if step==1:fresh=item['exchange']['context_plan']
                step+=1
            except ValueError as error:
                item['reply_error']=str(error)
                if summary['passed']:raise
        elif summary['passed'] and (row['result'] or row['function'] not in (0x100c,0x1020)):raise ValueError('Unsupported channel event')
        offset+=row['bytes'];slot=(slot+row['bytes']//4096)%63;previous=row['elapsed_us'];rows.append(item)
    if offset!=summary['bytes'] or summary['passed'] and (step!=5 or slot!=summary['rx_reader']):raise ValueError('Missing channel exchange evidence')
    result['records']=rows;result['exchanges_verified']=bool(summary['passed'] and step==5)
    for sent,raw in enumerate(requests):
        if raw!=tx.request(sent,fresh):raise ValueError('Native channel request differs from Python')
    result['sent_requests_verified']=len(requests)
    result['firmware_assertions']=[item['file'] for item in rows if item.get('nocat',{}).get('assert_record')]
    result['firmware_assertions_unresolved']=bool(result['firmware_assertions'])
    for which,key in ((0,'root_bytes'),(1,'child_bytes')):
        count=result['snapshot'][key]
        data=b''.join(backend.channel_snapshot_data(which,off,min(4096,count-off)) for off in range(0,count,4096))
        (output/('root-capture.bin' if which==0 else 'children-capture.bin')).write_bytes(data)
        if len(data)!=count:raise ValueError('Channel snapshot transport length')
    result['table_readback_verified']=False
    if result['exchanges_verified']:
        if fresh is None or not result['plan']['valid'] or result['plan']['buffers']!=fresh['buffers']:raise ValueError('Native plan differs from fresh GR reply')
        root=(output/'root-capture.bin').read_bytes();children=(output/'children-capture.bin').read_bytes()
        if result['snapshot']['passed']:
            # Builder wants the prepublication entry; preserve captured GSP-owned entries.
            old=bytearray(root)
            if struct.unpack_from('<Q',old,gmmu.PARENT_OFFSET)[0]!=gmmu.PARENT_VALUE:raise ValueError('Captured channel parent differs')
            struct.pack_into('<Q',old,gmmu.PARENT_OFFSET,0)
            expected=gmmu.build(bytes(old),gmmu.mappings(fresh))
            if children!=expected['children']:raise ValueError('Actual GPU child tables differ from independent image')
            for va,pa,size in expected['ranges']:
                for off in range(0,size,4096):
                    if gmmu.walk(root,children,va+off)!=pa+off or gmmu.walk(root,children,va+off+4095)!=pa+off+4095:raise ValueError('Captured GPU tables fail software walk')
            result['table_readback_verified']=True
    result['passed']=bool(summary['complete'] and result['exchanges_verified'] and result['table_readback_verified'] and result['ring']['passed'] and result['ring']['window_restored'] and result['contexts']['passed'])
    (output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
