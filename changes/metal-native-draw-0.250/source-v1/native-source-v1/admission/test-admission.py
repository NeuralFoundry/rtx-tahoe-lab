from pathlib import Path
import concurrent.futures,hashlib,json,os,shutil,sys,tempfile
import resident_admission as R
source=Path(__file__).resolve().parent;release=json.loads((source/'admission-release.json').read_bytes());rows=release['programs'];approved={r['manifest_sha256']:release['compiler_review_sha256'] for r in rows};checks=0
def check(value):
 global checks
 checks+=1
 if not value:raise AssertionError(checks)
def rejects(f,kind=Exception):
 global checks
 try:f()
 except kind:checks+=1;return
 raise AssertionError('Expected rejection')
store=R.Registry(approved);images=[];receipts=[]
for row in rows:
 receipt=store.install(source/row['path'],row['manifest_sha256']);receipts.append(receipt)
 manifest=json.loads((source/row['path']/'runtime-pipeline.json').read_bytes());image=(source/row['path']/manifest['files']['container']['path']).read_bytes();images.append(image)
 check(R.digest(image)==row['container_sha256'] and R.digest(image[640:])==row['payload_sha256'] and receipt['code_sha256']==row['code_sha256'])
 check(store.admit(image[640:])==receipt)
 check(store.install(source/row['path'],row['manifest_sha256'])==receipt)
check(receipts[0]['entry']==receipts[1]['entry'] and receipts[0]['code_sha256']!=receipts[1]['code_sha256'])
check(store.snapshot()['registered_transactions']==store.snapshot()['unique_payloads']==2)
# Returned JSON and the caller's initial dictionary do not change owned policy.
receipts[0]['payload_sha256']='0'*64;approved.clear();check(store.admit(images[0][640:])['payload_sha256']==rows[0]['payload_sha256'])
for bad in (None,b'',images[0],images[0][641:],images[0][639:],bytearray(images[0][640:])):check(store.admit(bad) is None)
mutations=0
for original in images:
 for offset in [0,8,12,16,20,24,31,32,63,64,80,95,96,127,128,255,256,383,384,495,496,511,512,528,640,768,896,1023,1024,1151,2048,4095,4096,4607]:
  data=bytearray(original[640:]);data[offset]^=1;check(store.admit(bytes(data)) is None);mutations+=1
# Concurrent admission remains consistent with both owned shader versions.
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 results=list(pool.map(lambda i:store.admit(images[i%2][640:])['payload_sha256'],range(128)))
check(results==[rows[i%2]['payload_sha256'] for i in range(128)])
approved={r['manifest_sha256']:release['compiler_review_sha256'] for r in rows}
limited=R.Registry(approved,1);limited.install(source/rows[0]['path'],rows[0]['manifest_sha256']);rejects(lambda:limited.install(source/rows[1]['path'],rows[1]['manifest_sha256']))
untrusted=R.Registry({rows[0]['manifest_sha256']:'1'*64});rejects(lambda:untrusted.install(source/rows[0]['path'],rows[0]['manifest_sha256']))
rejects(lambda:store.install(source,'2'*64));check(store.snapshot()['unique_payloads']==2)
with tempfile.TemporaryDirectory() as tmp:
 root=Path(tmp)/'program';shutil.copytree(source/rows[0]['path'],root);manifest=json.loads((root/'runtime-pipeline.json').read_bytes());p=root/manifest['files']['air']['path'];original=p.read_bytes();p.write_bytes(original[:24]+bytes([original[24]^1])+original[25:])
 changed=R.Registry(approved);rejects(lambda:changed.install(root,rows[0]['manifest_sha256']));check(changed.snapshot()['unique_payloads']==0)
 check(store.admit(images[0][640:]) is not None)
for value in (0,257,True,'64'):rejects(lambda:R.Registry(approved,value))
for value in ({},{'0'*64:'1'*64},{'1'*64:'0'*64},[]):rejects(lambda:R.Registry(value))
if not hasattr(os,'geteuid') or os.geteuid()!=0:rejects(lambda:R.RootCallback(store),PermissionError)
pid=store._pid;store._pid=pid+1;rejects(lambda:store.admit(images[0][640:]),RuntimeError);store._pid=pid
store.close();check(store.admit(images[0][640:]) is None);rejects(lambda:store.install(source/rows[0]['path'],rows[0]['manifest_sha256']))
report=dict(passed=True,checks=checks,programs=2,same_function_name=True,different_code=True,rejected_payload_corruptions=mutations,concurrent_admissions=len(results),native_iokit_connected=False,gpu_commands_submitted=False)
print(json.dumps(report))
