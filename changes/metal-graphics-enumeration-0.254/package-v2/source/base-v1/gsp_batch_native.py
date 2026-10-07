"""Read-only four-job diagnostics. Import performs no device access."""
from pathlib import Path
import hashlib,json,struct
import gsp_compute_native as legacy
import gsp_execution_native as execution
import gsp_vector_native as vector
ROOT=Path(__file__).resolve().parent
MAGICS=(0x525458424d4d3331,0x52545842534d3331,0x5254584243503331)
INITIAL_SHA='0e1ec056c7a0f4e5d098fafbc511ff4222fdd2783c50203fad243f577f187e56'
COUNTS=(1,32,61,64)
MEMORY_FIELDS=legacy.MEMORY_FIELDS;CAPTURE_FIELDS=legacy.CAPTURE_FIELDS
SUBMIT_FIELDS=vector.SUBMIT_FIELDS+['job','jobs','session_completed','reserved']

def initial_image():
    raw=(ROOT/'changes/gsp-batch-compute-0.31/windows/image.bin').read_bytes()
    if len(raw)!=24576 or hashlib.sha256(raw).hexdigest()!=INITIAL_SHA:raise ValueError('Pinned batch image')
    return raw

def common(raw,index):
    if type(raw) is not bytes or len(raw)!=512 or struct.unpack_from('<Q',raw)[0]!=MAGICS[index]:raise ValueError('Batch ABI')
    return struct.pack('<Q',legacy.MAGICS[index])+raw[8:]

def memory(raw,generation):
    result=legacy.memory(common(raw,0),generation);result['magic']=MAGICS[0];return result
def capture_sizes(raw):return legacy.capture_sizes(common(raw,2))
def capture_info(raw,generation):
    result=legacy.capture_info(common(raw,2),generation);result['magic']=MAGICS[2];return result

def command(job):return struct.pack('<8I',0x20012000,0xc7c0,0x200125a6,0x1011,0x200120ad,0x10200070+job,0x200120b0,9)
def entry(job):return (0x1020001040+job*64)|(1<<41)|(8<<42)
def outputs(initial,job):
    a=struct.unpack_from('<64I',initial,8192+job*1024+512);b=struct.unpack_from('<64I',initial,8192+job*1024+768)
    sums=[(x+y)&0xffffffff for x,y in zip(a,b)];poisons=[x^0xffffffff for x in sums]
    return poisons,[x if i<COUNTS[job] else poisons[i] for i,x in enumerate(sums)]

def submit(raw,generation,job):
    if type(job) is not int or not 0<=job<4:raise ValueError('Job index')
    r=execution.words(raw,SUBMIT_FIELDS,MAGICS[1],generation)
    flags=('claimed passed command_attempted entry_attempted put_attempted bell_attempted immutable_verified guards_verified stable '
           'pinned owned native_claimed native_notified lease memory_ready').split()
    legacy.booleans(r,flags)
    constants=dict(command_physical=0x3402040+job*64,entry_physical=0x3400008+job*8,get_physical=0x3400888,put_physical=0x340088c,
        output_physical=0x340d000+job*1024,completion_physical=0x340e000+job*256,command_va=0x1020001040+job*64,output_va=0x1020008000+job*1024,
        completion_va=0x1020009000+job*256,input_a_va=0x1020006200+job*1024,input_b_va=0x1020006300+job*1024,doorbell=0xbb0090,
        budget_ns=5_000_000_000,max_operations=65536,command_bytes=32,entry_bytes=8,count=COUNTS[job],job=job,jobs=4,reserved=0)
    if any(r[k]!=v for k,v in constants.items()):raise ValueError('Batch submit profile')
    u32=('token last_address initial_get initial_put initial_completion get put host_fence completion').split()+SUBMIT_FIELDS[64:192]
    if (r['failure']>12 or r['operations']>65536 or r['reads']+r['writes']>r['operations'] or r['writes']>3 or
        r['polls']>r['operations'] or r['completed_elements']>COUNTS[job] or r['native_phase']>3 or r['session_completed']>4 or any(r[k]>0xffffffff for k in u32)):raise ValueError('Batch bounds')
    if r['claimed'] and (not execution.retained(r) or not all(r[k] for k in ('native_claimed','lease','memory_ready'))):raise ValueError('Batch owner')
    if r['passed']:
        if (r['failure'] or not all(r[k] for k in flags) or r['native_phase']!=3 or r['writes']!=3 or not r['polls'] or r['elapsed_ns']>=r['budget_ns'] or r['session_completed']<=job or
            (r['initial_get'],r['initial_put'],r['initial_completion'],r['get'],r['put'],r['host_fence'],r['completion'],r['completed_elements'])!=(job+1,job+1,0,job+2,job+2,0x30602401,0x306031f0+job,COUNTS[job]) or
            [r['word'+str(i)] for i in range(8)]!=list(struct.unpack('<8I',command(job))) or r['entry']!=entry(job)):raise ValueError('Incomplete batch submission')
        poisons,expected=outputs(initial_image(),job)
        if any(r['initial_output'+str(i)]!=poisons[i] or r['output'+str(i)]!=expected[i] for i in range(64)):raise ValueError('Batch output/poison')
    return r

def validate_bytes(root,children,device,before_root,before_children):
    if any(type(x) is not bytes for x in (root,children,device,before_root,before_children)):raise ValueError('Immutable captures')
    if len(root)!=12288 or len(device)!=36864 or not 8192<=len(children)<=45056 or len(children)%4096 or len(before_children)!=len(children):raise ValueError('Capture sizes')
    if root!=before_root or struct.unpack_from('<Q',before_children,8)[0]!=0x100602:raise ValueError('Root/leaf identity')
    patched=bytearray(before_children)
    for i in range(6):
        off=4096+(4+i)*8
        if struct.unpack_from('<Q',patched,off)[0]:raise ValueError('Occupied PTE')
        struct.pack_into('<Q',patched,off,(6<<56)|((0x3409000+i*4096)>>4)|1)
    if children!=patched:raise ValueError('Additive PTE publication')
    for i in range(6):
        for edge in (0,4095):
            if execution.tables.p.g.walk(root,children,0x1020004000+i*4096+edge)!=0x3409000+i*4096+edge:raise ValueError('Batch table walk')
    ring=bytearray(256);struct.pack_into('<Q',ring,0,0x1020001000|(1<<41)|(5<<42))
    page=bytearray(4096);struct.pack_into('<5I',page,0,0x20040004,0x10,0x20002000,0x30602401,0x01000002)
    fence=bytearray(4096);struct.pack_into('<I',fence,0,0x30602401)
    for j in range(4):struct.pack_into('<Q',ring,8+j*8,entry(j));page[64+j*64:96+j*64]=command(j)
    if device[:256]!=ring or device[4096:8192]!=page or device[8192:12288]!=fence or struct.unpack_from('<II',device,0x888)!=(5,5):raise ValueError('Batch queue/HOST guards')
    initial=initial_image();expected=bytearray(initial);all_outputs=[]
    # Hardware owns the four submitted descriptors; retain their captured bytes.
    expected[12288:13312]=device[24576:25600]
    for j in range(4):
        poison,result=outputs(initial,j);all_outputs.append(result)
        if list(struct.unpack_from('<64I',initial,16384+j*1024))!=poison:raise ValueError('Initial poison')
        struct.pack_into('<64I',expected,16384+j*1024,*result);struct.pack_into('<I',expected,20480+j*256,0x306031f0+j)
    if device[12288:]!=expected:raise ValueError('Batch arithmetic, immutable inputs or guards')
    return dict(valid_snapshot=True,table_readback_verified=True,jobs=4,active_elements=158,inactive_elements=98,guard_bytes=7152,
                completions=[0x306031f0+i for i in range(4)],outputs=all_outputs)

def capture(backend,generation,output,prior=None,prior_output=None):
    output.mkdir(exist_ok=False);result=dict(passed=False,compute_verified=False,metal_verified=False,profile='batch-031');raw={};errors={}
    getters=[('memory',backend.batch_memory_info)]+[('submit-'+str(j),lambda j=j:backend.batch_submit_info(j)) for j in range(4)]+[('capture',backend.batch_capture_info)]
    for name,getter in getters:
        try:data=getter();(output/(name+'-info.bin')).write_bytes(data);raw[name]=data
        except (ValueError,OSError,RuntimeError) as error:errors[name]=str(error)
    decoders=[('memory',memory)]+[('submit-'+str(j),lambda data,gen,j=j:submit(data,gen,j)) for j in range(4)]+[('capture',capture_info)]
    for name,decode in decoders:
        if name in raw:
            try:result[name]=decode(raw[name],generation)
            except ValueError as error:errors[name]=str(error)
    try:
        sizes=capture_sizes(raw['capture']) if 'capture' in raw else (0,0,0)
        for which,(name,count) in enumerate(zip(('root','children','device'),sizes)):
            try:
                with (output/(name+'-capture.bin')).open('xb') as stream:
                    for off in range(0,count,4096):
                        data=backend.batch_capture_data(which,off,4096)
                        if type(data) is not bytes or len(data)!=4096:raise ValueError('Capture truncated chunk')
                        stream.write(data)
            except (ValueError,OSError,RuntimeError) as error:errors[name+'_data']=str(error)
        if errors:raise ValueError('Batch diagnostics rejected: '+repr(errors))
        if not all(result[k]['passed'] for k,_ in decoders):return result
        if not prior or not prior.get('passed') or not prior.get('host_command_verified') or not prior.get('table_readback_verified') or prior_output is None:raise ValueError('Same-run execution/HOST required')
        if (prior['rm']['generation']!=generation or result['memory']['child_bytes']!=result['capture']['child_bytes'] or
            result['memory']['physical_mode'] or not result['memory']['window_observed'] or any(result[k]['owner_phase']!=17 for k,_ in decoders)):raise ValueError('Batch owner/run/cleanup')
        for j in range(4):
            r=result['submit-'+str(j)]
            if r['token']!=prior['rm']['candidate'] or r['session_completed']!=4:raise ValueError('Batch token/sequence')
        images=[(output/(name+'-capture.bin')).read_bytes() for name in ('root','children','device')]
        before=[(prior_output/(name+'-capture.bin')).read_bytes() for name in ('root','children')]
        result['bytes']=validate_bytes(*images,*before)
        for j in range(4):
            if result['bytes']['outputs'][j]!=[result['submit-'+str(j)]['output'+str(i)] for i in range(64)]:raise ValueError('Batch ABI/capture disagreement')
        result['passed']=True;return result
    except (ValueError,OSError,RuntimeError) as error:result['error']=str(error);result['passed']=False;return result
    finally:
        if errors:result['diagnostic_errors']=errors
        (output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
