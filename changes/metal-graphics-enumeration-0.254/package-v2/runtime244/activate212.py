"""Pinned compiler/bootstrap import closure, without importing old native owners."""
from pathlib import Path
import json,sys

def activate(source):
    runtime=source.parent/'runtime244';decoder=source/'decoder-v1';admission=source/'admission-v1';support=source/'runtime-v1';base=source/'base-v1'
    sys.path[:0]=list(map(str,(runtime,decoder,admission,support,base)))
    import runtime_admission,uploaded_library,resident_admission,channel_chain_audit,startup_evidence
    for module in (runtime_admission,uploaded_library,resident_admission):
        if not Path(module.__file__).resolve().is_relative_to(admission):raise ValueError('Mixed compiler admission module: '+module.__name__)
    for module in (channel_chain_audit,startup_evidence):
        if not Path(module.__file__).resolve().is_relative_to(decoder):raise ValueError('Mixed diagnostic module: '+module.__name__)
    release=json.loads((admission/'admission-release.json').read_bytes());row=release['programs'][0]
    catalog,image,receipt=runtime_admission.load(admission/row['path'],row['manifest_sha256'])
    if image!=(support/'selected.rtxlib').read_bytes():raise ValueError('Exact initial bootstrap image')
    return catalog,image,receipt
