"""CPU replay of captured hardware bytes; no live backend/device access."""
import hashlib,json,struct,sys,tempfile,unittest
from pathlib import Path
import gsp_channel_native as n
import test_gsp_channel_native as old
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'changes/gsp-submit-0.24/transactions'))
import execution_codec as e
EVIDENCE=ROOT/'evidence/channel-reply'

def repair(raw):
    b=bytearray(raw);struct.pack_into('<I',b,32,0);checksum=0
    for i in range(0,(48+struct.unpack_from('<I',b,56)[0]+7)&~7,4):checksum^=struct.unpack_from('<I',b,i)[0]
    struct.pack_into('<I',b,32,checksum);return bytes(b)

class Recorded(old.Fake):
    """Actual summaries/pages plus explicitly reconstructed, non-native index."""
    def __init__(self):
        self.files={p.name:p.read_bytes() for p in EVIDENCE.glob('*.bin')}
        rows=json.loads((EVIDENCE/'decoded.json').read_text())['records']
        self.files['index.bin']=b''.join(struct.pack('<9Q',*(row[k] for k in n.prep.ROW_FIELDS)) for row in rows)
        self.files['records.bin']=b''.join(self.files[row['file']] for row in rows)
        self.files['requests.bin']=n.tx.request(0) # CPU canonical; absent from old hardware capture.
        self.requests=[]
    def channel_request(self,step):
        self.requests.append(step);return super().channel_request(step)

class ChannelReply(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.live=(EVIDENCE/'record-016.bin').read_bytes()
        cls.gr=(ROOT/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/record-011.bin').read_bytes()[104:1768]
        cls.golden=n.tx.c.context_plan(cls.gr)
        raw=bytearray(e.request(e.CHANNEL_STEP,100,cls.golden,3,None));struct.pack_into('<II',raw,64,0,0);cls.execution=repair(raw)
    def test_exact_hardware_reply(self):
        self.assertEqual(hashlib.sha256(self.live).hexdigest(),'8e7556c980e83d41fa1d5ed678f5dcfca5a37db054b8528c868df5db3dfdfe1f')
        r=n.tx.reply(self.live,0,16)
        self.assertEqual((r['channel_id'],r['subdevice_mask'],r['physical_channel_group']),(3,0,0xc9f00000))
        self.assertFalse(r['hardware_accessed'])
    def test_opaque_group_does_not_change_identity_or_token(self):
        for value in (0,1,0xc9f00000,0xc9f00001,0xffffffff):
            a=bytearray(self.live);b=bytearray(self.execution)
            struct.pack_into('<I',a,352,value);struct.pack_into('<I',b,352,value)
            r=n.tx.reply(repair(a),0,16);s=e.reply(repair(b),100,e.CHANNEL_STEP,self.golden,3,None)
            self.assertEqual((r['physical_channel_group'],s['physical_channel_group']),(value,value))
            self.assertEqual((r['channel_id'],s['channel_id']),(3,4));self.assertNotIn('raw_token',s)
    def test_all_immutable_bytes_rejected(self):
        for offset in range(368):
            if 132<=offset<140 or 240<=offset<244:continue
            a=bytearray(self.live);b=bytearray(self.execution);a[112+offset]^=1;b[112+offset]^=1
            with self.subTest(offset=offset):
                with self.assertRaises(ValueError):n.tx.reply(repair(a),0,16)
                with self.assertRaises(ValueError):e.reply(repair(b),100,e.CHANNEL_STEP,self.golden,3,None)
    def test_failed_native_result_preserves_sent_request_and_asserts(self):
        f=Recorded()
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'capture';r=n.capture(f,4294969869,out)
            self.assertFalse(r['passed']);self.assertFalse(r['exchanges_verified']);self.assertFalse(r['metal_verified'])
            self.assertEqual(f.requests,[0]);self.assertEqual(r['sent_requests_verified'],1)
            self.assertEqual((out/'request-0.bin').read_bytes(),f.files['requests.bin'])
            self.assertEqual((out/'index.bin').read_bytes(),f.files['index.bin'])
            self.assertEqual((out/'records.bin').read_bytes(),f.files['records.bin'])
            self.assertTrue(r['firmware_assertions_unresolved']);self.assertEqual(len(r['firmware_assertions']),3)
            self.assertEqual([x['nocat']['error_code'] for x in r['records'] if 'nocat' in x],['0x1a9068a','0x1a90cf4','0x1394270'])
    def test_corrupt_index_still_keeps_requests_and_journal(self):
        f=Recorded();b=bytearray(f.files['index.bin']);b[0]^=1;f.files['index.bin']=bytes(b)
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'capture'
            with self.assertRaises(ValueError):n.capture(f,4294969869,out)
            self.assertEqual((out/'request-0.bin').read_bytes(),n.tx.request(0))
            self.assertEqual((out/'index.bin').read_bytes(),bytes(b));self.assertEqual((out/'records.bin').read_bytes(),f.files['records.bin'])
    def test_failed_reply_still_keeps_request(self):
        f=Recorded();b=bytearray(self.live);b[112+244]^=1;b=repair(b)
        f.files['records.bin']=f.files['records.bin'][:-4096]+b
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'capture';r=n.capture(f,4294969869,out)
            self.assertIn('reply_error',r['records'][-1]);self.assertFalse(r['passed'])
            self.assertEqual((out/'record-016.bin').read_bytes(),b);self.assertTrue((out/'request-0.bin').is_file())
    def test_truncated_request_does_not_pass(self):
        f=Recorded();f.files['requests.bin']=f.files['requests.bin'][:-1]
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'capture'
            with self.assertRaises(ValueError):n.capture(f,4294969869,out)
            self.assertEqual((out/'request-0.bin').stat().st_size,4095)
    def test_zero_sent_requests_do_not_call_getter(self):
        f=Recorded();f.files['rm-info.bin']=old.Fake().files['rm-info.bin']
        # A bounded pre-send failure has no records or requests; no fabricated ones.
        raw=f.files['rm-info.bin'];values=dict(passed=0,attempted=0,validated=0,claimed=0,prefix_consumed=0,complete=0,
          completed=0,sent=0,doorbells=0,count=0,pages=0,bytes=0,consumer_writes=0,failure=1)
        for k,v in values.items():raw=old.changed(raw,n.RM_FIELDS.index(k),v)
        f.files['rm-info.bin']=raw
        for name in ('ring-info.bin','contexts-info.bin','plan-info.bin','snapshot-info.bin'):f.files[name]=old.Fake().files[name]
        for name in ('root-capture.bin','children-capture.bin'):f.files[name]=old.Fake().files[name]
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'capture';r=n.capture(f,777,out)
            self.assertEqual(f.requests,[]);self.assertEqual(r['sent_requests_verified'],0);self.assertFalse(r['passed'])

if __name__=='__main__':unittest.main()
