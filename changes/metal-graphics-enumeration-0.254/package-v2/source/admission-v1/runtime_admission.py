"""Bind a real runtime pipeline result to reproducible native upload metadata.

The expected manifest hash is supplied by the owner of the reviewed compiler
transaction. Hashes preserve that provenance; they are not a SASS verifier.
Import does not open a device or initialize firmware.
"""
from pathlib import Path,PurePosixPath
import base64,re,struct
from uploaded_library import Program,Catalog,need,parse,sha

LIMITS={'air':1048576,'ir':131072,'spirv':131072,'assembly':131072,'ptx':131072,'cubin':131072,'lowering':131072,'reflection':131072,'request':262144,'response':262144,'container':5248}

def json_bytes(raw,limit=262144):
 from uploaded_library import pairs
 import json
 need(type(raw) is bytes and 0<len(raw)<=limit,'runtime JSON extent')
 return json.loads(raw,object_pairs_hook=pairs)

def load(directory,expected_manifest_sha256):
 directory=Path(directory).resolve();manifest_path=directory/'runtime-pipeline.json'
 need(not manifest_path.is_symlink() and 0<manifest_path.stat().st_size<=131072,'runtime manifest file')
 raw=manifest_path.read_bytes();need(type(expected_manifest_sha256) is str and re.fullmatch('[0-9a-f]{64}',expected_manifest_sha256) and sha(raw)==expected_manifest_sha256,'runtime manifest identity')
 manifest=parse(raw);need(type(manifest) is dict and set(manifest)=={'abi','files','compiler_review_sha256'},'runtime manifest schema');need(type(manifest['abi']) is int and manifest['abi']==1,'runtime manifest ABI')
 need(type(manifest['compiler_review_sha256']) is str and re.fullmatch('[0-9a-f]{64}',manifest['compiler_review_sha256']),'runtime review identity')
 files=manifest['files'];need(type(files) is dict and set(files)==set(LIMITS),'runtime compiler files');data={}
 for name,row in files.items():
  need(type(row) is dict and set(row)=={'path','bytes','sha256'} and type(row['path']) is str,'runtime file record');rel=PurePosixPath(row['path'])
  need(not rel.is_absolute() and '..' not in rel.parts and str(rel)==row['path'] and '\\' not in str(rel) and ':' not in str(rel),'runtime file path')
  p=directory/str(rel);need(not p.is_symlink() and p.resolve().is_relative_to(directory),'runtime file scope')
  need(type(row['bytes']) is int and 0<row['bytes']<=LIMITS[name] and p.stat().st_size==row['bytes'],'runtime file size');blob=p.read_bytes();need(sha(blob)==row['sha256'],'runtime file identity');data[name]=blob
 request=json_bytes(data['request']);response=json_bytes(data['response']);reflection=json_bytes(data['reflection'])
 need(set(request)=={'version','id','entry','air_sha256','ir','ir_sha256'} and type(request['version']) is int and request['version']==1,'compiler request schema')
 need(type(request['id']) is str and re.fullmatch('[0-9a-f]{32}',request['id']),'compiler request identity');name=request['entry']
 need(type(name) is str and re.fullmatch('[A-Za-z_][A-Za-z_0-9]{0,126}',name),'runtime function name')
 response_fields={'version','id','entry','air_sha256','ir_sha256','ok','container','container_sha256','code_sha256','abi'}
 need(type(response) is dict and set(response) in (response_fields,response_fields|{'library_abi','local_size'},response_fields|{'library_abi','local_size','container_abi'}),'compiler response schema')
 payload_abi=1
 if 'library_abi' in response:
  need(type(response['library_abi']) is int and response['library_abi']==2,'general compiler library ABI')
  from geometry164 import validate_local
  validate_local(response['local_size']);payload_abi=2
 need(type(response['version']) is int and response['version']==1 and response['ok'] is True,'successful compiler response')
 need(all(response[k]==request[k] for k in ('id','entry','air_sha256','ir_sha256')),'runtime response binding')
 need(request['air_sha256']==sha(data['air']) and request['ir_sha256']==sha(data['ir']) and base64.b64decode(request['ir'],validate=True)==data['ir'],'runtime AIR/IR binding')
 air=data['air'];need(len(air)>24,'runtime AIR header');magic,version,offset,size,cpu=struct.unpack_from('<5I',air)
 need((magic,version,offset,cpu)==(0xb17c0de,0,20,0xffffffff) and 4<=size<=len(air)-20 and 0<=len(air)-20-size<16 and not any(air[20+size:]) and air[20:24]==b'BC\xc0\xde','runtime AIR wrapper')
 need(re.findall(rb'air64_v[0-9]+-apple-macosx[0-9.]+',air)==[b'air64_v28-apple-macosx26.6.0'] and b'target triple = "air64_v28-apple-macosx26.6.0"' in data['ir'],'runtime AIR target')
 need(reflection['entry_point']==name and reflection['stage']=='Kernel','runtime reflection entry')
 need(len(data['spirv'])>=20 and len(data['spirv'])%4==0 and struct.unpack_from('<I',data['spirv'])[0]==0x07230203,'runtime SPIR-V header')
 program=Program(name,data['assembly'],data['ptx'],data['cubin'],data['lowering'],data['ir'],payload_abi=payload_abi);catalog=Catalog((program,))
 if payload_abi==2:need(list(program.local_size)==response['local_size'],'compiled/requested local dimensions')
 # Reuse the production ELF reader and compare the complete resource ABI,
 # not just the instruction hash supplied in the compiler response.
 if payload_abi==1:from uploaded_compiler.cubin_audit import audit
 else:from uploaded_compiler.cubin_general222 import audit
 code,abi=audit(data['cubin'],len(program.bindings),list(program.local_size));need(code==program.code and abi==response['abi'] and sha(code)==response['code_sha256'],'runtime executable ABI binding')
 container=data['container'];need(len(container)==5248 and base64.b64decode(response['container'],validate=True)==container and response['container_sha256']==sha(container),'runtime container response')
 names=bytearray(name.encode('ascii')+bytes(512-len(name)));container_abi=response.get('container_abi',payload_abi)
 metadata=parse(data['lowering'])
 if 'resource_bindings' in metadata:
  from texture_container225 import resource_bytes
  need(payload_abi==2 and type(container_abi) is int and container_abi==3 and 'container_abi' in response,'texture container ABI')
  names[128:256]=resource_bytes(metadata['resource_bindings'])
 else:need('container_abi' not in response,'unexpected texture container ABI')
 body=bytes(names)+catalog.library+catalog.code
 expected=struct.pack('<Q6I',0x5254584d4c423336,container_abi,5248,0x86,1,512,4608)+bytes.fromhex(sha(body))+bytes(64)+body
 need(container==expected,'runtime pipeline/native catalog mismatch')
 return catalog,container,dict(abi=1,request_id=request['id'],entry=name,native_air_sha256=sha(air),ir_sha256=sha(data['ir']),container_sha256=sha(container),payload_sha256=catalog.digest,code_sha256=sha(code),compiler_review_sha256=manifest['compiler_review_sha256'],gpu_uploaded=False)
