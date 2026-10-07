"""Immutable first-compute captures. CPU fixtures never establish hardware use."""
import hashlib
import json
from pathlib import Path
import struct
import gsp_execution_native as execution

ROOT=Path(__file__).resolve().parent
MEMORY_FIELDS=execution.MEMORY_FIELDS[:57]+('host_verified images_prepared claimed write_phase register_phase backing_physical backing_bytes').split()
SUBMIT_FIELDS=('magic abi generation claimed passed failure command_attempted entry_attempted put_attempted bell_attempted '
 'immutable_verified guards_verified stable operations reads writes polls token last_address initial_get initial_put initial_output initial_completion '
 'get put host_fence output completion start_ns elapsed_ns command_physical entry_physical get_physical put_physical output_physical completion_physical '
 'command_va output_va completion_va output_value completion_value doorbell budget_ns max_operations owner_phase pinned owned pci_command '
 'native_claimed native_notified native_phase command_bytes entry_bytes word0 word1 word2 word3 word4 word5 word6 word7 entry lease memory_ready').split()+['reserved'+str(i) for i in range(64,80)]
CAPTURE_FIELDS=('magic abi generation attempted passed failure root_bytes child_bytes device_bytes requested reads last_address last_value elapsed_ns '
 'owner_phase pinned owned pci_command root_physical children_physical root_capacity children_capacity device_capacity budget_ns').split()+['address'+str(i) for i in range(9)]+['reserved'+str(i) for i in range(33,64)]
ADDRESSES=(0x3400000,0x3402000,0x3403000)+tuple(0x3409000+i*4096 for i in range(6))
COMMAND=struct.pack('<8I',0x20012000,0xc7c0,0x200125a6,0x1011,0x200120ad,0x10200070,0x200120b0,9)
ENTRY=struct.pack('<Q',0x1020001040|(1<<41)|(8<<42))
MAGICS=(0x525458434d4d3235,0x525458434d533235,0x525458434d433235)

def booleans(r,names):
    if any(r[k] not in (0,1) for k in names):raise ValueError('Compute boolean')

def memory(raw,generation):
    r=execution.words(raw,MEMORY_FIELDS,MAGICS[0],generation)
    booleans(r,execution.MEMORY_BOOLS+['host_verified','images_prepared','claimed'])
    if (r['stage'],r['backing_physical'],r['backing_bytes'],r['budget_ns'],r['cleanup_budget_ns'],r['max_operations'])!=(2,0x3409000,24576,90_000_000_000,5_000_000_000,100000):raise ValueError('Compute memory profile')
    if (r['failure']>13 or r['inv_failure']>8 or r['operations']>100000 or r['operations']!=r['reads']+r['writes'] or
        r['inv_operations']>4096 or r['inv_operations']!=r['inv_reads']+r['inv_writes'] or r['write_phase']>18 or r['register_phase']>3 or
        r['child_bytes']>45056 or r['child_bytes']%4096 or r['verified_child_bytes']>r['child_bytes'] or r['verified_child_bytes']%4096 or
        r['links_published']>6 or r['zeroed_bytes'] or r['inspected_bytes']>24576 or r['verified_backing_bytes']>24576 or r['verified_backing_bytes']%4096):raise ValueError('Compute memory bounds')
    if r['attempted'] and (not execution.retained(r) or not all(r[k] for k in ('host_verified','images_prepared','claimed','lease','mapped','ring_claimed','contexts_claimed')) or
        not 0<r['bar1_base']<1<<40 or r['bar1_base']%0x4000000 or r['mapping_physical']!=r['bar1_base']+0x1002000):raise ValueError('Compute memory owner')
    if r['inv_passed'] and (r['inv_failure'] or not r['inv_completed'] or not r['inv_command_attempted'] or r['inv_writes']!=3 or
        r['inv_last_address']!=0x30b0 or r['inv_last_value']&0x80000000 or r['inv_elapsed_ns']>=2_000_000_000):raise ValueError('Compute invalidation proof')
    if r['passed'] and (r['failure'] or not all(r[k] for k in ('attempted','modified','parent_attempted','backing_verified','children_verified','window_saved','inv_passed')) or
        (r['write_phase'],r['register_phase'],r['links_published'],r['inspected_bytes'],r['verified_backing_bytes'])!=(18,3,6,24576,24576) or
        r['child_bytes']<8192 or r['verified_child_bytes']!=r['child_bytes'] or r['window_before'] or r['window_restored'] or r['cleanup_ns'] or r['elapsed_ns']>=r['budget_ns']):raise ValueError('Incomplete compute memory')
    return r

def submit(raw,generation):
    r=execution.words(raw,SUBMIT_FIELDS,MAGICS[1],generation)
    flags=('claimed passed command_attempted entry_attempted put_attempted bell_attempted immutable_verified guards_verified stable pinned owned native_claimed native_notified lease memory_ready').split()
    booleans(r,flags)
    constants=dict(command_physical=0x3402040,entry_physical=0x3400008,get_physical=0x3400888,put_physical=0x340088c,output_physical=0x340d000,
        completion_physical=0x340e000,command_va=0x1020001040,output_va=0x1020008000,completion_va=0x1020009000,
        output_value=0x30602501,completion_value=0x306025f0,doorbell=0xbb0090,budget_ns=5_000_000_000,max_operations=65536,command_bytes=32,entry_bytes=8)
    if any(r[k]!=v for k,v in constants.items()):raise ValueError('Compute submit profile')
    if (r['failure']>12 or r['operations']>65536 or r['reads']+r['writes']>r['operations'] or r['writes']>3 or r['polls']>r['operations'] or
        r['native_phase']>3 or any(r[k]>0xffffffff for k in ('token','last_address','initial_get','initial_put','initial_output','initial_completion','get','put','host_fence','output','completion'))):raise ValueError('Compute submit bounds')
    if r['claimed'] and (not execution.retained(r) or not all(r[k] for k in ('native_claimed','lease','memory_ready'))):raise ValueError('Compute submit owner')
    if r['passed']:
        if (r['failure'] or not all(r[k] for k in flags) or r['native_phase']!=3 or r['writes']!=3 or not r['polls'] or r['elapsed_ns']>=r['budget_ns'] or
            (r['initial_get'],r['initial_put'],r['initial_output'],r['initial_completion'],r['get'],r['put'],r['host_fence'],r['output'],r['completion'])!=(1,1,0,0,2,2,0x30602401,0x30602501,0x306025f0) or
            [r['word'+str(i)] for i in range(8)]!=list(struct.unpack('<8I',COMMAND)) or r['entry']!=struct.unpack('<Q',ENTRY)[0]):raise ValueError('Incomplete compute submission')
    return r

def capture_sizes(raw):
    # Only bounded lengths are trusted here. Malformed metadata is still saved
    # and rejected by capture_info; it must not hide independently readable data.
    if type(raw) is not bytes or len(raw)!=512:raise ValueError('Compute capture size header')
    counts=struct.unpack_from('<3Q',raw,48)
    if any(n>limit or n%4096 for n,limit in zip(counts,(12288,45056,36864))):raise ValueError('Compute capture capacity')
    return counts

def capture_info(raw,generation):
    r=execution.words(raw,CAPTURE_FIELDS,MAGICS[2],generation);capture_sizes(raw)
    booleans(r,('attempted','passed','pinned','owned'))
    if ((r['root_physical'],r['children_physical'],r['root_capacity'],r['children_capacity'],r['device_capacity'],r['budget_ns'])!=(0x1002000,0x1005000,12288,45056,36864,5_000_000_000) or
        tuple(r['address'+str(i)] for i in range(9))!=ADDRESSES):raise ValueError('Compute capture profile')
    if r['failure']>6 or r['requested']>45056 or r['requested']%4096 or r['child_bytes']>r['requested'] or r['reads']>23:raise ValueError('Compute capture bounds')
    if r['attempted'] and (not execution.retained(r) or r['requested']<8192):raise ValueError('Compute capture owner')
    if r['passed'] and (r['failure'] or not r['attempted'] or (r['root_bytes'],r['child_bytes'],r['device_bytes'])!=(12288,r['requested'],36864) or
        r['reads']!=12+r['requested']//4096 or r['last_address']!=0x340e000 or r['elapsed_ns']>=r['budget_ns']):raise ValueError('Incomplete compute capture')
    return r

def initial_image():
    refs=ROOT/'changes/gsp-compute-0.25/memory/reference'
    digests={'shader-code.bin':'b13907739b5feea896376f292f317942cc91491d4b68273fe2f69d396406e1ad',
        'constant.bin':'cdbbba9f8af7522e4504b134b35f72d196c9e0fc1857b73dad0796751eded20a',
        'qmd.bin':'0b8f54b76abfef0444e6398bb0bdd8c3debb5f36e8063f1f3db87e5450dfd151',
        'command.bin':'968d1f9cac879fa8332d004fefb3dfc3f92525db468c2bfc867df06ffda3ab9e'}
    data={name:(refs/name).read_bytes() for name in digests}
    if any(hashlib.sha256(data[name]).hexdigest()!=digest for name,digest in digests.items()) or data['command.bin']!=COMMAND:raise ValueError('Pinned shader input changed')
    image=data['shader-code.bin']+bytes(8192-256)+data['constant.bin']+data['qmd.bin']+bytes(4096-256)
    for page in (4,5):image+=bytes(4)+bytes((0xa5 if page==4 else 0x5a)^((i*13+7)&255) for i in range(4,4096))
    if len(image)!=24576:raise ValueError('Initial shader image dimensions')
    return image

def validate_bytes(root,children,device,before_root,before_children):
    if any(type(v) is not bytes for v in (root,children,device,before_root,before_children)):raise ValueError('Immutable compute bytes required')
    if len(root)!=12288 or len(device)!=36864 or not 8192<=len(children)<=45056 or len(children)%4096 or len(before_children)!=len(children):raise ValueError('Compute image sizes')
    if root!=before_root or struct.unpack_from('<Q',before_children,8)[0]!=0x100602:raise ValueError('Compute root/first leaf identity')
    patched=bytearray(before_children)
    for i in range(6):
        off=4096+(4+i)*8
        if struct.unpack_from('<Q',patched,off)[0]:raise ValueError('Compute PTE was already occupied')
        struct.pack_into('<Q',patched,off,(6<<56)|((0x3409000+i*4096)>>4)|1)
    if children!=bytes(patched):raise ValueError('Compute page table publication differs')
    for i in range(6):
        for edge in (0,4095):
            if execution.tables.p.g.walk(root,children,0x1020004000+i*4096+edge)!=0x3409000+i*4096+edge:raise ValueError('Compute page table walk')
    if (device[:16]!=execution.ENTRY+ENTRY or any(device[16:256]) or struct.unpack_from('<II',device,0x888)!=(2,2) or
        device[4096:8192]!=execution.COMMAND+bytes(44)+COMMAND+bytes(4000) or device[8192:12288]!=struct.pack('<I',0x30602401)+bytes(4092)):raise ValueError('Compute queue/HOST evidence')
    backing=device[12288:];initial=initial_image()
    if backing[:12288]!=initial[:12288] or backing[12544:16384]!=initial[12544:16384]:raise ValueError('Shader/constant/QMD-tail changed')
    if (struct.unpack_from('<I',backing,16384)[0],struct.unpack_from('<I',backing,20480)[0])!=(0x30602501,0x306025f0):raise ValueError('Both shader and completion markers required')
    if backing[16388:20480]!=initial[16388:20480] or backing[20484:]!=initial[20484:]:raise ValueError('Shader output guards changed')
    return dict(valid_snapshot=True,table_readback_verified=True,shader_output=0x30602501,completion=0x306025f0)

def capture(backend,generation,output,prior=None,prior_output=None):
    output.mkdir(exist_ok=False);result=dict(passed=False,compute_verified=False,metal_verified=False);raw={};errors={}
    for name,getter in (('memory',backend.compute_memory_info),('submit',backend.compute_submit_info),('capture',backend.compute_capture_info)):
        try:
            data=getter();(output/(name+'-info.bin')).write_bytes(data);raw[name]=data
        except (ValueError,OSError,RuntimeError) as error:errors[name]=str(error)
    for name,decode in (('memory',memory),('submit',submit),('capture',capture_info)):
        if name in raw:
            try:result[name]=decode(raw[name],generation)
            except ValueError as error:errors[name]=str(error)
    try:
        sizes=capture_sizes(raw['capture']) if 'capture' in raw else (0,0,0)
        for which,(name,count) in enumerate(zip(('root','children','device'),sizes)):
            try:
                with (output/(name+'-capture.bin')).open('xb') as stream:
                    for off in range(0,count,4096):
                        data=backend.compute_capture_data(which,off,4096)
                        if type(data) is not bytes or len(data)!=4096:raise ValueError('Compute capture truncated chunk')
                        stream.write(data)
            except (ValueError,OSError,RuntimeError) as error:errors[name+'_data']=str(error)
        if errors:raise ValueError('Compute diagnostics rejected: '+repr(errors))
        if not all(result[k]['passed'] for k in ('memory','submit','capture')):return result
        if not prior or not prior.get('passed') or not prior.get('host_command_verified') or not prior.get('table_readback_verified') or prior_output is None:raise ValueError('Same-run completed execution/HOST required')
        if (prior['rm']['generation']!=generation or result['submit']['token']!=prior['rm']['candidate'] or
            result['memory']['child_bytes']!=result['capture']['child_bytes'] or result['memory']['physical_mode'] or not result['memory']['window_observed'] or
            any(result[k]['owner_phase']!=17 for k in ('memory','submit','capture'))):raise ValueError('Compute owner/run/cleanup mismatch')
        images=[(output/(name+'-capture.bin')).read_bytes() for name in ('root','children','device')]
        before=[(prior_output/(name+'-capture.bin')).read_bytes() for name in ('root','children')]
        result['bytes']=validate_bytes(*images,*before);result['passed']=True
        return result
    except (ValueError,OSError,RuntimeError) as error:
        result['error']=str(error);result['passed']=False;return result
    finally:
        if errors:result['diagnostic_errors']=errors
        (output/'decoded.json').write_text(json.dumps(result,indent=2)+'\n')
