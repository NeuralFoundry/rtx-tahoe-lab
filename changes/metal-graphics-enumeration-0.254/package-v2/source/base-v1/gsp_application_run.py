"""Bounded application work on the already bootstrapped owning connection.

All retrieval calls read retained CPU evidence. A failed or uncertain submission
is never retried. Raw diagnostics are collected before decoding and before close.
"""
import hashlib
import json
import struct
from pathlib import Path
import application_request_codec as request
import gsp_application_native as native

ERRORS=(ValueError,OSError,RuntimeError)
COUNTS=(3,17,47,64)
PARTS=('root','children','device','request','plan')

def requests(generation):
    return [request.encode(generation,j+1,
        [((0xfffffff0+i*0x1234567)&0xffffffff)^j for i in range(n)],
        [(i*0x1020304+j+16)&0xffffffff for i in range(n)]) for j,n in enumerate(COUNTS)]

def save_chunks(path,size,getter):
    with path.open('xb') as output:
        for offset in range(0,size,4096):
            length=min(4096,size-offset);data=getter(offset,length)
            if type(data) is not bytes or len(data)!=length:raise ValueError('Truncated application capture')
            output.write(data)

def collect(backend,generation,output,index=None):
    """Save independent outputs even when one getter/header is malformed."""
    output=Path(output);output.mkdir();raw={};errors={}
    getters=[('runtime',backend.runtime_info)]
    if index is None:
        getters += [('memory',backend.batch_memory_info),('capture',backend.batch_capture_info)]
    else:
        getters += [('job',lambda:backend.application_job_info(index)),
                    ('submit',lambda:backend.batch_submit_info(index))]
    for name,getter in getters:
        try:
            data=getter();(output/(name+'-info.bin')).write_bytes(data);raw[name]=data
        except ERRORS as error:errors[name]=str(error)
    sizes=None
    try:
        if index is None:sizes=native.legacy.capture_sizes(raw['capture'])
        else:
            data=raw['job']
            if len(data)!=1024:raise ValueError('Job size header')
            sizes=struct.unpack_from('<3Q',data,51*8)
            if any(n>cap or n%4096 for n,cap in zip(sizes,(12288,45056,36864))):raise ValueError('Job capture capacity')
            staged=struct.unpack_from('<Q',data,29*8)[0]==1
            sizes+=((576,1280) if staged else (0,0))
    except (KeyError,)+ERRORS as error:errors['sizes']=str(error)
    if sizes is not None:
        for part,(name,size) in enumerate(zip(PARTS,sizes)):
            try:
                getter=(lambda off,n:backend.batch_capture_data(part,off,n)) if index is None else (lambda off,n:backend.application_data(index,part,off,n))
                save_chunks(output/(name+'-capture.bin'),size,getter)
            except ERRORS as error:errors[name+'_data']=str(error)
    decoders={'runtime':lambda b:native.info(b,generation)}
    if index is None:decoders.update(memory=lambda b:native.memory(b,generation),capture=lambda b:native.capture_info(b,generation))
    else:decoders['job']=lambda b:native.job(b,generation,index)
    result=dict(passed=False,hardware_accessed=False,metal_verified=False)
    for name,decode in decoders.items():
        if name not in raw:continue
        try:result[name]=decode(raw[name])
        except ERRORS as error:errors[name+'_decode']=str(error)
    result['diagnostic_errors']=errors
    (output/'collected.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def bootstrap(backend,generation,output,execution,execution_output):
    output=Path(output);result=collect(backend,generation,output)
    try:
        if result['diagnostic_errors']:raise ValueError('Initial application diagnostics rejected')
        r=result['runtime'];m=result['memory'];c=result['capture']
        if not r['ready'] or r['completed'] or not m['passed'] or not c['passed']:raise ValueError('Runtime must begin ready without a shader submission')
        if not execution or not all(execution.get(k) for k in ('passed','host_command_verified','table_readback_verified')) or execution['rm']['generation']!=generation:raise ValueError('Same-run execution/HOST proof required')
        if m['physical_mode'] or not m['window_observed'] or m['owner_phase']!=18 or c['owner_phase']!=18 or m['child_bytes']!=c['child_bytes']:raise ValueError('Initial application owner/mapping')
        before=[(Path(execution_output)/(n+'-capture.bin')).read_bytes() for n in PARTS[:2]]
        images=[(output/(n+'-capture.bin')).read_bytes() for n in PARTS[:3]]
        result['bytes']=native.verify_bootstrap(*images,*before,generation)
        result['passed']=True
    except ERRORS as error:result['error']=str(error)
    (output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n');return result

def dispatch(backend,generation,output,initial,execution,execution_output,wires=None):
    output=Path(output);output.mkdir()
    result=dict(passed=False,profile='application-032',jobs=[],hardware_accessed=False,metal_verified=False)
    try:
        if not initial.get('passed') or not execution.get('passed') or execution['rm']['generation']!=generation:raise ValueError('Verified bootstrap required before application jobs')
        wires=requests(generation) if wires is None else wires
        if type(wires) is not list or len(wires)!=4:raise ValueError('Exactly four bounded requests required')
        for j,wire in enumerate(wires):
            parsed=request.decode(wire)
            if parsed['generation']!=generation or parsed['request_id']!=j+1:raise ValueError('Application request order/generation')
            # Persist all user inputs before any native request is attempted.
            (output/('request-'+str(j)+'.bin')).write_bytes(wire)
        before=[(Path(execution_output)/(n+'-capture.bin')).read_bytes() for n in PARTS[:2]]
        history=[]
        for j,wire in enumerate(wires):
            item=dict(index=j,request_sha256=hashlib.sha256(wire).hexdigest(),passed=False)
            result['jobs'].append(item)
            raw=backend.runtime_info();(output/('runtime-before-'+str(j)+'.bin')).write_bytes(raw)
            ready=native.info(raw,generation)
            if not ready['ready'] or ready['completed']!=j:raise ValueError('Runtime readiness changed before request')
            item['submit_attempted']=True
            try:backend.application_submit(wire)
            except ERRORS as error:item['submit_error']=str(error)
            dest=output/('job-'+str(j));evidence=collect(backend,generation,dest,j);item['evidence']=evidence
            # A nonzero transport return may still follow real device exposure.
            # Save every readable record, but do not retry or count it as a pass.
            if item.get('submit_error') or evidence['diagnostic_errors']:raise ValueError('Application call or evidence failed at slot '+str(j))
            r=evidence['runtime'];job=evidence['job'];count=len(request.decode(wire)['a'])
            submit=native.submit((dest/'submit-info.bin').read_bytes(),generation,j,wire);evidence['submit']=submit
            flags=('acquired restored stage_passed submit_claimed capture_passed queue_claimed queue_notified capture_complete submit_passed').split()
            if not all(job[k] for k in flags) or job['window_failure'] or job['cleanup_failure'] or job['queue_phase']!=3:raise ValueError('Application native stage/capture/cleanup failed')
            phase=5 if j==3 else 1
            if (r['phase'],r['completed'],job['phase'],job['completed'],job['queue_completed'],job['count'],job['expected_count'],job['completed_elements'],job['completion'])!=(phase,j+1,phase,j+1,j+1,count,count,count,0x306032f0+j):raise ValueError('Application state/counters disagree')
            if not submit['passed'] or submit['session_completed']!=j+1 or submit['token']!=execution['rm']['candidate']:raise ValueError('Application submit sequence/token')
            if (dest/'request-capture.bin').read_bytes()!=wire:raise ValueError('Staged request differs from application input')
            history.append(wire);canonical=native.canonical(history,generation)
            plan=canonical[8192+j*1024:8192+(j+1)*1024]+canonical[16384+j*1024:16640+j*1024]
            if (dest/'plan-capture.bin').read_bytes()!=plan:raise ValueError('Staged input/poison plan differs')
            images=[(dest/(n+'-capture.bin')).read_bytes() for n in PARTS[:3]]
            item['bytes']=native.verify_capture(*images,*before,history,generation)
            item['passed']=True
            (dest/'decoded.json').write_text(json.dumps(item,indent=2)+'\n')
        raw=backend.runtime_info();(output/'runtime-final.bin').write_bytes(raw)
        result['final']=native.info(raw,generation)
        if not result['final']['exhausted'] or result['final']['completed']!=4:raise ValueError('Final runtime exhaustion proof missing')
        result['active_elements']=sum(len(request.decode(w)['a']) for w in wires)
        result['inactive_elements']=256-result['active_elements'];result['passed']=True
    except ERRORS as error:result['error']=str(error)
    finally:(output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
