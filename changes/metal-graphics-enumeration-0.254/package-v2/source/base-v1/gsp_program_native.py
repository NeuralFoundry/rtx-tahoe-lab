"""Versioned 0.33 ABI and independent program-result evidence. No device access."""
import struct
import program_request_codec as request
import program_model as model
import gsp_application_execution_native as execution
import gsp_application_compute_native as legacy
INFO_MAGIC=0x5254585254493333;JOB_MAGIC=0x52545852544a3333
MEMORY_MAGIC=0x525458424d4d3333;SUBMIT_MAGIC=0x52545842534d3333;CAPTURE_MAGIC=0x5254584243503333
INFO_FIELDS=('magic abi generation phase completed capacity request_bytes prepared opened bootstrap_captured owner_phase pinned owned pci_command lease ready queue_completed exhausted metal_verified program_count library_bytes code_bytes image_bytes plan_bytes closed prepare_attempted prepare_passed prepare_failure prepare_reads prepare_bytes prepare_started prepare_elapsed open_attempted open_passed open_failure open_window').split()+['reserved'+str(i) for i in range(36,64)]
JOB_FIELDS=('magic abi generation job capacity phase completed groups window_attempted window_saved mutation_attempted acquired restore_attempted restored window_failure cleanup_failure window_before window_selected window_after window_generation window_request_id window_started window_elapsed cleanup_started cleanup_elapsed stage_claimed stage_writes submit_claimed capture_passed stage_attempted stage_result_claimed data_attempted cb_attempted qmd_attempted readback committed stage_passed stage_failure stage_slot stage_operations stage_reads stage_result_writes stage_verified_writes stage_last_address stage_started stage_elapsed queue_claimed queue_notified queue_phase queue_completed capture_attempted capture_complete capture_failure root_bytes child_bytes device_bytes requested capture_reads capture_last_address capture_last_value capture_elapsed submit_passed completion program request_id request_generation').split()+['reserved'+str(i) for i in range(66,128)]
SUBMIT_FIELDS=('magic abi generation job passed failure command_attempted entry_attempted put_attempted bell_attempted immutable_verified guards_verified stable operations reads writes polls token last_address initial_get initial_put initial_completion get put host_fence completion attempted claimed start_ns elapsed_ns command_physical entry_physical get_physical put_physical data_physical completion_physical command_va data_va completion_va constant_va qmd_va doorbell budget_ns max_operations owner_phase pinned owned pci_command native_claimed native_notified native_phase session_completed jobs').split()+['word'+str(i) for i in range(8)]+['entry','lease','memory_ready']


def flags(r,names):
    if any(r[k] not in (0,1) for k in names.split()):raise ValueError('program boolean')


def info(raw,generation):
    r=execution.words(raw,INFO_FIELDS,INFO_MAGIC,generation)
    flags(r,'prepared opened bootstrap_captured pinned owned lease ready exhausted metal_verified closed prepare_attempted prepare_passed open_attempted open_passed')
    if (r['capacity'],r['request_bytes'],r['library_bytes'],r['code_bytes'],r['image_bytes'],r['plan_bytes'])!=(4,2112,512,4096,24576,4096):raise ValueError('program runtime dimensions')
    if r['phase']>6 or r['completed']>4 or r['queue_completed']>4 or r['owner_phase']>18 or r['program_count'] not in (0,3) or r['metal_verified'] or r['prepare_failure']>8 or r['open_failure']>8:raise ValueError('program runtime bounds')
    if bool(r['ready'])!=(r['phase']==1 and not r['closed']) or bool(r['exhausted'])!=(r['phase']==5):raise ValueError('runtime phase flags')
    if r['closed'] and r['phase']!=6:raise ValueError('closed runtime must be retained')
    if r['prepare_reads']>96 or r['prepare_bytes']>24576 or r['prepare_bytes']%256 or r['prepare_bytes']>r['prepare_reads']*256:raise ValueError('preparation dimensions')
    if r['prepare_passed'] and (not r['prepare_attempted'] or r['prepare_failure'] or r['prepare_reads']!=96 or r['prepare_bytes']!=24576 or r['prepare_elapsed']>=5_000_000_000):raise ValueError('preparation proof')
    if r['open_passed'] and (not r['prepare_passed'] or not r['open_attempted'] or r['open_failure'] or r['open_window']>0xffffffff):raise ValueError('opening proof')
    if r['ready'] or r['exhausted']:
        if not all(r[k] for k in ('prepared','opened','bootstrap_captured','pinned','owned','lease','prepare_passed','open_passed')) or r['owner_phase']!=18 or r['pci_command']!=6 or r['queue_completed']!=r['completed'] or r['program_count']!=3:raise ValueError('runtime owner/readiness')
        if (r['ready'] and r['completed']>=4) or (r['exhausted'] and r['completed']!=4):raise ValueError('runtime capacity')
    return r


def job(raw,generation,index):
    request.integer(index,'job index',0,3);r=execution.words(raw,JOB_FIELDS,JOB_MAGIC,generation)
    if r['job']!=index or r['capacity']!=4 or r['phase']>6 or r['completed']>4 or r['groups']>1 or r['program']>2:raise ValueError('job identity')
    flags(r,'window_attempted window_saved mutation_attempted acquired restore_attempted restored stage_claimed submit_claimed capture_passed stage_attempted stage_result_claimed data_attempted cb_attempted qmd_attempted readback committed stage_passed queue_claimed queue_notified capture_attempted capture_complete submit_passed')
    if r['window_failure']>8 or r['cleanup_failure']>8 or r['stage_failure']>10 or r['capture_failure']>6 or r['stage_writes']>3 or r['stage_result_writes']>3 or r['stage_verified_writes']>r['stage_result_writes'] or r['stage_operations']>256 or r['stage_reads']+r['stage_result_writes']>r['stage_operations'] or r['queue_phase']>3 or r['queue_completed']>4:raise ValueError('job bounds')
    if r['request_id'] and (r['request_id']!=index+1 or r['request_generation']!=generation or r['groups']!=1):raise ValueError('accepted history identity')
    if r['window_attempted'] and (r['window_generation']!=generation or r['window_request_id']!=index+1):raise ValueError('window identity')
    if r['restored'] and (not all(r[k] for k in ('window_attempted','window_saved','mutation_attempted','restore_attempted')) or r['cleanup_failure'] or r['window_before']!=r['window_after'] or r['cleanup_elapsed']>=5_000_000_000):raise ValueError('window cleanup')
    if r['stage_passed'] and (not all(r[k] for k in ('stage_claimed','stage_attempted','stage_result_claimed','data_attempted','cb_attempted','qmd_attempted','readback','committed')) or r['stage_failure'] or (r['stage_slot'],r['stage_reads'],r['stage_result_writes'],r['stage_writes'],r['stage_verified_writes'])!=(index,73,3,3,3) or r['stage_elapsed']>=5_000_000_000):raise ValueError('stage proof')
    sizes=r['root_bytes'],r['child_bytes'],r['device_bytes']
    if any(n>cap or n%4096 for n,cap in zip(sizes,(12288,45056,36864))) or r['capture_reads']>23 or r['requested']>45056 or r['requested']%4096:raise ValueError('capture capacity')
    if r['capture_complete'] and (not r['capture_attempted'] or r['capture_failure'] or sizes[0]!=12288 or sizes[2]!=36864 or not 8192<=sizes[1]<=45056 or r['requested']!=sizes[1] or r['capture_reads']!=12+sizes[1]//4096 or r['capture_elapsed']>=5_000_000_000):raise ValueError('capture proof')
    if r['capture_passed'] and not r['capture_complete']:raise ValueError('capture validation without bytes')
    if r['submit_passed'] and (not r['submit_claimed'] or not r['queue_claimed'] or not r['queue_notified'] or r['queue_phase']!=3 or r['completion']!=0x306033f0+index):raise ValueError('job completion')
    return r


def memory(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or struct.unpack_from('<Q',raw)[0]!=MEMORY_MAGIC:raise ValueError('program memory ABI')
    result=legacy.memory(struct.pack('<Q',legacy.MAGICS[0])+raw[8:],generation);result['magic']=MEMORY_MAGIC;return result


def capture_info(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or struct.unpack_from('<Q',raw)[0]!=CAPTURE_MAGIC:raise ValueError('program capture ABI')
    result=legacy.capture_info(struct.pack('<Q',legacy.MAGICS[2])+raw[8:],generation);result['magic']=CAPTURE_MAGIC;return result


def submit(raw,generation,index,wire):
    request.integer(index,'job index',0,3);parsed=request.decode(wire)
    if parsed['generation']!=generation or parsed['request_id']!=index+1:raise ValueError('submit request binding')
    r=execution.words(raw,SUBMIT_FIELDS,SUBMIT_MAGIC,generation)
    if r['job']!=index or r['jobs']!=4 or r['session_completed']>4 or r['failure']>12:raise ValueError('submit identity')
    names='passed command_attempted entry_attempted put_attempted bell_attempted immutable_verified guards_verified stable attempted claimed pinned owned native_claimed native_notified lease memory_ready';flags(r,names)
    if r['writes']>3 or r['operations']>65536 or r['reads']+r['writes']>r['operations'] or r['polls']>r['operations'] or r['native_phase']>3:raise ValueError('submit bounds')
    constants=dict(command_physical=0x3402040+index*64,entry_physical=0x3400008+index*8,get_physical=0x3400888,put_physical=0x340088c,
                   data_physical=0x3409000+model.data_offset(index),completion_physical=0x340e000+index*256,command_va=0x1020001040+index*64,
                   data_va=0x1020004000+model.data_offset(index),completion_va=0x1020009000+index*256,constant_va=0x1020006000+index*1024,qmd_va=0x1020007000+index*256,
                   doorbell=0xbb0090,budget_ns=5_000_000_000,max_operations=65536)
    if any(r[k]!=v for k,v in constants.items()):raise ValueError('submit addresses/profile')
    if any(r[k]>0xffffffff for k in ('token','last_address','initial_get','initial_put','initial_completion','get','put','host_fence','completion')):raise ValueError('submit uint32')
    if r['claimed'] and (not execution.retained(r) or not all(r[k] for k in ('native_claimed','lease','memory_ready'))):raise ValueError('submit owner')
    if r['passed']:
        if r['failure'] or not all(r[k] for k in names.split()) or r['native_phase']!=3 or r['writes']!=3 or not r['polls'] or r['elapsed_ns']>=5_000_000_000:raise ValueError('submit completion flags')
        if (r['initial_get'],r['initial_put'],r['initial_completion'],r['get'],r['put'],r['host_fence'],r['completion'])!=(index+1,index+1,0,index+2,index+2,0x30602401,0x306033f0+index):raise ValueError('submit markers')
        plan=model.plan(wire)
        if tuple(r['word'+str(i)] for i in range(8))!=struct.unpack('<8I',plan[3584:3616]) or r['entry']!=struct.unpack('<Q',plan[3616:3624])[0]:raise ValueError('submit queue packet')
    return r


def verify_bootstrap(root,children,device,before_root,before_children,generation):
    return _capture(root,children,device,before_root,before_children,[],generation)


def verify_capture(root,children,device,before_root,before_children,history,generation):
    if not history:raise ValueError('completed program history required')
    return _capture(root,children,device,before_root,before_children,history,generation)


def _capture(root,children,device,before_root,before_children,history,generation):
    if any(type(b) is not bytes for b in (root,children,device,before_root,before_children)) or len(root)!=12288 or len(device)!=36864 or not 8192<=len(children)<=45056 or len(children)%4096 or len(before_children)!=len(children) or root!=before_root:raise ValueError('capture dimensions/identity')
    patched=bytearray(before_children)
    for i in range(6):
        offset=4096+(i+4)*8
        if struct.unpack_from('<Q',patched,offset)[0]:raise ValueError('occupied baseline PTE')
        struct.pack_into('<Q',patched,offset,(6<<56)|((0x3409000+i*4096)>>4)|1)
    if children!=patched:raise ValueError('program PTE proof')
    for i in range(6):
        for off in (0,4095):
            if execution.tables.p.g.walk(root,children,0x1020004000+i*4096+off)!=0x3409000+i*4096+off:raise ValueError('program PTE walk')
    plans=model.verify_backing(device[12288:],history,generation)
    completed=len(history);ring=bytearray(256);struct.pack_into('<Q',ring,0,0x1020001000|(1<<41)|(5<<42))
    commands=bytearray(4096);struct.pack_into('<5I',commands,0,0x20040004,0x10,0x20002000,0x30602401,0x01000002)
    for j,p in enumerate(plans):ring[8+j*8:16+j*8]=p[3616:3624];commands[64+j*64:96+j*64]=p[3584:3616]
    if device[:256]!=ring or device[4096:8192]!=commands or device[8192:12288]!=struct.pack('<I',0x30602401)+bytes(4092) or struct.unpack_from('<II',device,0x888)!=(completed+1,completed+1):raise ValueError('program queue/HOST evidence')
    return dict(passed=True,jobs=completed,programs=[request.decode(w)['program'] for w in history],active_elements=64*completed,
                arithmetic_verified=True,hardware_accessed=False,metal_verified=False)
