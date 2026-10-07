"""The graphics253 JSON producer and owned readiness-file lifetime.

Producing JSON is not hardware admission. The runtime must call require_ready
on an actual paired-machine observation before exposing this record.
"""
from pathlib import Path
import base64, hashlib, json, os, re, stat, struct

SERVICE = 'local.emre.RTXGraphicsBroker251.publication253.v1'
CATALOG = '321e8b29e64200ca09a1e193fd4e5b995933fa4222713a6e43a0d942c89412bc'

def need(ok, message):
    if not ok: raise ValueError(message)

def positive(value, name, maximum=(1 << 63)-1):
    need(type(value) is int and 0 < value <= maximum, name)
    return value

def memory_valid(raw, generation):
    need(type(raw) is bytes and len(raw) == 128, 'Memory evidence extent')
    positive(generation, 'Memory generation')
    need(struct.unpack_from('<4I', raw) == (0x4d585452,1,128,1), 'Memory ABI')
    need(struct.unpack_from('<Q', raw,16)[0] == generation, 'Memory generation binding')
    need(struct.unpack_from('<6I',raw,24) == (0x252010de,0x104c1043,6144,6144,2,1), 'Memory identity/readings')
    need(struct.unpack_from('<Q',raw,48)[0] == 6144 << 20, 'Memory capacity')
    need(struct.unpack_from('<12I',raw,56) == (1,1,2,0xb76000a1,0xb76000a1,2,0,2,0,1,0x100,0x1183a4), 'Memory provenance')
    need(raw[104:] == bytes(24), 'Memory reserved bytes')
    return raw

def path_value(value):
    need(type(value) is str and '\x00' not in value, 'Path string')
    p=Path(value)
    need(p.is_absolute() and str(p)==value and '..' not in p.parts, 'Absolute normalized path')
    return p

def spec_valid(spec):
    need(type(spec) is dict and set(spec)=={'abi','service','application_version','ready_path','container_sha256'},'Graphics specification schema')
    need(type(spec['abi']) is int and spec['abi']==253 and spec['service']==SERVICE and spec['application_version']=='0.253.0','Graphics application identity')
    need(spec['container_sha256']==CATALOG,'Reviewed graphics catalog')
    path_value(spec['ready_path'])


def make_ready(spec, binding, epoch, session, memory, root_pid):
    spec_valid(spec)
    need(type(binding) is dict and set(binding)=={'boot_uuid','generation','child_registry'}, 'Paired binding schema')
    boot=binding['boot_uuid'];need(type(boot) is str and re.fullmatch(r'[0-9A-F]{8}(?:-[0-9A-F]{4}){3}-[0-9A-F]{12}',boot) is not None,'Boot UUID')
    generation=positive(binding['generation'],'Parent generation');child=positive(binding['child_registry'],'Child registry')
    need(child!=generation,'Distinct parent and child')
    positive(epoch,'Program epoch');positive(session,'Publication session');positive(root_pid,'Root PID',0x7fffffff)
    memory_valid(memory,generation)
    port=dict(abi=4,root_abi=242,host_buffer_abi=2,owned_data_abi=181,owned_dispatch_abi=183,owned_graphics_abi=242,publication_abi=3,owned_root_verified=True,child_registry=child,probe_version='0.83.1',accelerator_version='0.253.0',application_version='0.253.0',program_epoch=epoch,publication_session=session,memory_base64=base64.b64encode(memory).decode())
    return dict(abi=253,protocol=251,service=SERVICE,allowed_uid=501,generation=generation,root_pid=root_pid,boot_uuid=boot,container_sha256=CATALOG,port_binding=port)

def encoded(value): return (json.dumps(value,sort_keys=True,separators=(',',':'))+'\n').encode()

def protected_directory(path, mode=None):
    p=path_value(str(path));s=p.lstat()
    need(p.resolve()==p and stat.S_ISDIR(s.st_mode) and s.st_uid==0 and not s.st_mode&0o022,'Protected canonical root directory')
    if mode is not None: need(stat.S_IMODE(s.st_mode)==mode,'Directory mode')
    return p

def protected_file(path, maximum):
    p=path_value(str(path));protected_directory(p.parent)
    need(p.resolve()==p,'Canonical protected file')
    fd=os.open(p,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        before=os.fstat(fd)
        need(stat.S_ISREG(before.st_mode) and before.st_uid==0 and not before.st_mode&0o022 and 0<before.st_size<=maximum,'Protected bounded file')
        raw=b''
        while len(raw)<before.st_size:
            part=os.read(fd,before.st_size-len(raw));need(part,'Complete protected file');raw+=part
        after=os.fstat(fd)
        fields=('st_dev','st_ino','st_mode','st_uid','st_size','st_mtime_ns','st_ctime_ns')
        need(all(getattr(before,k)==getattr(after,k) for k in fields),'Protected file changed')
        return raw,before
    finally: os.close(fd)

class ReadyFile:
    """A single exclusive publication; removal requires our original inode/data."""
    def __init__(self, path):
        need(os.geteuid()==0,'Root readiness owner');self.path=path_value(str(path));self.pid=os.getpid();self.identity=None;self.raw=None;self.attempted=False;self.removed=False
        protected_directory(self.path.parent,0o755)
    def _process(self): need(os.geteuid()==0 and os.getpid()==self.pid,'Same readiness owner')
    def publish(self, value):
        self._process();need(not self.attempted,'Readiness publishes once');self.attempted=True
        raw=encoded(value);need(0<len(raw)<=65536,'Readiness extent')
        temporary=self.path.with_name('.'+self.path.name+'.'+str(self.pid)+'.pending')
        fd=os.open(temporary,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o644)
        try:
            os.fchmod(fd,0o644)
            with os.fdopen(fd,'wb',closefd=False) as f:f.write(raw);f.flush();os.fsync(fd)
            # link is an atomic, no-replace publication on this same filesystem.
            os.link(temporary,self.path,follow_symlinks=False)
            self.identity=(os.fstat(fd).st_dev,os.fstat(fd).st_ino);self.raw=raw
        finally:
            os.close(fd);temporary.unlink()
        actual,s=protected_file(self.path,65536)
        need(actual==raw and (s.st_dev,s.st_ino)==self.identity,'Readiness publication identity')
        return hashlib.sha256(raw).hexdigest()
    def withdraw(self):
        self._process()
        if self.removed:return
        if self.identity is None:
            # Do not delete a pre-existing record that this owner did not create.
            need(not self.path.exists() and not self.path.is_symlink(),'Unowned readiness record remains')
        else:
            raw,s=protected_file(self.path,65536)
            need(raw==self.raw and (s.st_dev,s.st_ino)==self.identity,'Readiness identity changed before withdrawal')
            self.path.unlink()
        self.removed=True
