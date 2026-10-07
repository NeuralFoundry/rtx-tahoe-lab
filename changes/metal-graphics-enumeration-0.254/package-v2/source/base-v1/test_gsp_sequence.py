import struct
import tempfile
import unittest
from pathlib import Path
import gsp_rpc
import gsp_event_codec as initial
import gsp_ring_event_codec as ring
import gsp_sequence_codec as sequence
from gsp_sequence_client import RestrictedBackend, BindingError


def seq_row(passed=False):
    r=dict.fromkeys(sequence.FIELDS,0)
    r.update(magic=0x5254585345513031,abi=1,generation=17,opcode=0xffffffff,
             workspace_start=0x173c40000,workspace_end=0x173e00000,
             reader_before=0xffffffff,reader_after=0xffffffff,producer=0xffffffff,
             budget_ns=15_000_000_000,max_ticks=200000,payload_bytes=6296,used_words=1564,capacity_words=16354,operation_count=420)
    if passed:
        for key in ('validated','attempted','passed','falcon_start','falcon_halted','sec2_start','resumed',
                    'workspace_owned','consumer_attempted','consumer_written','consumer_verified',
                    'sequence_claimed','gsp_start_noted','sec2_start_noted'):r[key]=1
        r.update(completed=420,word=1564,opcode=8,ticks=1000,reads=145,writes=334,polls=110,resets=2,
                 imem_commands=64,dmem_commands=36,falcon_cpu=0x10,riscv=0x80,bcr=0x111,
                 handoff=0x04000000,elapsed_ns=1000000,libos_args=0x123456000,reader_before=0,reader_after=3,producer=3)
    return r


def pack_seq(r):return struct.pack('<64Q',*(r[k] for k in sequence.FIELDS))
def pack_ring(r):return struct.pack('<32Q',*(r[k] for k in ring.FIELDS))


def ring_fixture(packets,stop=4):
    index=[];offset=0;nocats=0
    for i,p in enumerate(packets):
        rpc=gsp_rpc.decode_record(p,expected_sequence=2+i).rpc
        flags=ring.classify(rpc.function,rpc.result,len(rpc.payload));nocats+=bool(flags&4)
        index.append(struct.pack('<9Q',offset,len(p),rpc.function,rpc.result,2+i,len(rpc.payload),flags,(3+offset//4096)%63,i))
        offset+=len(p)
    r=dict.fromkeys(ring.FIELDS,0)
    r.update(magic=0x52545845564e5431,abi=2,generation=17,stop=stop,header_valid=1,
             passed=int(bool(packets) and stop in (1,2,3,4)),count=len(packets),pages=offset//4096,bytes=offset,
             producer=(3+offset//4096)%63,polls=100,elapsed_ns=5_000_000_000,init_done=int(stop==1),sequencer=int(stop==2),
             nocat_count=nocats,failed_slot=0xffffffff,reader=3,hdr1=0x40000,hdr2=4096,hdr3=63,
             hdr4=(3+offset//4096)%63,hdr5=1,hdr6=32,hdr7=4096,max_records=62,max_pages=62,duration_ns=5_000_000_000,max_polls=50000)
    return r,b''.join(index)


class Backend(RestrictedBackend):
    def __init__(self,packets):self.data=b''.join(packets);self.index=ring_fixture(packets)[1];self.calls=[];self.closes=0
    def _invoke(self,sel,scalars,data,size):
        self.calls.append((sel,scalars,size))
        if sel==21:return self.index[scalars[0]*72:(scalars[0]+scalars[1])*72]
        if sel==22:return self.data[scalars[0]:scalars[0]+scalars[1]]
        return bytes(size)
    def _close(self):self.closes+=1


class SequenceTests(unittest.TestCase):
    def test_size_and_not_run(self):
        self.assertEqual(len(sequence.FIELDS),64)
        self.assertFalse(sequence.decode(pack_seq(seq_row()),17)['passed'])
    def test_complete_sequence(self):self.assertTrue(sequence.decode(pack_seq(seq_row(True)),17)['passed'])
    def test_partial_retained_diagnostic(self):
        r=seq_row(True);r.update(passed=0,resumed=0,sec2_start=0,sec2_start_noted=0,failure=8,completed=418,word=1562)
        self.assertFalse(sequence.decode(pack_seq(r),17)['passed'])
    def test_rejects_wrong_wire_identity(self):
        for raw in (bytes(511),bytes(513),bytearray(512)):
            with self.assertRaises(ValueError):sequence.decode(raw,17)
        for key in ('magic','abi','generation'):
            r=seq_row();r[key]+=1
            with self.assertRaises(ValueError):sequence.decode(pack_seq(r),17)
    def test_each_required_success_fact(self):
        for key,value in [('validated',0),('attempted',0),('resumed',0),('workspace_owned',0),('consumer_verified',0),
                          ('sequence_claimed',0),('gsp_start_noted',0),('sec2_start_noted',0),('falcon_start',0),
                          ('falcon_halted',0),('sec2_start',0),('completed',419),('word',1563),('resets',1),
                          ('imem_commands',63),('dmem_commands',35),('sec2_mailbox0',1),('riscv',0),('bcr',1),
                          ('handoff',0),('falcon_cpu',0),('libos_args',1),('failure',1)]:
            r=seq_row(True);r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):sequence.decode(pack_seq(r),17)
    def test_rejects_changed_limits_or_lease(self):
        for key in ('budget_ns','max_ticks','payload_bytes','used_words','capacity_words','operation_count','workspace_start','workspace_end','reserved'):
            r=seq_row();r[key]+=1
            with self.subTest(key=key),self.assertRaises(ValueError):sequence.decode(pack_seq(r),17)
    def test_consumer_inconsistent(self):
        for key,val in [('consumer_attempted',0),('consumer_written',0),('consumer_failure',1),('reader_before',1),('reader_after',4),('producer',2)]:
            r=seq_row(True);r[key]=val
            with self.subTest(key=key),self.assertRaises(ValueError):sequence.decode(pack_seq(r),17)
    def test_bad_counts(self):
        for key,val in [('ticks',200002),('writes',401),('polls',200001),('failure',14),('completed',421),('opcode',9),('after_stop',6)]:
            r=seq_row();r[key]=val
            with self.assertRaises(ValueError):sequence.decode(pack_seq(r),17)
    def test_continuation_requires_execution(self):
        r=seq_row();r.update(after_stop=4,after_slot=3,after_sequence=2)
        with self.assertRaises(ValueError):sequence.decode(pack_seq(r),17)
    def test_cross_check_and_init_done(self):
        r=seq_row(True);r.update(after_count=1,after_pages=1,after_bytes=4096,after_slot=3,after_sequence=2,
                               after_published=1,after_stop=1,after_passed=1,after_init_done=1,init_done_observed=1)
        row,_=ring_fixture([gsp_rpc.encode_record(0x1001,bytes(4),transport_sequence=2,result=0)],1)
        decoded=ring.decode_info(pack_ring(row),17)
        sequence.cross_check(sequence.decode(pack_seq(r),17),{'init_done':0},decoded)
        for key in ('count','pages','bytes','published','stop','passed','init_done','sequencer'):
            bad=dict(decoded);bad[key]+=1
            with self.assertRaises(ValueError):sequence.cross_check(r,{'init_done':0},bad)
    def test_ring_not_run(self):
        row,_=ring_fixture([],0)
        row.update(header_valid=0,polls=0,elapsed_ns=0,producer=0,reader=0,hdr4=0)
        self.assertEqual(ring.decode_info(pack_ring(row),17)['published'],0)
    def test_ring_origin_and_frame_agreement(self):
        packets=[gsp_rpc.encode_record(0x101e,bytes(5000),transport_sequence=2,result=0),
                 gsp_rpc.encode_record(0x1001,bytes(4),transport_sequence=3,result=0)]
        row,index=ring_fixture(packets,1);decoded=ring.decode_info(pack_ring(row),17)
        self.assertEqual([r['slot'] for r in ring.decode_index(index,decoded)],[3,5])
        with tempfile.TemporaryDirectory() as tmp:
            backend=Backend(packets);details=ring.capture(backend,decoded,Path(tmp));backend.close()
            self.assertTrue(details[-1]['init_done']['validated_init_done_record'])
            self.assertEqual({c[0] for c in backend.calls},{21,22});self.assertEqual(backend.closes,1)
    def test_ring_wrap(self):
        packets=[gsp_rpc.encode_record(0x101e,b'',transport_sequence=i+2,result=0) for i in range(62)]
        row,index=ring_fixture(packets,3);decoded=ring.decode_info(pack_ring(row),17)
        self.assertEqual(decoded['producer'],2);self.assertEqual(decoded['published'],62)
        rows=ring.decode_index(index,decoded)
        self.assertEqual([r['slot'] for r in rows[-3:]],[62,0,1])
    def test_reader_origin_wrong(self):
        row,_=ring_fixture([])
        for value in (0,2,4):
            row['reader']=value
            with self.assertRaises(ValueError):ring.decode_info(pack_ring(row),17)
    def test_ring_index_wrong_sequence_and_slot(self):
        packets=[gsp_rpc.encode_record(0x101e,b'',transport_sequence=2,result=0)]
        row,index=ring_fixture(packets)
        for off,val in ((32,0),(56,0)):
            raw=bytearray(index);struct.pack_into('<Q',raw,off,val)
            with self.assertRaises(ValueError):ring.decode_index(bytes(raw),row)
    def test_wrapped_failure_slot(self):
        packets=[gsp_rpc.encode_record(0x101e,b'',transport_sequence=2+i,result=0) for i in range(61)]
        row,index=ring_fixture(packets,5);row.update(failure=9,failed_slot=1)
        self.assertEqual(len(ring.decode_index(index,ring.decode_info(pack_ring(row),17))),61)
        row['failed_slot']=61
        with self.assertRaises(ValueError):ring.decode_info(pack_ring(row),17)
    def test_backend_bounded_readonly_selectors(self):
        b=Backend([]);b.sequence_info();b.after_info();b.close();b.close()
        self.assertEqual(b.calls,[(19,(),512),(20,(),256)]);self.assertEqual(b.closes,1)
        with self.assertRaises(BindingError):b.sequence_info()
        for start,count in ((-1,1),(62,1),(61,2),(0,33),(0,0),(True,1)):
            with self.assertRaises(ValueError):Backend([]).after_index(start,count)
        for off,count in ((-1,1),(253952,1),(253951,2),(0,4097),(0,0),(True,1)):
            with self.assertRaises(ValueError):Backend([]).after_data(off,count)


if __name__=='__main__':unittest.main()
