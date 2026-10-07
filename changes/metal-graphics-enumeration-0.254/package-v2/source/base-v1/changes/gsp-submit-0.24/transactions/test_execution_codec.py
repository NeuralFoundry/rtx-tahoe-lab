import hashlib,struct,unittest
from pathlib import Path
import execution_codec as e
import execution_transcript as t
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]

def repair(raw):
    b=bytearray(raw);struct.pack_into('<I',b,32,0);end=(48+struct.unpack_from('<I',b,56)[0]+7)&~7
    checksum=0
    for offset in range(0,end,4):checksum^=struct.unpack_from('<I',b,offset)[0]
    struct.pack_into('<I',b,32,checksum);return bytes(b)

class ExecutionCodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        d=ROOT/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm'
        raw,cls.gr=e.p.c.load_record(d/'record-011.bin',5,11)
        assert hashlib.sha256(raw).hexdigest()=='f061c08ec585f56bb9f9796ae67eedd15976337c83b646f15ee321070b8ff67f'
        cls.fifo=(d/'record-010.bin').read_bytes()
        assert hashlib.sha256(cls.fifo).hexdigest()=='54e227343795f679b161b7445bb625333af1bea202452eed148210bb30e00323'
        cls.golden=e.p.c.context_plan(cls.gr);cls.plan=e.p.make(cls.gr,cls.golden)
        cls.requests=(HERE/'windows/requests.bin').read_bytes();cls.replies=(HERE/'windows/replies.bin').read_bytes()

    def test_native_requests_and_replies(self):
        requests=b''.join(e.request(i,e.FIRST+i,self.golden,3,self.plan) for i in range(e.STEPS));self.assertEqual(requests,self.requests)
        replies=[]
        for i in range(e.STEPS):
            raw=bytearray(requests[i*4096:(i+1)*4096]);struct.pack_into('<I',raw,36,100+i);struct.pack_into('<II',raw,64,0,0)
            if i==e.SHARE_STEP:struct.pack_into('<I',raw,120,1)
            if i==e.CHANNEL_STEP:struct.pack_into('<I',raw,112+136,1)
            if i==e.SIZE_STEP:raw[104:104+1664]=self.gr
            if i==e.FIFO_STEP:raw[104:104+3212]=self.fifo[104:104+3212]
            if i==e.TOKEN_STEP:struct.pack_into('<I',raw,104,4)
            raw=repair(raw);replies.append(raw)
            result=e.reply(raw,100+i,i,self.golden,3,self.plan)
            self.assertTrue(result['accepted']);self.assertFalse(result['hardware_accessed']);self.assertFalse(result['doorbell_ready'])
        self.assertEqual(b''.join(replies),self.replies)
        result=t.verify(requests,self.replies,100,self.golden,3)
        self.assertEqual((result['completed'],result['candidate'],result['records'],result['events'],result['next_sequence']),(e.STEPS,4,e.STEPS,0,100+e.STEPS))

    def test_reply_identity_and_echo_mutations(self):
        for step in range(e.STEPS):
            raw=self.replies[step*4096:(step+1)*4096]
            for offset in (36,48,52,60,64,68,72,80,84,88,92,96,100):
                bad=bytearray(raw);bad[offset]^=1
                with self.assertRaises(ValueError):e.reply(repair(bad),100+step,step,self.golden,3,self.plan)
            if step not in (e.SIZE_STEP,e.FIFO_STEP,e.TOKEN_STEP):
                for offset in range(e.SIZES[step]):
                    if step==e.CHANNEL_STEP and offset==136:continue
                    bad=bytearray(raw);bad[80+(32 if step in e.ALLOC else 24)+offset]^=1
                    if (step==e.CHANNEL_STEP and (132<=offset<136 or 240<=offset<244)) or (step==e.SHARE_STEP and offset==8):
                        self.assertTrue(e.reply(repair(bad),100+step,step,self.golden,3,self.plan)['accepted'])
                    else:
                        with self.assertRaises(ValueError):e.reply(repair(bad),100+step,step,self.golden,3,self.plan)

    def test_share_veid_boundaries(self):
        for value in (0,1,63,64,0xffffffff):
            page=bytearray(self.replies[e.SHARE_STEP*4096:(e.SHARE_STEP+1)*4096])
            struct.pack_into('<I',page,120,value)
            if value<64:
                reply=e.reply(repair(page),100+e.SHARE_STEP,e.SHARE_STEP,self.golden,3,self.plan)
                self.assertEqual(reply['subcontext_id'],value)
                self.assertNotIn('raw_token',reply)
                self.assertNotIn('channel_id',reply)
            else:
                with self.assertRaises(ValueError):e.reply(repair(page),100+e.SHARE_STEP,e.SHARE_STEP,self.golden,3,self.plan)

    def test_request_bounds_and_context_prerequisite(self):
        for step in range(e.STEPS):
            for cid in (0,2,4,True,None):
                with self.assertRaises(ValueError):e.request(step,e.FIRST+step,self.golden,cid,self.plan)
            for sequence in (-1,0xffffffff,True,None):
                with self.assertRaises(ValueError):e.request(step,sequence,self.golden,3,self.plan)
        for step in (-1,e.STEPS,True,None):
            with self.assertRaises(ValueError):e.request(step,14,self.golden,3,self.plan)
        self.assertEqual(len(e.request(0,14,self.golden,3,None)),4096)
        for step in (e.PHYSICAL_STEP,e.VIRTUAL_STEP):
            with self.assertRaises(ValueError):e.request(step,e.FIRST+step,self.golden,3,None)

    def test_changed_fresh_gr_changes_both_promotions(self):
        fresh=bytearray(self.gr);struct.pack_into('<I',fresh,0,struct.unpack_from('<I',fresh)[0]+0x20000);struct.pack_into('<I',fresh,16*8,32768)
        plan=e.p.make(bytes(fresh),self.golden);self.assertEqual(plan['backing_bytes'],1167360)
        requests=b''.join(e.request(step,e.FIRST+step,self.golden,3,plan) for step in (e.PHYSICAL_STEP,e.VIRTUAL_STEP))
        self.assertEqual(requests,(HERE/'windows/changed-promotions.bin').read_bytes())
        for step in (e.PHYSICAL_STEP,e.VIRTUAL_STEP):
            with self.assertRaises(ValueError):e.reply(self.replies[step*4096:(step+1)*4096],100+step,step,self.golden,3,plan)
        altered=bytearray(self.replies);page=bytearray(altered[e.SIZE_STEP*4096:(e.SIZE_STEP+1)*4096]);page[104:104+1664]=fresh;altered[e.SIZE_STEP*4096:(e.SIZE_STEP+1)*4096]=repair(page)
        with self.assertRaises(ValueError):t.verify(self.requests,bytes(altered),100,self.golden,3)

    def test_token_and_runlist_identity(self):
        for token in (0,3,5,4095,0x10004,0xffffffff):
            raw=bytearray(self.replies[e.TOKEN_STEP*4096:]);struct.pack_into('<I',raw,104,token)
            with self.assertRaises(ValueError):e.reply(repair(raw),100+e.TOKEN_STEP,e.TOKEN_STEP,self.golden,3,self.plan)
        for runlist in (128,0xffffffff):
            raw=bytearray(self.replies[e.FIFO_STEP*4096:(e.FIFO_STEP+1)*4096]);struct.pack_into('<I',raw,104+12+12,runlist)
            with self.assertRaises(ValueError):e.reply(repair(raw),100+e.FIFO_STEP,e.FIFO_STEP,self.golden,3,self.plan)

    def test_missing_swapped_and_extra_replies(self):
        for step in range(e.STEPS-1):
            pages=[self.replies[i*4096:(i+1)*4096] for i in range(e.STEPS)]
            pages[step],pages[step+1]=pages[step+1],pages[step]
            for i in (step,step+1):
                page=bytearray(pages[i]);struct.pack_into('<I',page,36,100+i);pages[i]=repair(page)
            with self.assertRaises(ValueError):t.verify(self.requests,b''.join(pages),100,self.golden,3)
        for replies in (self.replies[:-4096],self.replies+self.replies[:4096],self.replies[:-1]):
            with self.assertRaises(ValueError):t.verify(self.requests,replies,100,self.golden,3)
        for i in range(e.STEPS):
            requests=bytearray(self.requests);requests[i*4096+84]^=1
            with self.assertRaises(ValueError):t.verify(bytes(requests),self.replies,100,self.golden,3)

    def test_allowed_events_are_bounded(self):
        for count in (1,3,4):
            pages=[e.rpc.encode_record(0x100c,bytes(8),transport_sequence=100+i,result=0,private_result=0) for i in range(count)]
            for i in range(e.STEPS):
                page=bytearray(self.replies[i*4096:(i+1)*4096]);struct.pack_into('<I',page,36,100+count+i);pages.append(repair(page))
            if count==4:
                with self.assertRaises(ValueError):t.verify(self.requests,b''.join(pages),100,self.golden,3)
            else:
                result=t.verify(self.requests,b''.join(pages),100,self.golden,3)
                self.assertEqual((result['events'],result['records'],result['next_sequence']),(count,count+e.STEPS,count+100+e.STEPS))

    def test_malformed_and_sequence_limits(self):
        for raw in (self.replies[:4095],self.replies[:79],b''):
            with self.assertRaises(ValueError):e.reply(raw,100,0,self.golden,3,self.plan)
        for sequence in (0xffffffff,0xffffffff-15,-1,True):
            with self.assertRaises(ValueError):t.verify(self.requests,self.replies,sequence,self.golden,3)
        raw=bytearray(self.replies);struct.pack_into('<I',raw,40,16)
        with self.assertRaises(ValueError):t.verify(self.requests,bytes(raw),100,self.golden,3)
        raw=bytearray(self.replies);raw[32]^=1
        with self.assertRaises(ValueError):t.verify(self.requests,bytes(raw),100,self.golden,3)
        with self.assertRaises(ValueError):t.verify(bytearray(self.requests),self.replies,100,self.golden,3)

if __name__=='__main__':unittest.main()
