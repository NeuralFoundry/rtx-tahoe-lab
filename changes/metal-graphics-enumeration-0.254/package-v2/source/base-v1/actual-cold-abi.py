"""Real compiled C ABI checks as non-root, without owner open/arm."""
import ctypes,json,os
from pathlib import Path
import native_metal_client as c
assert os.geteuid()!=0
release=c.native_binary_release.load()
for p,h in [(c.OWNER_PATH,release['owner_sha256']),(c.APP_PATH,release['app_sha256'])]:c.file_bytes(p,h,True)
owner=ctypes.CDLL(str(c.OWNER_PATH),mode=ctypes.RTLD_GLOBAL);app=ctypes.CDLL(str(c.APP_PATH));c.bind(owner,app)
before,_=c.read_info(owner,'owner',os.getpid());assert before['state']==0 and before['io_opens']==0
for serial in (0,1,2**64-1):
    values=(ctypes.c_uint64*1)(serial);out=ctypes.create_string_buffer(512);actual=ctypes.c_size_t(999)
    assert owner.rtx_native_call(77,values,1,None,0,out,512,ctypes.byref(actual))==0xe3600002 and actual.value==0
container=c.file_bytes(Path(__file__).with_name('selected.rtxlib'),c.CONTAINER_SHA);blob=ctypes.create_string_buffer(container,len(container))
assert app.rtx_standard_start(blob,len(container))==0xe3610002
request=ctypes.create_string_buffer(2112);output=ctypes.create_string_buffer(bytes([0xa5])*2048,2048)
assert app.rtx_standard_submit(request,2112,output,2048)==0xe3610002 and output.raw==bytes([0xa5])*2048
for n in [0,1,127,129,192]:
    row=ctypes.create_string_buffer(256);assert app.rtx_standard_info(row,n)==0xe3610001
assert app.rtx_standard_close()==0
closed,_=c.read_info(app,'app',os.getpid());after,_=c.read_info(owner,'owner',os.getpid())
assert before==after and after['open_attempts']==after['io_opens']==after['calls']==0 and closed['state']==3
print(json.dumps(dict(passed=True,owner=after,app=closed,actual_ctypes=True,native_opens=0,gpu_submissions=0)))
