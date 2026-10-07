"""Bounded immutable 0.24 capture decoder. Import performs no device access."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import gsp_channel_native as old
import gsp_compute_prep_codec as prep
import gsp_rpc
import gsp_event_codec
import gsp_external_native as external
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'changes/gsp-submit-0.24/transactions'))
sys.path.insert(0,str(ROOT/'changes/gsp-submit-0.24/layout'))
import execution_transcript as transcript
import execution_tables as tables
EXECUTION_STEPS=transcript.e.STEPS
EXECUTION_REQUEST_BYTES=transcript.e.REQUEST_BYTES
EXECUTION_FIRST=transcript.e.FIRST
EXECUTION_FINAL_PRODUCER=EXECUTION_FIRST+EXECUTION_STEPS
MEMORY_FIELDS=old.MEMORY_FIELDS
MEMORY_BOOLS=old.MEMORY_BOOLS
words=old.words
retained=old.retained

def memory(raw,generation,stage):
    r=words(raw,MEMORY_FIELDS,0x52545845584d3234,generation)
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
        if stage==0 and (r['zeroed_bytes']!=0x9000 or r['child_bytes']<8192 or r['links_published']!=3 or not r['window_saved']):raise ValueError('Ring staging dimensions')
        if stage==1 and (not r['zeroed_bytes'] or r['child_bytes']<12288 or r['links_published'] not in (1,2)):raise ValueError('Context staging dimensions')
    if r['window_restored'] and (stage!=0 or not r['window_saved'] or r['window_after']!=r['window_before'] or r['cleanup_ns']>=r['cleanup_budget_ns']):raise ValueError('Memory window restore')
    return r


RM_FIELDS=prep.FIELDS[:50]
RM_FIELDS[38:42]=['channel','compute','copy','page_bytes']
RM_FIELDS+=('fixed_preparation_attempted fixed_prepared context_preparation_attempted context_prepared channel_id subdevice_mask '
 'raw_token candidate excluded_staging_ns request_bytes owner_phase consumed runlist_valid runlist_sequence runlist_entry runlist_id pbdmas '
 'pbdma0 pbdma1 fault0 fault1').split()+['reserved'+str(i) for i in range(71,80)]
FENCE_FIELDS=('magic abi generation claimed passed failure command_attempted entry_attempted put_attempted bell_attempted '
 'operations polls reads writes initial_get initial_put initial_fence last_get last_put last_fence token start_ns elapsed_ns '
 'doorbell budget_ns max_operations ring_physical command_physical fence_physical get_physical put_physical command_va fence_va fence_value '
 'owner_phase pinned owned pci_command native_claimed native_notified native_phase word0 word1 word2 word3 word4 entry').split()+['reserved'+str(i) for i in range(47,64)]
CAPTURE_FIELDS=('magic abi generation attempted passed failure bytes reads last_address elapsed_ns owner_phase pinned owned pci_command '
 'ring_physical command_physical fence_physical').split()+['reserved'+str(i) for i in range(17,32)]
COMMAND=struct.pack('<5I',0x20040004,0x10,0x20002000,0x30602401,0x1000002)
ENTRY=struct.pack('<Q',0x1020001000|(1<<41)|(5<<42))


def rm(raw,generation):
    r=words(raw,RM_FIELDS,0x52545845584e3234,generation)
    bools=prep.BOOLS+['fixed_preparation_attempted','fixed_prepared','context_preparation_attempted','context_prepared','runlist_valid']
    if any(r[k] not in (0,1) for k in bools):raise ValueError('Execution RM boolean')
    if (r['client'],r['channel'],r['compute'],r['copy'],r['page_bytes'],r['max_records'],r['max_pages'],r['request_bytes'],r['budget_ns'],r['max_ticks'])!=(
        transcript.e.p.CLIENT,0xcf000007,0xcf000008,0xcf000009,4096,16,32,EXECUTION_REQUEST_BYTES,15_000_000_000,150000):raise ValueError('Execution RM profile')
    if (r['failure']>20 or r['step']>=EXECUTION_STEPS or not r['completed']<=r['sent']<=EXECUTION_STEPS or r['doorbells']>r['sent'] or
        not r['count']<=r['pages']<=32 or r['count']>16 or r['bytes']!=r['pages']*4096 or r['ticks']>150001 or r['polls']>r['ticks'] or
        r['consumer_writes']>r['count'] or r['rx_reader']>=63 or r['rx_producer']>=63 or r['consumed']>EXECUTION_STEPS or r['excluded_staging_ns']>=180_000_000_000):raise ValueError('Execution RM bounds')
    before_setup=r['tx_reader']==r['tx_writer']==14 and r['sent']==r['completed']==r['count']==0 and not r['passed']
    if r['validated'] and not before_setup and (not EXECUTION_FIRST<=r['tx_reader']<=r['tx_writer']<=EXECUTION_FINAL_PRODUCER or r['tx_writer']!=EXECUTION_FIRST+r['sent']):raise ValueError('Execution command producer')
    if r['attempted'] and (not retained(r) or not all(r[k] for k in ('validated','claimed','workspace','prefix_consumed','fixed_preparation_attempted'))):raise ValueError('Execution RM owner')
    if r['count'] and (r['initial_reader']>=63 or r['initial_sequence']>0xffffffff-16 or r['rx_sequence']!=r['initial_sequence']+r['count']):raise ValueError('Execution response sequence')
    if r['runlist_valid'] and (r['runlist_id']>=128 or r['runlist_entry']>=32 or r['pbdmas'] not in (1,2) or
        any(r['pbdma'+str(i)]>=32 or r['fault'+str(i)]>=256 for i in range(r['pbdmas'])) or
        r['pbdmas']==2 and (r['pbdma0']==r['pbdma1'] or r['fault0']==r['fault1'])):raise ValueError('Execution runlist')
    if r['passed'] and (r['failure'] or r['completed']!=EXECUTION_STEPS or r['sent']!=EXECUTION_STEPS or r['doorbells']!=EXECUTION_STEPS or r['tx_writer']!=EXECUTION_FINAL_PRODUCER or r['tx_reader']!=EXECUTION_FINAL_PRODUCER or
        r['consumed']!=EXECUTION_STEPS or r['consumer_writes']!=r['count'] or r['last_function']!=76 or r['last_result'] or r['last_param_status'] or
        not all(r[k] for k in ('fixed_prepared','context_preparation_attempted','context_prepared','runlist_valid')) or r['elapsed_ns']>=r['budget_ns'] or
        not transcript.e.p.session_id(r['channel_id']) or r['subdevice_mask']>1 or r['raw_token']!=4 or r['candidate']!=(r['runlist_id']<<16)|4):raise ValueError('Incomplete grouped execution RM success')
    return r


def plan(raw,generation,golden):
    fields='magic abi generation valid physical_end virtual_end backing_bytes count'.split()+['entry'+str(i) for i in range(21)]+['reserved'+str(i) for i in range(29,64)]
    r=words(raw,fields,0x5254584558503234,generation)
    if r['valid'] not in (0,1) or r['count']!=(3 if r['valid'] else 0):raise ValueError('Execution plan state/count')
    if not r['valid']:
        if any(r[k] for k in fields[4:29]):raise ValueError('Invalid execution plan contains backing')
        return dict(valid=False,buffers=[])
    keys='id kind physical va size allocated alignment'.split()
    buffers=[dict(zip(keys,[r['entry'+str(i*7+j)] for j in range(7)])) for i in range(3)]
    result=dict(valid=True,buffers=buffers,physical_end=r['physical_end'],virtual_end=r['virtual_end'],backing_bytes=r['backing_bytes'])
    if golden is not None:tables.p.validate(result,golden)
    return result


def snapshot(raw,generation):
    # Same capture wire layout, distinct experiment identity.
    words(raw,old.SNAP_FIELDS,0x5254584558533234,generation)
    return old.snapshot(struct.pack('<Q',0x525458534e503233)+raw[8:],generation)


def fence(raw,generation):
    r=words(raw,FENCE_FIELDS,0x525458464e433234,generation)
    flags='claimed passed command_attempted entry_attempted put_attempted bell_attempted pinned owned native_claimed native_notified'.split()
    if any(r[k] not in (0,1) for k in flags) or r['failure']>10 or r['native_phase']>3:raise ValueError('HOST fence state')
    keys='doorbell budget_ns max_operations ring_physical command_physical fence_physical get_physical put_physical command_va fence_va fence_value'.split()
    if tuple(r[k] for k in keys)!=(0xbb0090,5_000_000_000,65536,0x3400000,0x3402000,0x3403000,0x3400888,0x340088c,0x1020001000,0x1020002000,0x30602401):raise ValueError('HOST fence profile')
    if r['operations']>65537 or r['writes']>3 or r['reads']>r['operations'] or r['polls']>r['operations']:raise ValueError('HOST fence bounds')
    if r['claimed'] and (not retained(r) or not r['native_claimed']):raise ValueError('HOST fence owner')
    if r['passed'] and (r['failure'] or not all(r[k] for k in flags) or r['native_phase']!=3 or r['writes']!=3 or not r['polls'] or
        r['elapsed_ns']>=r['budget_ns'] or (r['initial_get'],r['initial_put'],r['initial_fence'])!=(0,0,0) or
        (r['last_get'],r['last_put'],r['last_fence'])!=(1,1,0x30602401) or r['token']>0xffffffff or
        any(r['word'+str(i)]>0xffffffff for i in range(5)) or
        struct.pack('<5I',*(r['word'+str(i)] for i in range(5)))!=COMMAND or struct.pack('<Q',r['entry'])!=ENTRY):raise ValueError('Incomplete HOST fence success')
    return r


def device(raw,generation):
    r=words(raw,CAPTURE_FIELDS,0x5254584341503234,generation)
    if any(r[k] not in (0,1) for k in ('attempted','passed','pinned','owned')) or r['failure']>3:raise ValueError('Device capture state')
    if (r['ring_physical'],r['command_physical'],r['fence_physical'])!=(0x3400000,0x3402000,0x3403000):raise ValueError('Device capture profile')
    if r['bytes']>12288 or r['bytes']%4096 or r['reads']>3 or r['bytes']>r['reads']*4096:raise ValueError('Device capture bounds')
    if r['attempted'] and not retained(r):raise ValueError('Device capture owner')
    if r['passed'] and (r['failure'] or not r['attempted'] or r['reads']!=3 or r['bytes']!=12288 or r['last_address']!=0x3403000 or r['elapsed_ns']>=5_000_000_000):raise ValueError('Incomplete device capture')
    return r


def device_bytes(raw,summary):
    if type(raw) is not bytes or len(raw)!=summary['bytes']:raise ValueError('Device capture transport size')
    if not summary['passed']:return False
    if (raw[:8]!=ENTRY or any(raw[8:256]) or struct.unpack_from('<II',raw,0x888)!=(1,1) or
        raw[4096:4116]!=COMMAND or any(raw[4116:8192]) or struct.unpack_from('<I',raw,8192)[0]!=0x30602401 or any(raw[8196:])):raise ValueError('Actual command/USERD/fence pages disagree')
    return True


def capture(backend,generation,output,golden=None,golden_output=None):
    output.mkdir(exist_ok=False)
    result=dict(passed=False,host_command_verified=False,compute_verified=False,metal_verified=False)
    result['external']=external.capture(backend,generation,output/'external',golden)
    diagnostics=(('fixed',lambda:backend.execution_memory_info(0),lambda b:memory(b,generation,0)),
        ('contexts',lambda:backend.execution_memory_info(1),lambda b:memory(b,generation,1)),
        ('rm',backend.execution_rm_info,lambda b:rm(b,generation)),
        ('plan',backend.execution_plan_info,lambda b:plan(b,generation,golden['plan'] if golden and golden.get('passed') else None)),
        ('snapshot',backend.execution_snapshot_info,lambda b:snapshot(b,generation)),
        ('fence',backend.host_fence_info,lambda b:fence(b,generation)),('device',backend.execution_device_info,lambda b:device(b,generation)))
    captured={};errors={}
    # Attempt all summary reads before rejecting malformed or unavailable data.
    for name,getter,_ in diagnostics:
        try:
            raw=getter();(output/(name+'-info.bin')).write_bytes(raw);captured[name]=raw
        except (ValueError,OSError,RuntimeError) as error:errors[name]=str(error)
    for name,_,decoder in diagnostics:
        if name in captured:
            try:result[name]=decoder(captured[name])
            except ValueError as error:errors[name]=str(error)
    if 'rm' in result and result['rm']['failure']:
        s=result['rm']
        result['native_stop']=dict(stage='execution_rm',failure=s['failure'],
            reason='record_capacity' if s['failure']==17 else 'native_failure',
            step=s['step'],completed=s['completed'],sent=s['sent'],records=s['count'])
    raw_data={}
    def save(name,count,getter,chunk=4096):
        # These counts come from bounded ABI decoders. Preserve each successful
        # chunk immediately and let failures in one image leave others readable.
        try:
            parts=[]
            with (output/name).open('xb') as stream:
                for off in range(0,count,chunk):
                    size=min(chunk,count-off);data=getter(off,size)
                    if type(data) is not bytes or len(data)!=size:raise ValueError('Truncated diagnostic chunk')
                    stream.write(data);parts.append(data)
            raw_data[name]=b''.join(parts)
        except (ValueError,OSError,RuntimeError) as error:errors[name]=str(error)
    # A successful memory capture does not mean HOST ran. Save the journal
    # before interpreting command bytes, replies, index rows, or page tables.
    if 'rm' in result:
        s=result['rm']
        save('index.bin',s['count']*72,lambda off,size:backend.execution_rm_index(off//72,size//72),16*72)
        save('records.bin',s['bytes'],backend.execution_rm_data)
        save('requests.bin',EXECUTION_REQUEST_BYTES,lambda off,size:backend.execution_request(off//4096))
    if 'snapshot' in result:
        for which,field,name in ((0,'root_bytes','root-capture.bin'),(1,'child_bytes','children-capture.bin')):
            save(name,result['snapshot'][field],lambda off,size,which=which:backend.execution_snapshot_data(which,off,size))
    if 'device' in result:save('device-capture.bin',result['device']['bytes'],backend.execution_device_data)
    try:
        if errors:raise ValueError('Execution diagnostics rejected: '+repr(errors))
        if 'rm' in result:
            summary=result['rm'];rows=[];parts=[];offset=0;slot=summary['initial_reader'];previous=0;step=0
            index=raw_data['index.bin']
            for i in range(summary['count']):
                row=dict(zip(prep.ROW_FIELDS,struct.unpack_from('<9Q',index,i*72)))
                if (row['offset']!=offset or row['slot']!=slot or row['sequence']!=summary['initial_sequence']+i or row['step']!=step or
                    not 4096<=row['bytes']<=65536 or row['bytes']%4096 or row['bytes']>summary['bytes']-offset or
                    not previous<=row['elapsed_us']<=summary['elapsed_ns']//1000):raise ValueError('Execution index geometry')
                raw=raw_data['records.bin'][offset:offset+row['bytes']]
                (output/('record-%03d.bin'%row['sequence'])).write_bytes(raw)
                p=gsp_rpc.decode_record(raw,expected_sequence=row['sequence'])
                if (p.rpc.function,p.rpc.result,len(p.rpc.payload))!=(row['function'],row['result'],row['payload_bytes']):raise ValueError('Execution packet/index disagreement')
                if p.rpc.function==0x1020:
                    row['nocat']=gsp_event_codec.decode_nocat(p.rpc.payload)
                if step<EXECUTION_STEPS and p.rpc.function==(103 if step in transcript.e.ALLOC else 76):step+=1
                parts.append(raw);rows.append(row);offset+=row['bytes'];slot=(slot+row['bytes']//4096)%63;previous=row['elapsed_us']
            if offset!=summary['bytes'] or summary['passed'] and (slot!=summary['rx_reader'] or step!=EXECUTION_STEPS):raise ValueError('Execution record totals')
            records=b''.join(parts);result['records']=rows;requests=raw_data['requests.bin']
            result['firmware_assertions']=[row['sequence'] for row in rows if row.get('nocat',{}).get('assert_record')]
            result['firmware_assertions_unresolved']=bool(result['firmware_assertions'])
        # A successful readback only proves capture. Expected HOST bytes apply
        # after the fence executor reports success; an earlier RM stop leaves
        # zeroed pages and must retain its own diagnosis and decoded journal.
        result['device_bytes_verified']=False
        if 'device' in result and result.get('fence',{}).get('passed'):
            result['device_bytes_verified']=device_bytes(raw_data['device-capture.bin'],result['device'])
        if 'native_stop' in result:
            s=result['native_stop']
            result['error']='Execution RM stopped: failure %d (%s), completed %d/%d'%(s['failure'],s['reason'],s['completed'],EXECUTION_STEPS)
        if not result['external']['passed']:
            if result['external'].get('native_stop'):result['native_stop']=result['external']['native_stop']
            result['error']=result['external'].get('error','External VA setup did not complete')
            return result
        if not all(result[k]['passed'] for k in ('fixed','contexts','rm','snapshot','fence','device')):return result
        if not golden or not golden.get('passed') or golden_output is None:raise ValueError('Successful same-run golden channel capture required')
        s=result['rm'];g=result['external']['info']
        if g['generation']!=generation or (g['rx_reader'],g['rx_sequence'])!=(s['initial_reader'],s['initial_sequence']):raise ValueError('Execution/external queue boundary')
        proof=transcript.verify(requests,records,s['initial_sequence'],golden['plan'],3)
        if (proof['records'],proof['pages'],proof['next_sequence'],proof['channel_id'],proof['subdevice_mask'],proof['raw_token'],proof['candidate'])!=(
            s['count'],s['pages'],s['rx_sequence'],s['channel_id'],s['subdevice_mask'],s['raw_token'],s['candidate']):raise ValueError('Execution journal summary differs')
        route=proof['runlist']
        if (route['sequence'],route['entry'],route['id'],len(route['pbdma']))!=(s['runlist_sequence'],s['runlist_entry'],s['runlist_id'],s['pbdmas']) or any(
            route['pbdma'][i]!=s['pbdma'+str(i)] or route['fault'][i]!=s['fault'+str(i)] for i in range(s['pbdmas'])):raise ValueError('Fresh FIFO route differs')
        if result['plan']!=proof['private_plan']:raise ValueError('Execution private plan differs from fresh GR')
        old_root=(golden_output/'root-capture.bin').read_bytes();old_children=(golden_output/'children-capture.bin').read_bytes()
        merged=tables.merge(old_root,old_children,golden['plan'],proof['private_plan'])
        root=(output/'root-capture.bin').read_bytes();children=(output/'children-capture.bin').read_bytes()
        if root!=old_root or children!=merged['children']:raise ValueError('Final execution page tables differ')
        for va,pa,size in tables.p.g.mappings(golden['plan'])+tables.p.mappings(proof['private_plan'],golden['plan']):
            for off in range(0,size,4096):
                if tables.p.g.walk(root,children,va+off)!=pa+off or tables.p.g.walk(root,children,va+off+4095)!=pa+off+4095:raise ValueError('Execution page table walk failed')
        fixed=result['fixed'];contexts=result['contexts'];f=result['fence']
        if (fixed['child_bytes']!=len(old_children) or contexts['child_bytes']!=len(children) or contexts['links_published']!=merged['added_leaf_pages'] or
            contexts['verified_backing_bytes']!=proof['private_plan']['backing_bytes'] or f['token']!=proof['candidate'] or not fixed['window_restored'] or
            any(result[k]['owner_phase']!=17 for k in ('fixed','contexts','rm','snapshot','fence','device')) or
            fixed['physical_mode'] or contexts['physical_mode'] or not result.get('device_bytes_verified')):raise ValueError('Execution final publication/owner/fence evidence')
        result.update(passed=True,host_command_verified=True,table_readback_verified=True,journal=proof)
        return result
    except (ValueError,OSError,RuntimeError) as error:
        result['error']=str(error);result['passed']=False;return result
    finally:
        if errors:result['summary_errors']=errors
        (output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
