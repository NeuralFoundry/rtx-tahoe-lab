"""Verify this CPU-only candidate and preserve cross-platform artifacts."""
from pathlib import Path
import datetime,hashlib,json,re
import shader_image as s
import vector_profile as v
ROOT=Path(__file__).resolve().parent

def verify_inputs():
    manifest=json.loads((ROOT/'source-manifest.json').read_text())
    if len({r['path'] for r in manifest})!=len(manifest):raise ValueError('Duplicate input')
    for row in manifest:
        p=ROOT/row['path']
        if p.is_symlink() or ROOT not in p.resolve().parents:raise ValueError('Input path escape')
        raw=p.read_bytes()
        if len(raw)!=row['bytes'] or hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError(row['path'])
    return manifest

def main():
    manifest=verify_inputs();out=ROOT/'mac'
    cubin=(ROOT/'windows/rtx_probe_vectoradd.cubin').read_bytes()
    if hashlib.sha256(cubin).hexdigest()!=s.CUBIN_SHA:raise ValueError('Compiler artifact hash')
    code,shader=s.extract(cubin)
    if code!=(ROOT/'windows/shader-code.bin').read_bytes():raise ValueError('Independent ELF extraction')
    sass=(ROOT/'windows/shader.sass').read_text()
    for text in ('S2R R6, SR_CTAID.X','S2R R3, SR_TID.X','ISETP.GE.U32.AND','c[0x0][0x178]','LDG.E R2, [R2.64]','LDG.E R5, [R4.64]','IADD3 R9, R2, R5, RZ','STG.E [R6.64], R9'):
        if text not in sass:raise ValueError('Compiler disassembly data flow')
    native=json.loads((out/'native.json').read_text());win=json.loads((ROOT/'windows/native.json').read_text())
    if native!=win or not native['passed'] or native['cases']!=896 or native['hardware_accessed']:raise ValueError('Native CPU comparison')
    log=(out/'python.log').read_text()
    if 'Ran 12 tests' not in log or not log.rstrip().endswith('OK'):raise ValueError('Python result')
    image,command,expected=v.build(code,*v.inputs(v.DEFAULT_SEED),61)
    for name,raw in (('image.bin',image),('qmd.bin',image[12288:12544]),('constant.bin',image[8192:12288]),('command.bin',command),('expected-output.bin',expected)):
        if (out/name).read_bytes()!=raw or (ROOT/'windows'/name).read_bytes()!=raw:raise ValueError(name)
    (out/'shader-code.bin').write_bytes(code)
    (out/'shader.json').write_text(json.dumps(shader,indent=2)+'\n')
    header='#pragma once\n#include <stdint.h>\nnamespace RtxVectorShader030 {\n// CPU-verified sm_86 vector candidate; not yet executed on GPU.\nconstexpr uint8_t code[512]={\n'
    header+=''.join(' '+','.join('0x%02x'%x for x in code[i:i+16])+',\n' for i in range(0,512,16))+'};\n}\n'
    (out/'ShaderImage.hpp').write_text(header)
    report=dict(recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=True,python_tests=12,cpp_programs=1,
        cpp_checks=native['checks'],cpp_cases=896,source_files=len(manifest),windows_mac_python_identical=True,
        shader=shader,grid=[2,1,1],block=[32,1,1],launched_threads=64,active_elements=61,inactive_elements=3,
        qmd_schema_fields=len(v.baseline.schema()),qmd_changed_fields=6,image_bytes=24576,
        native_integrated=False,kext_built=False,hardware_accessed=False,compute_verified=False,metal_verified=False)
    (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    files=[dict(path=p.name,bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(out.iterdir()) if p.is_file() and p.name!='artifact-manifest.json']
    (out/'artifact-manifest.json').write_text(json.dumps(files,indent=2)+'\n')
    print(json.dumps(report))

if __name__=='__main__':main()
