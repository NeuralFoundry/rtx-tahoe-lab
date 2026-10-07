"""Read-only captured channel-chain audit; never opens a device or starts firmware."""
import json
import gsp_application_channel_native as channel
import gsp_application_execution_native as execution

INPUTS=tuple(['channel/'+n+'-info.bin' for n in ('ring','contexts','rm','plan','snapshot')]+
 ['channel/request-'+str(i)+'.bin' for i in range(5)]+
 ['channel/'+n for n in ('index.bin','records.bin','root-capture.bin','children-capture.bin')]+
 ['execution/external/'+n for n in ('info.bin','index.bin','records.bin','requests.bin')]+
 ['execution/'+n+'-info.bin' for n in ('fixed','contexts','rm','plan','snapshot','fence','device')]+
 ['execution/'+n for n in ('requests.bin','index.bin','records.bin','root-capture.bin','children-capture.bin','device-capture.bin')])

def require_clean_chain(golden,run):
    for label,row in (('golden',golden),('external',run.get('external',{})),('execution',run)):
        if row.get('firmware_assertions')!=[] or row.get('firmware_assertions_unresolved',False):
            raise ValueError(label+': firmware assertions present or evidence missing')
        if row.get('passed') is not True:raise ValueError(label+': channel validation did not pass')
    if not golden.get('table_readback_verified') or not run.get('table_readback_verified') or not run.get('device_bytes_verified'):
        raise ValueError('GPU memory or HOST proof missing')
    g=golden['rm'];e=run['external']['info'];r=run['rm']
    if (g['generation'],e['generation'])!=(r['generation'],r['generation']):raise ValueError('Mixed channel generations')
    if (g['rx_reader'],g['rx_sequence'])!=(e['initial_reader'],e['initial_sequence']):raise ValueError('Golden/external RX boundary')
    if (e['rx_reader'],e['rx_sequence'])!=(r['initial_reader'],r['initial_sequence']):raise ValueError('External/execution RX boundary')
    if (g['tx_writer'],g['tx_reader'],e['tx_writer'],e['tx_reader'],r['tx_writer'],r['tx_reader'])!=(14,14,19,19,execution.EXECUTION_FINAL_PRODUCER,execution.EXECUTION_FINAL_PRODUCER):
        raise ValueError('Canonical TX boundary')
    return dict(passed=True,generation=r['generation'],golden_records=g['count'],external_records=e['count'],execution_records=r['count'],golden_assertions=0,external_assertions=0,execution_assertions=0,execution_rx_first=r['initial_sequence'],execution_rx_next=r['rx_sequence'],execution_tx_first=19,execution_tx_next=execution.EXECUTION_FINAL_PRODUCER)

class Snapshot:
    """Only reads the supplied immutable binary capture dictionary."""
    def __init__(self,data):
        if any(type(data.get(name)) is not bytes for name in INPUTS):raise ValueError('Missing immutable capture input')
        self.data={name:data[name] for name in INPUTS}
    def read(self,name,off=0,size=None):
        raw=self.data[name]
        if size is None:size=len(raw)
        if type(off) is not int or type(size) is not int or off<0 or size<0 or off>len(raw) or size>len(raw)-off:raise ValueError('Capture span')
        return raw[off:off+size]
    def channel_memory_info(self,stage):return self.read('channel/'+('ring' if stage==0 else 'contexts')+'-info.bin')
    def channel_rm_info(self):return self.read('channel/rm-info.bin')
    def channel_plan_info(self):return self.read('channel/plan-info.bin')
    def channel_snapshot_info(self):return self.read('channel/snapshot-info.bin')
    def channel_rm_index(self,start,count):return self.read('channel/index.bin',start*72,count*72)
    def channel_rm_data(self,off,size):return self.read('channel/records.bin',off,size)
    def channel_request(self,i):return self.read('channel/request-'+str(i)+'.bin')
    def channel_snapshot_data(self,which,off,size):return self.read('channel/'+('root' if which==0 else 'children')+'-capture.bin',off,size)
    def external_info(self):return self.read('execution/external/info.bin')
    def external_index(self,start,count):return self.read('execution/external/index.bin',start*72,count*72)
    def external_data(self,off,size):return self.read('execution/external/records.bin',off,size)
    def external_request(self,i):return self.read('execution/external/requests.bin',i*4096,4096)
    def execution_memory_info(self,stage):return self.read('execution/'+('fixed' if stage==0 else 'contexts')+'-info.bin')
    def execution_rm_info(self):return self.read('execution/rm-info.bin')
    def execution_plan_info(self):return self.read('execution/plan-info.bin')
    def execution_snapshot_info(self):return self.read('execution/snapshot-info.bin')
    def host_fence_info(self):return self.read('execution/fence-info.bin')
    def execution_device_info(self):return self.read('execution/device-info.bin')
    def execution_rm_index(self,start,count):return self.read('execution/index.bin',start*72,count*72)
    def execution_rm_data(self,off,size):return self.read('execution/records.bin',off,size)
    def execution_request(self,i):return self.read('execution/requests.bin',i*4096,4096)
    def execution_snapshot_data(self,which,off,size):return self.read('execution/'+('root' if which==0 else 'children')+'-capture.bin',off,size)
    def execution_device_data(self,off,size):return self.read('execution/device-capture.bin',off,size)

def verify(data,generation,output):
    output.mkdir(exist_ok=False)
    result=dict(passed=False,read_only_capture_audit=True,new_hardware_accessed=False,full_metal_verified=False)
    try:
        backend=Snapshot(data)
        g=channel.capture(backend,generation,output/'channel')
        e=execution.capture(backend,generation,output/'execution',g,output/'channel')
        result.update(require_clean_chain(g,e))
    except (ValueError,OSError,RuntimeError) as error:result['error']=str(error)
    (output/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result
