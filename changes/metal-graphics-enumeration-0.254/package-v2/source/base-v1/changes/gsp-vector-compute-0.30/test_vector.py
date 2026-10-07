import struct,unittest
from pathlib import Path
import shader_image as s
import vector_profile as v
ROOT=Path(__file__).resolve().parent

class VectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cubin=(ROOT/'windows/rtx_probe_vectoradd.cubin').read_bytes()
        cls.code=(ROOT/'windows/shader-code.bin').read_bytes()

    def changed(self,off,fmt,value):
        raw=bytearray(self.cubin);struct.pack_into(fmt,raw,off,value);return bytes(raw)

    def section(self,n):return struct.unpack_from('<Q',self.cubin,40)[0]+n*64

    def test_real_cubin_resources_and_four_parameters(self):
        code,r=s.extract(self.cubin)
        self.assertEqual(code,self.code);self.assertEqual(r['registers'],12)
        self.assertEqual(r['required_threads'],[32,1,1]);self.assertEqual(r['parameter_offsets'],[0,8,16,24])
        self.assertEqual(r['parameter_sizes'],[8,8,8,4]);self.assertFalse(r['runtime_relocations'])
        self.assertFalse(r['hardware_accessed'])

    def test_truncated_oversized_or_mutable_cubin(self):
        for raw in [self.cubin[:n] for n in (0,16,63,64,1000,len(self.cubin)-1)]+[bytes(65537),bytearray(self.cubin)]:
            with self.subTest(size=len(raw)),self.assertRaises(ValueError):s.extract(raw)

    def test_elf_header_and_section_bounds(self):
        for off,fmt,val in ((0,'<I',0),(18,'<H',62),(48,'<I',0),(60,'<H',0),(60,'<H',13),(62,'<H',14),(40,'<Q',2**64-1),(32,'<Q',2**64-1)):
            with self.subTest(off=off,val=val),self.assertRaises(ValueError):s.extract(self.changed(off,fmt,val))

    def test_register_code_size_and_kernel_symbol_shape(self):
        for off,fmt,val in ((self.section(13)+44,'<I',8<<24),(self.section(13)+32,'<Q',256),(self.section(13)+8,'<Q',2),(self.section(3)+56,'<Q',16),(0x2f0+8*24+16,'<Q',256)):
            with self.subTest(off=off),self.assertRaises(ValueError):s.extract(self.changed(off,fmt,val))

    def test_parameter_ordinals_offsets_count_and_required_threads(self):
        for off,fmt,val in ((0x524+0x1e,'<H',8),(0x524+0x22,'<H',8),(0x524+0x2c,'<H',2),(0x524+0x2e,'<H',20),(0x524+0x7c,'<I',64),(0x524+0x70,'<I',0)):
            with self.subTest(off=off),self.assertRaises(ValueError):s.extract(self.changed(off,fmt,val))

    def test_stack_runtime_relocations_and_changed_sass_rejected(self):
        for off,fmt,val in ((0x500+0x14,'<I',16),(self.section(11)+44,'<I',13),(0x780,'<I',0)):
            with self.subTest(off=off),self.assertRaises(ValueError):s.extract(self.changed(off,fmt,val))

    def test_attribute_duplicates_and_truncated_values(self):
        for raw in (b'\x04',struct.pack('<BBH',4,1,8)+bytes(4),struct.pack('<BBHBBH',1,2,0,1,2,0),bytes([2,1,0,0])):
            with self.assertRaises(ValueError):s.info(raw)
        self.assertEqual(len(s.info(struct.pack('<BBHBBH',1,0x17,0,1,0x17,0))[0x17]),2)

    def test_only_six_named_qmd_fields_change(self):
        old=v.baseline.values();new=v.values()
        self.assertEqual({k for k in new if old[k]!=new[k]}, {'CTA_RASTER_WIDTH','CTA_THREAD_DIMENSION0','REGISTER_COUNT_V','RELEASE0_PAYLOAD_LOWER','PROGRAM_PREFETCH_SIZE','CONSTANT_BUFFER_SIZE_SHIFTED4[0]'})
        image,command,_=v.build(self.code,*v.inputs(v.DEFAULT_SEED),61)
        self.assertEqual(image[12288:12544],(ROOT/'windows/qmd.bin').read_bytes())
        self.assertEqual(command,v.baseline.build()[2])

    def test_modular_arithmetic_partial_warp_and_poison(self):
        for seed in (0,1,0x30603001,0xffffffff):
            a,b=v.inputs(seed)
            for n in (0,1,31,32,33,61,64):
                image,_,expected=v.build(self.code,a,b,n)
                for i in range(64):
                    summed=(a[i]+b[i])%(2**32)
                    self.assertEqual(struct.unpack_from('<I',image,16384+i*4)[0],summed^0xffffffff)
                    self.assertEqual(struct.unpack_from('<I',expected,i*4)[0],summed if i<n else summed^0xffffffff)
                self.assertEqual(struct.unpack_from('<I',image,20480)[0],0)

    def test_reject_bad_inputs_before_profile(self):
        a,b=v.inputs(0)
        for n in (-1,65,True,1.5):
            with self.assertRaises(ValueError):v.build(self.code,a,b,n)
        for bad in ([0]*63,[0]*63+[-1],[0]*63+[2**32],[False]*64,None):
            with self.assertRaises(ValueError):v.build(self.code,bad,b,61)
        with self.assertRaises(ValueError):v.build(self.code[:-1],a,b,61)

    def test_capture_requires_arithmetic_and_completion_and_guards(self):
        image,_,expected=v.build(self.code,*v.inputs(7),61)
        actual=bytearray(image);actual[16384:16640]=expected;struct.pack_into('<I',actual,20480,v.COMPLETION)
        r=v.validate_capture(bytes(actual),image,61);self.assertTrue(r['bytes_valid']);self.assertFalse(r['compute_verified'])
        for pos in (0,511,512,8192+0x168,8192+0x178,8192+0x200,8192+0x300,12544,16384,16624,16628,16640,20480,20484,24575):
            actual[pos]^=1
            with self.subTest(pos=pos),self.assertRaises(ValueError):v.validate_capture(bytes(actual),image,61)
            actual[pos]^=1
        with self.assertRaises(ValueError):v.validate_capture(image,image,61)
        actual[12288:12544]=bytes([0xaa])*256
        self.assertTrue(v.validate_capture(bytes(actual),image,61)['bytes_valid'])

    def test_windows_full_image_and_expected_outputs_agree(self):
        image,command,expected=v.build(self.code,*v.inputs(v.DEFAULT_SEED),61)
        for name,raw in (('image.bin',image),('constant.bin',image[8192:12288]),('command.bin',command),('expected-output.bin',expected)):
            self.assertEqual((ROOT/'windows'/name).read_bytes(),raw,name)

if __name__=='__main__':unittest.main()
