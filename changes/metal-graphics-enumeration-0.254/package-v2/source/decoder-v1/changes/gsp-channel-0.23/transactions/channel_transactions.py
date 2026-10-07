"""Independent CPU-only five-exchange wire codec; does not send requests."""
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import channel_plan as c
import gsp_rpc as rpc

STEPS=5
SIZES=(368,1664,560,0,0)
OBJECTS=(0xcf000004,c.prep.OBJECTS[2],c.prep.OBJECTS[2],0xcf000005,0xcf000006)
FUNCTIONS=(103,76,76,103,103)
CLASSES=(0xc56f,0,0,0xc7c0,0xc7b5)
CONTROLS=(0,0x20800a32,0x2080012b,0,0)


def request(step,plan=None):
    if type(step) is not int or not 0<=step<STEPS:raise ValueError('Fixed transaction step')
    params=c.channel_parameters() if step==0 else bytes(1664) if step==1 else c.promote_parameters(plan) if step==2 else b''
    if FUNCTIONS[step]==103:
        parent=c.prep.OBJECTS[1] if step==0 else OBJECTS[0]
        header=struct.pack('<8I',c.prep.CLIENT,parent,OBJECTS[step],CLASSES[step],0,SIZES[step],0,0)
    else:header=struct.pack('<6I',c.prep.CLIENT,OBJECTS[step],CONTROLS[step],0,SIZES[step],0)
    return rpc.encode_record(FUNCTIONS[step],header+params,transport_sequence=9+step)


def reply(raw,step,sequence,plan=None):
    if type(step) is not int or not 0<=step<STEPS:raise ValueError('Fixed transaction step')
    d=rpc.decode_record(raw,expected_sequence=sequence)
    if d.rpc.function!=FUNCTIONS[step] or d.rpc.result or d.rpc.private_result or d.rpc.sequence:raise ValueError('Reply envelope/status')
    req=rpc.decode_record(request(step,plan));n=24 if FUNCTIONS[step]==76 else 32
    if len(d.rpc.payload)!=n+SIZES[step] or d.rpc.payload[:n]!=req.rpc.payload[:n]:raise ValueError('Reply handles/class/control/status')
    params=d.rpc.payload[n:];result=dict(accepted=True,step=step,hardware_accessed=False)
    if step==0:
        canonical=c.channel_parameters()
        if params[:132]!=canonical[:132] or params[140:240]!=canonical[140:240] or params[244:]!=canonical[244:]:raise ValueError('Channel backing changed')
        # Internal RM output, retained only for diagnosis; never an address/token.
        result['physical_channel_group']=struct.unpack_from('<I',params,240)[0]
        result['channel_id'],result['subdevice_mask']=struct.unpack_from('<II',params,132)
        if result['subdevice_mask']>1:raise ValueError('Unsupported multi-subdevice channel')
    elif step==1:result['context_plan']=c.context_plan(params)
    elif step==2:
        if params!=c.promote_parameters(plan):raise ValueError('Promotion echo changed')
    return result
