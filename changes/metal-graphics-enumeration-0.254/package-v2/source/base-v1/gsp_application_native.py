"""0.32 application request/response evidence; importing never opens hardware."""
from pathlib import Path
import hashlib,struct
import application_request_codec as request
import gsp_application_compute_native as legacy
import gsp_application_execution_native as execution
import gsp_batch_native as batch

INFO_MAGIC=0x5254585254493332;JOB_MAGIC=0x52545852544a3332
MEMORY_MAGIC=0x525458424d4d3332;SUBMIT_MAGIC=0x52545842534d3332;CAPTURE_MAGIC=0x5254584243503332
INFO_FIELDS=('magic abi generation phase completed capacity request_bytes prepared opened bootstrap_captured owner_phase pinned owned pci_command lease ready queue_completed exhausted metal_verified').split()+['reserved'+str(i) for i in range(19,64)]
JOB_FIELDS=('magic abi generation job capacity phase completed count window_attempted window_saved mutation_attempted acquired restore_attempted restored window_failure cleanup_failure window_before window_selected window_after window_generation window_request_id window_started window_elapsed cleanup_started cleanup_elapsed stage_claimed stage_writes submit_claimed capture_passed stage_attempted stage_result_claimed cb_attempted output_attempted readback committed stage_passed stage_failure stage_slot stage_operations stage_reads stage_result_writes stage_last_address stage_started stage_elapsed queue_claimed queue_notified queue_phase queue_completed capture_attempted capture_complete capture_failure root_bytes child_bytes device_bytes requested capture_reads capture_last_address capture_last_value capture_elapsed expected_count completed_elements completion submit_passed').split()+['reserved'+str(i) for i in range(63,128)]
def info(raw,generation):
    r=execution.words(raw,INFO_FIELDS,INFO_MAGIC,generation)
    if any(r[k] not in (0,1) for k in ('prepared opened bootstrap_captured pinned owned lease ready exhausted metal_verified').split()):raise ValueError('Runtime boolean')
    if r['capacity']!=4 or r['request_bytes']!=576 or r['phase']>6 or r['completed']>4 or r['queue_completed']>4 or r['owner_phase']>18 or r['metal_verified']:raise ValueError('Runtime bounds')
    if bool(r['ready'])!=(r['phase']==1) or bool(r['exhausted'])!=(r['phase']==5):raise ValueError('Runtime phase flags')
    if r['ready'] or r['exhausted']:
        if not all(r[k] for k in ('prepared','opened','bootstrap_captured','pinned','owned','lease')) or r['owner_phase']!=18 or r['pci_command']!=6 or r['queue_completed']!=r['completed']:raise ValueError('Runtime owner/readiness')
        if (r['ready'] and r['completed']>=4) or (r['exhausted'] and r['completed']!=4):raise ValueError('Runtime capacity')
    return r
def job(raw,generation,index):
    r=execution.words(raw,JOB_FIELDS,JOB_MAGIC,generation)
    if type(index) is not int or not 0<=index<4 or r['job']!=index or r['capacity']!=4 or r['phase']>6 or r['completed']>4 or r['count']>64:raise ValueError('Job identity')
    booleans=('window_attempted window_saved mutation_attempted acquired restore_attempted restored stage_claimed submit_claimed capture_passed stage_attempted stage_result_claimed cb_attempted output_attempted readback committed stage_passed queue_claimed queue_notified capture_attempted capture_complete submit_passed').split()
    if any(r[k] not in (0,1) for k in booleans):raise ValueError('Job boolean')
    if r['window_failure']>8 or r['cleanup_failure']>8 or r['stage_failure']>10 or r['capture_failure']>6 or r['stage_writes']>2 or r['stage_result_writes']>2 or r['stage_operations']>128 or r['queue_phase']>3 or r['queue_completed']>4 or r['expected_count']>64 or r['completed_elements']>64:raise ValueError('Job bounds')
    if r['window_attempted'] and (r['window_generation']!=generation or r['window_request_id']!=index+1):raise ValueError('Lease identity')
    if r['restored'] and (not all(r[k] for k in ('window_attempted','window_saved','mutation_attempted','restore_attempted')) or r['cleanup_failure'] or r['window_before']!=r['window_after'] or r['cleanup_elapsed']>=5_000_000_000):raise ValueError('Lease cleanup')
    if r['stage_passed'] and (not all(r[k] for k in ('stage_claimed','stage_attempted','stage_result_claimed','cb_attempted','output_attempted','readback','committed')) or r['stage_failure'] or (r['stage_slot'],r['stage_reads'],r['stage_result_writes'],r['stage_writes'])!=(index,17,2,2) or r['stage_elapsed']>=5_000_000_000):raise ValueError('Stage proof')
    sizes=(r['root_bytes'],r['child_bytes'],r['device_bytes'])
    if sizes[0]>12288 or sizes[1]>45056 or sizes[2]>36864 or any(n%4096 for n in sizes) or r['capture_reads']>23:raise ValueError('Capture capacity')
    if r['capture_complete'] and (not r['capture_attempted'] or r['capture_failure'] or sizes[0]!=12288 or sizes[2]!=36864 or not 8192<=sizes[1]<=45056 or r['requested']!=sizes[1] or r['capture_reads']!=12+sizes[1]//4096 or r['capture_elapsed']>=5_000_000_000):raise ValueError('Capture proof')
    return r
def memory(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or struct.unpack_from('<Q',raw)[0]!=MEMORY_MAGIC:raise ValueError('Application memory ABI')
    result=legacy.memory(struct.pack('<Q',legacy.MAGICS[0])+raw[8:],generation);result['magic']=MEMORY_MAGIC;return result
def submit(raw,generation,index,wire):
    parsed=request.decode(wire);count=len(parsed['a'])
    if parsed['generation']!=generation or parsed['request_id']!=index+1:raise ValueError('Request binding')
    r=execution.words(raw,batch.SUBMIT_FIELDS,SUBMIT_MAGIC,generation)
    if (r['job'],r['jobs'],r['count'])!=(index,4,count) or r['session_completed']>4 or r['completed_elements']>count or r['failure']>12:raise ValueError('Submit identity/bounds')
    flags=('claimed passed command_attempted entry_attempted put_attempted bell_attempted immutable_verified guards_verified stable pinned owned native_claimed native_notified lease memory_ready').split()
    if any(r[k] not in (0,1) for k in flags) or r['writes']>3 or r['operations']>65536 or r['reads']+r['writes']>r['operations'] or r['native_phase']>3:raise ValueError('Submit state')
    constants=dict(command_physical=0x3402040+index*64,entry_physical=0x3400008+index*8,get_physical=0x3400888,put_physical=0x340088c,output_physical=0x340d000+index*1024,completion_physical=0x340e000+index*256,command_va=0x1020001040+index*64,output_va=0x1020008000+index*1024,completion_va=0x1020009000+index*256,input_a_va=0x1020006200+index*1024,input_b_va=0x1020006300+index*1024,doorbell=0xbb0090,budget_ns=5_000_000_000,max_operations=65536,command_bytes=32,entry_bytes=8)
    if any(r[k]!=v for k,v in constants.items()):raise ValueError('Submit fixed addresses')
    if r['claimed'] and not execution.retained(r):raise ValueError('Submit owner')
    if r['passed']:
        if r['failure'] or not all(r[k] for k in flags) or r['native_phase']!=3 or r['writes']!=3 or not r['polls'] or r['elapsed_ns']>=5_000_000_000:raise ValueError('Submit completion flags')
        if (r['initial_get'],r['initial_put'],r['initial_completion'],r['get'],r['put'],r['host_fence'],r['completion'],r['completed_elements'])!=(index+1,index+1,0,index+2,index+2,0x30602401,0x306032f0+index,count):raise ValueError('Submit markers')
        if [r['word'+str(i)] for i in range(8)]!=list(struct.unpack('<8I',batch.command(index))) or r['entry']!=batch.entry(index):raise ValueError('Submit queue packets')
        sums=[(a+b)&0xffffffff for a,b in zip(parsed['a'],parsed['b'])]+[0]*(64-count)
        for i,v in enumerate(sums):
            if r['initial_output'+str(i)]!=(v^0xffffffff) or r['output'+str(i)]!=(v if i<count else v^0xffffffff):raise ValueError('Submit arithmetic')
    return r
def canonical(history,generation):
    initial=bytearray(batch.initial_image())
    if len(history)>4:raise ValueError('History capacity')
    for j in range(4):
        parsed=request.decode(history[j]) if j<len(history) else dict(generation=generation,request_id=j+1,a=[],b=[])
        if parsed['generation']!=generation or parsed['request_id']!=j+1:raise ValueError('History order')
        count=len(parsed['a']);a=parsed['a']+[0]*(64-count);b=parsed['b']+[0]*(64-count);cb=8192+j*1024
        initial[cb:cb+1024]=bytes(1024)
        for o,v in [(0x28,0xfffdc0),(0x160,0x1020008000+j*1024),(0x168,0x1020006200+j*1024),(0x170,0x1020006300+j*1024)]:struct.pack_into('<Q',initial,cb+o,v)
        struct.pack_into('<I',initial,cb+0x178,count);struct.pack_into('<64I',initial,cb+512,*a);struct.pack_into('<64I',initial,cb+768,*b)
        struct.pack_into('<64I',initial,16384+j*1024,*[((x+y)&0xffffffff)^0xffffffff for x,y in zip(a,b)])
        struct.pack_into('<I',initial,12288+j*256+104,0x306032f0+j);struct.pack_into('<I',initial,20480+j*256,0)
    return initial
def capture_info(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or struct.unpack_from('<Q',raw)[0]!=CAPTURE_MAGIC:raise ValueError('Application capture ABI')
    result=legacy.capture_info(struct.pack('<Q',legacy.MAGICS[2])+raw[8:],generation);result['magic']=CAPTURE_MAGIC;return result

def verify_bootstrap(root,children,device,before_root,before_children,generation):
    return _verify_capture(root,children,device,before_root,before_children,[],generation)

def verify_capture(root,children,device,before_root,before_children,history,generation):
    if not history:raise ValueError('Completed request history required')
    return _verify_capture(root,children,device,before_root,before_children,history,generation)

def _verify_capture(root,children,device,before_root,before_children,history,generation):
    if any(type(b) is not bytes for b in (root,children,device,before_root,before_children)) or len(root)!=12288 or len(device)!=36864 or not 8192<=len(children)<=45056 or len(children)%4096 or len(before_children)!=len(children) or root!=before_root:raise ValueError('Capture dimensions/identity')
    patched=bytearray(before_children)
    for i in range(6):
        offset=4096+(i+4)*8
        if struct.unpack_from('<Q',patched,offset)[0]:raise ValueError('Occupied baseline PTE')
        struct.pack_into('<Q',patched,offset,(6<<56)|((0x3409000+i*4096)>>4)|1)
    if children!=patched:raise ValueError('Additive PTE proof')
    for i in range(6):
        for off in (0,4095):
            if execution.tables.p.g.walk(root,children,0x1020004000+i*4096+off)!=0x3409000+i*4096+off:raise ValueError('PTE walk')
    completed=len(history);image=canonical(history,generation)
    ring=bytearray(256);struct.pack_into('<Q',ring,0,0x1020001000|(1<<41)|(5<<42));commands=bytearray(4096);struct.pack_into('<5I',commands,0,0x20040004,0x10,0x20002000,0x30602401,0x01000002);fence=bytearray(4096);struct.pack_into('<I',fence,0,0x30602401)
    for j,wire in enumerate(history):
        r=request.decode(wire);sums=[(a+b)&0xffffffff for a,b in zip(r['a'],r['b'])]
        for i,v in enumerate(sums):struct.pack_into('<I',image,16384+j*1024+i*4,v)
        struct.pack_into('<I',image,20480+j*256,0x306032f0+j);struct.pack_into('<Q',ring,8+j*8,batch.entry(j));commands[64+j*64:96+j*64]=batch.command(j)
    image[12288:12288+completed*256]=device[24576:24576+completed*256]
    if device[:256]!=ring or device[4096:8192]!=commands or device[8192:12288]!=fence or struct.unpack_from('<II',device,0x888)!=(completed+1,completed+1):raise ValueError('Queue guards/HOST proof')
    if device[12288:]!=image:raise ValueError('Application arithmetic/input/code/guard bytes')
    return dict(passed=True,jobs=completed,active_elements=sum(len(request.decode(w)['a']) for w in history),hardware_accessed=False,metal_verified=False)
