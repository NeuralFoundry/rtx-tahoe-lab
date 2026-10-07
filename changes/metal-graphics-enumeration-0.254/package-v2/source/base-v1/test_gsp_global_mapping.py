"""Native integration must match the separately verified CPU proposal exactly."""
from pathlib import Path
import hashlib,json,struct,unittest
import gsp_execution_native as n
ROOT=Path(__file__).resolve().parent
DATA=ROOT/'evidence/global-mapping028'
e=n.transcript.e

class SharedMapping(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        _,gr=e.p.c.load_record(ROOT/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/record-011.bin',5,11)
        cls.golden=e.p.c.context_plan(gr);cls.plan=e.p.make(gr,cls.golden)

    def test_actual_and_proposal_evidence_provenance(self):
        m=json.loads((DATA/'manifest.json').read_text())
        self.assertEqual(m['generation'],4294969659);self.assertEqual(len(m['files']),6)
        self.assertEqual(sum(r['kind']=='actual-0.28.0-capture' for r in m['files']),4)
        for r in m['files']:
            b=(DATA/r['path']).read_bytes()
            self.assertEqual((len(b),hashlib.sha256(b).hexdigest()),(r['bytes'],r['sha256']))
        failed=e.rpc.decode_record((DATA/'records.bin').read_bytes()[8*4096:],expected_sequence=34).rpc
        self.assertEqual(struct.unpack_from('<II',failed.payload,8),(0x2080012b,87))

    def test_native_and_python_match_the_independent_proposal(self):
        expected=(DATA/'virtual-request.bin').read_bytes()
        generated=e.request(6,25,self.golden,3,self.plan)
        native=(ROOT/'changes/gsp-submit-0.24/native/windows-client/requests.bin').read_bytes()[6*4096:7*4096]
        self.assertEqual(generated,expected);self.assertEqual(native,expected)
        self.assertEqual(e.p.promotion(self.plan,self.golden,False),(DATA/'virtual-parameters.bin').read_bytes())
        old=(DATA/'requests.bin').read_bytes();current=(ROOT/'changes/gsp-submit-0.24/native/windows-client/requests.bin').read_bytes()
        self.assertEqual(current[:6*4096],old[:6*4096])
        self.assertEqual(old[7*4096:],bytes(6*4096))

    def test_every_shared_alias_is_already_mapped_without_new_tables(self):
        root=(DATA/'root-capture.bin').read_bytes();child=(DATA/'children-capture.bin').read_bytes()
        params=e.p.promotion(self.plan,self.golden,False)
        self.assertEqual(struct.unpack_from('<I',params,40)[0],6)
        for i,b in enumerate(self.golden['buffers'][6:]):
            self.assertEqual(struct.unpack_from('<QQQI HBB',params,48+(i+3)*32),(0,b['virtual'],0,0,9+i,0,0))
            for offset in range(0,b['allocated_bytes'],4096):
                for edge in (0,4095):self.assertEqual(e.p.g.walk(root,child,b['virtual']+offset+edge),b['physical']+offset+edge)
        # Physical promotion remains the three private buffers only.
        physical=e.p.promotion(self.plan,self.golden,True)
        self.assertEqual(struct.unpack_from('<I',physical,40)[0],3)
        self.assertEqual(physical[144:],bytes(416))

    def test_truncated_old_success_echo_and_any_changed_shared_entry_reject(self):
        expected=e.rpc.decode_record(e.request(6,34,self.golden,3,self.plan)).rpc.payload
        good=e.rpc.encode_record(76,expected,transport_sequence=34,result=0,private_result=0)
        self.assertTrue(e.reply(good,34,6,self.golden,3,self.plan)['accepted'])
        for offset in [24+40]+list(range(24+48+3*32,24+48+6*32)):
            p=bytearray(expected);p[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(ValueError):
                e.reply(e.rpc.encode_record(76,bytes(p),transport_sequence=34,result=0,private_result=0),34,6,self.golden,3,self.plan)
        old=bytearray(e.rpc.decode_record((DATA/'requests.bin').read_bytes()[6*4096:7*4096]).rpc.payload)
        with self.assertRaises(ValueError):
            e.reply(e.rpc.encode_record(76,bytes(old),transport_sequence=34,result=0,private_result=0),34,6,self.golden,3,self.plan)

if __name__=='__main__':unittest.main()
