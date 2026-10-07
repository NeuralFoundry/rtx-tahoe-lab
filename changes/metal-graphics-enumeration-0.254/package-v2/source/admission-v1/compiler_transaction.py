"""Root-owned runtime compiler; all transaction paths originate here."""
from pathlib import Path
import base64,hashlib,json,os,socket,subprocess,uuid
from compiler_protocol import TARGET,air_request,receive,send,sha
import runtime_admission
import compile_plan154 as workload_policy

OUTPUTS={'spirv':'shader.spv','assembly':'input.spvasm','ptx':'shader.ptx','cubin':'shader.cubin','lowering':'lowering.json','reflection':'reflection.json','container':'compiled.rtxlib'}

class Compiler:
    def __init__(self,registry,configuration,directory,workload):
        if os.geteuid()!=0:raise PermissionError('Compiler owner must be root')
        self.registry=registry;self.configuration=json.loads(json.dumps(configuration));self.directory=Path(directory)
        if self.directory.resolve()!=self.directory or not self.directory.is_dir() or self.directory.stat().st_uid!=0 or self.directory.stat().st_mode&0o077:raise ValueError('Private root compiler directory')
        if registry._compiler_review!=configuration['compiler_review_sha256']:raise ValueError('Compiler/registry review identity')
        self.pid=os.getpid();self.count=0;self.workload=workload_policy.validate(workload);self.request_limit=len(self.workload["outcomes"])
    def compile(self,row,*,peer):
        if os.geteuid()!=0 or os.getpid()!=self.pid:raise PermissionError('Compiler owner changed')
        identity=workload_policy.peer(peer);entry,air=air_request(row)
        if self.count>=self.request_limit:raise ValueError('Compiler transaction capacity')
        self.count+=1;job=self.directory/('%03d'%self.count);job.mkdir(mode=0o700)
        def write(name,data):
            with (job/name).open('xb') as f:f.write(data)
        def save(name,value):write(name,(json.dumps(value,sort_keys=True)+'\n').encode())
        try:
            write('input.air',air);save('client-request.json',row);save('kernel-peer.json',identity)
            metal=Path(self.configuration['metal']['path'])
            if metal.resolve()!=metal or sha(metal.read_bytes())!=self.configuration['metal']['sha256']:raise ValueError('Apple compiler identity')
            argv=[str(metal),'-target',TARGET.decode(),'-S','-emit-llvm','-x','ir','-Xclang','-disable-llvm-passes',str(job/'input.air'),'-o',str(job/'input.ll')]
            with (job/'metal.stdout').open('xb') as out,(job/'metal.stderr').open('xb') as err:
                p=subprocess.run(argv,stdout=out,stderr=err,timeout=15)
            save('frontend-command.json',dict(argv=argv,returncode=p.returncode))
            if p.returncode!=0 or (job/'metal.stderr').stat().st_size:raise ValueError('Apple AIR conversion failed')
            ir=(job/'input.ll').read_bytes()
            if not 0<len(ir)<=131072:raise ValueError('Compiler IR extent')
            request=dict(version=1,id=uuid.uuid4().hex,entry=entry,air_sha256=sha(air),ir=base64.b64encode(ir).decode(),ir_sha256=sha(ir));save('request.json',request)
            port=self.configuration['port']
            if type(port) is not int or not 1<=port<=65535:raise ValueError('Compiler endpoint port')
            with socket.create_connection(('127.0.0.1',port),timeout=25) as s:
                s.settimeout(25);send(s,dict(token=self.configuration['token'],request=request),262144);backend=receive(s,1_500_000)
            if type(backend) is not dict or set(backend)!={'version','response','files'} or type(backend['version']) is not int or backend['version']!=1:raise ValueError('Backend artifact schema')
            response=base64.b64decode(backend['response'],validate=True)
            reply=runtime_admission.json_bytes(response)
            write('response.json',response)
            if reply.get('ok') is not True:raise ValueError(reply.get('error','Backend compilation failed'))
            if type(backend['files']) is not dict or set(backend['files'])!=set(OUTPUTS):raise ValueError('Backend artifact set')
            for kind,name in OUTPUTS.items():
                encoded=backend['files'][kind]
                if type(encoded) is not str or len(encoded)>180000:raise ValueError('Backend encoding extent')
                raw=base64.b64decode(encoded,validate=True)
                if not 0<len(raw)<=runtime_admission.LIMITS[kind]:raise ValueError('Backend artifact extent')
                write(name,raw)
            paths=dict(air='input.air',ir='input.ll',request='request.json',response='response.json',**OUTPUTS)
            manifest=dict(abi=1,compiler_review_sha256=self.configuration['compiler_review_sha256'],files={kind:dict(path=name,bytes=(job/name).stat().st_size,sha256=sha((job/name).read_bytes())) for kind,name in paths.items()})
            save('runtime-pipeline.json',manifest);identity=sha((job/'runtime-pipeline.json').read_bytes())
            # This is the only point that admits new executable bytes. The
            # directory and its manifest were built by this trusted root owner.
            receipt=self.registry.install_compiled(job,identity)
            container=(job/'compiled.rtxlib').read_bytes()
            if receipt['container_sha256']!=sha(container):raise ValueError('Admitted compiler container mismatch')
            result=dict(version=1,ok=True,entry=entry,air_sha256=sha(air),container=base64.b64encode(container).decode(),container_sha256=sha(container),admission=receipt)
            save('admitted.json',receipt);save('result.json',result);return result
        except Exception as e:
            result=dict(version=1,ok=False,entry=entry,air_sha256=sha(air),error=type(e).__name__+': '+str(e))
            save('result.json',result);return result
