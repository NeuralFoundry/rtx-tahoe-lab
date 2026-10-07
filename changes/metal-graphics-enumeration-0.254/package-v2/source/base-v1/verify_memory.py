"""Independent byte/PTE oracle for CPU-only ProgramMemory staging evidence."""
from pathlib import Path
import hashlib,json,struct,sys

RAW=('root.bin','children-before.bin','children-after.bin','image.bin','backing.bin','command.bin','writes.bin')
NATIVE=dict(passed=True,scenarios=387,checks=73498,rejected=388,operations=91,clocks=95,
            memory_writes=18,register_writes=3,links=6,backing_bytes=24576,child_bytes=40960,
            gpu_commands_submitted=False,cpu_simulation_only=True)


def verify_bytes(root,outputs):
    root=Path(root)
    actual=(root/'bootstrap-device.bin').read_bytes()
    assert len(actual)==36864 and hashlib.sha256(actual).hexdigest()=='7506628a4b191b790f544dd5af8d90ed141c9e33b4217cd8c5b8f25e3841bb57'
    initial=bytearray(actual[12288:]);initial[20480:20512]=bytes(32)
    assert initial[:4096]==(root/'code.bin').read_bytes()
    for i in range(4096,24576):
        value=0 if 8192<=i<16384 or 20480<=i<20512 or (20480<=i<21504 and (i-20480)%256<4) else (0x5a if i>=20480 else 0xa5)^((i*13+7)&255)
        assert initial[i]==value
    initial=bytes(initial)
    expected_root=bytearray(12288)
    for off,value in ((0,0x100322),(4096,0x100422),(8192+128*8,0x1122334455667788),(8192+129*8,0x100522)):
        struct.pack_into('<Q',expected_root,off,value)
    reference=root/'changes/gsp-compute-0.25/memory/reference'
    assert bytes(expected_root)==(reference/'execution-root.bin').read_bytes()
    before=(reference/'execution-children.bin').read_bytes()
    assert len(before)==40960 and hashlib.sha256(before).hexdigest()=='53dbd81abe4d6444c7dfab59e207c391a0cea37fce31a47d37e87745a1f9b346'
    after=bytearray(before)
    addresses=[0x03409000+i*4096 for i in range(6)]
    for i in range(6):
        off=4096+(4+i)*8
        assert before[off:off+8]==bytes(8)
        struct.pack_into('<Q',after,off,(6<<56)|((0x03409000+i*4096)>>4)|1)
        addresses.extend((0x1005000+off+4,0x1005000+off))
    expected={'root.bin':bytes(expected_root),'children-before.bin':before,'children-after.bin':bytes(after),
              'image.bin':initial,'backing.bin':initial,'command.bin':bytes(32),'writes.bin':struct.pack('<18I',*addresses)}
    for name,value in expected.items():
        assert outputs[name]==value,name
    # Decode the captured hierarchy, checking both page edges; compare all other
    # table bytes above so no existing mapping can silently change.
    child=outputs['children-after.bin'];edges=0
    for page in range(6):
        va=0x1020004000+page*4096
        lo,hi=struct.unpack_from('<QQ',child,((va-0x1020000000)>>21)*16)
        assert lo==0x20 and hi&255==2 and hi>>33==0
        table=((hi&0x1ffffff00)<<4)-0x1005000
        assert 4096<=table<=len(child)-4096
        pte=struct.unpack_from('<Q',child,table+((va>>12)&511)*8)[0]
        assert pte>>56==6 and pte&255==1 and not pte&~((6<<56)|0x1ffffff00|1)
        for edge in (0,4095):
            assert ((pte&0x1ffffff00)<<4)+edge==0x03409000+page*4096+edge
            edges+=1
    return dict(passed=True,raw_files=len(RAW),pte_entries=6,translation_edges=edges,
                root_unchanged=True,other_table_bytes_unchanged=True,backing_bytes=24576,
                compiler_code_immutable=True,shader_command_empty=True,cpu_simulation_only=True,
                gpu_commands_submitted=False,metal_verified=False,new_initial_fence_bytes=32)


def verify(root,out):
    out=Path(out)
    return verify_bytes(root,{name:(out/name).read_bytes() for name in RAW})


if __name__=='__main__':
    assert len(sys.argv)==2
    print(json.dumps(verify(Path(__file__).resolve().parent,Path(sys.argv[1]))))
