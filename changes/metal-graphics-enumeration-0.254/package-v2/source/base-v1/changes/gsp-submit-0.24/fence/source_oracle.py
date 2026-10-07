import hashlib,json,pathlib,re,struct

def verify(root, reports):
    folder=root/'changes/gsp-submit-0.24/fence';ref=folder/'reference'
    sources=json.loads((ref/'manifest.json').read_text(encoding='utf-8-sig'))
    for source in sources:
        assert source['source'].startswith('https://raw.githubusercontent.com/NVIDIA/open-gpu-kernel-modules/570.144/')
        assert hashlib.sha256((ref/source['path']).read_bytes()).hexdigest()==source['sha256']
    vm=(ref/'dev_vm_tu102.h').read_text()
    def value(name):return int(re.search(r'^#define '+name+r'\s+(0x[0-9A-Fa-f]+)\s',vm,re.M)[1],16)
    base=int(re.search(r'^#define NV_VIRTUAL_FUNCTION_FULL_PHYS_OFFSET\s+0x[0-9A-F]+:(0x[0-9A-F]+)',vm,re.M)[1],16)
    address=base+value('NV_VIRTUAL_FUNCTION_DOORBELL')
    assert address==0xbb0090
    assert 'return kfifoUpdateUsermodeDoorbell_TU102' in (ref/'kernel_fifo_ga100.c').read_text()
    assert 'GPU_VREG_WR32(pGpu, NV_VIRTUAL_FUNCTION_DOORBELL, workSubmitToken)' in (ref/'kernel_fifo_tu102.c').read_text()
    assert 'g->sriovState.virtualRegPhysOffset + a, v)' in (ref/'g_gpu_access_nvoc.h').read_text()
    assert 'return DRF_BASE(NV_VIRTUAL_FUNCTION_FULL_PHYS_OFFSET)' in (ref/'kern_gpu_tu102.c').read_text()
    assert 'IS_VIRTUAL_WITH_SRIOV(pGpu)' in (ref/'kern_gpu_tu102.c').read_text()
    command=(reports/'command.bin').read_bytes();entry=(reports/'entry.bin').read_bytes()
    words=struct.unpack('<5I',command);item=struct.unpack('<Q',entry)[0]
    assert words==(0x20040004,0x10,0x20002000,0x30602401,0x1000002)
    assert (words[0]>>29)==1 and ((words[0]>>16)&8191)==4 and (words[0]&8191)*4==0x10
    assert (words[1]<<32)|words[2]==0x1020002000
    assert item&((1<<40)-1)==0x1020001000 and (item>>41)&1==1 and item>>42==5
    return dict(passed=True,source_files=len(sources),doorbell_bar0=address,command_bytes=len(command),entry_bytes=len(entry),
                hardware_accessed=False,compute_verified=False,metal_verified=False)

if __name__=='__main__':
    root=pathlib.Path.cwd();result=verify(root,root/'reports')
    (root/'reports/source-oracle.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
