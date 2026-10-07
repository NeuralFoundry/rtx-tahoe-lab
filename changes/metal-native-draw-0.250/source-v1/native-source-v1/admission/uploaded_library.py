"""Immutable compiler-reviewed library, independent of entry names or catalog order.

Structural SPIR-V lowering is reproduced; cubin resources and parameter ABI are
audited. Producer validation/assembly evidence and source hashes belong to the
reviewed manifest. These checks do not authenticate arbitrary SASS semantics.
"""
from dataclasses import dataclass,field
from pathlib import Path,PurePosixPath
import hashlib,json,re,struct
from uploaded_compiler.spirv_ptx import translate
from uploaded_compiler.cubin_audit import audit
from air_math_policy import require_reviewed_math

def need(ok,message):
    if not ok:raise ValueError(message)
def integer(v,name,low,high):need(type(v) is int and low<=v<=high,name);return v
def sha(b):return hashlib.sha256(b).hexdigest()
def pairs(items):
    out={}
    for key,value in items:need(key not in out,'duplicate JSON key');out[key]=value
    return out
def parse(raw):
    need(type(raw) is bytes and 0<len(raw)<=131072,'JSON bytes/size')
    return json.loads(raw,object_pairs_hook=pairs)

@dataclass(frozen=True)
class Program:
    name:str
    assembly:bytes
    ptx:bytes
    cubin:bytes
    lowering:bytes
    air:bytes=b''
    code:bytes=field(init=False,repr=False)
    registers:int=field(init=False)
    constant_bytes:int=field(init=False)
    local_size:tuple=field(init=False)
    bindings:tuple=field(init=False)
    reads:tuple=field(init=False)
    writes:tuple=field(init=False)
    def __post_init__(self):
        need(type(self.name) is str and re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]{0,127}',self.name),'function name')
        for b in (self.assembly,self.ptx,self.cubin,self.lowering):need(type(b) is bytes and 0<len(b)<=131072,'compiler input size/type')
        observed=parse(self.lowering)
        need(type(observed) is dict, 'lowering metadata object')
        rounded=observed.get('float32_rounding',False)
        need(type(rounded) is bool and ('float32_rounding' not in observed or rounded is True), 'float rounding flag')
        fused=observed.get('float32_fma',False)
        need(type(fused) is bool and ('float32_fma' not in observed or fused is True), 'float fused flag')
        vectors=observed.get('local_vector_values',False)
        need(type(vectors) is bool and ('local_vector_values' not in observed or vectors is True), 'local vector flag')
        common_flow={'control_flow_blocks','control_flow_edges','phi_nodes'}
        loop_flow={'loop_control_flow','loop_headers','loop_back_edges'}
        flow_keys=common_flow | loop_flow | {'acyclic_control_flow'}
        if flow_keys & set(observed):
            need(common_flow <= set(observed), 'complete flow metadata')
            blocks=integer(observed['control_flow_blocks'],'flow block count',2,128)
            integer(observed['control_flow_edges'],'flow edge count',blocks-1,2*blocks)
            integer(observed['phi_nodes'],'flow phi count',0,2048)
            if loop_flow & set(observed):
                need(loop_flow <= set(observed) and 'acyclic_control_flow' not in observed and observed['loop_control_flow'] is True, 'loop flow metadata')
                headers=integer(observed['loop_headers'],'loop header count',1,blocks-1)
                need(integer(observed['loop_back_edges'],'loop back edge count',1,blocks-1)==headers, 'one back edge per loop')
            else:
                need(observed.get('acyclic_control_flow') is True, 'acyclic flow metadata')
        floating=observed.get('float32_arithmetic',False)
        need(type(floating) is bool,'float arithmetic flag')
        need(not fused or floating, 'fused arithmetic flag relationship')
        need(not rounded or floating, 'rounding arithmetic flag relationship')
        comparisons=observed.get('float32_comparisons',False)
        selection=observed.get('float32_selection',False)
        need(type(comparisons) is bool and type(selection) is bool,'float comparison/selection flags')
        if floating or comparisons or selection or vectors:
            need(type(self.air) is bytes and 0<len(self.air)<=131072,'reviewed AIR required for FP32')
            air=self.air.decode('utf-8');require_reviewed_math(air);policy=observed.get('float32_denorm')
            need(policy=='flush','current reviewed AIR denormal policy')
            options=re.findall(r'^!air\.compile_options = !\{([^}]+)\}$',air,re.M)
            need(len(options)==1,'AIR compile options linkage')
            ids=options[0].split(', ');need(all(re.fullmatch(r'!\d+',i) for i in ids) and len(ids)==len(set(ids)),'AIR compile option IDs')
            options_text=[]
            for i in ids:
                definitions=re.findall(r'^'+re.escape(i)+r' = !\{!"([a-z_.]+)"\}$',air,re.M)
                need(len(definitions)==1,'AIR option definition');options_text+=definitions
            need('air.compile.denorms_disable' in options_text and 'air.compile.fast_math_disable' in options_text,'AIR arithmetic flags')
        else:
            need(self.air==b'','unexpected AIR on legacy integer program');policy='preserve'
        ptx,meta=translate(self.assembly.decode('utf-8'),fp32_denorm=policy)
        if 'float32_arithmetic' not in observed:
            need(not meta['float32_arithmetic'] and not meta.get('float32_comparisons',False) and not meta.get('float32_selection',False) and not meta.get('local_vector_values',False),'legacy metadata cannot admit FP32')
            meta={k:v for k,v in meta.items() if k not in ('float32_arithmetic','float32_denorm')}
        # Preserve original producer bytes/hashes. Windows text output uses
        # CRLF; only that line ending is normalized for structural reproduction.
        need(ptx==self.ptx.decode('utf-8').replace('\r\n','\n') and meta==observed,'lowering reproduction')
        need(meta['entry']=='rtx_entry' and meta['target']=='sm_86' and meta['required_dispatch']=='whole-workgroups','compiler target')
        local=tuple(meta['local_size']);bindings=tuple(meta['parameter_bindings']);reads=tuple(meta['read_bindings']);writes=tuple(meta['written_bindings'])
        need(len(local)==3 and 1<=local[0]<=64 and local[1:]==(1,1),'current dispatch profile')
        need(1<=len(bindings)<=8 and bindings==tuple(sorted(set(bindings))) and all(type(b) is int and 0<=b<32 for b in bindings),'binding shape')
        need(writes and set(reads+writes)<=set(bindings),'binding access')
        code,abi=audit(self.cubin,len(bindings),list(local))
        for key,value in dict(code=code,registers=abi['registers'],constant_bytes=abi['constant_bytes'],local_size=local,bindings=bindings,reads=reads,writes=writes).items():object.__setattr__(self,key,value)
    @property
    def read_mask(self):return sum(1<<b for b in self.reads)
    @property
    def write_mask(self):return sum(1<<b for b in self.writes)
    def provenance(self):return dict(name=self.name,assembly_sha256=sha(self.assembly),ptx_sha256=sha(self.ptx),cubin_sha256=sha(self.cubin),lowering_sha256=sha(self.lowering),code_sha256=sha(self.code),local_size=self.local_size,bindings=self.bindings,**({'air_sha256':sha(self.air)} if self.air else {}))

@dataclass(frozen=True)
class Catalog:
    programs:tuple
    library:bytes=field(init=False,repr=False)
    code:bytes=field(init=False,repr=False)
    offsets:tuple=field(init=False)
    def __post_init__(self):
        need(type(self.programs) is tuple and 1<=len(self.programs)<=4 and all(type(p) is Program for p in self.programs),'program catalog')
        need(len({p.name for p in self.programs})==len(self.programs),'duplicate function name')
        library=bytearray(512);code=bytearray(4096);cursor=0;offsets=[]
        struct.pack_into('<QIIIII',library,0,0x5254584c49423333,1,512,len(self.programs),0,0x86)
        for index,p in enumerate(self.programs):
            need(0<len(p.code)<=4096-cursor and len(p.code)%128==0,'code image capacity');offsets.append(cursor)
            at=64+index*112;struct.pack_into('<11I',library,at,index+1,cursor,len(p.code),p.registers,*p.local_size,len(p.bindings),p.read_mask,p.write_mask,p.constant_bytes)
            struct.pack_into('<'+'I'*len(p.bindings),library,at+48,*p.bindings);code[cursor:cursor+len(p.code)]=p.code;cursor=(cursor+len(p.code)+255)&~255
        need(cursor<=4096,'code padding capacity');struct.pack_into('<I',library,20,cursor)
        object.__setattr__(self,'library',bytes(library));object.__setattr__(self,'code',bytes(code));object.__setattr__(self,'offsets',tuple(offsets))
    def record(self,index):
        integer(index,'program',0,len(self.programs)-1);p=self.programs[index]
        return dict(name=p.name,index=index,offset=self.offsets[index],code_bytes=len(p.code),registers=p.registers,local_size=list(p.local_size),bindings=list(p.bindings),read_mask=p.read_mask,write_mask=p.write_mask,constant_bytes=p.constant_bytes)
    @property
    def digest(self):return sha(self.library+self.code)
    def describe(self):return dict(payload_sha256=self.digest,library_sha256=sha(self.library),code_sha256=sha(self.code),programs=[dict(self.record(i),**{'provenance':p.provenance()}) for i,p in enumerate(self.programs)])
    def verify(self,library,code):need(type(library) is bytes and type(code) is bytes and library==self.library and code==self.code,'selected compiler package changed')

def load(path,expected_sha256):
    path=Path(path).resolve();integer(path.stat().st_size,'manifest length',1,131072);raw=path.read_bytes()
    need(type(expected_sha256) is str and re.fullmatch('[0-9a-f]{64}',expected_sha256) and sha(raw)==expected_sha256,'reviewed manifest digest')
    manifest=parse(raw);need(type(manifest) is dict and set(manifest)=={'abi','programs','library_sha256','code_sha256'},'manifest schema')
    need(manifest['abi'] in (1,2) and type(manifest['abi']) is int and type(manifest['programs']) is list and 1<=len(manifest['programs'])<=4,'manifest version/programs')
    programs=[]
    for r in manifest['programs']:
        need(type(r) is dict and set(r)=={'name','files'},'program row');files=r['files']
        need(type(files) is dict and set(files)==({'assembly','ptx','cubin','lowering'} if manifest['abi']==1 else {'assembly','ptx','cubin','lowering','air'}),'compiler files');values={}
        for kind,entry in files.items():
            need(type(entry) is dict and set(entry)=={'path','bytes','sha256'} and type(entry['path']) is str,'compiler file record')
            rel=PurePosixPath(entry['path']);need(not rel.is_absolute() and '..' not in rel.parts and str(rel)==entry['path'] and '\\' not in str(rel) and ':' not in str(rel),'compiler path')
            source=path.parent/str(rel);need(not source.is_symlink() and source.resolve().is_relative_to(path.parent),'compiler path scope')
            integer(entry['bytes'],'compiler size',1,131072);need(source.stat().st_size==entry['bytes'],'compiler file size')
            b=source.read_bytes();need(sha(b)==entry['sha256'],'compiler file digest');values[kind]=b
        programs.append(Program(r['name'],**values))
    catalog=Catalog(tuple(programs));need(sha(catalog.library)==manifest['library_sha256'] and sha(catalog.code)==manifest['code_sha256'],'reconstructed library identity')
    return catalog
