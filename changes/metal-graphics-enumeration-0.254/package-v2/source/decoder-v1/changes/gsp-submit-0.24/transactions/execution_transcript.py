"""Offline ordered journal checks. No claim about queue or memory publication."""
import struct
import execution_codec as e

def verify(requests,records,first_sequence,golden,predecessor):
    e.check(0,first_sequence,golden,predecessor)
    if type(requests) is not bytes or len(requests)!=e.REQUEST_BYTES or type(records) is not bytes or not e.REQUEST_BYTES<=len(records)<=131072 or len(records)%4096:
        raise ValueError('Execution journal sizes')
    if first_sequence>0xffffffff-16:raise ValueError('RX sequence overflow')
    offset=step=events=count=0;sequence=first_sequence;plan=None;channel=None;runlist=None;token=None;mask=None;graphics_caps=None;subcontext=None
    while offset<len(records):
        if step>=e.STEPS or count>=16:raise ValueError('Extra execution records')
        pages=struct.unpack_from('<I',records,offset+40)[0]
        if not 1<=pages<=16 or pages*4096>len(records)-offset:raise ValueError('Incomplete execution record')
        raw=records[offset:offset+pages*4096];r=e.rpc.decode_record(raw,expected_sequence=sequence)
        if r.rpc.function==(103 if step in e.ALLOC else 76):
            if requests[step*4096:(step+1)*4096]!=e.request(step,e.FIRST+step,golden,predecessor,plan):raise ValueError('Noncanonical execution request')
            result=e.reply(raw,sequence,step,golden,predecessor,plan)
            if step==e.CHANNEL_STEP:channel=result['channel_id'];mask=result['subdevice_mask']
            if step==e.SIZE_STEP:plan=result['private_plan']
            if step==e.GRAPHICS_STEP:graphics_caps=result['graphics_caps']
            if step==e.SHARE_STEP:subcontext=result['subcontext_id']
            if step==e.FIFO_STEP:runlist=result['runlist']
            if step==e.TOKEN_STEP:token=result['raw_token']
            step+=1
        else:
            payload=r.rpc.payload
            supported=(r.rpc.function==0x1020 and len(payload)==1212 or r.rpc.function==0x100c and len(payload)>=8 and struct.unpack_from('<I',payload,4)[0]==len(payload)-8)
            if r.rpc.result or not supported:raise ValueError('Unsupported execution event')
            events+=1
        offset+=len(raw);count+=1;sequence+=1
    if step!=e.STEPS or graphics_caps is None or subcontext!=0:raise ValueError('Incomplete graphics execution sequence')
    candidate=e.w.compose(runlist,e.p.HARDWARE_CHANNEL_ID,token)['candidate']
    return dict(passed=True,completed=step,records=count,events=events,pages=len(records)//4096,next_sequence=sequence,channel_id=channel,
                subdevice_mask=mask,graphics_caps=graphics_caps,subcontext_id=subcontext,raw_token=token,candidate=candidate,private_plan=plan,runlist=runlist,
                hardware_accessed=False,doorbell_ready=False,compute_verified=False,metal_verified=False)
