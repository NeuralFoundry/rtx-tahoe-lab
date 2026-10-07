from pathlib import Path
import ctypes,struct,unittest,os
import gsp_program_client as client
import gsp_program_native as native
import program_request_codec as codec
ROOT=Path(__file__).resolve().parent
FIXTURE=Path(os.environ.get('RTX_PROGRAM_FIXTURES',str(ROOT/'program-client-windows-v1')))
GEN=0x30603301
def raw(name):return (FIXTURE/name).read_bytes()
def changed(data,index,value):
    result=bytearray(data);struct.pack_into('<Q',result,index*8,value);return bytes(result)


class Codec(unittest.TestCase):
    def test_compiled_request_and_plan_bytes(self):
        for j in range(4):
            wire=raw(f'request-{j}.bin');r=codec.decode(wire)
            self.assertEqual(codec.encode(GEN,j+1,r['program'],r['a'],r['b'],r['initial']),wire)
            self.assertEqual(native.model.plan(wire),raw(f'plan-{j}.bin'))
    def test_request_header_and_unused_data_rejected(self):
        wire=raw('request-0.bin')
        for off in (0,8,12,16,24,32,36,40,63,832,2111):
            b=bytearray(wire);b[off]^=255
            if off==16:b[16:24]=bytes(8)
            with self.subTest(offset=off),self.assertRaises(ValueError):codec.decode(bytes(b))
        for value in (wire[:-1],wire+b'\0',bytearray(wire),None):
            with self.assertRaises(ValueError):codec.decode(value)
    def test_input_types_counts_and_identity(self):
        r=codec.decode(raw('request-0.bin'));args=[GEN,1,0,r['a'],r['b']]
        for index,value in [(0,0),(0,True),(0,2**64),(1,0),(1,5),(2,3),(2,True),(3,[]),(4,[1]*63),(3,[1]*63+[-1]),(4,[1]*63+[True])]:
            bad=list(args);bad[index]=value
            with self.subTest(index=index,value=str(value)[:20]),self.assertRaises(ValueError):codec.encode(*bad)
    def test_arithmetic_wrap_and_xor(self):
        self.assertEqual(codec.answer(0,0xffffffff,1),0)
        self.assertEqual(codec.answer(1,0x80000000,2),0)
        self.assertEqual(codec.answer(2,0xaaaaaaaa,0xffffffff),0x55555555)
    def test_sealed_library_code_identity(self):
        wire,code,_=native.model.sealed();native.model.verify_sealed(wire,code)
        for library,body in [(wire[:-1],code),(wire,code[:-1]),(wire,bytes([code[0]^1])+code[1:]),(bytes([wire[0]^1])+wire[1:],code)]:
            with self.assertRaises(ValueError):native.model.verify_sealed(library,body)


class ABI(unittest.TestCase):
    def test_complete_compiled_bootstrap_and_four_jobs(self):
        self.assertTrue(native.memory(raw('memory.bin'),GEN)['passed'])
        self.assertTrue(native.capture_info(raw('capture-bootstrap.bin'),GEN)['passed'])
        before=[raw('before-'+s+'.bin') for s in ('root','children')]
        r=native.verify_bootstrap(*[raw('bootstrap-'+s+'.bin') for s in ('root','children','device')],*before,GEN)
        self.assertEqual(r['jobs'],0);history=[]
        for j in range(4):
            wire=raw(f'request-{j}.bin');history.append(wire)
            self.assertEqual(native.info(raw(f'info-{j}.bin'),GEN)['completed'],j+1)
            self.assertTrue(native.job(raw(f'job-{j}.bin'),GEN,j)['stage_passed'])
            self.assertTrue(native.submit(raw(f'submit-{j}.bin'),GEN,j,wire)['passed'])
            r=native.verify_capture(*[raw(f'{s}-{j}.bin') for s in ('root','children','device')],*before,history,GEN)
            self.assertEqual(r['active_elements'],64*(j+1));self.assertFalse(r['hardware_accessed']);self.assertFalse(r['metal_verified'])
    def test_info_corruptions(self):
        data=raw('info-ready.bin')
        values={'magic':0,'abi':2,'generation':GEN+1,'phase':6,'request_bytes':576,'capacity':5,'owner_phase':17,'pinned':0,'ready':0,'program_count':4,'code_bytes':256,'prepare_reads':95,'prepare_failure':1,'prepare_passed':0,'prepare_elapsed':5_000_000_000,'open_passed':0,'open_failure':1,'closed':1,'reserved63':1}
        for key,value in values.items():
            with self.subTest(key=key),self.assertRaises(ValueError):native.info(changed(data,native.INFO_FIELDS.index(key),value),GEN)
    def test_job_corruptions(self):
        data=raw('job-0.bin')
        values={'magic':0,'generation':GEN+1,'job':1,'groups':2,'window_saved':0,'window_after':0,'cleanup_failure':1,'window_request_id':2,'stage_reads':72,'stage_writes':2,'qmd_attempted':0,'committed':0,'device_bytes':32768,'capture_reads':21,'capture_failure':1,'request_id':2,'request_generation':GEN+1,'completion':0,'reserved127':1}
        for key,value in values.items():
            with self.subTest(key=key),self.assertRaises(ValueError):native.job(changed(data,native.JOB_FIELDS.index(key),value),GEN,0)
    def test_submit_corruptions(self):
        data=raw('submit-0.bin');wire=raw('request-0.bin')
        values={'magic':0,'generation':GEN+1,'job':1,'jobs':5,'data_physical':0,'completion':0x306032f0,'native_phase':2,'writes':2,'owner_phase':17,'get':3,'word5':0,'entry':0,'immutable_verified':0,'token':2**32,'elapsed_ns':5_000_000_000}
        for key,value in values.items():
            with self.subTest(key=key),self.assertRaises(ValueError):native.submit(changed(data,native.SUBMIT_FIELDS.index(key),value),GEN,0,wire)
    def test_partial_wrong_type_and_generation(self):
        entries=[('info-ready.bin',lambda b,g:native.info(b,g)),('job-0.bin',lambda b,g:native.job(b,g,0)),('submit-0.bin',lambda b,g:native.submit(b,g,0,raw('request-0.bin'))),('memory.bin',native.memory),('capture-bootstrap.bin',native.capture_info)]
        for name,decode in entries:
            for b in (raw(name)[:-1],raw(name)+b'\0',bytearray(raw(name))):
                with self.subTest(name=name),self.assertRaises(ValueError):decode(b,GEN)
            with self.assertRaises(ValueError):decode(raw(name),GEN+1)
    def test_capture_rejects_input_output_table_and_queue_changes(self):
        history=[raw(f'request-{j}.bin') for j in range(2)]
        source=[raw('root-1.bin'),raw('children-1.bin'),raw('device-1.bin'),raw('before-root.bin'),raw('before-children.bin')]
        cases=[(0,900),(1,4128),(1,600),(2,8),(2,0x888),(2,4096+64),(2,8192),(2,12288+32),(2,12288+4096),(2,12288+4096+512),(2,12288+8192+0x160),(2,12288+20480),(2,12288+12288+512),(3,900),(4,600)]
        for part,offset in cases:
            changed_parts=list(source);b=bytearray(source[part]);b[offset]^=1;changed_parts[part]=bytes(b)
            with self.subTest(part=part,offset=offset),self.assertRaises(ValueError):native.verify_capture(*changed_parts,history,GEN)
    def test_arithmetic_separate_from_allowed_writes(self):
        history=[raw('request-0.bin')];device=bytearray(raw('device-0.bin'));device[12288+4096+512]^=1
        native.model.verify_backing(bytes(device[12288:]),history,GEN,arithmetic=False)
        with self.assertRaises(ValueError):native.model.verify_backing(bytes(device[12288:]),history,GEN)
        device=bytearray(raw('device-0.bin'));device[12288+12288]^=1
        native.model.verify_backing(bytes(device[12288:]),history,GEN) # Completed QMD belongs to GPU.
    def test_history_order_and_generation(self):
        for history in [[raw('request-1.bin')],[raw('request-0.bin')]*2,[raw('request-0.bin')]*5]:
            with self.assertRaises(ValueError):native.model.canonical(history,GEN)
        with self.assertRaises(ValueError):native.model.canonical([raw('request-0.bin')],GEN+1)


class FakeIO:
    def __init__(self):self.calls=[];self.partial=False;self.error=0;self.scalar_extra=False;self.closes=[]
    def IOServiceClose(self,connection):self.closes.append(connection);return 0
    def IOConnectCallMethod(self,connection,selector,scalars,count,data,size,scalar_out,scalar_count,out,out_count):
        assert connection==91 and scalar_out is None
        values=tuple(scalars[i] for i in range(count)) if count else ()
        self.calls.append((selector,values,data.raw if size else b'',size,out_count._obj.value))
        payload=raw('info-ready.bin') if selector==68 else raw(f'job-{values[0]}.bin') if selector==70 else raw(f'submit-{values[0]}.bin') if selector==65 else b''
        if selector==71:
            j,part,offset,length=values
            full=raw(f'{("root","children","device","request","plan")[part]}-{j}.bin') if part<5 else native.model.sealed()[part-5]
            payload=full[offset:offset+length]
        if payload:ctypes.memmove(out,payload,len(payload))
        if self.partial:out_count._obj.value-=1
        if self.scalar_extra:scalar_count._obj.value=1
        return self.error


class Transport(unittest.TestCase):
    def backend(self):
        b=object.__new__(client.MacIOKitBackend);b.connection=91;b.io=FakeIO();b.closed=False;return b
    def test_actual_ctypes_transport(self):
        b=self.backend();self.assertEqual(b.runtime_info(),raw('info-ready.bin'))
        for j in range(4):
            wire=raw(f'request-{j}.bin');self.assertEqual(b.program_submit(wire),b'');self.assertEqual(b.io.calls[-1][2],wire)
            self.assertEqual(b.program_job_info(j),raw(f'job-{j}.bin'));self.assertEqual(b.program_submit_info(j),raw(f'submit-{j}.bin'))
            for part in range(7):
                expected=raw(f'{("root","children","device","request","plan")[part]}-{j}.bin') if part<5 else native.model.sealed()[part-5]
                actual=b''.join(b.program_data(j,part,off,min(4096,len(expected)-off)) for off in range(0,len(expected),4096))
                self.assertEqual(actual,expected)
        self.assertTrue(any(len(call[1])==4 for call in b.io.calls))
    def test_bad_shapes_and_ranges_do_not_enter_ffi(self):
        b=self.backend();wire=raw('request-0.bin')
        cases=[(68,(),b'',511),(69,(),wire[:-1],0),(69,(1,),wire,0),(70,(),b'',1024),(65,(0,),b'',1568),(71,(0,0,0),b'',4096),(71,(0,0,0,4097),b'',4097),(72,(),b'',0),(71,(0,0,-1,4),b'',4),(71,(4,0,0,4),b'',4),(71,(0,7,0,4),b'',4),(71,(0,5,511,2),b'',2),(68,(),b'',True),(68,(),b'',-1),(68,(),bytearray(),512)]
        for args in cases:
            with self.subTest(args=str(args)[:50]),self.assertRaises(ValueError):b._invoke(*args)
        for args in [(4,0,0,4),(0,7,0,4),(0,3,2111,2),(0,5,511,2),(0,2,0,4097)]:
            with self.assertRaises(ValueError):b.program_data(*args)
        self.assertFalse(b.io.calls)
    def test_partial_scalar_and_native_errors(self):
        b=self.backend();b.io.partial=True
        with self.assertRaises(client.BindingError):b.runtime_info()
        b.io.partial=False;b.io.scalar_extra=True
        with self.assertRaises(client.BindingError):b.runtime_info()
        b.io.scalar_extra=False;b.io.error=0xe00002bd
        with self.assertRaisesRegex(client.BindingError,'e00002bd'):b.program_submit(raw('request-0.bin'))
    def test_close_once_and_never_dispatch_after_close(self):
        b=self.backend();b.close();b.close();self.assertEqual(b.io.closes,[91]);self.assertTrue(b.closed)
        with self.assertRaises(client.BindingError):b.runtime_info()
        with self.assertRaises(client.BindingError):b._invoke(68,(),b'',512)
        self.assertFalse(b.io.calls)


if __name__=='__main__':unittest.main()
