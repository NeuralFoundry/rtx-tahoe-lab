"""Read-only failure capture on the existing native owner; never admission."""
from pathlib import Path
import ctypes,hashlib,json,struct
FIELDS=('magic abi generation begun phase decoded inventory_error rows leaves child_bytes '
 'arena_phase root used_tables pool_bytes capacity reserved15 reserved16 initial_ack journal_retained post_checked admitted '
 'initial_legacy_rows context_legacy_rows final_legacy_rows external_tx_writer '
 'reserved25 reserved26 reserved27 reserved28 reserved29 reserved30 reserved31 '
 'context_invalidate_passed context_invalidate_failure context_invalidate_attempted context_publication_phase '
 'program_invalidate_passed program_invalidate_failure program_invalidate_attempted program_publication_phase '
 'rpc_failure rpc_attempted record_count record_bytes initial_reader initial_sequence '
 'rx_reader rx_producer rx_sequence last_function last_result tx_reader sent completed doorbells '
 'legacy_rows data_buffers data_pages logical_bytes runtime_admitted public_generation data_begin data_end reserved63').split()
assert len(FIELDS)==64
LIMITS=(4096,131072,262144,256840,12288,45056,576,64)
def shape(selector,scalars,size):
    if type(selector)is not int or type(scalars)is not tuple or type(size)is not int or any(type(v)is not int or not 0<=v<1<<64 for v in scalars):raise ValueError('Diagnostic call types')
    if selector==85 and scalars==()and size==512:return
    if selector==87 and len(scalars)==1 and scalars[0]<64 and size==24:return
    if selector==86 and len(scalars)==3:
        part,off,n=scalars
        if part<8 and n==size and 0<n<=4096 and off<=LIMITS[part]and n<=LIMITS[part]-off:return
    raise ValueError('Read-only root diagnostic shape')
def native_read(owner,selector,scalars,size):
    shape(selector,scalars,size);args=(ctypes.c_uint64*len(scalars))(*scalars)if scalars else None
    output=ctypes.create_string_buffer(size);actual=ctypes.c_size_t()
    code=owner.rtx_native_call(selector,args,len(scalars),None,0,output,size,ctypes.byref(actual))
    if code or actual.value!=size:raise ValueError('Diagnostic selector %d: 0x%08x, bytes %d'%(selector,code&0xffffffff,actual.value))
    return output.raw
def decode(raw,generation):
    if type(raw)is not bytes or len(raw)!=512 or type(generation)is not int or not 0<generation<1<<63:raise ValueError('Root diagnostic size/generation')
    q=struct.unpack('<64Q',raw)
    if q[:3]!=(0x525458524f4f5430,242,generation)or q[4]>7 or q[10]>7 or any(q[i]for i in(15,16,25,26,27,28,29,30,31,63)):raise ValueError('Root diagnostic identity/reserved/phase')
    if any(q[i]>1 for i in(3,5,17,18,19,20,32,34,36,38,41,59)):raise ValueError('Root diagnostic flag')
    if q[7]>6433 or q[8]>10 or q[9]>45056 or q[9]%4096 or q[12]>64 or q[13]>262144 or q[13]%4096 or q[14]>64 or q[12]>q[14]or q[13]!=q[14]*4096:raise ValueError('Root diagnostic table extent')
    if q[42]>16 or q[43]>131072 or q[43]%4096 or any(q[i]>5120 for i in(21,22,23,55))or q[35]>4 or q[39]>4:raise ValueError('Root diagnostic journal/stage extent')
    if q[56]>9 or q[57]>1313 or q[58]>5349717 or q[60]not in(0,generation):raise ValueError('Root diagnostic data extent')
    return dict(zip(FIELDS,q))
def capture(read,folder,generation):
    folder=Path(folder);folder.mkdir(mode=0o700);files={};errors={};calls=0;before=None;info=None
    def save(name,raw):
        with(folder/name).open('xb')as f:f.write(raw)
        files[name]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
    def call(selector,scalars,size):
        nonlocal calls
        shape(selector,scalars,size);calls+=1
        if calls>320:raise ValueError('Diagnostic call bound')
        raw=read(selector,scalars,size)
        if type(raw)is not bytes or len(raw)!=size:raise ValueError('Diagnostic short read')
        return raw
    def part(part,name,n):
        try:save(name,b''.join(call(86,(part,off,min(4096,n-off)),min(4096,n-off))for off in range(0,n,4096)))
        except Exception as error:errors[name]=str(error)
    out=dict(captured=False,stable=False,admission_granted=False,gpu_work_submitted=False)
    try:
        before=call(85,(),512);save('info-before.bin',before);info=decode(before,generation);out['info']=info
        # A failed early allocation may not have reached SET yet. Preserve its
        # unavailable part explicitly and continue collecting the other parts.
        if info['rpc_attempted']:part(0,'request.bin',4096)
        if info['record_bytes']:part(1,'records.bin',info['record_bytes'])
        if info['decoded']:
            part(4,'source-root.bin',12288)
            if info['child_bytes']:part(5,'source-children.bin',info['child_bytes'])
        if info['rows']:part(3,'rows.bin',info['rows']*40)
        if info['capacity']:
            try:save('table-pages.bin',b''.join(call(87,(i,),24)for i in range(info['capacity'])))
            except Exception as error:errors['table-pages.bin']=str(error)
            if info['pool_bytes']:part(2,'tree.bin',info['pool_bytes'])
            part(7,'digests.bin',64)
        if info['admitted']and info['runtime_admitted']:part(6,'handles.bin',576)
    except Exception as error:errors['before']=str(error)
    try:
        after=call(85,(),512);save('info-after.bin',after);decode(after,generation)
        if before!=after:raise ValueError('Root changed during diagnostic capture')
        out['stable']=True
    except Exception as error:errors['after']=str(error)
    out.update(captured=bool(info is not None and out['stable']and not errors),calls=calls,files=files,errors=errors)
    with(folder/'diagnostics.json').open('x')as f:json.dump(out,f,indent=2);f.write('\n')
    return out
