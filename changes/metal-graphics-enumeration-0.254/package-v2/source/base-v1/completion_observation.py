"""Decode the read-only 0.37 observation snapshot; never access hardware."""
import struct
MAGIC=0x5254584f42533337
FIELDS=('magic abi generation serial phase completed failure attempted passed restored writes notifications polls operations started elapsed observations mismatch_mask capture_kind capture_serial capture_passed capture_bytes notified observation_order core_passed core_failure backing_completed closed').split()
RECORD=('stage sequence read_attempted read_passed complete elapsed_before elapsed_after get put qmd timeline').split()
def need(value,message):
    if not value:raise ValueError(message)
def difference(a,b):return sum((1<<i) for i,k in enumerate(('get','put','qmd','timeline')) if a[k]!=b[k])
def poll_mask(o,serial):
    entry=serial&31;put=(entry+1)&31;previous=serial-1
    return (int(o['get'] not in (entry,put))|int(o['put']!=put)<<1|int(o['qmd'] not in (0,serial))<<2|
        int(o['timeline'] not in (previous,serial))<<3|int(o['timeline']==serial and o['qmd']!=serial)<<4)
def decode(data,generation,serial,allow_legacy=False):
    need(type(data) is bytes and len(data)==512,'observation bytes')
    need(type(generation) is int and 0<generation<2**64 and type(serial) is int and 0<=serial<2**64,'expected scope')
    words=struct.unpack('<64Q',data);r=dict(zip(FIELDS,words))
    need((r['magic'],r['abi'],r['generation'],r['serial'])==(MAGIC,1,generation,serial),'observation identity/ABI')
    need(not any(words[28:32]+words[43:48]+words[59:64]),'observation reserved words')
    need(r['observation_order'] in ((0,1) if allow_legacy else (1,)),'ordered observation policy')
    need(r['phase'] in (0,1,3,4) and r['failure']<=14 and r['core_failure']<=14 and r['mismatch_mask']<=31,'observation state')
    for key in ('attempted','passed','restored','capture_passed','notified','core_passed','closed'):need(r[key] in (0,1),'boolean '+key)
    need(r['writes']<=7 and r['notifications']<=1 and r['operations']<=65536 and r['polls']<=r['observations']<=r['operations']+1,'bounded counters')
    need(r['notified']==bool(r['notifications']) and r['capture_kind']<=4 and r['capture_bytes']<=94208 and r['capture_bytes']%4096==0,'notification/capture shape')
    records=[]
    for offset in (32,48):
        o=dict(zip(RECORD,words[offset:offset+11]));records.append(o)
        need(o['stage']<=5 and o['complete'] in (0,1) and o['read_passed'] in (0,1) and o['read_attempted'] in (0,1),'record shape')
        need(o['complete']<=o['read_passed']<=o['read_attempted'],'record validity flags')
        need(o['elapsed_before']<=o['elapsed_after']<=r['elapsed'],'record elapsed bounds')
        if o['stage']==0:need(not any(o.values()),'unused record')
        else:need(o['sequence']>0,'record sequence')
        if not o['read_attempted']:need(not any(o[k] for k in RECORD[3:]),'unattempted read has values')
    last,previous=records;r['last']=last;r['previous']=previous
    need(last['sequence']==r['observations'] and previous['sequence']==max(0,r['observations']-1),'record continuity')
    if last['stage']:
        expected_previous={1:0,2:1,3:2,4:3 if r['polls']==1 else 4,5:4}[last['stage']]
        need(previous['stage']==expected_previous,'record stage continuity')
        need(r['observations']==(last['stage'] if last['stage']<=3 else r['polls']+(3 if last['stage']==4 else 4)),'observation count/phase')
    if not r['attempted']:
        need(serial==r['completed']==r['backing_completed']==0,'empty scope')
        need(not any(r[k] for k in FIELDS[6:23]) and not any(r[k] for k in FIELDS[24:27]),'empty operation evidence')
        return r
    need(serial>0 and r['completed'] in (serial-1,serial) and r['backing_completed'] in (serial-1,serial),'completion scope')
    passed=bool(r['core_passed'] and r['backing_completed']==serial)
    failure=13 if r['core_failure']==0 and r['core_passed'] and not passed else r['core_failure']
    need(r['passed']==passed and r['failure']==failure,'core/backing result agreement')
    if r['capture_kind']:need(r['capture_serial']==serial,'current capture serial')
    else:need(r['capture_serial']==r['capture_passed']==r['capture_bytes']==0,'empty capture')
    if passed:
        need(r['failure']==0 and r['completed']==serial and r['restored']==r['notifications']==1 and r['writes']==7 and not r['mismatch_mask'],'completed operation')
        need(last['stage']==5 and last['complete']==previous['complete']==1,'completed observations')
        expected=dict(get=((serial&31)+1)&31,put=((serial&31)+1)&31,qmd=serial,timeline=serial)
        need(not difference(last,expected) and not difference(previous,expected),'completed queue/releases')
    else:need(r['phase']==3 and r['failure']!=0,'failed operation retained')
    if r['mismatch_mask']:
        need(r['core_failure']==10 and last['complete']==1,'rejected complete observation')
        stage=last['stage'];need(stage>0,'mismatch phase')
        if stage==4:mask=poll_mask(last,serial)
        elif stage in (2,5):mask=difference(last,previous)
        else:
            entry=serial&31;mask=difference(last,dict(get=entry,put=entry,qmd=0 if stage==3 else serial-1,timeline=serial-1))
        need(mask==r['mismatch_mask'],'observation mismatch mask')
    return r
