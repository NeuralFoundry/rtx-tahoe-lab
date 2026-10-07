"""Legacy CID5 evidence and synthetic grouped replies; fixed hardware route stays ChID4."""
import hashlib,struct,unittest
from pathlib import Path
import gsp_execution_native as n
e=n.transcript.e
t=n.transcript
ROOT=Path(__file__).resolve().parent
def repaired(raw):
    b=bytearray(raw);struct.pack_into('<I',b,32,0);value=0
    for i in range(0,(48+struct.unpack_from('<I',b,56)[0]+7)&~7,4):value^=struct.unpack_from('<I',b,i)[0]
    struct.pack_into('<I',b,32,value);return bytes(b)
class IdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        d=ROOT/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm'
        _,gr=e.p.c.load_record(d/'record-011.bin',5,11);cls.golden=e.p.c.context_plan(gr)
        cls.actual=(ROOT/'evidence/execution-identity/records.bin').read_bytes()
        grouped=bytearray(cls.actual)
        struct.pack_into('<I',grouped,80,e.p.CLIENT)
        struct.pack_into('<I',grouped,84,e.p.GROUP)
        struct.pack_into('<I',grouped,136,e.p.SHARE)
        struct.pack_into('<I',grouped,140,0)
        struct.pack_into('<I',grouped,240,0)
        struct.pack_into('<I',grouped,356,0x14)
        cls.grouped=repaired(grouped)  # Synthetic user-channel changes, never live evidence.
        cls.requests=(ROOT/'changes/gsp-submit-0.24/transactions/windows/requests.bin').read_bytes()
        cls.replies=(ROOT/'changes/gsp-submit-0.24/transactions/windows/replies.bin').read_bytes()
    def test_legacy_reply_rejected_and_synthetic_grouped_reply_accepted(self):
        self.assertEqual(hashlib.sha256(self.actual).hexdigest(),'b6594b306943267055f55b265855dabb29359702a6f89bd9d77b3e9e1ce58ed5')
        with self.assertRaises(ValueError):e.reply(self.actual,21,e.CHANNEL_STEP,self.golden,3,None)
        r=e.reply(self.grouped,21,e.CHANNEL_STEP,self.golden,3,None)
        self.assertEqual((r['channel_id'],r['physical_channel_group']),(5,0xc9f00007))
        self.assertFalse(r['compute_verified']);self.assertFalse(r['metal_verified'])
    def test_session_id_and_hardware_token_are_independent(self):
        for cid in (4,5,37,4096,0xfffffffe):
            pages=bytearray(self.replies);offset=e.CHANNEL_STEP*4096
            page=bytearray(pages[offset:offset+4096]);struct.pack_into('<I',page,244,cid)
            pages[offset:offset+4096]=repaired(page)
            proof=t.verify(self.requests,bytes(pages),100,self.golden,3)
            self.assertEqual((proof['channel_id'],proof['raw_token'],proof['candidate']),(cid,4,4))
    def test_invalid_session_ids_and_backing_still_reject(self):
        for value in (0,3,0xffffffff):
            page=bytearray(self.grouped);struct.pack_into('<I',page,244,value)
            with self.assertRaises(ValueError):e.reply(repaired(page),21,e.CHANNEL_STEP,self.golden,3,None)
        for off in (120,136,144,152,256,280,304,328,356):
            page=bytearray(self.grouped);page[off]^=1
            with self.assertRaises(ValueError):e.reply(repaired(page),21,e.CHANNEL_STEP,self.golden,3,None)
    def test_cid_cannot_be_substituted_for_hardware_token(self):
        pages=bytearray(self.replies);offset=e.CHANNEL_STEP*4096
        channel=bytearray(pages[offset:offset+4096]);struct.pack_into('<I',channel,244,5)
        pages[offset:offset+4096]=repaired(channel)
        last=bytearray(self.replies[-4096:]);struct.pack_into('<I',last,104,5)
        pages[-4096:]=repaired(last)
        with self.assertRaises(ValueError):t.verify(self.requests,bytes(pages),100,self.golden,3)
    def test_native_summary_preserves_cid_and_fixed_route(self):
        raw=(ROOT/'changes/gsp-submit-0.24/native/windows-client/rm-info.bin').read_bytes()
        page=bytearray(raw);struct.pack_into('<Q',page,54*8,5)
        self.assertEqual(n.rm(bytes(page),0x12345678)['channel_id'],5)
        struct.pack_into('<Q',page,56*8,5)
        with self.assertRaises(ValueError):n.rm(bytes(page),0x12345678)
