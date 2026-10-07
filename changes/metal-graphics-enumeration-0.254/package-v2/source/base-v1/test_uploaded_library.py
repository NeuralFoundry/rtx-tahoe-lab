from pathlib import Path
import dataclasses,hashlib,json,struct,tempfile,unittest
import uploaded_library as lib
import uploaded_request as request
from uploaded_model import Model
from uploaded_compiler.checked_ptx_model import evaluate
ROOT=Path(__file__).resolve().parent
def catalog():return lib.load(ROOT/'selected-library.json',(ROOT/'selected-library.sha256').read_text().strip())
class Library(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.catalog=catalog()
    def test_actual_compiler_profiles(self):
        c=self.catalog;self.assertEqual(len(c.programs),4);self.assertEqual(c.programs[0].bindings,(0,3,7,11));self.assertEqual(c.programs[0].local_size,(32,1,1))
        self.assertEqual(c.programs[0].registers,18);self.assertEqual(c.programs[0].constant_bytes,384)
        self.assertEqual(c.library,(ROOT/'selected-library.bin').read_bytes());self.assertEqual(c.code,(ROOT/'selected-code.bin').read_bytes())
    def test_reference_agrees_with_authored_formulas(self):
        c=self.catalog;wires=request.requests(c,0x130603601);total=0;groups=set()
        for wire in wires:
            r=request.decode(c,wire);data=r['data'];actual,proof=request.evaluate(c,wire);expected=bytearray(data);p=c.programs[r['program']]
            inputs=[struct.unpack_from('<64I',data,i*256) for i in range(len(p.bindings)-1)]
            for i in range(r['invocations']):
                if p.name=='weighted_mix':v=(inputs[0][i]*inputs[1][i]+inputs[2][i])^(i*0x9e3779b9);groups.add(r['groups'])
                elif p.name=='vector_xor':v=inputs[0][i]^inputs[1][i]
                elif p.name=='vector_add':v=inputs[0][i]+inputs[1][i]
                elif p.name=='vector_multiply':v=inputs[0][i]*inputs[1][i]
                else:self.fail('unknown authored test program')
                struct.pack_into('<I',expected,(len(p.bindings)-1)*256+i*4,v&0xffffffff)
            self.assertEqual(actual,bytes(expected));self.assertEqual(proof['output_words'],r['invocations']);total+=proof['output_words']
        self.assertEqual(total,3872);self.assertEqual(groups,{1,2})
    def test_names_and_order_do_not_select_arithmetic(self):
        p=dataclasses.replace(self.catalog.programs[0],name='another_entry');c=lib.Catalog((p,));wire=request.requests(c,7,1)[0]
        original=request.encode(self.catalog,7,1,0,1,request.decode(c,wire)['data'])
        self.assertEqual(request.evaluate(c,wire),request.evaluate(self.catalog,original))
        c=lib.Catalog(tuple(reversed(self.catalog.programs)));self.assertNotEqual(c.library,self.catalog.library)
        for index,p in enumerate(c.programs):self.assertEqual(c.code[c.offsets[index]:c.offsets[index]+len(p.code)],p.code)
    def test_immutable_storage(self):
        with self.assertRaises(dataclasses.FrozenInstanceError):self.catalog.code=b'bad'
        with self.assertRaises(dataclasses.FrozenInstanceError):self.catalog.programs[0].bindings=(0,1)
        with self.assertRaises(ValueError):lib.Catalog(list(self.catalog.programs))
        with self.assertRaises(ValueError):lib.Catalog((self.catalog.programs[0],self.catalog.programs[0]))
    def test_lowering_mismatch(self):
        p=self.catalog.programs[0]
        with self.assertRaises(ValueError):dataclasses.replace(p,ptx=p.ptx+b'\n')
        meta=json.loads(p.lowering);meta['parameter_bindings']=[0,1,2,3]
        with self.assertRaises(ValueError):dataclasses.replace(p,lowering=json.dumps(meta).encode())
        with self.assertRaises(ValueError):dataclasses.replace(p,lowering=b'{"a":1,"a":2}')
    def test_manifest_binding(self):
        with self.assertRaises(ValueError):lib.load(ROOT/'selected-library.json','0'*64)
        with tempfile.TemporaryDirectory() as name:
            path=Path(name)/'package.json';raw=(ROOT/'selected-library.json').read_bytes();path.write_bytes(raw)
            with self.assertRaises((ValueError,OSError)):lib.load(path,hashlib.sha256(raw).hexdigest())
            row=json.loads(raw);row['programs'][0]['files']['assembly']['path']='../outside.ptx';raw=json.dumps(row).encode();path.write_bytes(raw)
            with self.assertRaises(ValueError):lib.load(path,hashlib.sha256(raw).hexdigest())
    def test_request_shape_and_generations(self):
        c=self.catalog;wire=request.requests(c,3,1)[0];r=request.decode(c,wire)
        for serial in (1,0xffffffff,0x100000000,2**64-1):
            w=request.encode(c,2**64-1,serial,0,2,r['data']);self.assertEqual(request.decode(c,w)['serial'],serial)
            q=int.from_bytes(Model(c).plan(w)[3136:3392],'little');self.assertEqual((q>>832)&(2**64-1),serial)
        for args in ((0,1,0,1),(1,0,0,1),(1,1,4,1),(1,1,0,0),(1,1,0,3),(True,1,0,1)):
            with self.assertRaises(ValueError):request.encode(c,*args,r['data'])
        for at in (0,8,12,40,63,1088,2111):
            b=bytearray(wire);b[at]^=128
            with self.assertRaises(ValueError):request.decode(c,bytes(b))
    def test_interpreter_bounds_and_undefined_shift(self):
        for address in (4,0xffffffffffffffff):
            text='ld.param.u64 %r0, [arg0];\nadd.u64 %r1, %r0, '+str(address)+';\nld.global.u32 %r2, [%r1];\nret;\n'
            with self.assertRaises(AssertionError):evaluate(text,(0,0,0),(1,1,1),[{0:1}],[0x10000])
        for count in (32,33,0xffffffff):
            with self.assertRaises(ValueError):evaluate('shl.b32 %r0, 1, '+str(count)+';\nret;\n',(0,0,0),(1,1,1),[{0:1}],[0x10000])
    def test_edge_values(self):
        c=self.catalog;p=c.programs[0]
        for fill in (0,1,0x7fffffff,0x80000000,0xffffffff):
            data=struct.pack('<192I',*([fill]*192))+struct.pack('<64I',*([0xcafecafe]*64))+bytes(1024)
            wire=request.encode(c,1,1,0,2,data);out,_=request.evaluate(c,wire)
            for i in range(64):self.assertEqual(struct.unpack_from('<I',out,768+4*i)[0],((fill*fill+fill)^(i*0x9e3779b9))&0xffffffff)
if __name__=='__main__':unittest.main()
