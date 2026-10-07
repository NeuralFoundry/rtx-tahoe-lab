"""Preserve the real 0.27.1 failure while testing the proposed user channel bytes."""
from pathlib import Path
import hashlib,json,struct,tempfile,unittest
import gsp_execution_native as n
from test_gsp_execution_native import Fake

ROOT=Path(__file__).resolve().parent
EVIDENCE=ROOT/'evidence/virtual-context0271'
GEN=4294969711
e=n.transcript.e

class Saved(Fake):
    def __init__(self):
        self.files={p.name:p.read_bytes() for p in EVIDENCE.glob('*.bin')}
        self.calls=[];self.short=None;self.error_selector=None

class UserChannel(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        record=ROOT/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/record-011.bin'
        _,gr=e.p.c.load_record(record,5,11)
        cls.golden=e.p.c.context_plan(gr)
        cls.plan=e.p.make(gr,cls.golden)

    def test_actual_evidence_hashes_and_failure_status(self):
        manifest=json.loads((EVIDENCE/'manifest.json').read_text())
        self.assertEqual(manifest['generation'],GEN)
        self.assertEqual(len(manifest['files']),17)
        for row in manifest['files']:
            raw=(EVIDENCE/row['path']).read_bytes()
            self.assertEqual((len(raw),hashlib.sha256(raw).hexdigest()),(row['bytes'],row['sha256']))
        records=(EVIDENCE/'records.bin').read_bytes()
        failed=e.rpc.decode_record(records[8*4096:9*4096],expected_sequence=34).rpc
        self.assertEqual((failed.function,failed.result,failed.private_result,len(failed.payload)),(76,0,0,584))
        self.assertEqual(struct.unpack_from('<II',failed.payload,8),(0x2080012b,87))

    def test_old_native_stop_and_assertions_remain_visible(self):
        backend=Saved()
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'capture';result=n.capture(backend,GEN,out)
            self.assertEqual(result['native_stop'],dict(stage='execution_rm',failure=16,reason='native_failure',step=6,completed=6,sent=7,records=9))
            self.assertEqual(result['firmware_assertions'],[32,33])
            self.assertEqual([row['nocat']['error_code'] for row in result['records'] if 'nocat' in row],['0x1443f48','0x1418a2a'])
            self.assertTrue(result['external']['info']['passed'])
            self.assertFalse(result['external']['passed'])
            for key in ('passed','host_command_verified','compute_verified','metal_verified','device_bytes_verified'):
                self.assertFalse(result[key],key)
            for name in ('records.bin','requests.bin','index.bin'):
                self.assertEqual((out/name).read_bytes(),backend.files[name])
            self.assertEqual(result['fence']['command_attempted'],0)
            self.assertTrue(all(44<=call[0]<=63 for call in backend.calls))

    def test_two_channel_words_and_explicit_shared_virtual_entries(self):
        old=(EVIDENCE/'requests.bin').read_bytes()
        native=(ROOT/'changes/gsp-submit-0.24/native/windows-client/requests.bin').read_bytes()
        for step in range(7):
            actual=old[step*4096:(step+1)*4096]
            proposed=e.request(step,19+step,self.golden,3,self.plan)
            self.assertEqual(proposed,native[step*4096:(step+1)*4096])
            if step==e.CHANNEL_STEP:
                self.assertEqual(struct.unpack_from('<I',actual,240)[0],1)
                self.assertEqual(struct.unpack_from('<I',actual,356)[0],0x16)
                payload=bytearray(e.rpc.decode_record(actual).rpc.payload)
                struct.pack_into('<I',payload,32+128,0)
                struct.pack_into('<I',payload,32+244,0x14)
                self.assertEqual(proposed,e.rpc.encode_record(103,bytes(payload),transport_sequence=21))
                self.assertEqual([i for i in range(0,4096,4) if actual[i:i+4]!=proposed[i:i+4]],[32,240,356])
            elif step==e.VIRTUAL_STEP:
                payload=bytearray(e.rpc.decode_record(actual).rpc.payload)
                struct.pack_into('<I',payload,24+40,6)
                for i,b in enumerate(self.golden['buffers'][6:]):
                    struct.pack_into('<QQQI HBB',payload,24+48+(i+3)*32,0,b['virtual'],0,0,b['buffer_id'],0,0)
                self.assertEqual(proposed,e.rpc.encode_record(76,bytes(payload),transport_sequence=25))
            else:self.assertEqual(proposed,actual,step)
        self.assertEqual(old[7*4096:],bytes(6*4096))

    def test_real_kernel_channel_reply_is_not_relabelled_as_user_success(self):
        raw=(EVIDENCE/'records.bin').read_bytes()[2*4096:3*4096]
        with self.assertRaisesRegex(ValueError,'Channel backing echo'):
            e.reply(raw,28,e.CHANNEL_STEP,self.golden,3,self.plan)
        packet=e.rpc.decode_record(raw,expected_sequence=28).rpc
        payload=bytearray(packet.payload)
        struct.pack_into('<I',payload,32+128,0)
        struct.pack_into('<I',payload,32+244,0x14)
        synthetic=e.rpc.encode_record(103,bytes(payload),transport_sequence=28,result=0,private_result=0)
        result=e.reply(synthetic,28,e.CHANNEL_STEP,self.golden,3,self.plan)
        self.assertEqual(result['channel_id'],6)
        self.assertTrue(result['accepted'])
        self.assertFalse(result['compute_verified']);self.assertFalse(result['metal_verified'])

if __name__=='__main__':unittest.main()
