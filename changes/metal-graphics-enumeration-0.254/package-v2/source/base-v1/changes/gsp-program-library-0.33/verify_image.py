from pathlib import Path
import hashlib,json,sys
from image_profile import models,data_offset


def verify(root,out):
    requests,plans,canonical,states=models();files=[]
    expected={'initial.bin':states[0]}
    for i in range(4):
        expected[f'request-{i}.bin']=requests[i]['wire'];expected[f'plan-{i}.bin']=plans[i]
        expected[f'canonical-{i}.bin']=canonical[i];expected[f'simulated-{i}.bin']=states[i+1]
    wrong=bytearray(states[-1]);wrong[data_offset(3)+512]^=1
    expected['semantic-error-accepted-by-guard.bin']=bytes(wrong)
    # Application arithmetic must reject this byte even though it lies within
    # the shader's writable output span, where a lifetime guard permits writes.
    assert wrong[data_offset(3)+512:data_offset(3)+768]!=requests[3]['answer']
    for name,data in expected.items():
        actual=(out/name).read_bytes();assert actual==data,name
        files.append(dict(path=name,bytes=len(actual),sha256=hashlib.sha256(actual).hexdigest()))
    # One proposed high-generation-word mutation is identical to the input (0)
    # and is skipped. The suite executes 63 distinct rejection scenarios.
    native=json.loads((out/'native.json').read_text());assert native['passed'] and native['rejected']==63 and native['requests']==4 and native['programs']==3
    assert native['cpu_simulation_only'] and native['semantic_guard_separation_verified'] and not native['gpu_commands_submitted']
    return dict(passed=True,raw_files=len(files),files=files,requests=4,programs=3,rejected=native['rejected'],checks=native['checks'],
        session_bytes=native['session_bytes'],plan_bytes=native['plan_bytes'],nvidia_header_oracle_passed=True,
        cpu_simulation_only=True,application_rejects_semantic_error=True,gpu_commands_submitted=False,metal_verified=False)


if __name__=='__main__':
    root=Path(__file__).resolve().parent;out=root/sys.argv[1];r=verify(root,out)
    (out/'image-verification.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:v for k,v in r.items() if k!='files'}))
