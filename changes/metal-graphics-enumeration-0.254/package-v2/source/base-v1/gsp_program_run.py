"""Bounded program work on the already bootstrapped owning connection.

All retrieval calls read retained CPU evidence. A failed or uncertain submission
is never retried. Raw diagnostics are collected before decoding and before close.
"""
import hashlib
import json
import struct
from pathlib import Path
import program_request_codec as request
import gsp_program_native as native

ERRORS=(ValueError,OSError,RuntimeError)
COUNTS=(64,64,64,64)
PROGRAMS=(0,1,2,0)
PARTS=('root','children','device','request','plan','library','code')

def requests(generation):
    result=[];seed=0x33001000
    for j,program in enumerate(PROGRAMS):
        a=[];b=[]
        for _ in range(64):
            seed=(seed*1664525+1013904223)&0xffffffff;a.append(seed)
            seed=(seed*1664525+1013904223)&0xffffffff;b.append(seed)
        a[:4]=[0,0xffffffff,0x80000000,0x7fffffff];b[:4]=[0xffffffff,1,0x80000000,1]
        result.append(request.encode(generation,j+1,program,a,b))
    return result


def save_chunks(path,size,getter):
    with path.open('xb') as output:
        for offset in range(0,size,4096):
            length=min(4096,size-offset);data=getter(offset,length)
            if type(data) is not bytes or len(data)!=length:raise ValueError('Truncated program capture')
            output.write(data)

def collect(backend,generation,output,index=None):
    """Save independent outputs even when one getter/header is malformed."""
    output=Path(output);output.mkdir();raw={};errors={}
    getters=[('runtime',backend.runtime_info)]
    if index is None:
        getters += [('memory',backend.program_memory_info),('capture',backend.program_capture_info)]
    else:
        getters += [('job',lambda:backend.program_job_info(index)),
                    ('submit',lambda:backend.program_submit_info(index))]
    for name,getter in getters:
        try:
            data=getter();(output/(name+'-info.bin')).write_bytes(data);raw[name]=data
        except ERRORS as error:errors[name]=str(error)
    sizes=[None,None,None,None,None,512,4096]
    try:
        if index is None:sizes[:5]=list(native.legacy.capture_sizes(raw['capture']))+[0,0]
        else:
            data=raw['job']
            if type(data) is not bytes or len(data)!=1024:raise ValueError('Job size header')
            counts=struct.unpack_from('<3Q',data,53*8)
            if any(n>cap or n%4096 for n,cap in zip(counts,(12288,45056,36864))):raise ValueError('Job capture capacity')
            staged=struct.unpack_from('<Q',data,29*8)[0]==1
            claimed=struct.unpack_from('<Q',data,30*8)[0]==1
            sizes[:5]=list(counts)+[2112 if staged else 0,4096 if claimed else 0]
    except (KeyError,)+ERRORS as error:errors['sizes']=str(error)
    for part,(name,size) in enumerate(zip(PARTS,sizes)):
        if size is None:continue
        try:
            getter=(lambda off,n:backend.program_capture_data(part,off,n)) if index is None and part<3 else (lambda off,n:backend.program_data(0 if index is None else index,part,off,n))
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
        if result['diagnostic_errors']:raise ValueError('Initial program diagnostics rejected')
        r=result['runtime'];m=result['memory'];c=result['capture']
        if not r['ready'] or r['completed'] or not m['passed'] or not c['passed']:raise ValueError('Runtime must begin ready without a shader submission')
        if not execution or not all(execution.get(k) for k in ('passed','host_command_verified','table_readback_verified')) or execution['rm']['generation']!=generation:raise ValueError('Same-run execution/HOST proof required')
        if m['physical_mode'] or not m['window_observed'] or m['owner_phase']!=18 or c['owner_phase']!=18 or m['child_bytes']!=c['child_bytes']:raise ValueError('Initial program owner/mapping')
        before=[(Path(execution_output)/(n+'-capture.bin')).read_bytes() for n in PARTS[:2]]
        images=[(output/(n+'-capture.bin')).read_bytes() for n in PARTS[:3]]
        native.model.verify_sealed((output/'library-capture.bin').read_bytes(),(output/'code-capture.bin').read_bytes())
        result['bytes']=native.verify_bootstrap(*images,*before,generation)
        result['passed']=True
    except ERRORS as error:result['error']=str(error)
    (output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n');return result

def dispatch(backend,generation,output,initial,execution,execution_output,wires=None):
    output=Path(output);output.mkdir()
    result=dict(passed=False,profile='program-033',jobs=[],hardware_accessed=False,metal_verified=False)
    try:
        if not initial.get('passed') or not execution.get('passed') or execution['rm']['generation']!=generation:raise ValueError('Verified bootstrap required before program jobs')
        wires=requests(generation) if wires is None else wires
        if type(wires) is not list or len(wires)!=4:raise ValueError('Exactly four bounded requests required')
        for j,wire in enumerate(wires):
            parsed=request.decode(wire)
            if parsed['generation']!=generation or parsed['request_id']!=j+1 or parsed['program']!=PROGRAMS[j]:raise ValueError('Program request order/generation')
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
            try:backend.program_submit(wire)
            except ERRORS as error:item['submit_error']=str(error)
            dest=output/('job-'+str(j));evidence=collect(backend,generation,dest,j);item['evidence']=evidence
            # A nonzero transport return may still follow real device exposure.
            # Save every readable record, but do not retry or count it as a pass.
            if item.get('submit_error') or evidence['diagnostic_errors']:raise ValueError('Program call or evidence failed at slot '+str(j))
            r=evidence['runtime'];job=evidence['job']
            submit=native.submit((dest/'submit-info.bin').read_bytes(),generation,j,wire);evidence['submit']=submit
            flags=('acquired restored stage_passed submit_claimed capture_passed queue_claimed queue_notified capture_complete submit_passed').split()
            if not all(job[k] for k in flags) or job['window_failure'] or job['cleanup_failure'] or job['queue_phase']!=3:raise ValueError('Program native stage/capture/cleanup failed')
            phase=5 if j==3 else 1
            if (r['phase'],r['completed'],job['phase'],job['completed'],job['queue_completed'],job['groups'],job['program'],job['request_id'],job['completion'])!=(phase,j+1,phase,j+1,j+1,1,PROGRAMS[j],j+1,0x306033f0+j):raise ValueError('Program state/counters disagree')
            if not submit['passed'] or submit['session_completed']!=j+1 or submit['token']!=execution['rm']['candidate']:raise ValueError('Program submit sequence/token')
            if (dest/'request-capture.bin').read_bytes()!=wire:raise ValueError('Staged request differs from program input')
            native.model.verify_sealed((dest/'library-capture.bin').read_bytes(),(dest/'code-capture.bin').read_bytes())
            history.append(wire);plan=native.model.plan(wire)
            if (dest/'plan-capture.bin').read_bytes()!=plan:raise ValueError('Staged input/poison plan differs')
            images=[(dest/(n+'-capture.bin')).read_bytes() for n in PARTS[:3]]
            item['bytes']=native.verify_capture(*images,*before,history,generation)
            item['passed']=True
            (dest/'decoded.json').write_text(json.dumps(item,indent=2)+'\n')
        raw=backend.runtime_info();(output/'runtime-final.bin').write_bytes(raw)
        result['final']=native.info(raw,generation)
        if not result['final']['exhausted'] or result['final']['completed']!=4:raise ValueError('Final runtime exhaustion proof missing')
        result['active_elements']=sum(len(request.decode(w)['a']) for w in wires)
        result['inactive_elements']=256-result['active_elements'];result['programs']=list(PROGRAMS);result['passed']=True
    except ERRORS as error:result['error']=str(error)
    finally:(output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
