"""Replay the actual rejected 0.26 channel request and pin the required four-byte fix."""
import hashlib,re,struct,unittest
from pathlib import Path
import gsp_execution_native as native
from test_execution_identity import repaired
e=native.transcript.e
ROOT=Path(__file__).parent
class ContextVasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder=ROOT/'evidence/context-vas026'
        cls.request=(folder/'channel-request.bin').read_bytes()
        cls.response=(folder/'channel-reply.bin').read_bytes()
        _,gr=e.p.c.load_record(ROOT/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/record-011.bin',5,11)
        cls.golden=e.p.c.context_plan(gr)

    def test_actual_failure_has_both_nonzero_handles(self):
        self.assertEqual(hashlib.sha256(self.request).hexdigest(),'f51b18d488ff9bb7b6ba85d19481d887fa8ac82d67dcf83df8b140c180b961fb')
        self.assertEqual(hashlib.sha256(self.response).hexdigest(),'247e8ec4aaaefcb1b4d3aa022ab48ae00889bb48da6070e3a21ba3b9c713e2ef')
        self.assertEqual(struct.unpack_from('<II',self.request,136),(0xcf00000b,0xcf000003))
        reply=e.rpc.decode_record(self.response,expected_sequence=23)
        self.assertEqual(reply.rpc.result,31)
        self.assertEqual(struct.unpack_from('<I',self.response,96)[0],31)

    def test_external_client_preserves_the_corrected_channel_vaspace(self):
        expected=bytearray(self.request);struct.pack_into('<I',expected,140,0)
        struct.pack_into('<I',expected,80,e.p.CLIENT);struct.pack_into('<I',expected,36,e.FIRST+e.CHANNEL_STEP)
        # Extend the synthetic comparison to the proposed user-channel profile.
        struct.pack_into('<I',expected,240,0);struct.pack_into('<I',expected,356,0x14);expected=repaired(expected)
        current=e.request(e.CHANNEL_STEP,e.FIRST+e.CHANNEL_STEP,self.golden,3,None)
        windows=(ROOT/'changes/gsp-submit-0.24/transactions/windows/requests.bin').read_bytes()[8192:12288]
        self.assertEqual(current,expected);self.assertEqual(windows,expected)
        self.assertEqual(struct.unpack_from('<II',current,136),(0xcf00000b,0))
        self.assertEqual(struct.unpack_from('<I',e.parameters(e.SHARE_STEP,self.golden,3,None),0)[0],0xcf000013)

    def test_contradictory_handles_reject_even_if_status_is_success(self):
        old=bytearray(self.response)
        for offset in (64,68,96):struct.pack_into('<I',old,offset,0)
        with self.assertRaises(ValueError):e.reply(repaired(old),23,e.CHANNEL_STEP,self.golden,3,None)
        current=bytearray(e.request(e.CHANNEL_STEP,23,self.golden,3,None))
        for offset in (64,68,96):struct.pack_into('<I',current,offset,0)
        self.assertTrue(e.reply(repaired(current),23,e.CHANNEL_STEP,self.golden,3,None)['accepted'])

    def test_pinned_nvidia_contract_and_error_code(self):
        path=ROOT/'changes/gsp-submit-0.24/reference/kernel_channel.c'
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),'2f366454ca726e80bf91b6902db05d472c7a7ea09029c04b3470370bab9739a5')
        source=path.read_text()
        self.assertIn('if ((hKernelCtxShare != NV01_NULL_OBJECT) && (pChannelGpfifoParams->hVASpace != NV01_NULL_OBJECT))',source)
        start=source.index('// Context share and vaspace handles')
        self.assertIn('return NV_ERR_INVALID_ARGUMENT;',source[start:start+600])
        self.assertIn("TSG channels can't use an explicit vaspace",source)
        codes=(ROOT/'evidence/context-vas026/nvstatuscodes.h').read_bytes()
        self.assertEqual(hashlib.sha256(codes).hexdigest(),'cad1684cc2fafaeb8def4289ab107d7a21706425781af8e96865b850c15f4205')
        self.assertRegex(codes.decode(),r'NV_STATUS_CODE\(NV_ERR_INVALID_ARGUMENT,\s+0x0000001F,')

if __name__=='__main__':unittest.main()
