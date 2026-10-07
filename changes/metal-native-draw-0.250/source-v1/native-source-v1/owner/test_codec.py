from pathlib import Path
import hashlib,struct,unittest
import library_upload as u
ROOT=Path(__file__).resolve().parent
class Codec(unittest.TestCase):
    def setUp(self):self.library=(ROOT/'library.bin').read_bytes();self.code=(ROOT/'code.bin').read_bytes()
    def test_header_full_width(self):
        for gen in (1,2**32-1,2**32,2**64-1):
            h=u.begin_header(gen,self.library,self.code)
            self.assertEqual(len(h),128);self.assertEqual(struct.unpack_from('<Q',h,16)[0],gen)
            self.assertEqual(h[40:72],hashlib.sha256(self.library+self.code).digest());self.assertEqual(h[72:],bytes(56))
    def test_generation_rejected(self):
        for gen in (True,False,0,-1,2**64,1.0,None,'1'):
            with self.assertRaises(ValueError):u.begin_header(gen,self.library,self.code)
    def test_payload_types_and_sizes(self):
        for part in (0,1):
            for value in (None,bytearray(self.library if part==0 else self.code),b'',bytes(511 if part==0 else 4097)):
                args=[self.library,self.code];args[part]=value
                with self.assertRaises(ValueError):u.begin_header(1,*args)
                with self.assertRaises(ValueError):u.upload_chunks(*args)
    def test_chunks(self):
        chunks=u.upload_chunks(self.library,self.code)
        self.assertEqual([o for o,_ in chunks],[0,1024,2048,3072,4096]);self.assertEqual([len(b) for _,b in chunks],[1024]*4+[512])
        self.assertEqual(b''.join(b for _,b in chunks),self.library+self.code)
    def test_empty_info(self):
        b=bytearray(256);struct.pack_into('<QII',b,0,u.INFO_MAGIC,1,256);struct.pack_into('<4I',b,40,4608,1024,5,64)
        self.assertEqual(u.decode_info(bytes(b))['phase'],0)
        for off in range(256):
            corrupt=bytearray(b);corrupt[off]^=128
            with self.assertRaises(ValueError,msg=str(off)):u.decode_info(bytes(corrupt))
    def test_info_requires_bytes(self):
        for b in (None,bytes(255),bytes(257),bytearray(256)):
            with self.assertRaises(ValueError):u.decode_info(b)
if __name__=='__main__':unittest.main()
