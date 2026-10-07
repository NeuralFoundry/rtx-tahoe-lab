import struct,unittest
from pathlib import Path
import snapshot,source_oracle
ROOT=Path(__file__).resolve().parent

def inputs():
    return [(ROOT/path).read_bytes() for path in ('windows/simulated-queue.bin','windows/simulated-backing.bin','reference/initial-image.bin',
      'reference/host-command.bin','reference/host-entry.bin','windows/command.bin','windows/entry.bin')]

class Submission(unittest.TestCase):
    def test_nvidia_entry_and_working_runtime_submission_sequence(self):
        self.assertTrue(source_oracle.verify()['passed'])
        self.assertEqual(inputs()[-1],source_oracle.entry())
        self.assertEqual(struct.unpack('<3I',(ROOT/'windows/write-trace.bin').read_bytes()),(0x3402040,0x3400008,0x340088c))
    def test_simulated_complete_snapshot_has_both_markers_and_guards(self):
        self.assertTrue(snapshot.validate(*inputs())['valid_snapshot'])
    def test_one_marker_or_queue_get_alone_is_insufficient(self):
        for index,off in ((0,0x888),(0,0x88c),(1,16384),(1,20480)):
            args=inputs();changed=bytearray(args[index]);struct.pack_into('<I',changed,off,0);args[index]=bytes(changed)
            with self.subTest(index=index,offset=off),self.assertRaises(ValueError):snapshot.validate(*args)
    def test_program_constants_guards_host_and_queue_corruption_rejected(self):
        for index,off in ((0,0),(0,8),(0,255),(0,0x2000),(0,0x2040),(0,0x2060),(0,0x3000),(0,0x3fff),
                          (1,0),(1,256),(1,8192),(1,12287),(1,12544),(1,16383),(1,16388),(1,20479),(1,20484),(1,24575)):
            args=inputs();changed=bytearray(args[index]);changed[off]^=1;args[index]=bytes(changed)
            with self.subTest(index=index,offset=off),self.assertRaises(ValueError):snapshot.validate(*args)
    def test_hardware_qmd_state_changes_are_allowed(self):
        args=inputs();changed=bytearray(args[1]);changed[12288:12544]=bytes([0x7f])*256;args[1]=bytes(changed)
        self.assertTrue(snapshot.validate(*args)['valid_snapshot'])
    def test_truncated_or_mutable_snapshots_rejected(self):
        for index in range(7):
            args=inputs();args[index]=args[index][:-1]
            with self.assertRaises(ValueError):snapshot.validate(*args)
        args=inputs();args[0]=bytearray(args[0])
        with self.assertRaises(ValueError):snapshot.validate(*args)

if __name__=='__main__':unittest.main()
