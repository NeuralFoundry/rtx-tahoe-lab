"""Replay the real empty ACK; do not promote the old stopped run to success."""
import hashlib,json,struct,unittest
from pathlib import Path
import gsp_external_native as n
from test_gsp_execution_native import Fake
import test_gsp_external_native as external_tests
e=n.protocol
ROOT=Path(__file__).parent/'evidence/external-ack027'

class ActualAck(unittest.TestCase):
    def test_evidence_hashes_and_native_stop_remain_unchanged(self):
        m=json.loads((ROOT/'manifest.json').read_text());self.assertEqual(m['generation'],4294969575)
        for row in m['files']:self.assertEqual(hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest(),row['sha256'])
        info=n.info((ROOT/'info.bin').read_bytes(),m['generation'])
        self.assertEqual((info['passed'],info['failure'],info['completed'],info['consumer_writes'],info['directory_checks']),(0,16,4,4,1))
    def test_five_actual_replies_parse_with_exact_empty_ack(self):
        data=(ROOT/'records.bin').read_bytes();requests=(ROOT/'requests.bin').read_bytes()
        self.assertEqual(requests,b''.join(e.request(i) for i in range(5)))
        for step in range(5):self.assertTrue(e.reply(step,21+step,data[step*4096:(step+1)*4096])['accepted'])
        p=n.gsp_rpc.decode_record(data[16384:]).rpc
        self.assertEqual((p.function,p.result,p.private_result,p.sequence,p.payload),(54,0,0,0,b''))
    def test_unused_actual_slot_tail_does_not_authenticate_fields(self):
        raw=(ROOT/'record-025.bin').read_bytes()
        for off in (80,84,96,108,127,128,4095):
            bad=bytearray(raw);bad[off]^=0x5a
            self.assertTrue(e.reply(4,25,bytes(bad))['accepted'])
    def test_status_sequence_and_nonempty_echo_reject(self):
        encode=n.gsp_rpc.encode_record
        for raw in (encode(54,b'',transport_sequence=25,result=31,private_result=0),
            encode(54,b'',transport_sequence=25,result=0,private_result=1),
            encode(54,b'',transport_sequence=26,result=0,private_result=0),
            encode(103,b'',transport_sequence=25,result=0,private_result=0),
            encode(54,e.parameters(4),transport_sequence=25,result=0,private_result=0)):
            with self.assertRaises(ValueError):e.reply(4,25,raw)
    def test_synthetic_full_capture_ignores_ack_tail_only(self):
        f=Fake();raw=bytearray(f.files['external-records.bin']);raw[16384+80]^=1;f.files['external-records.bin']=bytes(raw)
        result,_=external_tests.External().collect(f)
        self.assertTrue(result['passed'],result.get('error'))
        raw[16384+64]^=1;f.files['external-records.bin']=bytes(raw)
        self.assertFalse(external_tests.External().collect(f)[0]['passed'])

if __name__=='__main__':unittest.main()
