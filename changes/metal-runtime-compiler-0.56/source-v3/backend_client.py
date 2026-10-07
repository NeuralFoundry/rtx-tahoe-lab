"""Mac runtime compiler client; pinned local AIR reader and forwarded compiler."""
from pathlib import Path
import base64,hashlib,json,os,re,socket,struct,subprocess,sys,uuid
if os.getpgrp()!=os.getpid():os.setsid()
sha=lambda data:hashlib.sha256(data).hexdigest()
config=json.loads(Path(sys.argv[1]).read_bytes());job=Path(sys.argv[2]);result=None
try:
 air=(job/'input.air').read_bytes();entry=(job/'entry.txt').read_text()
 if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,126}',entry):raise ValueError('entry name')
 if not 20<len(air)<=1048576:raise ValueError('AIR extent')
 magic,version,offset,size,cpu=struct.unpack_from('<5I',air)
 if (magic,version,offset,cpu)!=(0xb17c0de,0,20,0xffffffff) or size>len(air)-20 or not 0<=len(air)-20-size<16 or any(air[20+size:]) or air[20:24]!=b'BC\xc0\xde':raise ValueError('AIR wrapper')
 triples=re.findall(rb'air64_v[0-9]+-apple-macosx[0-9.]+',air)
 if triples!=[b'air64_v28-apple-macosx26.6.0']:raise ValueError('AIR target')
 metal=Path(config['metal']['path'])
 if sha(metal.read_bytes())!=config['metal']['sha256']:raise ValueError('Metal tool identity')
 with (job/'metal.stdout').open('xb') as out,(job/'metal.stderr').open('xb') as err:
  p=subprocess.run([str(metal),'-target',triples[0].decode(),'-S','-emit-llvm','-x','ir','-Xclang','-disable-llvm-passes',str(job/'input.air'),'-o',str(job/'input.ll')],stdout=out,stderr=err,timeout=15)
 if p.returncode or (job/'metal.stderr').stat().st_size:raise ValueError('Metal AIR conversion failed')
 ir=(job/'input.ll').read_bytes()
 if not 1<=len(ir)<=131072:raise ValueError('IR extent')
 request=dict(version=1,id=uuid.uuid4().hex,entry=entry,air_sha256=sha(air),ir=base64.b64encode(ir).decode(),ir_sha256=sha(ir))
 (job/'request.json').write_text(json.dumps(request,sort_keys=True)+'\n')
 envelope=json.dumps(dict(token=config['token'],request=request),separators=(',',':')).encode()
 with socket.create_connection(('127.0.0.1',config['port']),timeout=25) as s:
  s.settimeout(25);s.sendall(struct.pack('!I',len(envelope))+envelope)
  def read(n):
   data=b''
   while len(data)<n:
    part=s.recv(n-len(data))
    if not part:raise ValueError('truncated compiler response')
    data+=part
   return data
  n=struct.unpack('!I',read(4))[0]
  if not 1<=n<=262144:raise ValueError('compiler response extent')
  response=read(n);result=json.loads(response)
 (job/'response.json').write_bytes(response)
 if result.get('version')!=1 or result.get('id')!=request['id']:raise ValueError('response identity')
 if not result.get('ok'):raise ValueError(result.get('error','compiler failure'))
 for key in ('entry','air_sha256','ir_sha256'):
  if result.get(key)!=request[key]:raise ValueError('response mismatch '+key)
 container=base64.b64decode(result['container'],validate=True)
 if len(container)!=5248 or sha(container)!=result['container_sha256']:raise ValueError('container identity')
 (job/'compiled.rtxlib').write_bytes(container)
 (job/'result.json').write_text(json.dumps(dict(passed=True,request_id=request['id'],container_sha256=sha(container)))+'\n')
except Exception as e:
 (job/'result.json').write_text(json.dumps(dict(passed=False,error=type(e).__name__+': '+str(e)))+'\n');sys.exit(1)
