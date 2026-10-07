from pathlib import Path
import struct,unittest
import vector_capture as c
ROOT=Path(__file__).resolve().parent

class CaptureTests(unittest.TestCase):
    def setUp(self):
        raw=(ROOT/'windows/simulated-queue.bin').read_bytes()
        self.queue=raw[:4096]+raw[8192:16384]
        self.backing=(ROOT/'windows/simulated-backing.bin').read_bytes()
        self.initial=(ROOT.parent/'windows/image.bin').read_bytes()

    def test_cpu_capture_is_consistent_but_not_hardware_proof(self):
        r=c.validate(self.queue,self.backing,self.initial)
        self.assertTrue(r['bytes_valid']);self.assertFalse(r['compute_verified']);self.assertFalse(r['hardware_accessed'])

    def test_all_initial_poison_and_completion_are_required(self):
        with self.assertRaises(ValueError):c.validate(self.queue,self.initial,self.initial)
        for off in (16384,16384+30*4,16384+60*4,20480):
            bad=bytearray(self.backing);bad[off:off+4]=self.initial[off:off+4]
            with self.subTest(off=off),self.assertRaises(ValueError):c.validate(self.queue,bytes(bad),self.initial)

    def test_tail_inputs_and_guards_are_immutable(self):
        for off in (0,511,512,8192+0x200,8192+0x300,12544,16384+61*4,16639,16640,20479,20484,24575):
            bad=bytearray(self.backing);bad[off]^=1
            with self.subTest(off=off),self.assertRaises(ValueError):c.validate(self.queue,bytes(bad),self.initial)

    def test_queue_and_host_success_cannot_substitute_for_compute(self):
        for off in (0,8,16,0x888,0x88c,4096,4116,4160,4192,8192,8196,12287):
            bad=bytearray(self.queue);bad[off]^=1
            with self.subTest(off=off),self.assertRaises(ValueError):c.validate(bytes(bad),self.backing,self.initial)

    def test_changed_used_qmd_is_allowed_only_with_full_result(self):
        bad=bytearray(self.backing);bad[12288:12544]=bytes([0xff])*256
        self.assertTrue(c.validate(self.queue,bytes(bad),self.initial)['bytes_valid'])
        struct.pack_into('<I',bad,16384+60*4,0)
        with self.assertRaises(ValueError):c.validate(self.queue,bytes(bad),self.initial)

    def test_truncated_and_mutable_captures_rejected(self):
        for q,b,i in ((self.queue[:-1],self.backing,self.initial),(bytearray(self.queue),self.backing,self.initial),
                      (self.queue,self.backing[:-1],self.initial),(self.queue,self.backing,self.initial[:-1])):
            with self.assertRaises(ValueError):c.validate(q,b,i)

if __name__=='__main__':unittest.main()
