"""GA106 CPU token decoder; no device access or doorbell writer."""
from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[3]))
import gsp_compute_prep_codec as prep
def fifo(raw,sequence,client=prep.CLIENT,subdevice=prep.OBJECTS[2]):
    packet=prep.gsp_rpc.decode_record(raw,expected_sequence=sequence).rpc
    if (packet.function!=76 or packet.result or packet.private_result or packet.sequence or len(packet.payload)!=3236 or
        struct.unpack_from('<6I',packet.payload)!=(client,subdevice,0x20801112,0,3212,0)):
        raise ValueError('FIFO reply owner/status')
    r=prep.fifo_params(packet.payload[24:])
    if not r['complete'] or len(r['graphics_entries'])!=1:raise ValueError('Complete unique GR engine required')
    e=r['graphics_entries'][0];count=e['num_pbdmas']
    if not 0<=e['runlist']<128 or not 1<=count<=2:raise ValueError('GA106 runlist/PBDMA bounds')
    pbdmas=e['pbdma_ids'][:count];faults=e['pbdma_fault_ids'][:count]
    if any(v>=32 for v in pbdmas) or any(v>=256 for v in faults) or len(set(pbdmas))!=count or len(set(faults))!=count:raise ValueError('Ambiguous PBDMA routing')
    return dict(valid=True,sequence=sequence,entry=e['index'],id=e['runlist'],pbdma=pbdmas,fault=faults)
def compose(runlist,channel_id,raw_token):
    if runlist.get('valid') is not True or type(runlist.get('id')) is not int or not 0<=runlist['id']<128:raise ValueError('Validated runlist required')
    if type(channel_id) is not int or type(raw_token) is not int or not 0<=channel_id<4096 or raw_token!=channel_id:raise ValueError('Unrecognized/mismatched GSP channel token')
    return dict(candidate=(runlist['id']<<16)|channel_id,doorbell_ready=False,hardware_accessed=False,
                requires_fresh_same_generation_channel_and_token=True,requires_native_pf_profile=True)
