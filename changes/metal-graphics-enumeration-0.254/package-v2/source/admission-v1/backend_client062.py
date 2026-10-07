"""Existing native MTL pipeline API helper, now admitted by the root compiler."""
from pathlib import Path
import base64,json,os,socket,sys
from compiler_protocol import MAX_REQUEST,MAX_RESPONSE,air_request,peer_uid,receive,send,sha

def main():
    if os.getpgrp()!=os.getpid():os.setsid()
    config=json.loads(Path(sys.argv[1]).read_bytes());job=Path(sys.argv[2])
    try:
        air=(job/'input.air').read_bytes();entry=(job/'entry.txt').read_text()
        row=dict(version=1,entry=entry,air=base64.b64encode(air).decode(),air_sha256=sha(air));air_request(row)
        path=Path(config['socket_path'])
        if path.resolve()!=path or path.parent.stat().st_uid!=0 or path.parent.stat().st_mode&0o022:raise ValueError('Root compiler endpoint path')
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
            s.settimeout(45);s.connect(str(path))
            if peer_uid(s)!=0:raise PermissionError('Compiler endpoint is not root-owned')
            send(s,row,MAX_REQUEST);result=receive(s,MAX_RESPONSE)
        (job/'response.json').write_text(json.dumps(result,sort_keys=True)+'\n')
        if result.get('version')!=1 or result.get('ok') is not True:raise ValueError(result.get('error','Compiler refused pipeline'))
        if result.get('entry')!=entry or result.get('air_sha256')!=sha(air):raise ValueError('Compiler response identity')
        container=base64.b64decode(result['container'],validate=True)
        if len(container)!=5248 or sha(container)!=result['container_sha256'] or result['admission']['container_sha256']!=sha(container) or result['admission']['admission_origin']!='owner_runtime_compiler':raise ValueError('Compiler admitted container identity')
        (job/'compiled.rtxlib').write_bytes(container)
        (job/'result.json').write_text(json.dumps(dict(passed=True,container_sha256=sha(container),admission=result['admission']))+'\n')
        return 0
    except Exception as e:
        (job/'result.json').write_text(json.dumps(dict(passed=False,error=type(e).__name__+': '+str(e)))+'\n');return 1

if __name__=='__main__':raise SystemExit(main())
