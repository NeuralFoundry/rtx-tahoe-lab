"""An explicit bounded compiler workload, independent of application count.

The root registry currently retains 64 entries including the bootstrap image.
This run contract therefore admits at most63 new transactions. It is not an
unbounded daily-use compiler cache or permission to discard in-use pipelines.
"""
import hashlib,json,re
def need(value,message):
    if not value:raise ValueError(message)
def validate(value):
    need(type(value)is dict and set(value)=={'abi','outcomes'},'Compiler workload fields')
    need(type(value['abi'])is int and value['abi']==1,'Compiler workload ABI')
    out=value['outcomes'];need(type(out)is list and 1<=len(out)<=63 and all(type(x)is bool for x in out),'Compiler outcome sequence')
    return dict(abi=1,outcomes=list(out))
def digest(value):
    return hashlib.sha256(json.dumps(validate(value),sort_keys=True,separators=(',',':')).encode()).hexdigest()
def peer(value):
    need(type(value)is dict and set(value)=={'uid','pid'},'Kernel compiler peer fields')
    need(type(value['uid'])is int and value['uid']==501 and type(value['pid'])is int and 0<value['pid']<=0x7fffffff,'Kernel compiler peer identity')
    return dict(value)
def completion(bridge,service,compiler_count,receipts,workload):
    def pin(v):return type(v)is str and re.fullmatch('[0-9a-f]{64}',v)is not None
    plan=validate(workload);outcomes=plan['outcomes'];need(type(compiler_count)is int and compiler_count==len(outcomes),'Planned compiler transactions')
    need(service['stopped']is True and service['active']is False and service['socket_removed']is True and type(service['dropped_records'])is int and service['dropped_records']==0,'Compiler service drained')
    records=service['records'];need(type(records)is list and len(records)==len(outcomes),'Compiler service record count')
    for row,ok in zip(records,outcomes):
        peer(dict(uid=row['peer_uid'],pid=row['peer_pid']))
        need(row['accepted']is True and type(row['compiled'])is bool and row['compiled']==ok and row['error']is None,'Compiler result sequence')
        need(pin(row.get('air_sha256')) and type(row.get('entry'))is str and re.fullmatch('[A-Za-z_][A-Za-z_0-9]{0,126}',row['entry']) is not None,'Compiler request identity')
    need(type(receipts)is list and len(receipts)==sum(outcomes),'All successful compiler receipts')
    need(bridge['retired']is True and type(bridge['calls'])is int and bridge['calls']>=0 and type(bridge['dropped_records'])is int and bridge['dropped_records']==0 and len(bridge['records'])==bridge['calls'],'Resident callback drain')
    registry=bridge['registry'];need(registry['closed']is False,'Registry prematurely closed')
    registered=registry['receipts'];need(type(registered)is list and type(registry['registered_transactions'])is int and len(registered)==len(receipts)+1==registry['registered_transactions'],'Bootstrap plus completed transactions')
    need(all(type(r)is dict and pin(r.get('runtime_manifest_sha256')) and pin(r.get('payload_sha256')) for r in registered),'Compiler receipt hashes')
    by_manifest={r['runtime_manifest_sha256']:r for r in registered};need(len(by_manifest)==len(registered),'Distinct compiler transactions')
    need(type(registry['unique_payloads'])is int and registry['unique_payloads']==len({r['payload_sha256']for r in registered}),'Distinct payload count')
    need(len({r['runtime_manifest_sha256']for r in receipts})==len(receipts),'No repeated compiler result')
    for r,row in zip(receipts,[r for r in records if r['compiled']]):
        need(r.get('admission_origin')=='owner_runtime_compiler' and r.get('admitted_bytes')==4608 and r.get('gpu_uploaded')is False and by_manifest.get(r.get('runtime_manifest_sha256'))==r,'Root-owned compiler receipt')
        need(r.get('native_air_sha256')==row['air_sha256'] and r.get('entry')==row['entry'],'Compiler receipt matches connected peer request')
    # A compiled program can be used repeatedly or remain unused. Every actual
    # replacement callback must refer to an admitted transaction, regardless
    # of whether another application previously compiled identical payloads.
    for i,row in enumerate(bridge['records']):
        receipt=by_manifest.get(row.get('runtime_manifest_sha256'))
        need(type(row.get('call'))is int and row['call']==i+1 and type(row.get('status'))is int and row['status']==0 and row.get('error')is None and receipt is not None and row.get('payload_sha256')==receipt['payload_sha256'],'Resident callback provenance')
    return dict(passed=True,compiler_requests=len(outcomes),compiled_programs=sum(outcomes),rejected_compiles=len(outcomes)-sum(outcomes),compiler_drained=True,compiler_admissions=bridge['calls'],compiler_peer_pids=[r['peer_pid']for r in records],workload_sha256=digest(plan),physical_gpu_verified=False)
