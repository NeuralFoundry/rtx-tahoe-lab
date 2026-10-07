"""Read-only 0.30 vector diagnostic decoding; import performs no device access."""
from pathlib import Path
import hashlib,json,struct,sys
import gsp_compute_native as legacy
import gsp_execution_native as execution
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'changes/gsp-vector-compute-0.30/submit'))
import vector_capture
MAGICS=(0x525458564d4d3330,0x52545856534d3330,0x5254585643503330)
MEMORY_FIELDS=legacy.MEMORY_FIELDS
CAPTURE_FIELDS=legacy.CAPTURE_FIELDS
INITIAL_SHA='b44e7136fac5f914e113d66688f591707e3f93fec05d29c85f1e9522482883de'
SUBMIT_FIELDS=('magic abi generation claimed passed failure command_attempted entry_attempted put_attempted bell_attempted '
 'immutable_verified guards_verified stable operations reads writes polls token last_address initial_get initial_put initial_completion '
 'get put host_fence completion completed_elements count start_ns elapsed_ns command_physical entry_physical get_physical put_physical '
 'output_physical completion_physical command_va output_va completion_va input_a_va input_b_va doorbell budget_ns max_operations '
 'owner_phase pinned owned pci_command native_claimed native_notified native_phase command_bytes entry_bytes '
 'word0 word1 word2 word3 word4 word5 word6 word7 entry lease memory_ready').split()
SUBMIT_FIELDS+=['initial_output'+str(i) for i in range(64)]+['output'+str(i) for i in range(64)]

def initial_image():
    data=(ROOT/'changes/gsp-vector-compute-0.30/windows/image.bin').read_bytes()
    if len(data)!=24576 or hashlib.sha256(data).hexdigest()!=INITIAL_SHA:raise ValueError('Pinned vector initial image changed')
    return data

def common(raw,index):
    if type(raw) is not bytes or len(raw)!=512 or struct.unpack_from('<Q',raw)[0]!=MAGICS[index]:raise ValueError('Vector diagnostic ABI')
    # Only the common memory/capture layout is shared with 0.25. The required
    # input magic above rejects old captures before the shared validator runs.
    return struct.pack('<Q',legacy.MAGICS[index])+raw[8:]

def memory(raw,generation):
    result=legacy.memory(common(raw,0),generation);result['magic']=MAGICS[0];return result

def capture_sizes(raw):return legacy.capture_sizes(common(raw,2))

def capture_info(raw,generation):
    result=legacy.capture_info(common(raw,2),generation);result['magic']=MAGICS[2];return result

def submit(raw,generation):
    r=execution.words(raw,SUBMIT_FIELDS,MAGICS[1],generation)
    flags=('claimed passed command_attempted entry_attempted put_attempted bell_attempted immutable_verified guards_verified stable '
           'pinned owned native_claimed native_notified lease memory_ready').split()
    legacy.booleans(r,flags)
    constants=dict(command_physical=0x3402040,entry_physical=0x3400008,get_physical=0x3400888,put_physical=0x340088c,
        output_physical=0x340d000,completion_physical=0x340e000,command_va=0x1020001040,output_va=0x1020008000,
        completion_va=0x1020009000,input_a_va=0x1020006200,input_b_va=0x1020006300,doorbell=0xbb0090,
        budget_ns=5_000_000_000,max_operations=65536,command_bytes=32,entry_bytes=8,count=61)
    if any(r[k]!=v for k,v in constants.items()):raise ValueError('Vector submit profile')
    u32=('token last_address initial_get initial_put initial_completion get put host_fence completion').split()+SUBMIT_FIELDS[64:]
    if (r['failure']>12 or r['operations']>65536 or r['reads']+r['writes']>r['operations'] or r['writes']>3 or
        r['polls']>r['operations'] or r['completed_elements']>61 or r['native_phase']>3 or any(r[k]>0xffffffff for k in u32)):raise ValueError('Vector submit bounds')
    if r['claimed'] and (not execution.retained(r) or not all(r[k] for k in ('native_claimed','lease','memory_ready'))):raise ValueError('Vector submit owner')
    if r['passed']:
        if (r['failure'] or not all(r[k] for k in flags) or r['native_phase']!=3 or r['writes']!=3 or not r['polls'] or r['elapsed_ns']>=r['budget_ns'] or
            (r['initial_get'],r['initial_put'],r['initial_completion'],r['get'],r['put'],r['host_fence'],r['completion'],r['completed_elements'])!=(1,1,0,2,2,0x30602401,0x306030f0,61) or
            [r['word'+str(i)] for i in range(8)]!=list(struct.unpack('<8I',vector_capture.COMMAND)) or
            r['entry']!=struct.unpack_from('<Q',vector_capture.ENTRIES,8)[0]):raise ValueError('Incomplete vector submission')
        initial=initial_image();a=struct.unpack_from('<64I',initial,8192+0x200);b=struct.unpack_from('<64I',initial,8192+0x300)
        for i,(x,y) in enumerate(zip(a,b)):
            summed=(x+y)&0xffffffff;poison=summed^0xffffffff
            if r['initial_output'+str(i)]!=poison or r['output'+str(i)]!=(summed if i<61 else poison):raise ValueError('Vector ABI arithmetic or inactive tail')
    return r

def validate_bytes(root,children,device,before_root,before_children):
    if any(type(x) is not bytes for x in (root,children,device,before_root,before_children)):raise ValueError('Immutable vector captures required')
    if len(root)!=12288 or len(device)!=36864 or not 8192<=len(children)<=45056 or len(children)%4096 or len(before_children)!=len(children):raise ValueError('Vector capture sizes')
    if root!=before_root or struct.unpack_from('<Q',before_children,8)[0]!=0x100602:raise ValueError('Vector root/leaf identity')
    patched=bytearray(before_children)
    for i in range(6):
        off=4096+(4+i)*8
        if struct.unpack_from('<Q',patched,off)[0]:raise ValueError('Vector PTE already occupied')
        struct.pack_into('<Q',patched,off,(6<<56)|((0x3409000+i*4096)>>4)|1)
    if children!=bytes(patched):raise ValueError('Vector additive PTE publication')
    for i in range(6):
        for edge in (0,4095):
            if execution.tables.p.g.walk(root,children,0x1020004000+i*4096+edge)!=0x3409000+i*4096+edge:raise ValueError('Vector table walk')
    vector_capture.validate(device[:12288],device[12288:],initial_image())
    return dict(valid_snapshot=True,table_readback_verified=True,active_elements=61,inactive_elements=3,
                completion=0x306030f0,output=list(struct.unpack_from('<64I',device,12288+16384)))

def capture(backend,generation,output,prior=None,prior_output=None):
    output.mkdir(exist_ok=False);result=dict(passed=False,compute_verified=False,metal_verified=False,profile='vector-030');raw={};errors={}
    for name,getter in (('memory',backend.vector_memory_info),('submit',backend.vector_submit_info),('capture',backend.vector_capture_info)):
        try:
            data=getter();(output/(name+'-info.bin')).write_bytes(data);raw[name]=data
        except (ValueError,OSError,RuntimeError) as error:errors[name]=str(error)
    for name,decode in (('memory',memory),('submit',submit),('capture',capture_info)):
        if name in raw:
            try:result[name]=decode(raw[name],generation)
            except ValueError as error:errors[name]=str(error)
    try:
        # Preserve bounded raw pages even if the other summaries are rejected.
        sizes=capture_sizes(raw['capture']) if 'capture' in raw else (0,0,0)
        for which,(name,count) in enumerate(zip(('root','children','device'),sizes)):
            try:
                with (output/(name+'-capture.bin')).open('xb') as stream:
                    for off in range(0,count,4096):
                        data=backend.vector_capture_data(which,off,4096)
                        if type(data) is not bytes or len(data)!=4096:raise ValueError('Vector capture truncated chunk')
                        stream.write(data)
            except (ValueError,OSError,RuntimeError) as error:errors[name+'_data']=str(error)
        if errors:raise ValueError('Vector diagnostics rejected: '+repr(errors))
        if not all(result[k]['passed'] for k in ('memory','submit','capture')):return result
        if not prior or not prior.get('passed') or not prior.get('host_command_verified') or not prior.get('table_readback_verified') or prior_output is None:raise ValueError('Same-run execution/HOST required')
        if (prior['rm']['generation']!=generation or result['submit']['token']!=prior['rm']['candidate'] or
            result['memory']['child_bytes']!=result['capture']['child_bytes'] or result['memory']['physical_mode'] or not result['memory']['window_observed'] or
            any(result[k]['owner_phase']!=17 for k in ('memory','submit','capture'))):raise ValueError('Vector owner/run/cleanup mismatch')
        images=[(output/(name+'-capture.bin')).read_bytes() for name in ('root','children','device')]
        before=[(prior_output/(name+'-capture.bin')).read_bytes() for name in ('root','children')]
        result['bytes']=validate_bytes(*images,*before)
        if result['bytes']['output']!=[result['submit']['output'+str(i)] for i in range(64)]:raise ValueError('Vector ABI/captured output disagreement')
        result['passed']=True;return result
    except (ValueError,OSError,RuntimeError) as error:
        result['error']=str(error);result['passed']=False;return result
    finally:
        if errors:result['diagnostic_errors']=errors
        (output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
