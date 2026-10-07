from pathlib import Path
import subprocess, json, hashlib, datetime, struct

root=Path.cwd()
component=root/'changes/gsp-application-runtime-0.32/runtime'
out=root/'application-runtime-mac'
assert not out.exists()
out.mkdir()
inputs=json.loads((root/'application-runtime-inputs.json').read_text())
def verify_inputs():
    seen=set()
    for row in inputs:
        p=root/row['path']
        assert p.resolve().is_relative_to(root.resolve()) and p.is_file() and not p.is_symlink() and row['path'] not in seen
        seen.add(row['path'])
        assert p.stat().st_size==row['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256']
verify_inputs()
def run(args,name):
    with (out/name).open('w') as log:
        subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)
for name,source in [('runtime','test_runtime_native.cpp'),('owner','test_runtime_owner.cpp')]:
    run(['xcrun','clang++','-O2','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(component/source),'-o',str(out/name)],name+'-compile.log')
evidence='changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/'
run([str(out/'runtime'),evidence+'record-011.bin',evidence+'record-010.bin',str(out)],'native.json')
run([str(out/'owner')],'owner.txt')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
version=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-version'],text=True).strip()
run(['xcrun','clang++','-arch','x86_64','-std=c++14','-mkernel','-DKERNEL','-DKERNEL_EXTENSION','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-common','-fno-stack-protector','-mno-red-zone','-mmacosx-version-min='+version,'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-Wframe-larger-than=4096','-fstack-usage','-isysroot',sdk,'-isystem',sdk+'/System/Library/Frameworks/Kernel.framework/Headers','-c',str(component/'kernel-smoke.cpp'),'-o',str(out/'runtime-kernel.o')],'kernel-compile.log')
stacks=[]
for p in out.glob('*.su'):
    for line in p.read_text().splitlines():
        fields=line.split('\t');assert len(fields)>=3 and fields[1].isdigit();stacks.append(int(fields[1]))
assert stacks and max(stacks)<=4096
native=json.loads((out/'native.json').read_text())
assert native==json.loads((component/'windows/native.json').read_text())
assert (out/'owner.txt').read_bytes()==(component/'windows/owner.txt').read_bytes().replace(b'\r\n',b'\n')
baseline=(root/'changes/gsp-batch-compute-0.31/windows/image.bin').read_bytes()
assert hashlib.sha256(baseline).hexdigest()=='0e1ec056c7a0f4e5d098fafbc511ff4222fdd2783c50203fad243f577f187e56'
counts=[3,17,47,64];mask=2**32-1
def p32(b,o,v):struct.pack_into('<I',b,o,v&mask)
def p64(b,o,v):struct.pack_into('<Q',b,o,v)
def values(j,n):return [((0xfffffff0+i*0x1234567)&mask)^j for i in range(n)]+[0]*(64-n),[(i*0x1020304+j+16)&mask for i in range(n)]+[0]*(64-n)
def expected(completed):
    image=bytearray(baseline)
    for j in range(4):
        n=counts[j] if j<completed else 0;a,b=values(j,n);cb=8192+j*1024
        image[cb:cb+1024]=bytes(1024)
        for offset,value in [(0x28,0xfffdc0),(0x160,0x1020008000+j*1024),(0x168,0x1020006000+j*1024+512),(0x170,0x1020006000+j*1024+768)]:p64(image,cb+offset,value)
        p32(image,cb+0x178,n)
        for i in range(64):
            p32(image,cb+512+i*4,a[i]);p32(image,cb+768+i*4,b[i]);p32(image,16384+j*1024+i*4,~(a[i]+b[i]))
        p32(image,12288+j*256+104,0x306032f0+j);p32(image,20480+j*256,0)
    return image
checked=0
def compare(name,data):
    global checked
    assert (out/name).read_bytes()==bytes(data)==(component/'windows'/name).read_bytes(),name
    checked+=1
compare('initial.bin',expected(0))
for j in range(4):
    a,b=values(j,counts[j]);wire=bytearray(576)
    for o,v in [(0,0x5254584a4f423332),(16,7),(24,j+1)]:p64(wire,o,v)
    for o,v in [(8,1),(12,576),(32,counts[j]),(36,1)]:p32(wire,o,v)
    for i in range(64):p32(wire,64+i*4,a[i]);p32(wire,320+i*4,b[i])
    compare('request-'+str(j)+'.bin',wire)
    image=expected(j+1);compare('expected-prefix-'+str(j+1)+'.bin',image)
    compare('plan-'+str(j)+'.bin',image[8192+j*1024:8192+(j+1)*1024]+image[16384+j*1024:16640+j*1024])
    for prior in range(j+1):
        x,y=values(prior,counts[prior])
        for i in range(counts[prior]):p32(image,16384+prior*1024+i*4,x[i]+y[i])
        p32(image,20480+prior*256,0x306032f0+prior)
    compare('simulated-prefix-'+str(j+1)+'.bin',image)
assert checked==17
verify_inputs()
report=dict(passed=True,recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),source_files=len(inputs),maximum_kernel_stack_frame=max(stacks),binary_artifacts=checked,windows_mac_python_identical=True,cpp_sanitizers=['address','undefined'],native_adapter_compiled=True,driver_entry_integrated=False,kext_built=False,hardware_accessed=False,metal_verified=False,native=native)
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
artifacts=[dict(path=p.name,bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(out.iterdir()) if p.is_file()]
(out/'artifact-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
print(json.dumps(report))
