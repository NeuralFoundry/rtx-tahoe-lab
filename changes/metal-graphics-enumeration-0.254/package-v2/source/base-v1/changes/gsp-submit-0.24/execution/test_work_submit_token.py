import hashlib,json,re,struct,unittest
from pathlib import Path
import work_submit_token as t
ROOT=Path(__file__).resolve().parent
PACKET=ROOT.parents[2]/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/record-010.bin'
def changed(raw,offset,value):
    p=bytearray(raw);struct.pack_into('<I',p,offset,value);struct.pack_into('<I',p,32,0)
    end=(48+struct.unpack_from('<I',p,56)[0]+7)&~7;checksum=0
    for i in range(0,end,4):checksum^=struct.unpack_from('<I',p,i)[0]
    struct.pack_into('<I',p,32,checksum);return bytes(p)
class Token(unittest.TestCase):
    def test_historical_fifo_hash_and_route(self):
        raw=PACKET.read_bytes();self.assertEqual(hashlib.sha256(raw).hexdigest(),'54e227343795f679b161b7445bb625333af1bea202452eed148210bb30e00323')
        r=t.fifo(raw,10);self.assertEqual((r['id'],r['entry'],r['pbdma'],r['fault']),(0,0,[0,1],[32,33]))
        token=t.compose(r,37,37);self.assertEqual(token['candidate'],37);self.assertFalse(token['doorbell_ready'])
    def test_vendor_sources_and_ga106_dispatch(self):
        for r in json.loads((ROOT/'reference/manifest.json').read_text()):self.assertEqual(hashlib.sha256((ROOT/'reference'/r['file']).read_bytes()).hexdigest(),r['sha256'])
        source=(ROOT/'reference/g_kernel_fifo_nvoc.c').read_text()
        branch=re.search(r'/\* ChipHal: GA100 \| GA102 \| GA103 \| GA104 \| GA106 .*?__kfifoGenerateWorkSubmitTokenHal__ = &kfifoGenerateWorkSubmitTokenHal_GA100;',source,re.S)
        self.assertIsNotNone(branch)
        bits=(ROOT/'reference/dev_ctrl.h').read_text()
        self.assertRegex(bits,r'NV_CTRL_VF_DOORBELL_VECTOR\s+11:0')
        self.assertRegex(bits,r'NV_CTRL_VF_DOORBELL_RUNLIST_ID\s+22:16')
        impl=(ROOT/'reference/kernel_fifo_ga100.c').read_text()
        self.assertIn('FLD_SET_DRF_NUM(_CTRL, _VF_DOORBELL, _RUNLIST_ID, runlistId, val)',impl)
        self.assertIn('FLD_SET_DRF_NUM(_CTRL, _VF_DOORBELL, _VECTOR,     chId,      val)',impl)
    def test_routing_corruption_rejected(self):
        raw=PACKET.read_bytes()
        for offset,value in ((104,1),(108,0),(108,33),(112,1),(124,9),(224,1),(128,128),(196,0),(196,3),(180,32),(184,0),(188,256),(192,32)):
            with self.subTest(offset=offset,value=value),self.assertRaises(ValueError):t.fifo(changed(raw,offset,value),10)
    def test_identity_and_sequence_rejected(self):
        raw=PACKET.read_bytes()
        for off in (36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100):
            value=struct.unpack_from('<I',raw,off)[0]^1
            with self.subTest(off=off),self.assertRaises(ValueError):t.fifo(changed(raw,off,value),10)
        with self.assertRaises(ValueError):t.fifo(raw[:-1],10)
        with self.assertRaises(ValueError):t.fifo(raw,11)
    def test_channel_token_and_field_bounds(self):
        r=t.fifo(PACKET.read_bytes(),10)
        for channel,token in ((-1,-1),(4096,4096),(37,38),(37,0x10025),(True,1),(1,True)):
            with self.assertRaises(ValueError):t.compose(r,channel,token)
        for value in (-1,128,True):
            bad=dict(r,id=value)
            with self.assertRaises(ValueError):t.compose(bad,37,37)
        with self.assertRaises(ValueError):t.compose(dict(r,valid=False),37,37)
    def test_field_edges_and_no_implicit_runlist_zero(self):
        r=t.fifo(PACKET.read_bytes(),10)
        for runlist in (0,1,63,127):
            for channel in (0,1,37,2047,4095):
                candidate=t.compose(dict(r,id=runlist),channel,channel)
                self.assertEqual(candidate['candidate'],runlist*65536+channel)
                self.assertFalse(candidate['doorbell_ready'])
        with self.assertRaises(ValueError):t.compose({'valid':True},37,37)
if __name__=='__main__':unittest.main()
