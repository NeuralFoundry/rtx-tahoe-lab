import hashlib,struct,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def expected_image():
    ref=ROOT/'reference';image=bytearray(24576)
    code=(ref/'shader-code.bin').read_bytes()
    if hashlib.sha256(code).hexdigest()!='b13907739b5feea896376f292f317942cc91491d4b68273fe2f69d396406e1ad':raise ValueError('Shader checkpoint')
    image[:256]=code;image[8192:12288]=(ref/'constant.bin').read_bytes();image[12288:12544]=(ref/'qmd.bin').read_bytes()
    for page,seed in ((4,0xa5),(5,0x5a)):
        image[page*4096:page*4096+4096]=bytes(4)+bytes(seed^((i*13+7)&255) for i in range(4,4096))
    return bytes(image)

def expected_tables():
    child=bytearray((ROOT/'reference/execution-children.bin').read_bytes())
    for i in range(6):
        off=4096+(4+i)*8
        if struct.unpack_from('<Q',child,off)[0]:raise ValueError('Occupied mapping')
        struct.pack_into('<Q',child,off,(6<<56)|((0x03409000+i*4096)>>4)|1)
    return bytes(child)

class Memory(unittest.TestCase):
    def test_complete_image_and_command_match_verified_shader_profile(self):
        self.assertEqual((ROOT/'windows/image.bin').read_bytes(),expected_image())
        self.assertEqual((ROOT/'windows/command.bin').read_bytes(),(ROOT/'reference/command.bin').read_bytes())
    def test_only_six_pte_slots_change_and_root_is_preserved(self):
        self.assertEqual((ROOT/'windows/children.bin').read_bytes(),expected_tables())
        self.assertEqual((ROOT/'windows/root.bin').read_bytes(),(ROOT/'reference/execution-root.bin').read_bytes())
    def test_six_translations_include_program_prefetch_guard(self):
        root=(ROOT/'windows/root.bin').read_bytes();child=expected_tables()
        self.assertEqual(struct.unpack_from('<Q',root,8192+129*8)[0],0x100522)
        for page in range(6):
            va=0x1020004000+page*4096
            lo,hi=struct.unpack_from('<QQ',child,((va-0x1020000000)>>21)*16)
            self.assertEqual(lo,0x20)
            table=((hi&0x1ffffff00)<<4)-0x1005000
            pte=struct.unpack_from('<Q',child,table+((va>>12)&511)*8)[0]
            self.assertEqual(pte>>56,6);self.assertEqual(pte&255,1)
            for edge in (0,4095):self.assertEqual(((pte&0x1ffffff00)<<4)+edge,0x3409000+page*4096+edge)
    def test_cpu_initialization_cannot_fake_shader_or_completion(self):
        image=expected_image()
        self.assertEqual(struct.unpack_from('<I',image,16384)[0],0)
        self.assertEqual(struct.unpack_from('<I',image,20480)[0],0)
        self.assertNotEqual(image[16384:20480],image[20480:24576])
        self.assertEqual(image[256:8192],bytes(8192-256))
    def test_trace_uploads_before_high_word_then_valid_word(self):
        trace=list(struct.unpack('<18I',(ROOT/'windows/write-trace.bin').read_bytes()))
        expected=[0x3409000+i*4096 for i in range(6)]
        for i in range(6):expected.extend((0x1005000+4096+(4+i)*8+4,0x1005000+4096+(4+i)*8))
        self.assertEqual(trace,expected)
        self.assertNotIn(0xbb0090,trace)

if __name__=='__main__':unittest.main()
