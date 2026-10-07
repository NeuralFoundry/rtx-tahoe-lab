"""Read-only five-RPC external address-space journal. No I/O on import."""
import json
from pathlib import Path
import struct
import sys
import gsp_application_channel_native as old
import gsp_compute_prep_codec as prep
import gsp_event_codec
import gsp_rpc
sys.path.insert(0,str(Path(__file__).resolve().parent/'changes/gsp-external-vas-0.27'))
import external_vas as protocol

FIELDS=prep.FIELDS[:50]
FIELDS[38:42]=['device','subdevice','vaspace','page_bytes']
FIELDS+=('directory_checks directory_acknowledged consumed complete base size internal_lo internal_hi big_page '
         'root_physical root_entries directory_flags request_bytes owner_phase').split()

def info(raw,generation):
    r=old.words(raw,FIELDS,0x5254584556413237,generation)
    if any(r[k] not in (0,1) for k in prep.BOOLS+['directory_acknowledged','complete']):raise ValueError('External ABI boolean')
    keys='client device subdevice vaspace page_bytes max_records max_pages budget_ns max_ticks root_physical root_entries directory_flags request_bytes'.split()
    if tuple(r[k] for k in keys)!=(protocol.CLIENT,*protocol.OBJECTS[1:],4096,16,32,15_000_000_000,150000,protocol.ROOT_PA,4,8,20480):
        raise ValueError('External ABI fixed profile')
    if (r['failure']>20 or r['step']>=5 or not r['completed']<=r['sent']<=5 or r['doorbells']>r['sent'] or
        not r['count']<=r['pages']<=32 or r['count']>16 or r['bytes']!=r['pages']*4096 or r['ticks']>150001 or r['polls']>r['ticks'] or
        r['consumer_writes']>r['count'] or r['rx_reader']>=63 or r['rx_producer']>=63 or r['consumed']>5 or r['directory_checks']>2):
        raise ValueError('External ABI bounds')
    if r['validated'] and (not 14<=r['tx_reader']<=r['tx_writer']<=19 or r['tx_writer']!=14+r['sent']):raise ValueError('External command producer')
    if r['attempted'] and (not old.retained(r) or not all(r[k] for k in ('validated','claimed','workspace','prefix_consumed'))):raise ValueError('External retained owner')
    if r['count'] and (r['initial_reader']>=63 or r['initial_sequence']>0xffffffff-16 or r['rx_sequence']!=r['initial_sequence']+r['count']):raise ValueError('External sequence bounds')
    if r['passed'] and (r['failure'] or not all(r[k] for k in ('validated','attempted','prefix_consumed','directory_acknowledged','complete')) or
        r['directory_checks']!=2 or r['consumed']!=5 or r['sent']!=5 or r['completed']!=5 or r['doorbells']!=5 or r['step']!=4 or
        r['tx_writer']!=19 or r['tx_reader']!=19 or r['consumer_writes']!=r['count'] or r['last_function']!=54 or r['last_result'] or
        r['last_param_status'] or r['elapsed_ns']>=r['budget_ns']):raise ValueError('Incomplete external setup success')
    return r

def capture(backend,generation,output,golden=None):
    output.mkdir(exist_ok=False)
    result=dict(passed=False,compute_verified=False,metal_verified=False)
    try:
        raw=backend.external_info();(output/'info.bin').write_bytes(raw)
        s=info(raw,generation);result['info']=s
        if s['failure']:
            result['native_stop']=dict(stage='external_va',failure=s['failure'],step=s['step'],sent=s['sent'],completed=s['completed'],records=s['count'])
        data={};errors={}
        for name,count,getter,stride in (
            ('index.bin',s['count']*72,lambda off,n:backend.external_index(off//72,n//72),1152),
            ('records.bin',s['bytes'],backend.external_data,4096),
            ('requests.bin',20480,lambda off,n:backend.external_request(off//4096),4096)):
            try:
                with (output/name).open('xb') as stream:
                    parts=[]
                    for off in range(0,count,stride):
                        n=min(stride,count-off);part=getter(off,n)
                        if type(part) is not bytes or len(part)!=n:raise ValueError('External diagnostic chunk size')
                        stream.write(part);parts.append(part)
                    data[name]=b''.join(parts)
            except (ValueError,OSError,RuntimeError) as error:errors[name]=str(error)
        if errors:result['capture_errors']=errors;raise ValueError('External raw capture incomplete')
        rows=[];offset=pages=step=previous=0;va=None
        for i in range(s['count']):
            row=dict(zip(prep.ROW_FIELDS,struct.unpack_from('<9Q',data['index.bin'],i*72)))
            if (step>=5 or row['offset']!=offset or row['slot']!=(s['initial_reader']+pages)%63 or row['sequence']!=s['initial_sequence']+i or
                row['step']!=step or not 4096<=row['bytes']<=65536 or row['bytes']%4096 or row['bytes']>s['bytes']-offset or
                not previous<=row['elapsed_us']<=s['elapsed_ns']//1000):raise ValueError('External index geometry')
            packet=data['records.bin'][offset:offset+row['bytes']]
            (output/('record-%03d.bin'%row['sequence'])).write_bytes(packet)
            p=gsp_rpc.decode_record(packet,expected_sequence=row['sequence']).rpc
            if (p.function,p.result,len(p.payload))!=(row['function'],row['result'],row['payload_bytes']):raise ValueError('External packet/index disagreement')
            if p.function==0x1020:row['nocat']=gsp_event_codec.decode_nocat(p.payload)
            expected=p.function==(103 if step<4 else 54)
            if expected:
                if s['passed']:
                    decoded=protocol.reply(step,row['sequence'],packet)
                    if step==3:va=decoded
                step+=1
            elif s['passed']:
                # The same bounded informational events accepted by native RM.
                supported=(p.function==0x1020 and len(p.payload)==1212 or
                    p.function==0x100c and len(p.payload)>=8 and struct.unpack_from('<I',p.payload,4)[0]==len(p.payload)-8)
                if p.result or not supported:raise ValueError('Unsupported external event')
            rows.append(row);offset+=row['bytes'];pages+=row['bytes']//4096;previous=row['elapsed_us']
        result['records']=rows
        result['firmware_assertions']=[r['sequence'] for r in rows if r.get('nocat',{}).get('assert_record')]
        if offset!=s['bytes'] or pages!=s['pages']:raise ValueError('External journal totals')
        if not s['passed']:return result
        if step!=5 or (s['initial_reader']+pages)%63!=s['rx_reader']:raise ValueError('External completed journal boundary')
        if data['requests.bin']!=b''.join(protocol.request(i) for i in range(5)):raise ValueError('External canonical requests')
        if not va or any(va[k]!=s[k] for k in ('base','size','internal_lo','internal_hi','big_page')):raise ValueError('External returned VA differs from summary')
        if not golden or not golden.get('passed'):raise ValueError('Same-run golden prefix required for external VA')
        g=golden['rm']
        if g['generation']!=generation or g['channel_id']!=3 or (g['tx_writer'],g['tx_reader'])!=(14,14) or (g['rx_reader'],g['rx_sequence'])!=(s['initial_reader'],s['initial_sequence']):
            raise ValueError('External/golden queue boundary')
        result.update(passed=True,vaspace=va)
        return result
    except (ValueError,OSError,RuntimeError) as error:
        result['error']=str(error);return result
    finally:
        (output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
