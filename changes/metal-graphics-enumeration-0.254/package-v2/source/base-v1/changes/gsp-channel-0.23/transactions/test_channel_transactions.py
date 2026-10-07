from pathlib import Path
import struct
import unittest
import channel_transactions as t
import test_channel_plan as source

BASE=Path(__file__).parent


def response(step,plan,transport_sequence=20,params=None,**fields):
    req=t.rpc.decode_record(t.request(step,plan));n=24 if t.FUNCTIONS[step]==76 else 32
    payload=req.rpc.payload if params is None else req.rpc.payload[:n]+params
    return t.rpc.encode_record(t.FUNCTIONS[step],payload,transport_sequence=transport_sequence,result=fields.pop('result',0),
                               private_result=fields.pop('private_result',0),**fields)


class TransactionsTests(unittest.TestCase):
    def setUp(self):self.gr=source.recorded_gr();self.plan=t.c.context_plan(self.gr)

    def test_native_five_request_fixtures(self):
        actual=(BASE/'channel-requests-native.bin').read_bytes()
        expected=b''.join(t.request(step,self.plan) for step in range(5))
        self.assertEqual(actual,expected)
        for step in range(5):
            packet=t.rpc.decode_record(actual[step*4096:(step+1)*4096],expected_sequence=9+step)
            self.assertEqual(packet.rpc.function,t.FUNCTIONS[step])
        self.assertEqual(expected[:4096],t.c.channel_request())

    def test_changed_post_channel_gr_changes_promotion(self):
        changed=bytearray(self.gr);struct.pack_into('<I',changed,0,struct.unpack_from('<I',changed)[0]+0x20000)
        received=t.reply(response(1,self.plan,params=bytes(changed)),1,20)
        plan=received['context_plan'];self.assertGreater(plan['total_backing_bytes'],self.plan['total_backing_bytes'])
        actual=(BASE/'channel-requests-changed-gr-native.bin').read_bytes()
        self.assertEqual(actual,b''.join(t.request(step,plan) for step in range(5)))
        baseline=(BASE/'channel-requests-native.bin').read_bytes()
        self.assertEqual(actual[:8192],baseline[:8192]);self.assertNotEqual(actual[8192:12288],baseline[8192:12288]);self.assertEqual(actual[12288:],baseline[12288:])

    def test_returned_channel_id_is_diagnostic(self):
        params=bytearray(t.c.channel_parameters());struct.pack_into('<II',params,132,37,1)
        result=t.reply(response(0,self.plan,params=bytes(params)),0,20)
        self.assertEqual((result['channel_id'],result['subdevice_mask']),(37,1));self.assertFalse(result['hardware_accessed'])
        struct.pack_into('<I',params,136,2)
        with self.assertRaises(ValueError):t.reply(response(0,self.plan,params=bytes(params)),0,20)

    def test_channel_backing_changes_rejected(self):
        for offset in (8,16,20,28,64,128,144,152,160,168,192,216,244,360):
            params=bytearray(t.c.channel_parameters());params[offset]^=1
            with self.assertRaises(ValueError,msg=str(offset)):t.reply(response(0,self.plan,params=bytes(params)),0,20)

    def test_promotion_echo_required(self):
        self.assertTrue(t.reply(response(2,self.plan),2,20,self.plan)['accepted'])
        original=t.c.promote_parameters(self.plan)
        for offset in (0,12,16,40,48,56,64,72,76,78,79,80,559):
            params=bytearray(original);params[offset]^=1
            with self.assertRaises(ValueError,msg=str(offset)):t.reply(response(2,self.plan,params=bytes(params)),2,20,self.plan)

    def test_identity_checksum_sequence_and_status(self):
        for step in range(5):
            params=self.gr if step==1 else None
            good=response(step,self.plan,params=params)
            self.assertTrue(t.reply(good,step,20,self.plan)['accepted'])
            for fields in ({'result':0x56},{'private_result':1},{'sequence':1}):
                with self.assertRaises(ValueError):t.reply(response(step,self.plan,params=params,**fields),step,20,self.plan)
            with self.assertRaises(ValueError):t.reply(good,step,21,self.plan)
            corrupt=bytearray(good);corrupt[80]^=1
            with self.assertRaises(ValueError):t.reply(bytes(corrupt),step,20,self.plan)
            decoded=t.rpc.decode_record(good)
            for offset in (0,4,8,12,16,20):
                payload=bytearray(decoded.rpc.payload);payload[offset]^=1
                bad=t.rpc.encode_record(t.FUNCTIONS[step],bytes(payload),transport_sequence=20,result=0,private_result=0)
                with self.assertRaises(ValueError):t.reply(bad,step,20,self.plan)

    def test_invalid_post_channel_gr_blocks_planning(self):
        for kind in (0,16,17,18,19,20,23,24):
            for amount,alignment in ((0,4096),(0xffffffff,4096),(4096,0),(4096,3),(4096,0xffffffff)):
                gr=bytearray(self.gr);struct.pack_into('<II',gr,kind*8,amount,alignment)
                with self.assertRaises(ValueError):t.reply(response(1,self.plan,params=bytes(gr)),1,20)

    def test_fixed_classes_and_rejected_steps(self):
        for step,handle,klass in ((3,0xcf000005,0xc7c0),(4,0xcf000006,0xc7b5)):
            req=t.rpc.decode_record(t.request(step,self.plan))
            self.assertEqual(struct.unpack('<8I',req.rpc.payload),(t.c.prep.CLIENT,0xcf000004,handle,klass,0,0,0,0))
        for step in (-1,5,True,'0'):
            with self.assertRaises(ValueError):t.request(step,self.plan)


if __name__=='__main__':unittest.main()
