import hashlib,json,re,struct,unittest
from pathlib import Path
import submit_codec as s
ROOT=Path(__file__).resolve().parent
def response(step,token=37):
    p=bytearray(s.request(step,20+step));struct.pack_into('<II',p,64,0,0)
    if step==2:struct.pack_into('<I',p,104,token)
    return reseal(p)
def reseal(p):
    struct.pack_into('<I',p,32,0);end=(48+struct.unpack_from('<I',p,56)[0]+7)&~7
    value=0
    for i in range(0,end,4):value^=struct.unpack_from('<I',p,i)[0]
    struct.pack_into('<I',p,32,value);return bytes(p)
class Submit(unittest.TestCase):
    def test_pinned_vendor_extracts(self):
        rows=json.loads((ROOT/'reference/manifest.json').read_text())
        for row in rows:self.assertEqual(hashlib.sha256((ROOT/'reference'/row['file']).read_bytes()).hexdigest(),row['sha256'])
        extract=(ROOT/'vendor-extract.hpp').read_text()
        for name in ('NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS','NVA06F_CTRL_BIND_PARAMS','NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS'):
            text=(ROOT/'reference'/('ctrlc36f.h' if name.startswith('NVC') else 'ctrla06fgpfifo.h')).read_text()
            original=re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',text,re.S).group(0)
            self.assertIn(original,extract)
    def test_three_controls(self):
        for i in range(3):self.assertTrue(s.reply(response(i),i,20+i)['accepted'])
        self.assertEqual(len(s.rpc.decode_record(s.request(1,21)).rpc.payload),26)
    def test_token_is_only_diagnostic(self):
        for token in (0,1,37,0x12345678,0xfffffffe):
            r=s.reply(response(2,token),2,22);self.assertEqual(r['raw_token'],token);self.assertFalse(r['doorbell_ready'])
        with self.assertRaises(ValueError):s.reply(response(2,0xffffffff),2,22)
    def test_every_identity_word_and_status(self):
        for step in range(3):
            for off in (36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100):
                p=bytearray(response(step));p[off]^=1
                with self.subTest(step=step,off=off),self.assertRaises(ValueError):s.reply(reseal(p),step,20+step)
    def test_transport_corruption(self):
        for i in range(3):
            raw=response(i)
            for bad in (raw[:-1],raw+b'\0',raw[:110]+b'\1'+raw[111:]):
                with self.assertRaises(ValueError):s.reply(bad,i,20+i)
    def test_userd_requires_full_standard_layout(self):
        self.assertFalse(s.userd(0x100,0x20,4096)) # Existing golden-image descriptor is not a submission descriptor.
        self.assertTrue(s.userd(0x100,512,4096));self.assertFalse(s.userd(4096-508,512,4096))
        self.assertFalse(s.userd(1,512,4096));self.assertFalse(s.userd(0,511,4096))
    def test_entry_and_method_boundaries(self):
        self.assertEqual(struct.unpack('<Q',s.entry(s.COMMAND_VA,20))[0],s.COMMAND_VA|(1<<41)|(5<<42))
        for address,count in ((0,4),(1,4),(1<<40,4),(4,0),(4,3),(4,4100),((1<<40)-4,8)):
            with self.assertRaises(ValueError):s.entry(address,count)
        for method,sub,count in ((1,0,1),(0,8,1),(0,0,0),(0,0,8192),(0x3ffc,0,2)):
            with self.assertRaises(ValueError):s.increment(method,sub,count)
        self.assertEqual(s.increment(0x3ffc,7,1),0x2001efff)
    def test_fence_and_cpp_bytes(self):
        raw=(ROOT/'cpu-fixture-windows.bin').read_bytes()
        expected=b''.join(s.request(i,20+i) for i in range(3))+s.fence()+s.entry(s.COMMAND_VA,20)
        self.assertEqual(raw,expected)
        self.assertEqual(struct.unpack('<5I',s.fence()),(0x20040004,0x10,0x20002000,0x30602401,0x01000002))
    def test_boolean_negative_and_extra_step_rejected(self):
        for step in (True,-1,3):
            with self.assertRaises(ValueError):s.request(step,20)
        for sequence in (True,-1,0xffffffff):
            with self.assertRaises(ValueError):s.request(0,sequence)
if __name__=='__main__':unittest.main()
