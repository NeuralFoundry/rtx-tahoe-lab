"""Read-only identity for the reviewed 0.83.1/0.253.0 pair; requires actual OS load before use."""
import base64, plistlib, re, subprocess
from ready253 import need, positive, memory_valid

PARENT_UUID='87C52767-5913-3185-B122-0C2BB5D44085'
CHILD_UUID='933FE97D-1A00-361B-ACCC-B33C9FBEA5D8'
COLD_COUNTERS=('GSPOwnerPhase GSPFirmwareStartMask GSPProgramCompleted GSPShaderUploadPhase GSPShaderUploadBytes GSPShaderUploadError GSPResidentProgramEpoch GSPResidentProgramPhase OwnedGraphicsCompleted').split()
COLD_FLAGS=('GSPExecutionAttempted GSPDmaResourcesHeld GSPDmaProviderOpen GSPDmaPinnedUntilRestart FirmwareStartAttempted FirmwareExecuted GSPResidentProgramReplaced GSPHostFencePassed GSPInitDoneObserved RTXMetalVerified OwnedRootAcknowledged OwnedRootRuntimeEnabled OwnedDispatchActive OwnedGraphicsActive OwnedGraphicsRetained').split()
PUBLISHED=('RTXMetalGPUReady MetalPluginName MetalPluginClassName RTXMetalPublicationEpoch RTXMetalPublicationSession').split()
KEYS=set(COLD_COUNTERS+COLD_FLAGS+PUBLISHED+('IORegistryEntryID ProbeVersion OwnedRootABI HostBufferABI OwnedDataABI OwnedGraphicsABI OwnedDispatchABI OwnedProgramABI OwnedProgramInputABI ProbeComplete ProbePassed TargetIdentity TargetSubsystem GSPProgramReady RTXMetalAcceleratorVersion RTXMetalParentProbeVersion RTXMetalParentRegistryID RTXMetalPublicationABI').split())

def observe():
    def command(argv):return subprocess.check_output(argv,timeout=25)
    boot=command(['/usr/sbin/sysctl','-n','kern.bootsessionuuid']).decode().strip()
    loaded=command(['/usr/bin/kmutil','showloaded','--list-only']).decode()
    result=dict(boot_uuid=boot,amd_present='AMDRadeon' in loaded,loaded_rtx=[l.strip() for l in loaded.splitlines() if 'local.emre.RTX' in l])
    for cls in ('RTXProbe','RTXMetalAccelerator132','RTXMetalAccelerator107'):
        raw=command(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c',cls]);rows=plistlib.loads(raw) if raw.strip() else []
        result[cls]=[]
        for row in rows:
            item={k:row[k] for k in KEYS if k in row}
            if 'ProbeMemoryEvidence107' in row:
                need(type(row['ProbeMemoryEvidence107']) is bytes,'Memory property type')
                item['memory_base64']=base64.b64encode(row['ProbeMemoryEvidence107']).decode()
            result[cls].append(item)
    return result

def fields(row, expected):
    for k,v in expected.items():need(type(row.get(k)) is type(v) and row[k]==v,'Paired property: '+k)

def require_loaded(value,binding):
    need(type(binding) is dict and set(binding)=={'boot_uuid','generation','child_registry'},'Paired binding schema')
    positive(binding['generation'],'Parent generation');positive(binding['child_registry'],'Child registry')
    need(binding['generation']!=binding['child_registry'],'Distinct paired identities')
    need(value['boot_uuid']==binding['boot_uuid'] and value['amd_present'] is True,'Current boot/AMD')
    lines=value['loaded_rtx'];need(len(lines)==2,'Exactly two approved modules')
    for name,version,uuid in [('RTXProbe','0.83.1',PARENT_UUID),('RTXMetalAccelerator132','0.253.0',CHILD_UUID)]:
        need(sum(bool(re.search(r'local\.emre\.'+name+r'\s+\('+re.escape(version)+r'\)',l)) and uuid in l for l in lines)==1,'OS-loaded paired UUID/version')
    need(not value['RTXMetalAccelerator107'] and len(value['RTXProbe'])==len(value['RTXMetalAccelerator132'])==1,'Unique parent and child')
    parent=value['RTXProbe'][0];child=value['RTXMetalAccelerator132'][0]
    fields(parent,dict(IORegistryEntryID=binding['generation'],ProbeVersion='0.83.1',OwnedRootABI=242,HostBufferABI=2,OwnedDataABI=181,OwnedDispatchABI=183,OwnedGraphicsABI=242,OwnedProgramABI=205,OwnedProgramInputABI=206,ProbeComplete=True,ProbePassed=True,TargetIdentity=0x252010de,TargetSubsystem=0x104c1043))
    fields(child,dict(IORegistryEntryID=binding['child_registry'],RTXMetalAcceleratorVersion='0.253.0',RTXMetalParentProbeVersion='0.83.1',RTXMetalParentRegistryID=binding['generation'],RTXMetalPublicationABI=3))
    memory=base64.b64decode(parent['memory_base64'],validate=True);memory_valid(memory,binding['generation'])
    return parent,child,memory

def require_hidden(value,binding):
    parent,child,memory=require_loaded(value,binding)
    need(not any(k in child for k in PUBLISHED),'Child must have no publication properties')
    return parent,child,memory

def require_cold(value):
    need(len(value['RTXProbe'])==len(value['RTXMetalAccelerator132'])==1,'Unique cold pair')
    binding=dict(boot_uuid=value['boot_uuid'],generation=value['RTXProbe'][0]['IORegistryEntryID'],child_registry=value['RTXMetalAccelerator132'][0]['IORegistryEntryID'])
    parent,_,_=require_hidden(value,binding)
    fields(parent,{k:0 for k in COLD_COUNTERS});fields(parent,{k:False for k in COLD_FLAGS});fields(parent,dict(GSPProgramReady=False))
    return binding

def require_ready(value,binding):
    parent,child,memory=require_hidden(value,binding)
    fields(parent,{k:True for k in ('OwnedRootAcknowledged','OwnedRootRuntimeEnabled','OwnedDispatchActive','GSPDmaProviderOpen','GSPProgramReady','GSPHostFencePassed','GSPInitDoneObserved','OwnedGraphicsActive')})
    fields(parent,dict(OwnedGraphicsRetained=False))
    epoch=positive(parent['GSPResidentProgramEpoch'],'Live resident epoch')
    return epoch,memory
