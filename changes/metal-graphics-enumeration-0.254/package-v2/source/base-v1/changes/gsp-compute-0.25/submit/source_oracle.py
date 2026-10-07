import hashlib,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def entry():
    text=(ROOT/'reference/clc56f.h').read_text()
    def low(name):return int(re.search(r'^#define '+name+r'\s+\d+:(\d+)\s*$',text,re.M)[1])
    def value(name):return int(re.search(r'^#define '+name+r'\s+(0x[0-9A-Fa-f]+)\s*$',text,re.M)[1],16)
    address=0x1020001040
    lo=(address>>low('NVC56F_GP_ENTRY0_GET'))<<low('NVC56F_GP_ENTRY0_GET')
    hi=(address>>32)|(value('NVC56F_GP_ENTRY1_LEVEL_SUBROUTINE')<<low('NVC56F_GP_ENTRY1_LEVEL'))|(8<<low('NVC56F_GP_ENTRY1_LENGTH'))
    return struct.pack('<II',lo&0xffffffff,hi)

def verify():
    ref=ROOT/'reference';rows=json.loads((ref/'manifest.json').read_text())
    assert len(rows)==8
    for row in rows:
        assert row['source'].startswith(('https://raw.githubusercontent.com/NVIDIA/open-gpu-kernel-modules/570.144/',
          'https://raw.githubusercontent.com/tinygrad/tinygrad/33cd373ad35371ccb483c9645d0c0637a04debc2/'))
        assert hashlib.sha256((ref/row['path']).read_bytes()).hexdigest()==row['sha256']
    vm=(ref/'dev_vm_tu102.h').read_text()
    base=int(re.search(r'^#define NV_VIRTUAL_FUNCTION_FULL_PHYS_OFFSET\s+0x[0-9A-F]+:(0x[0-9A-F]+)',vm,re.M)[1],16)
    offset=int(re.search(r'^#define NV_VIRTUAL_FUNCTION_DOORBELL\s+(0x[0-9A-Fa-f]+)\s',vm,re.M)[1],16)
    assert base+offset==0xbb0090
    assert 'return kfifoUpdateUsermodeDoorbell_TU102' in (ref/'kernel_fifo_ga100.c').read_text()
    assert 'GPU_VREG_WR32(pGpu, NV_VIRTUAL_FUNCTION_DOORBELL, workSubmitToken)' in (ref/'kernel_fifo_tu102.c').read_text()
    assert 'return DRF_BASE(NV_VIRTUAL_FUNCTION_FULL_PHYS_OFFSET)' in (ref/'kern_gpu_tu102.c').read_text()
    assert 'g->sriovState.virtualRegPhysOffset + a, v)' in (ref/'g_gpu_access_nvoc.h').read_text()
    tiny=(ref/'tinygrad-ops_nv.py').read_text()
    for text in ('(cmdq_addr//4 << 2) | (len(self._q) << 42) | (1 << 41)',
      'gpfifo.gpput[0] = (gpfifo.put_value + 1) % gpfifo.entries_count','dev.gpu_mmio[0x90 // 4] = gpfifo.token'):
        assert text in tiny
    return dict(passed=True,reference_files=8,doorbell_bar0=base+offset,entry_bytes=entry().hex(),hardware_accessed=False)
