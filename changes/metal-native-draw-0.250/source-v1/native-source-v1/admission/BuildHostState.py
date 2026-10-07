"""Read-only CPU build identity; calendar-derived boot time is observational.

This permits only the recorded retained0.75.0 boot and property set. It neither
authorizes a GPU operation nor establishes the clean state needed for one.
"""
import plistlib,re

def identity(state):
    return {k:v for k,v in state.items() if k not in ('boot_seconds','boot_microseconds')}

def validate(boot,loaded,probe):
    if not __debug__:
        raise RuntimeError('Build validation requires assertions')
    uuids=re.findall(rb'^kern.bootsessionuuid: ([A-F0-9-]+)$',boot,re.M)
    times=re.findall(rb'^kern.boottime: \{ sec = ([0-9]+), usec = ([0-9]+) \}[^\r\n]*$',boot,re.M)
    if len(uuids)!=1 or len(times)!=1:
        raise ValueError('Boot identity fields')
    if uuids[0]!=b'96A304DA-1DDC-4337-9DFF-C3694C0808A1':
        raise ValueError('Build host boot changed')
    seconds,microseconds=map(int,times[0])
    if not (0<seconds<1<<63 and 0<=microseconds<1_000_000):
        raise ValueError('Boot calendar fields')
    if b'local.emre.RTXProbe (0.75.0)' not in loaded or b'AMDRadeon' not in loaded or b'RTXMetalAccelerator' in loaded or b'local.emre.RTXProbe (0.75.1)' in loaded:
        raise ValueError('Build host loaded drivers changed')
    rows=plistlib.loads(probe)
    if type(rows) is not list or len(rows)!=1 or type(rows[0]) is not dict:
        raise ValueError('Build host unique RTX service')
    row=rows[0]
    expected=dict(ProbeVersion='0.75.0',IORegistryEntryID=4294969795,ProbeComplete=True,ProbePassed=True,
                  GSPOwnerPhase=7,GSPFirmwareStartMask=3,GSPProgramCompleted=0,GSPResidentProgramEpoch=1,
                  GSPResidentProgramPhase=1,GSPResidentProgramReplaced=False,GSPDmaPinnedUntilRestart=True,
                  GSPDmaResourcesHeld=True,GSPDmaProviderOpen=True,GSPHostFencePassed=True,GSPInitDoneObserved=True)
    for key,value in expected.items():
        if type(row.get(key)) is not type(value) or row[key]!=value:
            raise ValueError('Build host property changed: '+key)
    return dict(boot_uuid=uuids[0].decode(),boot_seconds=seconds,boot_microseconds=microseconds,
                properties=expected,prior_kernel_retained=True,candidate_loaded=False)
