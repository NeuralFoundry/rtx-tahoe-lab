"""Read-only current-boot and OS-loaded artifact binding."""
import json,plistlib,re,subprocess
from pathlib import Path
KERNEL_UUID='07FE2680-BDF7-3F7B-8D4B-A4025993AD59'
KEYS=('IORegistryEntryID ProbeVersion OwnedRootABI OwnedDataABI OwnedDispatchABI ProbeComplete ProbePassed TargetIdentity TargetSubsystem GSPOwnerPhase GSPFirmwareStartMask GSPProgramCompleted GSPShaderUploadPhase GSPShaderUploadBytes GSPShaderUploadError GSPResidentProgramEpoch GSPResidentProgramPhase GSPExecutionAttempted GSPDmaResourcesHeld GSPDmaProviderOpen GSPDmaPinnedUntilRestart FirmwareStartAttempted FirmwareExecuted GSPResidentProgramReplaced GSPHostFencePassed GSPInitDoneObserved RTXMetalVerified OwnedRootAcknowledged OwnedRootRuntimeEnabled').split()
def observe():
    def command(argv):return subprocess.check_output(argv,timeout=25)
    boot=command(['/usr/sbin/sysctl','-n','kern.bootsessionuuid']).decode().strip()
    loaded=command(['/usr/bin/kmutil','showloaded','--list-only']).decode()
    result=dict(boot_uuid=boot,loaded_rtx=[line.strip() for line in loaded.splitlines() if re.search(r'local\.emre\.RTX',line)])
    for cls in ('RTXProbe','RTXMetalAccelerator132','RTXMetalAccelerator107'):
        raw=command(['/usr/sbin/ioreg','-a','-l','-w0','-r','-c',cls]);rows=plistlib.loads(raw)if raw.strip()else []
        result[cls]=[{k:r[k]for k in KEYS if k in r}for r in rows]
    return result
def require_loaded(value,binding):
    if value['boot_uuid']!=binding['boot_uuid']:raise ValueError('Current boot differs from load binding')
    lines=value['loaded_rtx']
    if len(lines)!=1 or not re.search(r'local\.emre\.RTXProbe\s+\(0\.79\.0\)',lines[0]) or KERNEL_UUID not in lines[0]:raise ValueError('OS-loaded RTXProbe version/UUID')
    if value['RTXMetalAccelerator132']or value['RTXMetalAccelerator107']:raise ValueError('Unexpected publication child in parent-only experiment')
    rows=value['RTXProbe']
    if len(rows)!=1:raise ValueError('Unique RTXProbe service')
    for k,v in dict(IORegistryEntryID=binding['generation'],ProbeVersion='0.79.0',OwnedRootABI=195,OwnedDataABI=181,OwnedDispatchABI=183).items():
        if type(rows[0].get(k))is not type(v) or rows[0][k]!=v:raise ValueError('Loaded parent binding: '+k)
def require_cold(value):
    rows=value['RTXProbe']
    if len(rows)!=1:raise ValueError('Unique cold parent')
    binding=dict(boot_uuid=value['boot_uuid'],generation=rows[0]['IORegistryEntryID']);require_loaded(value,binding)
    for k in ('GSPOwnerPhase','GSPFirmwareStartMask','GSPProgramCompleted','GSPShaderUploadPhase','GSPShaderUploadBytes','GSPShaderUploadError','GSPResidentProgramEpoch','GSPResidentProgramPhase'):
        if type(rows[0].get(k))is not int or rows[0][k]!=0:raise ValueError('Non-cold native counter: '+k)
    for k in ('GSPExecutionAttempted','GSPDmaResourcesHeld','GSPDmaProviderOpen','GSPDmaPinnedUntilRestart','FirmwareStartAttempted','FirmwareExecuted','GSPResidentProgramReplaced','GSPHostFencePassed','GSPInitDoneObserved','RTXMetalVerified','OwnedRootAcknowledged','OwnedRootRuntimeEnabled'):
        if rows[0].get(k)is not False:raise ValueError('Non-cold native flag: '+k)
    return binding
