"""Runtime AIR-text backend, no source catalogue or name-based code selection."""
from pathlib import Path
import base64,hashlib,json,os,re,struct,subprocess,sys
from spirv_ptx import translate
from cubin_audit import audit

def compile_request(request,out,config):
 def sha(data):return hashlib.sha256(data).hexdigest()
 if set(request)!={'version','id','entry','air_sha256','ir','ir_sha256'} or request['version']!=1:raise ValueError('request schema')
 if not re.fullmatch(r'[a-zA-Z_][a-zA-Z0-9_]{0,126}',request['entry']):raise ValueError('entry name')
 if not re.fullmatch(r'[0-9a-f]{64}',request['air_sha256']):raise ValueError('AIR identity')
 if not re.fullmatch(r'[0-9a-f]{32}',request['id']):raise ValueError('request identity')
 ir=base64.b64decode(request['ir'],validate=True)
 if not 1<=len(ir)<=131072 or sha(ir)!=request['ir_sha256']:raise ValueError('IR extent or identity')
 text=ir.decode('utf-8')
 if 'target triple = "air64_v28-apple-macosx26.6.0"' not in text or '!"air.compile.fast_math_disable"' not in text or '!"air.compile.denorms_disable"' not in text:raise ValueError('unsupported AIR target or math contract')
 (out/'input.ll').write_bytes(ir)
 tools={}
 for name,row in config.items():
  p=Path(row['path'])
  if sha(p.read_bytes())!=row['sha256']:raise ValueError('tool identity '+name)
  tools[name]=p
 commands=[]
 def run(argv,name):
  with (out/name).open('xb') as f:p=subprocess.run([str(a) for a in argv],cwd=out,stdout=f,stderr=subprocess.STDOUT,timeout=15,env=dict(os.environ,METAL2VULKAN_SPIRV_VAL=str(tools['spirv-val'])))
  commands.append(dict(argv=[str(a) for a in argv],log=name,returncode=p.returncode))
  if p.returncode:raise ValueError('compiler rejected input: '+name)
 run([tools['metal2vulkan'],out/'input.ll',out/'shader.spv','--stage','kernel','--whole-workgroups','--local','64,1,1','--emit-meta',out/'reflection.json'],'translate.log')
 reflection=json.loads((out/'reflection.json').read_bytes())
 if reflection['entry_point']!=request['entry'] or reflection['stage']!='Kernel':raise ValueError('entry identity or stage')
 run([tools['spirv-val'],'--target-env','vulkan1.2',out/'shader.spv'],'validate.log')
 run([tools['spirv-dis'],'--raw-id',out/'shader.spv','-o',out/'input.spvasm'],'disassemble.log')
 ptx,meta=translate((out/'input.spvasm').read_text(),fp32_denorm='flush')
 (out/'shader.ptx').write_text(ptx,encoding='utf-8');(out/'lowering.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
 bindings=meta['parameter_bindings']
 if not 1<=len(bindings)<=8 or meta['local_size']!=[64,1,1]:raise ValueError('program ABI limit')
 run([tools['ptxas'],'-arch=sm_86','-O3','-v',out/'shader.ptx','-o',out/'shader.cubin'],'ptxas.log')
 run([tools['nvdisasm'],out/'shader.cubin'],'shader.sass')
 code,abi=audit((out/'shader.cubin').read_bytes(),len(bindings),[64,1,1])
 names=bytearray(512);name=request['entry'].encode('ascii');names[:len(name)]=name
 wire=bytearray(512);program=bytearray(4096);program[:len(code)]=code
 struct.pack_into('<Q5I',wire,0,0x5254584c49423333,1,512,1,(len(code)+255)&~255,0x86)
 values=[1,0,len(code),abi['registers'],64,1,1,len(bindings),sum(1<<b for b in meta['read_bindings']),sum(1<<b for b in meta['written_bindings']),abi['constant_bytes'],0]+bindings+[0]*(8-len(bindings))+[0]*8
 assert len(values)==28;struct.pack_into('<28I',wire,64,*values)
 body=bytes(names+wire+program);container=struct.pack('<Q6I',0x5254584d4c423336,1,5248,0x86,1,512,4608)+hashlib.sha256(body).digest()+bytes(64)+body
 assert len(container)==5248;(out/'compiled.rtxlib').write_bytes(container);(out/'code.bin').write_bytes(code)
 result=dict(version=1,id=request['id'],entry=request['entry'],air_sha256=request['air_sha256'],ir_sha256=request['ir_sha256'],ok=True,container=base64.b64encode(container).decode(),container_sha256=sha(container),code_sha256=sha(code),abi=abi)
 (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8');return result

if __name__=='__main__':
 out=Path(sys.argv[1]);request=json.loads((out/'request.json').read_bytes())
 try:result=compile_request(request,out,json.loads(Path(sys.argv[2]).read_bytes()))
 except Exception as e:result=dict(version=1,id=request.get('id'),ok=False,error=type(e).__name__+': '+str(e))
 (out/'response.json').write_text(json.dumps(result,sort_keys=True)+'\n',encoding='utf-8')
