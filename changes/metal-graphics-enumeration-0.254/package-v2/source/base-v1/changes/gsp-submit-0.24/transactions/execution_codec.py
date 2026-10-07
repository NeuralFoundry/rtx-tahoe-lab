"""Fixed execution RM proposal. Pure bytes; no queue or hardware imports."""
from pathlib import Path
import struct,sys
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'layout'))
sys.path.insert(0,str(HERE.parent/'execution'))
import execution_plan as p
import work_submit_token as w
rpc=p.c.gsp_rpc
STEPS=13
FIRST=p.external.FIRST+p.external.STEPS
REQUEST_BYTES=STEPS*4096
GROUP_STEP,SHARE_STEP,CHANNEL_STEP,COMPUTE_STEP,SIZE_STEP,PHYSICAL_STEP,VIRTUAL_STEP,COPY_STEP,FIFO_STEP,BIND_STEP,ENABLE_STEP,SCHEDULE_STEP,TOKEN_STEP=range(STEPS)
SIZES=(20,12,368,0,1664,560,560,0,3212,4,2,2,4)
ALLOC=(GROUP_STEP,SHARE_STEP,CHANNEL_STEP,COMPUTE_STEP,COPY_STEP)

def check(step,sequence,golden,predecessor):
    if type(step) is not int or not 0<=step<STEPS or type(sequence) is not int or not 0<=sequence<0xffffffff:raise ValueError('Execution step/sequence')
    if type(predecessor) is not int or predecessor!=3:raise ValueError('Verified predecessor ChID3 required')
    p.golden_fits(golden)

def object_handle(step):
    return p.GROUP if step in (GROUP_STEP,SCHEDULE_STEP) else p.SHARE if step==SHARE_STEP else p.CHANNEL if step in (CHANNEL_STEP,BIND_STEP,ENABLE_STEP,TOKEN_STEP) else 0xcf000008 if step==COMPUTE_STEP else 0xcf000009 if step==COPY_STEP else p.SUBDEVICE

def parent_handle(step):return p.DEVICE if step==GROUP_STEP else p.GROUP if step in (SHARE_STEP,CHANNEL_STEP) else p.CHANNEL

def control(step):return {SIZE_STEP:0x20800a32,PHYSICAL_STEP:0x2080012b,VIRTUAL_STEP:0x2080012b,FIFO_STEP:0x20801112,BIND_STEP:0xa06f0104,ENABLE_STEP:0xa06f0103,SCHEDULE_STEP:0xa06c0101,TOKEN_STEP:0xc36f0108}.get(step,0)

def parameters(step,golden,predecessor,plan):
    check(step,0,golden,predecessor)
    if step==GROUP_STEP:return struct.pack('<4IB3x',0,0,0,1,0)
    if step==SHARE_STEP:return struct.pack('<3I',p.VASPACE,1,0)
    if step==CHANNEL_STEP:return p.channel_parameters(golden,predecessor)
    if step in (PHYSICAL_STEP,VIRTUAL_STEP):
        if type(plan) is not dict:raise ValueError('Fresh private context plan required')
        return p.promotion(plan,golden,step==PHYSICAL_STEP)
    return b'\x01\0\0\0' if step==BIND_STEP else b'\x01\0' if step in (ENABLE_STEP,SCHEDULE_STEP) else b'\xff'*4 if step==TOKEN_STEP else bytes(SIZES[step])

def request(step,sequence,golden,predecessor,plan):
    check(step,sequence,golden,predecessor);params=parameters(step,golden,predecessor,plan)
    if step in ALLOC:
        header=struct.pack('<8I',p.CLIENT,parent_handle(step),
                           object_handle(step),{GROUP_STEP:0xa06c,SHARE_STEP:0x9067,CHANNEL_STEP:0xc56f,COMPUTE_STEP:0xc7c0,COPY_STEP:0xc7b5}[step],0,len(params),0,0)
    else:header=struct.pack('<6I',p.CLIENT,object_handle(step),control(step),0,len(params),0)
    return rpc.encode_record(103 if step in ALLOC else 76,header+params,transport_sequence=sequence)

def reply(raw,sequence,step,golden,predecessor,plan):
    check(step,sequence,golden,predecessor)
    r=rpc.decode_record(raw,expected_sequence=sequence)
    if r.rpc.function!=(103 if step in ALLOC else 76) or r.rpc.result or r.rpc.private_result or r.rpc.sequence:raise ValueError('Execution reply envelope')
    expected=rpc.decode_record(request(step,sequence,golden,predecessor,plan)).rpc.payload
    header=32 if step in ALLOC else 24
    if len(r.rpc.payload)!=len(expected) or r.rpc.payload[:header]!=expected[:header]:raise ValueError('Execution reply identity/status/size')
    params=r.rpc.payload[header:];canonical=expected[header:]
    result=dict(accepted=True,hardware_accessed=False,doorbell_ready=False,compute_verified=False,metal_verified=False)
    if step==SIZE_STEP:result['private_plan']=p.make(params,golden)
    elif step==FIFO_STEP:result['runlist']=w.fifo(raw,sequence,p.CLIENT,p.SUBDEVICE)
    elif step==TOKEN_STEP:
        result['raw_token']=struct.unpack('<I',params)[0]
        if result['raw_token']!=p.HARDWARE_CHANNEL_ID:raise ValueError('Token must match verified requested ChID4')
    elif step==SHARE_STEP:
        if params[:8]!=canonical[:8]:raise ValueError('Context share owner or flags differ')
        result['subcontext_id']=struct.unpack_from('<I',params,8)[0]
        if result['subcontext_id']>63:raise ValueError('Context share output is not a supported VEID')
    elif step==CHANNEL_STEP:
        if params[:132]!=canonical[:132] or params[140:240]!=canonical[140:240] or params[244:]!=canonical[244:]:raise ValueError('Channel backing echo differs')
        result['physical_channel_group']=struct.unpack_from('<I',params,240)[0]
        result['channel_id'],result['subdevice_mask']=struct.unpack_from('<II',params,132)
        if not p.session_id(result['channel_id'],predecessor) or result['subdevice_mask']>1:raise ValueError('Unexpected execution channel identity')
    elif params!=canonical:raise ValueError('Execution reply parameter echo differs')
    return result
