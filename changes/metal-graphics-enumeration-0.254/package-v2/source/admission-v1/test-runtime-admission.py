"""CPU tests for dynamic admission, protocol extents and immutable receipts."""
from pathlib import Path
import base64,concurrent.futures,copy,json,os,shutil,socket,struct,tempfile,threading
import compiler_protocol as P
import resident_admission as R

h=Path(__file__).resolve().parent;release=json.loads((h/'admission-release.json').read_bytes());review=release['compiler_review_sha256'];rows=release['programs'];checks=0
def check(value):
    global checks
    checks+=1
    assert value,checks
def rejects(fn):
    global checks
    try:fn()
    except Exception:checks+=1
    else:raise AssertionError('Invalid input accepted')
first=h/rows[0]['path'];manifest=json.loads((first/'runtime-pipeline.json').read_bytes());air=(first/manifest['files']['air']['path']).read_bytes()
request=dict(version=1,entry='runtime_4017_same',air=base64.b64encode(air).decode(),air_sha256=P.sha(air))
check(P.air_request(request)==('runtime_4017_same',air))
for key,value in [('version',True),('version',2),('entry','../bad'),('entry','f'*128),('air_sha256','0'*64),('air','%%%')]:
    r=dict(request);r[key]=value;rejects(lambda:P.air_request(r))
for key in request:
    r=dict(request);r.pop(key);rejects(lambda:P.air_request(r))
for extra in ('manifest','directory','ir','compiler_review_sha256','code','container'):
    r=dict(request);r[extra]='peer choice';rejects(lambda:P.air_request(r))
for offset in (0,8,12,16,20):
    raw=bytearray(air);raw[offset]^=1;r=dict(request,air=base64.b64encode(raw).decode(),air_sha256=P.sha(raw));rejects(lambda:P.air_request(r))
rejects(lambda:P.json_data(b'{"x":1,"x":2}',100));rejects(lambda:P.json_data(b'{}',1))
# Framing behaves correctly with actual fragmented stream bytes and closure.
class Fragments:
    def __init__(self,data):self.data=data
    def recv(self,n):
        b=self.data[:min(n,3)];self.data=self.data[len(b):];return b
wire=json.dumps(request).encode();check(P.receive(Fragments(struct.pack('!I',len(wire))+wire),P.MAX_REQUEST)==request)
for wire in (b'',b'\0\0',struct.pack('!I',P.MAX_REQUEST+1),struct.pack('!I',1),struct.pack('!I',0)):
    rejects(lambda:P.receive(Fragments(wire),P.MAX_REQUEST))
rejects(lambda:R.Registry({},compiler_review_sha256='0'*64))
store=R.Registry({},compiler_review_sha256=review)
if not hasattr(os,'geteuid') or os.geteuid()!=0:rejects(lambda:store.install_compiled(first,rows[0]['manifest_sha256']))
# Unit-only owner identity shim. Real root peer checks are exercised on Mac.
original=getattr(os,'geteuid',None);os.geteuid=lambda:0
try:
    for row in rows:
        receipt=store.install_compiled(h/row['path'],row['manifest_sha256'])
        check(receipt['admission_origin']=='owner_runtime_compiler' and receipt['compiler_review_sha256']==review)
        check(store.install_compiled(h/row['path'],row['manifest_sha256'])==receipt)
        manifest=json.loads((h/row['path']/'runtime-pipeline.json').read_bytes());image=(h/row['path']/manifest['files']['container']['path']).read_bytes()
        check(store.admit(image[640:])==receipt)
        receipt['payload_sha256']='0'*64;check(store.admit(image[640:])['payload_sha256']==row['payload_sha256'])
    check(store.snapshot()['registered_transactions']==2 and store.snapshot()['unique_payloads']==2)
    wrong=R.Registry({},compiler_review_sha256='1'*64)
    rejects(lambda:wrong.install_compiled(first,rows[0]['manifest_sha256']));check(wrong.snapshot()['registered_transactions']==0)
    static=R.Registry({rows[0]['manifest_sha256']:review});rejects(lambda:static.install_compiled(first,rows[0]['manifest_sha256']))
    rejects(lambda:store.install(first,'1'*64))
    limited=R.Registry({},1,review);limited.install_compiled(first,rows[0]['manifest_sha256']);rejects(lambda:limited.install_compiled(h/rows[1]['path'],rows[1]['manifest_sha256']))
    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary)/'altered';shutil.copytree(first,directory)
        row=json.loads((directory/'runtime-pipeline.json').read_bytes());p=directory/row['files']['air']['path'];raw=bytearray(p.read_bytes());raw[24]^=1;p.write_bytes(raw)
        empty=R.Registry({},compiler_review_sha256=review);rejects(lambda:empty.install_compiled(directory,rows[0]['manifest_sha256']));check(empty.snapshot()['registered_transactions']==0)
    mixed=R.Registry({rows[0]['manifest_sha256']:review},compiler_review_sha256=review)
    old=mixed.install(first,rows[0]['manifest_sha256']);check('admission_origin' not in old)
    new=mixed.install_compiled(h/rows[1]['path'],rows[1]['manifest_sha256']);check(new['admission_origin']=='owner_runtime_compiler')
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        values=list(pool.map(lambda i:store.install_compiled(h/rows[i%2]['path'],rows[i%2]['manifest_sha256'])['payload_sha256'],range(64)))
    check(values==[rows[i%2]['payload_sha256'] for i in range(64)])
    store.close();rejects(lambda:store.install_compiled(first,rows[0]['manifest_sha256']))
finally:
    if original is None:del os.geteuid
    else:os.geteuid=original
print(json.dumps(dict(passed=True,checks=checks,concurrent_installs=64,owner_identity_shim=True,native_peer_credentials_tested=False,native_iokit_connected=False,gpu_commands_submitted=False)))
