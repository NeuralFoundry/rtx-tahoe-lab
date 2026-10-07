import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

import gsp_init_done
import gsp_init_event_codec
import gsp_rm_codec as rm
import gsp_rpc


def info():
    r=dict.fromkeys(rm.FIELDS,0)
    r.update(magic=0x525458524d303138,abi=1,generation=42,validated=1,attempted=1,passed=1,step=2,
             completed=3,sent=3,doorbells=3,count=3,pages=3,bytes=12288,tx_writer=5,tx_reader=5,
             rx_reader=10,rx_producer=10,rx_sequence=9,ticks=300,polls=3,imports=30,reads=50,writes=10,publishes=10,
             elapsed_ns=1_000_000,budget_ns=15_000_000_000,max_ticks=150000,initial_reader=7,initial_sequence=6,
             consumer_writes=4,prefix_consumed=1,last_function=103,client=rm.CLIENT,root_object=rm.OBJECTS[0],
             device=rm.OBJECTS[1],subdevice=rm.OBJECTS[2],request_bytes=12288,max_records=16,max_pages=32,
             claimed=1,owned=1,workspace=1,pinned=1,pci_command=6,start_ns=100)
    return r


def packed(r): return struct.pack('<64Q',*(r[k] for k in rm.FIELDS))


class Backend:
    def __init__(self):
        self.requests=b''.join(rm.request(i) for i in range(3))
        self.records=b''.join(gsp_rpc.encode_record(103,gsp_rpc.decode_record(rm.request(i)).rpc.payload,
            transport_sequence=6+i,result=0,private_result=0) for i in range(3))
        self.index=b''.join(struct.pack('<9Q',i*4096,4096,103,0,6+i,32+rm.SIZES[i],i,7+i,100+i) for i in range(3))
    def rm_requests(self,off,n): return self.requests[off:off+n]
    def rm_data(self,off,n): return self.records[off:off+n]
    def rm_index(self,start,n): return self.index[start*72:(start+n)*72]


class RmTests(unittest.TestCase):
    def test_init_empty_and_placeholder(self):
        for body in (b'',bytes(4),b'abcd'):
            raw=gsp_rpc.encode_record(0x1001,body,transport_sequence=5,result=0,private_result=0)
            r=gsp_init_done.parse(raw,expected_sequence=5)
            self.assertEqual(r['payload_bytes'],len(body));self.assertFalse(r['compute_verified'])
            self.assertFalse(r['native_ownership_verified'])
            self.assertEqual(gsp_init_event_codec.classify(0x1001,0,len(body))&1,1)

    def test_init_rejects_wrong_function_result_length_sequence_checksum(self):
        for fn,status,body in ((0x100c,0,b''),(0x1001,1,b''),(0x1001,0,b'x'),(0x1001,0,bytes(8))):
            raw=gsp_rpc.encode_record(fn,body,transport_sequence=5,result=status,private_result=0)
            with self.assertRaises(ValueError):gsp_init_done.parse(raw,expected_sequence=5)
        raw=gsp_rpc.encode_record(0x1001,transport_sequence=5,result=0,private_result=0)
        with self.assertRaises(ValueError):gsp_init_done.parse(raw,expected_sequence=6)
        changed=bytearray(raw);changed[32]^=1
        with self.assertRaises(ValueError):gsp_init_done.parse(bytes(changed),expected_sequence=5)

    def test_real_init_done(self):
        raw=Path('results/gsp-sequencer-20260906T192106Z/after-sequence/event-005.bin').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),'2b3e342c40324e6e0c081224b62ce2e7dfe24ac2f61254e50c24f259641b6181')
        self.assertTrue(gsp_init_done.parse(raw,expected_sequence=5)['empty_payload'])

    def test_fixed_alloc_abi(self):
        for i in range(3):
            r=gsp_rpc.decode_record(rm.request(i),expected_sequence=i+2)
            self.assertEqual((r.rpc.function,r.rpc.result,r.rpc.private_result),(103,0xffffffff,0xffffffff))
            self.assertEqual(len(r.rpc.payload),32+rm.SIZES[i])
            if i==0:self.assertEqual(r.rpc.payload[32:],bytes(120))
            if i==1:self.assertEqual(struct.unpack_from('<I',r.rpc.payload,36)[0],rm.CLIENT)
        for i in (-1,3,True,'0'):
            with self.assertRaises(ValueError):rm.request(i)

    def test_info_success(self):
        r=rm.decode(packed(info()),42)
        self.assertEqual(r['completed'],3)

    def test_info_rejects_changed_identity_limits_and_proofs(self):
        changes=dict(magic=1,abi=2,generation=43,validated=0,claimed=0,owned=0,workspace=0,pinned=0,
                     pci_command=0,completed=2,sent=4,doorbells=2,count=17,pages=33,bytes=1,tx_writer=4,
                     tx_reader=4,rx_reader=11,rx_sequence=10,ticks=150002,consumer_writes=3,last_function=0,
                     last_result=1,last_param_status=1,prefix_consumed=0,client=1,request_bytes=0,
                     max_ticks=1,budget_ns=1,max_pages=33,elapsed_ns=15_000_000_000,reserved50=1)
        for key,value in changes.items():
            with self.subTest(field=key):
                r=info();r[key]=value
                with self.assertRaises(ValueError):rm.decode(packed(r),42)

    def test_info_rejects_mutable_or_short_data(self):
        for raw in (b'',bytes(511),bytearray(packed(info()))):
            with self.assertRaises(ValueError):rm.decode(raw,42)

    def test_reply_handles_and_parameter_status(self):
        raw=Backend().records[:4096]
        self.assertEqual(rm.reply(raw,0,6)['hclass'],0)
        p=gsp_rpc.decode_record(raw).rpc
        for offset in (0,4,8,12,16,20,24,28):
            changed=bytearray(p.payload);changed[offset]^=1
            bad=gsp_rpc.encode_record(103,bytes(changed),transport_sequence=6,result=0,private_result=0)
            with self.assertRaises(ValueError):rm.reply(bad,0,6)

    def test_capture_cross_checks_requests_and_replies(self):
        with tempfile.TemporaryDirectory() as folder:
            r=rm.capture(Backend(),info(),dict(init_done=1,pages=4,count=4),Path(folder))
            self.assertTrue(r['exchanges_verified']);self.assertEqual(r['successful_reply_steps'],[0,1,2])
            self.assertFalse(r['compute_verified']);self.assertFalse(r['metal_verified'])

    def test_capture_rejects_changed_request_index_and_packet(self):
        for attr,offset in (('requests',116),('index',0),('index',7*8),('records',80)):
            b=Backend();changed=bytearray(getattr(b,attr));changed[offset]^=1;setattr(b,attr,bytes(changed))
            with tempfile.TemporaryDirectory() as folder:
                with self.assertRaises(ValueError):rm.capture(b,info(),dict(init_done=1,pages=4,count=4),Path(folder))

    def test_capture_rejects_missing_init_or_wrong_origin(self):
        for a in (dict(init_done=0,pages=4,count=4),dict(init_done=1,pages=5,count=4),dict(init_done=1,pages=4,count=5)):
            with tempfile.TemporaryDirectory() as folder:
                with self.assertRaises(ValueError):rm.capture(Backend(),info(),a,Path(folder))


if __name__=='__main__':unittest.main()
