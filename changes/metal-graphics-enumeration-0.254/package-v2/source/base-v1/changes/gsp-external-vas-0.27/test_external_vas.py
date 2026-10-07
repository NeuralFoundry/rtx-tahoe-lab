from pathlib import Path
import json,struct,unittest
import external_vas as e
import gsp_rpc
ROOT=Path(__file__).resolve().parent

def reframe(raw,sequence=None,**fields):
    p=gsp_rpc.decode_record(raw)
    return gsp_rpc.encode_record(p.rpc.function,fields.pop('payload',p.rpc.payload),transport_sequence=p.sequence if sequence is None else sequence,
        result=fields.pop('result',p.rpc.result),private_result=fields.pop('private_result',p.rpc.private_result),**fields)

class External(unittest.TestCase):
    def test_five_requests_match_native_and_sdk_layout(self):
        native=(ROOT/'windows/requests.bin').read_bytes();self.assertEqual(len(native),5*4096)
        self.assertEqual(b''.join(e.request(i) for i in range(5)),native)
        self.assertEqual(b''.join(e.parameters(i) for i in range(5)),(ROOT/'windows/sdk-parameters.bin').read_bytes())
        p=gsp_rpc.decode_record(e.request(4)).rpc
        self.assertEqual(p.function,54);self.assertEqual(len(p.payload),48)
        self.assertEqual(struct.unpack('<4IQ6I',p.payload),(e.CLIENT,e.OBJECTS[1],0xffffffff,0,e.ROOT_PA,4,8,e.OBJECTS[3],0,1,0xffffffff))

    def test_five_synthetic_replies_and_root_output(self):
        data=(ROOT/'windows/replies.bin').read_bytes()
        for i in range(5):self.assertTrue(e.reply(i,21+i,data[i*4096:(i+1)*4096])['accepted'])
        root=gsp_rpc.decode_record(data[:4096]).rpc
        self.assertEqual(struct.unpack_from('<I',root.payload,32)[0],e.CLIENT)
        self.assertEqual(e.parameters(0)[:4],bytes(4))

    def test_bootstrap_root_success_is_not_user_client_success(self):
        raw=(ROOT/'evidence/bootstrap-root-reply.bin').read_bytes()
        p=gsp_rpc.decode_record(raw)
        self.assertEqual(p.rpc.result,0)
        with self.assertRaisesRegex(ValueError,'ownership'):e.reply(0,p.sequence,raw)

    def test_status_sequence_and_ownership_mutations(self):
        data=(ROOT/'windows/replies.bin').read_bytes()
        for i in range(5):
            raw=data[i*4096:(i+1)*4096]
            for bad in (reframe(raw,result=31),reframe(raw,private_result=1),reframe(raw,sequence=99),raw[:-1]):
                with self.subTest(step=i),self.assertRaises(ValueError):e.reply(i,21+i,bad)
            p=bytearray(gsp_rpc.decode_record(raw).rpc.payload)
            if i==4:p.extend(bytes(4))
            else:p[0]^=1
            with self.assertRaises(ValueError):e.reply(i,21+i,reframe(raw,payload=bytes(p)))

    def test_va_extent_and_internal_overlap(self):
        raw=(ROOT/'windows/replies.bin').read_bytes()[3*4096:4*4096];p=gsp_rpc.decode_record(raw).rpc.payload
        for off,value in ((40,0),(40,e.REQUIRED_HI-4096),(72,0),(72,e.REQUIRED_LO+4096),(48,e.REQUIRED_LO),(56,1<<49)):
            mutated=bytearray(p);struct.pack_into('<Q',mutated,off,value)
            with self.subTest(off=off,value=value),self.assertRaises(ValueError):e.reply(3,24,reframe(raw,payload=bytes(mutated)))

    def test_request_step_bounds(self):
        for i in (-1,5,True,None,0.0):
            with self.assertRaises(ValueError):e.request(i)

if __name__=='__main__':unittest.main()
