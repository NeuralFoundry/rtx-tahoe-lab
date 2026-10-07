"""Real publication253 primitive and graphics readiness lifetime.

Default observations are actual IOKit registry reads. A timed-out property
process is retained and prevents withdrawal/retirement until it has exited.
No synchronous Hello is sent from a native208 callback on its own serial queue.
"""
from pathlib import Path
import hashlib,json,os,stat,subprocess
import machine253
from ready253 import need,spec_valid,positive,protected_file,protected_directory,ReadyFile,make_ready

class PropertyCommand:
    def __init__(self,binary,expected_sha,evidence,binding,session):
        self.binary=Path(binary);self.expected_sha=expected_sha;self.evidence=Path(evidence);self.binding=dict(binding);self.session=positive(session,'Publication token');self.pending=None;self.ordinal=0
        protected_directory(self.evidence)
        self._verify()
    def _verify(self):
        raw,_=protected_file(self.binary,1024*1024)
        need(hashlib.sha256(raw).hexdigest()==self.expected_sha,'Reviewed publication primitive')
    def settled(self):
        if self.pending is None:return True
        if self.pending.poll() is None:return False
        out,err=self.pending.communicate();self._record(out,err,self.pending.returncode);self.pending=None;return True
    def _record(self,out,err,code):
        stem=self.evidence/('publication-%03d'%self.ordinal)
        for suffix,data in [('.stdout',out),('.stderr',err),('.json',(json.dumps(dict(argv=self.pending.args,returncode=code))+'\n').encode())]:
            with Path(str(stem)+suffix).open('xb') as f:f.write(data)
    def run(self,operation,epoch):
        need(operation in ('publish','withdraw') and self.settled(),'Publication command still pending')
        positive(epoch,'Publication epoch');self._verify();self.ordinal+=1
        argv=[str(self.binary),operation,str(self.binding['child_registry']),str(self.binding['generation']),str(epoch),str(self.session)]
        with(self.evidence/('publication-%03d-attempt.json'%self.ordinal)).open('x')as f:json.dump(dict(argv=argv),f)
        self.pending=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:out,err=self.pending.communicate(timeout=25)
        except subprocess.TimeoutExpired:
            # Do not convert missing output into proof that a property write ended.
            raise RuntimeError('Publication command not yet settled')
        code=self.pending.returncode;self._record(out,err,code);self.pending=None
        need(code==0,'Publication primitive returned failure')
        report=json.loads(out)
        expected=dict(operation=operation,child=self.binding['child_registry'],parent=self.binding['generation'],epoch=epoch,session=self.session,property_returncode=0,verified=True,actual_iokit=True,gpu_jobs=0,standard_metal_enumeration=False)
        need(set(report)==set(expected) and all(type(report[k]) is type(v) and report[k]==v for k,v in expected.items()),'Publication primitive receipt')
        return report

class Publication:
    def __init__(self,spec,binding,session,command,observe=machine253.observe):
        self.spec=dict(spec);spec_valid(self.spec);self.binding=dict(binding);self.session=positive(session,'Publication token');self.command=command;self.observe=observe;self.pid=os.getpid()
        self.file=ReadyFile(spec['ready_path']);self.epoch=None;self.ready_attempted=False;self.withdrawn=False;self.records=[]
    def activate(self):
        need(os.getpid()==self.pid and os.geteuid()==0 and not self.ready_attempted,'One root activation');self.ready_attempted=True
        before=self.observe();epoch,memory=machine253.require_ready(before,self.binding);self.epoch=epoch
        ready=make_ready(self.spec,self.binding,epoch,self.session,memory,self.pid)
        digest=self.file.publish(ready)
        # Recheck native readiness immediately before the property call.
        current_epoch,current_memory=machine253.require_ready(self.observe(),self.binding)
        need((current_epoch,current_memory)==(epoch,memory),'Native readiness changed before graphics publication')
        receipt=self.command.run('publish',epoch)
        self.records.append(dict(kind='published',ready_sha256=digest,property=receipt))
    def withdraw(self):
        need(os.getpid()==self.pid and os.geteuid()==0,'Original publication owner')
        if self.withdrawn:return
        need(self.command.settled(),'Late publication remains possible')
        value=self.observe();_,child,_=machine253.require_loaded(value,self.binding)
        if any(k in child for k in machine253.PUBLISHED):
            need(self.epoch is not None,'No owned publication epoch')
            machine253.fields(child,dict(RTXMetalGPUReady=True,MetalPluginName='RTXMetalGraphics253-normal',MetalPluginClassName='RTXMetalApplicationDevice209',RTXMetalPublicationEpoch=self.epoch,RTXMetalPublicationSession=self.session))
            receipt=self.command.run('withdraw',self.epoch);self.records.append(dict(kind='withdrawn',property=receipt))
        machine253.require_hidden(self.observe(),self.binding)
        self.file.withdraw()
        self.records.append(dict(kind='publication_closed',ready_removed=True));self.withdrawn=True
