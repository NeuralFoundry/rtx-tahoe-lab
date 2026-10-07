"""Strict 0.35 info/job decoders. All inputs are retained CPU records."""
import struct
import reusable_request as request
import gsp_program_native as bootstrap_abi
INFO_MAGIC=0x5254585254493335;JOB_MAGIC=0x52545852544a3335
INFO=('magic abi generation phase completed capacity request_bytes prepared opened bootstrap_captured owner_phase pinned owned pci_command lease ready queue_completed exhausted metal_verified program_count library_bytes code_bytes image_bytes plan_bytes closed prepare_attempted prepare_passed prepare_reads prepare_bytes prepare_started prepare_elapsed open_attempted active_capture last_serial backing_phase child_bytes').split()
JOB={0:'magic',1:'abi',2:'generation',3:'serial',4:'phase',5:'completed',6:'queue_completed',7:'closed',8:'attempted',9:'passed',10:'failure',11:'writes',12:'notifications',13:'operations',14:'polls',15:'elapsed',16:'started',17:'core_passed',
 20:'window_current',21:'window_attempted',22:'window_saved',23:'mutation_attempted',24:'acquired',25:'restore_attempted',26:'restored',27:'window_before',28:'window_selected',29:'window_after',30:'window_started',31:'window_elapsed',32:'cleanup_started',33:'cleanup_elapsed',
 40:'capture_kind',41:'capture_serial',42:'capture_attempted',43:'capture_passed',44:'capture_reads',45:'capture_bytes',46:'capture_last_address',47:'capture_started',48:'capture_elapsed',50:'before_passed',51:'staged_passed',52:'completed_passed',60:'program',61:'groups',62:'entry',63:'put'}
def words(raw,count,magic,generation):
    request.integer(generation,'generation',1,request.MAX_SERIAL)
    if type(raw) is not bytes or len(raw)!=count*8:raise ValueError('Native ABI size/type')
    v=struct.unpack('<%dQ'%count,raw)
    if v[:3]!=(magic,1,generation):raise ValueError('Native ABI identity/generation')
    return v
def flags(row,names):
    if any(row[k] not in (0,1) for k in names.split()):raise ValueError('Native boolean')
def info(raw,generation):
    v=words(raw,64,INFO_MAGIC,generation)
    if any(v[36:]):raise ValueError('Info reserved words')
    r=dict(zip(INFO,v));flags(r,'prepared opened bootstrap_captured pinned owned lease ready exhausted metal_verified closed prepare_attempted prepare_passed open_attempted')
    if (r['capacity'],r['request_bytes'],r['library_bytes'],r['code_bytes'],r['image_bytes'],r['plan_bytes'])!=(32,2112,512,4096,24576,4096):raise ValueError('Runtime geometry')
    if r['phase']>4 or r['owner_phase']>18 or r['backing_phase']>5 or r['program_count'] not in (0,3) or r['active_capture']>4 or r['metal_verified']:raise ValueError('Runtime bounds')
    if r['queue_completed']>r['completed'] or r['completed']-r['queue_completed']>1 or r['child_bytes']>45056 or r['child_bytes']%4096:raise ValueError('Runtime counts')
    if bool(r['ready'])!=(r['phase']==1 and not r['closed']) or bool(r['exhausted'])!=(r['phase']==4):raise ValueError('Runtime phase flags')
    if r['prepare_reads']>23 or r['prepare_bytes']>94208 or r['prepare_bytes']%4096 or r['prepare_bytes']>r['prepare_reads']*4096:raise ValueError('Prepare capture counts')
    if r['prepared'] and (not r['prepare_attempted'] or not r['prepare_passed'] or not 8192<=r['child_bytes']<=45056 or r['prepare_bytes']!=49152+r['child_bytes'] or r['prepare_reads']!=12+r['child_bytes']//4096 or r['prepare_elapsed']>=5_000_000_000):raise ValueError('Prepare proof')
    if r['ready'] or r['exhausted']:
        if not all(r[k] for k in ('prepared','opened','bootstrap_captured','pinned','owned','lease','prepare_passed','open_attempted')) or r['owner_phase']!=18 or r['pci_command']!=6 or r['backing_phase']!=1 or r['queue_completed']!=r['completed'] or r['last_serial']!=r['completed'] or r['program_count']!=3:raise ValueError('Runtime owner/retirement proof')
        if r['exhausted']!=(r['completed']==request.MAX_SERIAL):raise ValueError('Serial exhaustion')
        if r['active_capture']!=(4 if r['completed'] else 1):raise ValueError('Current capture kind')
    return r
def job(raw,generation,serial,child_bytes):
    request.integer(serial,'serial',1,request.MAX_SERIAL);request.integer(child_bytes,'child bytes',8192,45056)
    if child_bytes%4096:raise ValueError('Child alignment')
    v=words(raw,128,JOB_MAGIC,generation)
    if any(x for i,x in enumerate(v) if i not in JOB):raise ValueError('Job reserved words')
    r={name:v[i] for i,name in JOB.items()}
    if r['serial']!=serial or r['phase']>4 or r['failure']>14 or r['program']>2 or r['groups']!=1 or (r['entry'],r['put'])!=(serial&31,((serial&31)+1)&31):raise ValueError('Job identity/geometry')
    flags(r,'closed attempted passed core_passed window_current window_attempted window_saved mutation_attempted acquired restore_attempted restored capture_attempted capture_passed before_passed staged_passed completed_passed')
    if r['completed'] not in (serial-1,serial) or r['queue_completed'] not in (serial-1,serial) or r['queue_completed']>r['completed']:raise ValueError('Job completion count')
    if not r['attempted'] or r['writes']>7 or r['notifications']>1 or r['operations']>65536 or r['polls']>r['operations']:raise ValueError('Job operation bounds')
    if not r['window_current'] and any(v[21:34]):raise ValueError('Stale window metadata')
    if r['window_current'] and not r['window_attempted']:raise ValueError('Window attempt missing')
    if any(r[k]>0xffffffff for k in ('window_before','window_selected','window_after')):raise ValueError('Window register size')
    if r['acquired'] and (not r['window_saved'] or not r['mutation_attempted'] or r['window_selected'] or r['window_elapsed']>=5_000_000_000):raise ValueError('Window acquisition proof')
    if r['restored'] and (not all(r[k] for k in ('window_current','window_saved','mutation_attempted','restore_attempted')) or r['window_before']!=r['window_after'] or r['cleanup_elapsed']>=5_000_000_000):raise ValueError('Window restoration proof')
    if r['capture_kind'] not in (0,2,3,4):raise ValueError('Capture kind')
    if not r['capture_kind'] and any(v[41:49]):raise ValueError('Stale capture metadata')
    if r['capture_kind'] and (r['capture_serial']!=serial or not r['capture_attempted']):raise ValueError('Capture serial')
    if r['capture_reads']>12+child_bytes//4096 or r['capture_bytes']>49152+child_bytes or r['capture_bytes']%4096 or r['capture_bytes']>r['capture_reads']*4096:raise ValueError('Capture bounds')
    if r['capture_passed'] and (r['capture_bytes']!=49152+child_bytes or r['capture_reads']!=12+child_bytes//4096 or r['capture_elapsed']>=5_000_000_000 or r['capture_last_address']!=0x0340e000):raise ValueError('Complete capture proof')
    if r['passed']:
        if not all(r[k] for k in ('core_passed','acquired','restored','capture_passed','before_passed','staged_passed','completed_passed')) or r['failure'] or r['writes']!=7 or r['notifications']!=1 or r['capture_kind']!=4 or r['queue_completed']!=serial or r['elapsed']>=5_000_000_000:raise ValueError('Job retirement proof')
    return r
memory=bootstrap_abi.memory
capture_info=bootstrap_abi.capture_info
def sizes(raw,serial,child_bytes):
    if type(raw) is not bytes or len(raw)!=1024:raise ValueError('Capture size record')
    v=struct.unpack('<128Q',raw)
    if v[40] not in (0,2,3,4) or v[45]>49152+child_bytes or v[45]%4096:raise ValueError('Capture size bounds')
    if not v[40]:return (0,0,0)
    if v[41]!=serial or v[42]!=1:raise ValueError('Capture size serial')
    root=min(v[45],12288);children=min(v[45]-root,child_bytes);return root,children,v[45]-root-children
