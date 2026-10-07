from pathlib import Path
import hashlib,json,sys
from profile import library,expected


def verify(root,out):
    wire,code,programs=library()
    assert (root/'fixtures/library.bin').read_bytes()==wire and (root/'fixtures/code.bin').read_bytes()==code
    assert json.loads((root/'fixtures/programs.json').read_text())==programs
    report=json.loads((out/'native.json').read_text());assert report['passed'] and report['plans']==12 and report['rejected']>=35
    assert not report['gpu_commands_submitted']
    files=[]
    for program in programs:
        for slot in range(4):
            name=f'plan-{program["index"]}-{slot}.bin';raw=(out/name).read_bytes()
            assert raw==expected(program,slot),name
            files.append(dict(path=name,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()))
    assert len({(out/x['path']).read_bytes() for x in files})==12
    return dict(passed=True,programs=3,plans=12,rejected=report['rejected'],checks=report['checks'],
        wire_sha256=hashlib.sha256(wire).hexdigest(),code_sha256=hashlib.sha256(code).hexdigest(),
        nvidia_header_oracle_passed=True,files=files,gpu_commands_submitted=False,metal_verified=False)


if __name__=='__main__':
    root=Path(__file__).resolve().parent;out=root/sys.argv[1]
    r=verify(root,out);(out/'profile-verification.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
