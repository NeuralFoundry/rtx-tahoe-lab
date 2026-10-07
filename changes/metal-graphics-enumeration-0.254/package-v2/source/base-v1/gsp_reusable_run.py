"""Sequential reusable submissions with complete saved evidence and no replay."""
from pathlib import Path
import hashlib,json,struct
import reusable_request as request
import reusable_model as model
import reusable_native as native
ERRORS=(ValueError,OSError,RuntimeError)
JOB_COUNT=65
PARTS=('root','children','device','request','plan','library','code')
def save_chunks(path,size,getter):
    with path.open('xb') as out:
        for off in range(0,size,4096):
            length=min(4096,size-off);b=getter(off,length)
            if type(b) is not bytes or len(b)!=length:raise ValueError('Partial evidence response')
            out.write(b)
def collect(backend,generation,output,serial=None,child_bytes=None):
    output=Path(output);output.mkdir();raw={};errors={};result=dict(passed=False,hardware_accessed=False,metal_verified=False)
    getters=[('runtime',backend.runtime_info)]
    if serial is None:getters.extend([('memory',backend.program_memory_info),('capture',backend.program_capture_info)])
    else:getters.append(('job',lambda:backend.program_job_info(serial)))
    for name,getter in getters:
        try:
            b=getter();(output/(name+'-info.bin')).write_bytes(b);raw[name]=b
        except ERRORS as ex:errors[name]=str(ex)
    try:
        if child_bytes is None:
            b=raw['runtime']
            if type(b) is not bytes or len(b)!=512:raise ValueError('Runtime size record')
            child_bytes=struct.unpack_from('<Q',b,35*8)[0]
        request.integer(child_bytes,'child bytes',8192,45056)
        if child_bytes%4096:raise ValueError('Child alignment')
    except (KeyError,)+ERRORS as ex:errors['child_bytes']=str(ex);child_bytes=None
    sizes=[12288,child_bytes,36864,2112 if serial else 0,4096 if serial else 0,512,4096]
    try:
        if serial is None:sizes[:3]=native.bootstrap_abi.legacy.capture_sizes(raw['capture'])
        elif child_bytes is not None:sizes[:3]=native.sizes(raw['job'],serial,child_bytes)
    except (KeyError,)+ERRORS as ex:errors['sizes']=str(ex)
    for part,(name,size) in enumerate(zip(PARTS,sizes)):
        if size is None or not size:continue
        try:
            getter=(lambda off,n:backend.program_capture_data(part,off,n)) if serial is None and part<3 else (lambda off,n:backend.program_data(0 if part>=5 else serial,part,off,n))
            save_chunks(output/(name+'-capture.bin'),size,getter)
        except ERRORS as ex:errors[name+'_data']=str(ex)
    decoders={'runtime':lambda b:native.info(b,generation)}
    if serial is None:decoders.update(memory=lambda b:native.memory(b,generation),capture=lambda b:native.capture_info(b,generation))
    elif child_bytes is not None:decoders['job']=lambda b:native.job(b,generation,serial,child_bytes)
    for name,decoder in decoders.items():
        if name not in raw:continue
        try:result[name]=decoder(raw[name])
        except ERRORS as ex:errors[name+'_decode']=str(ex)
    result['diagnostic_errors']=errors;(output/'collected.json').write_text(json.dumps(result,indent=2)+'\n');return result
def bootstrap(backend,generation,output,execution,execution_output):
    output=Path(output);r=collect(backend,generation,output)
    try:
        if r['diagnostic_errors']:raise ValueError('Bootstrap diagnostics rejected')
        info,m,c=r['runtime'],r['memory'],r['capture']
        if not info['ready'] or info['completed'] or not m['passed'] or not c['passed']:raise ValueError('Empty ready bootstrap required')
        if not execution or not all(execution.get(k) for k in ('passed','host_command_verified','table_readback_verified')) or execution['rm']['generation']!=generation:raise ValueError('Same-run execution/HOST proof required')
        if m['physical_mode'] or not m['window_observed'] or m['owner_phase']!=18 or c['owner_phase']!=18 or m['child_bytes']!=c['child_bytes'] or m['child_bytes']!=info['child_bytes']:raise ValueError('Bootstrap owner/mapping')
        before=[(Path(execution_output)/(n+'-capture.bin')).read_bytes() for n in PARTS[:3]]
        images=[(output/(n+'-capture.bin')).read_bytes() for n in PARTS[:3]]
        model.verify_sealed((output/'library-capture.bin').read_bytes(),(output/'code-capture.bin').read_bytes())
        r['bytes']=model.bootstrap(*images,*before);r['passed']=True
    except ERRORS as ex:r['error']=str(ex)
    (output/'decoded.json').write_text(json.dumps(r,indent=2)+'\n');return r
def dispatch(backend,generation,output,initial,execution,execution_output,bootstrap_output,wires=None):
    output=Path(output);output.mkdir();r=dict(passed=False,profile='reusable-035',jobs=[],hardware_accessed=False,metal_verified=False)
    try:
        if not initial.get('passed') or not execution.get('passed') or execution['rm']['generation']!=generation:raise ValueError('Verified bootstrap required')
        wires=request.requests(generation) if wires is None else wires
        if type(wires) is not list or len(wires)!=JOB_COUNT:raise ValueError('Exactly 65 reviewed requests required')
        for serial,wire in enumerate(wires,1):
            parsed=request.decode(wire)
            if parsed['generation']!=generation or parsed['serial']!=serial or parsed['program']!=(serial-1)%3:raise ValueError('Request order/generation/program')
        for serial,wire in enumerate(wires,1):(output/('request-'+str(serial)+'.bin')).write_bytes(wire)
        initial_dir=Path(bootstrap_output)
        root,children,previous=[(initial_dir/(n+'-capture.bin')).read_bytes() for n in PARTS[:3]]
        before=[(Path(execution_output)/(n+'-capture.bin')).read_bytes() for n in PARTS[:3]]
        model.bootstrap(root,children,previous,*before)
        child_bytes=initial['runtime']['child_bytes']
        for serial,wire in enumerate(wires,1):
            item=dict(serial=serial,request_sha256=hashlib.sha256(wire).hexdigest(),passed=False);r['jobs'].append(item)
            b=backend.runtime_info();(output/('runtime-before-'+str(serial)+'.bin')).write_bytes(b);ready=native.info(b,generation)
            if not ready['ready'] or ready['completed']!=serial-1 or ready['child_bytes']!=child_bytes:raise ValueError('Runtime readiness changed')
            item['submit_attempted']=True
            try:backend.program_submit(wire)
            except ERRORS as ex:item['submit_error']=str(ex)
            dest=output/('job-'+str(serial));evidence=collect(backend,generation,dest,serial,child_bytes);item['evidence']=evidence
            if item.get('submit_error') or evidence['diagnostic_errors']:raise ValueError('Submission or evidence failed at serial '+str(serial))
            info,job=evidence['runtime'],evidence['job']
            if not info['ready'] or info['completed']!=serial or info['queue_completed']!=serial or info['closed'] or not job['passed'] or job['closed']:raise ValueError('Native retirement was not committed')
            if (dest/'request-capture.bin').read_bytes()!=wire or (dest/'plan-capture.bin').read_bytes()!=model.plan(wire):raise ValueError('Protected request/plan mismatch')
            model.verify_sealed((dest/'library-capture.bin').read_bytes(),(dest/'code-capture.bin').read_bytes())
            if (dest/'root-capture.bin').read_bytes()!=root or (dest/'children-capture.bin').read_bytes()!=children:raise ValueError('Page tables changed during reuse')
            actual=(dest/'device-capture.bin').read_bytes();item['bytes']=model.completed(previous,wire,actual);previous=actual;item['passed']=True
            (dest/'decoded.json').write_text(json.dumps(item,indent=2)+'\n')
        b=backend.runtime_info();(output/'runtime-final.bin').write_bytes(b);r['final']=native.info(b,generation)
        if not r['final']['ready'] or r['final']['completed']!=JOB_COUNT or r['final']['queue_completed']!=JOB_COUNT:raise ValueError('Final reusable retirement proof')
        r.update(passed=True,programs=[request.decode(w)['program'] for w in wires],active_elements=JOB_COUNT*64,ring_wraps=JOB_COUNT//32)
    except ERRORS as ex:r['error']=str(ex)
    finally:(output/'decoded.json').write_text(json.dumps(r,indent=2)+'\n')
    return r
