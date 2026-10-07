from pathlib import Path
import ctypes,struct,unittest
import gsp_application_client as client
import gsp_application_native as native
import application_request_codec as codec
FIXTURE=Path(__file__).resolve().parent/'changes/gsp-application-runtime-0.32/entry/windows'
def raw(name):return (FIXTURE/name).read_bytes()
def changed(data,index,value):
    result=bytearray(data);struct.pack_into('<Q',result,index*8,value);return bytes(result)
class ABI(unittest.TestCase):
    def test_info(self):
        self.assertEqual(native.info(raw('runtime-before.bin'),7)['completed'],0)
        self.assertEqual(native.info(raw('runtime-after.bin'),7)['completed'],4)
        for key,value in [('magic',0),('abi',2),('generation',8),('phase',6),('capacity',5),('request_bytes',575),('owner_phase',17),('pinned',0),('ready',0),('reserved63',1)]:
            with self.subTest(key=key),self.assertRaises(ValueError):native.info(changed(raw('runtime-before.bin'),native.INFO_FIELDS.index(key),value),7)
    def test_all_cpp_artifacts(self):
        self.assertTrue(native.memory(raw('memory-info.bin'),7)['passed']);history=[]
        for j in range(4):
            prefix='job-'+str(j);wire=raw(prefix+'-request.bin');history.append(wire)
            request=codec.decode(wire);job=native.job(raw(prefix+'-info.bin'),7,j);submit=native.submit(raw(prefix+'-submit.bin'),7,j,wire)
            self.assertEqual(job['count'],len(request['a']));self.assertTrue(job['restored'] and job['stage_passed'] and job['capture_complete'] and submit['passed'])
            canonical=native.canonical(history,7);plan=canonical[8192+j*1024:8192+(j+1)*1024]+canonical[16384+j*1024:16640+j*1024]
            self.assertEqual(raw(prefix+'-plan.bin'),plan)
            result=native.verify_capture(*[raw(prefix+'-'+name+'.bin') for name in ('root','children','device')],raw('before-root.bin'),raw('before-children.bin'),history,7)
            self.assertTrue(result['passed']);self.assertFalse(result['hardware_accessed']);self.assertFalse(result['metal_verified'])
    def test_job_corruptions(self):
        data=raw('job-0-info.bin')
        for key,value in [('magic',0),('generation',8),('job',1),('count',65),('window_saved',0),('window_after',0),('cleanup_failure',1),('window_request_id',2),('stage_reads',16),('stage_writes',1),('committed',0),('device_bytes',32768),('capture_reads',21),('capture_failure',1),('reserved127',1)]:
            with self.subTest(key=key),self.assertRaises(ValueError):native.job(changed(data,native.JOB_FIELDS.index(key),value),7,0)
    def test_submit_corruptions(self):
        data=raw('job-0-submit.bin');wire=raw('job-0-request.bin');fields=native.batch.SUBMIT_FIELDS
        for key,value in [('magic',0),('generation',8),('job',1),('count',4),('output0',1),('initial_output63',0),('completion',0x306031f0),('native_phase',2),('writes',2),('owner_phase',17),('get',3),('word5',0),('entry',0),('reserved',1)]:
            with self.subTest(key=key),self.assertRaises(ValueError):native.submit(changed(data,fields.index(key),value),7,0,wire)
    def test_partial_and_replayed_requests(self):
        for name,decode in [('runtime-before.bin',lambda b:native.info(b,7)),('job-0-info.bin',lambda b:native.job(b,7,0)),('job-0-submit.bin',lambda b:native.submit(b,7,0,raw('job-0-request.bin')))]:
            data=raw(name)
            for bad in (data[:-1],data+b'\0',bytearray(data)):
                with self.assertRaises(ValueError):decode(bad)
        for gen,j in ((8,1),(7,2)):
            with self.assertRaises(ValueError):native.submit(raw('job-0-submit.bin'),7,0,codec.encode(gen,j,[1],[2]))
    def test_full_capture_changes(self):
        j=1;history=[raw('job-'+str(p)+'-request.bin') for p in range(j+1)]
        source=[raw('job-1-'+name+'.bin') for name in ('root','children','device')]+[raw('before-root.bin'),raw('before-children.bin')]
        for part,offset in [(0,900),(1,4128),(1,600),(2,8),(2,0x888),(2,4096+64),(2,8192),(2,12288+32),(2,12288+8192+512),(2,12288+16384),(2,12288+20480),(2,12288+12288+512),(3,900),(4,600)]:
            copy=list(source);data=bytearray(copy[part]);data[offset]^=1;copy[part]=bytes(data)
            with self.subTest(part=part,offset=offset),self.assertRaises(ValueError):native.verify_capture(*copy,history,7)
        copy=list(source);data=bytearray(copy[2]);data[12288+12288]^=1;copy[2]=bytes(data)
        self.assertTrue(native.verify_capture(*copy,history,7)['passed']) # Submitted QMD belongs to GPU.
class FakeIO:
    def __init__(self):self.calls=[];self.partial=False;self.error=0
    def IOConnectCallMethod(self,connection,selector,scalars,count,data,size,scalar_out,scalar_count,out,out_count):
        assert connection==91 and scalar_out is None
        values=tuple(scalars[i] for i in range(count)) if count else ()
        self.calls.append((selector,values,data.raw if size else b'',size,out_count._obj.value))
        payload=raw('runtime-before.bin') if selector==68 else raw('job-'+str(values[0])+'-info.bin') if selector==70 else b''
        if selector==71:
            j,part,offset,length=values;payload=raw('job-'+str(j)+'-'+('root','children','device','request','plan')[part]+'.bin')[offset:offset+length]
        if payload:ctypes.memmove(out,payload,len(payload))
        if self.partial:out_count._obj.value-=1
        return self.error
class Transport(unittest.TestCase):
    def backend(self):
        b=object.__new__(client.MacIOKitBackend);b.connection=91;b.io=FakeIO();b.closed=False;return b
    def test_actual_ctypes_transport_new_selectors(self):
        b=self.backend();self.assertEqual(b.runtime_info(),raw('runtime-before.bin'))
        for j in range(4):
            wire=raw('job-'+str(j)+'-request.bin');self.assertEqual(b.application_submit(wire),b'');self.assertEqual(b.io.calls[-1][2],wire)
            self.assertEqual(b.application_job_info(j),raw('job-'+str(j)+'-info.bin'))
            for part,name in enumerate(('root','children','device','request','plan')):
                expected=raw('job-'+str(j)+'-'+name+'.bin');actual=b''.join(b.application_data(j,part,offset,min(4096,len(expected)-offset)) for offset in range(0,len(expected),4096))
                self.assertEqual(actual,expected)
        self.assertTrue(any(len(call[1])==4 for call in b.io.calls))
    def test_rejected_shapes_never_enter_ffi(self):
        b=self.backend();wire=raw('job-0-request.bin')
        for selector,scalars,data,size in [(68,(),b'',511),(69,(),wire[:-1],0),(69,(1,),wire,0),(70,(),b'',1024),(71,(0,0,0),b'',4096),(71,(0,0,0,4097),b'',4097),(72,(),b'',0),(71,(0,0,-1,4),b'',4)]:
            with self.assertRaises(ValueError):b._invoke(selector,scalars,data,size)
        for args in [(4,0,0,4),(0,5,0,4),(0,3,575,2),(0,2,0,4097)]:
            with self.assertRaises(ValueError):b.application_data(*args)
        self.assertFalse(b.io.calls)
    def test_short_and_error_results(self):
        b=self.backend();b.io.partial=True
        with self.assertRaises(client.BindingError):b.runtime_info()
        b.io.partial=False;b.io.error=0xe00002bd
        with self.assertRaises(client.BindingError):b.application_submit(raw('job-0-request.bin'))
if __name__=='__main__':unittest.main()
