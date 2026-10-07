"""Own the existing062 compiler service inside the real058 native bootstrap."""
from pathlib import Path
import hashlib,json,os,re
from compiler_transaction import Compiler
from compiler_service import Service
from compiler_protocol import pairs

def need(value,message):
    if not value:raise ValueError(message)

def validate_configuration(value,release):
    need(type(value) is dict and set(value)=={'token','port','metal','compiler_review_sha256'},'Compiler configuration schema')
    need(type(value['token']) is str and re.fullmatch('[0-9a-f]{64}',value['token']),'Compiler endpoint token')
    need(type(value['port']) is int and value['port']==release['compiler_port'],'Compiler loopback port')
    need(value['compiler_review_sha256']==release['compiler_review_sha256'],'Compiler review')
    need(type(value['metal']) is dict and set(value['metal'])=={'path','sha256'} and value['metal']==dict(path=release['compiler_metal_path'],sha256=release['compiler_metal_sha256']),'Compiler toolchain identity')
    return json.loads(json.dumps(value))

def load_configuration(path,release):
    path=Path(path)
    need(os.geteuid()==0 and path.is_absolute() and path.resolve()==path and path.is_file() and path.stat().st_uid==0 and not path.stat().st_mode&0o077 and 0<path.stat().st_size<=8192,'Private root compiler configuration')
    value=validate_configuration(json.loads(path.read_bytes(),object_pairs_hook=pairs),release)
    binary=Path(value['metal']['path'])
    need(binary.resolve()==binary and binary.is_file() and hashlib.sha256(binary.read_bytes()).hexdigest()==value['metal']['sha256'],'Pinned Apple compiler binary')
    return value

from compile_plan154 import completion, validate as validate_workload

class RuntimeCompiler:
    def __init__(self,registry,configuration,evidence,public,release,workload):
        self.workload=validate_workload(workload);self.configuration=load_configuration(configuration,release)
        self.registry=registry;self.evidence=Path(evidence);self.pid=os.getpid()
        need(self.evidence.resolve()==self.evidence and self.evidence.is_dir() and self.evidence.stat().st_uid==0 and not self.evidence.stat().st_mode&0o022,'Owned compiler evidence')
        public=Path(public)
        need(public.is_absolute() and public.resolve()==public and public.is_dir() and public.stat().st_uid==0 and public.stat().st_mode&0o777==0o755,'Root public compiler socket directory')
        self.socket_path=public/'compiler.sock'
        self.compiler=None;self.service=None;self.closed=None;self.start_attempted=False
    def _process(self):need(os.getpid()==self.pid and os.geteuid()==0,'Compiler owner changed')
    def start(self):
        self._process();need(not self.start_attempted and self.closed is None,'Compiler service starts once');self.start_attempted=True
        jobs=self.evidence/'compiler-jobs';jobs.mkdir(mode=0o700)
        self.compiler=Compiler(self.registry,self.configuration,jobs,self.workload);self.service=Service(self.compiler,self.socket_path);self.service.start()
        return dict(socket_path=str(self.service.path))
    def close(self):
        self._process()
        if self.closed is not None:return json.loads(json.dumps(self.closed))
        if self.service is None:
            report=dict(stopped=True,active=False,records=[],dropped_records=0,socket_removed=True)
        else:
            report=self.service.close()
            need(report['stopped'] is True and report['active'] is False and report['socket_removed'] is True,'Compiler handler still active')
        with (self.evidence/'compiler-closed.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
        self.closed=report;return json.loads(json.dumps(report))
    def validate_completion(self,bridge):
        self._process();need(self.compiler is not None and self.closed is not None,'Compiler completion after drain')
        receipts=[json.loads((self.evidence/'compiler-jobs'/name/'admitted.json').read_bytes()) for i,ok in enumerate(self.workload['outcomes'],1) if ok for name in ('%03d'%i,)]
        return completion(bridge,self.closed,self.compiler.count,receipts,self.workload)
