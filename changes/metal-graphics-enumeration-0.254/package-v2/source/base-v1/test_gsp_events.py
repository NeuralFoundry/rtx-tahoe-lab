import hashlib
import struct
import tempfile
import unittest
from pathlib import Path
import gsp_event_codec as codec
import gsp_event_client as client
import gsp_rpc


def fixture(packets, stop=4, failure=0):
    rows, offset, nocats = [], 0, 0
    for i, raw in enumerate(packets):
        rpc = gsp_rpc.decode_record(raw, expected_sequence=i).rpc
        flags = codec.classify(rpc.function, rpc.result, len(rpc.payload))
        rows.append((offset, len(raw), rpc.function, rpc.result, i, len(rpc.payload), flags, offset//4096, i))
        nocats += bool(flags & 4); offset += len(raw)
    row = dict.fromkeys(codec.FIELDS, 0)
    row.update(magic=0x52545845564e5431, abi=1, generation=17, stop=stop, header_valid=1,
               passed=int(stop in (1, 2, 3, 4) and bool(rows)), count=len(rows), pages=offset//4096,
               bytes=offset, producer=offset//4096, polls=20, elapsed_ns=5_000_000_000,
               init_done=int(stop == 1), sequencer=int(stop == 2), nocat_count=nocats,
               failure=failure, failed_slot=offset//4096 if failure else 0xffffffff,
               hdr1=0x40000, hdr2=4096, hdr3=63, hdr4=offset//4096, hdr5=1, hdr6=32, hdr7=4096,
               max_records=62, max_pages=62, duration_ns=5_000_000_000, max_polls=50000)
    return row, b''.join(struct.pack('<9Q', *r) for r in rows)


def pack(row): return struct.pack('<32Q', *(row[k] for k in codec.FIELDS))
def packet(i, fn=0x101e, body=b'', result=0):
    return gsp_rpc.encode_record(fn, body, transport_sequence=i, result=result)


class Backend(client.RestrictedBackend):
    def __init__(self, packets):
        self.data=b''.join(packets); self.index=fixture(packets)[1]; self.calls=[]; self.closes=0
    def _invoke(self, selector, scalars, data, output_size):
        self.calls.append((selector, scalars, output_size))
        if selector == 17:
            start, count = scalars
            return self.index[start*72:(start+count)*72]
        if selector == 18:
            off, size = scalars
            return self.data[off:off+size]
        return bytes(output_size)
    def _close(self): self.closes += 1


class EventTests(unittest.TestCase):
    def test_multi_page_then_init_done(self):
        packets=[packet(0), packet(1, body=bytes(5000)), packet(2, 0x1001, bytes(4))]
        row, index=fixture(packets, 1); summary=codec.decode_info(pack(row),17)
        self.assertEqual([r['slot'] for r in codec.decode_index(index,summary)], [0,1,3])
        with tempfile.TemporaryDirectory() as tmp:
            backend=Backend(packets); details=codec.capture(backend,summary,Path(tmp)); backend.close()
            self.assertTrue(details[-1]['init_done']['validated_init_done_record'])
            self.assertEqual(details[1]['sha256'],hashlib.sha256(packets[1]).hexdigest())
            self.assertEqual({c[0] for c in backend.calls},{17,18}); self.assertEqual(backend.closes,1)
    def test_real_assert_then_init_done(self):
        raw=(Path(__file__).parent/'results/gsp-first-boot-fix-20260906T172355Z/first-status.bin').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),'18f195493d3068be1c0ce542899740c2408010fc4ea8f7bcbd1dbd13d50de2e0')
        packets=[raw,packet(1,0x1001,bytes(4))]; row,_=fixture(packets,1)
        with tempfile.TemporaryDirectory() as tmp:
            details=codec.capture(Backend(packets),codec.decode_info(pack(row),17),Path(tmp))
            self.assertEqual(details[0]['nocat']['source'],'ASSERT')
            self.assertEqual(details[0]['nocat']['error_code'],'0x132d3b2')
            self.assertTrue(details[0]['nocat']['assert_record'])
            self.assertTrue(details[1]['init_done']['validated_init_done_record'])
    def test_empty_deadline_does_not_pass(self):
        row,index=fixture([]); summary=codec.decode_info(pack(row),17)
        self.assertFalse(summary['passed']); self.assertEqual(codec.decode_index(index,summary),[])
    def test_deadline_capture_is_distinct_from_init(self):
        row,_=fixture([packet(0)]); summary=codec.decode_info(pack(row),17)
        self.assertTrue(summary['passed']); self.assertFalse(summary['init_done'])
    def test_failure_preserves_verified_prefix(self):
        packets=[packet(0)]; row,index=fixture(packets,5,9); summary=codec.decode_info(pack(row),17)
        self.assertFalse(summary['passed']); self.assertEqual(len(codec.decode_index(index,summary)),1)
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(len(codec.capture(Backend(packets),summary,Path(tmp))),1)
    def test_error_diagnostic_header_may_be_invalid(self):
        row,_=fixture([packet(0)],5,4); row['hdr3']=99
        self.assertFalse(codec.decode_info(pack(row),17)['passed'])
    def test_summary_wrong_size_type_version_generation(self):
        row,_=fixture([])
        for raw in (bytes(255),bytes(257),bytearray(pack(row))):
            with self.assertRaises(ValueError): codec.decode_info(raw,17)
        for field in ('magic','abi','generation'):
            r=dict(row); r[field]+=1
            with self.assertRaises(ValueError): codec.decode_info(pack(r),17)
        with self.assertRaises(ValueError): codec.decode_info(pack(row),0)
    def test_summary_rejects_inconsistent_limits_counts_flags(self):
        row,_=fixture([packet(0)])
        for key,val in [('max_records',63),('max_pages',63),('duration_ns',1),('max_polls',1),
                        ('stop',6),('failure',1),('passed',0),('header_valid',2),('count',63),
                        ('pages',0),('bytes',1),('producer',63),('polls',50001),('elapsed_ns',1),
                        ('init_done',1),('sequencer',1),('nocat_count',2),('reader',1),('hdr4',2),
                        ('partial_polls',21),('pending_pages',17),('failed_slot',0)]:
            r=dict(row); r[key]=val
            with self.subTest(key=key),self.assertRaises(ValueError): codec.decode_info(pack(r),17)
    def test_summary_failure_fields(self):
        row,_=fixture([packet(0)],5,9)
        for key,val in [('failure',0),('failed_slot',0)]:
            r=dict(row); r[key]=val
            with self.assertRaises(ValueError): codec.decode_info(pack(r),17)
    def test_not_run(self):
        row,_=fixture([],0)
        row.update(polls=0,elapsed_ns=0,header_valid=0)
        self.assertEqual(codec.decode_info(pack(row),17)['stop_name'],'not-run')
        row['polls']=1
        with self.assertRaises(ValueError): codec.decode_info(pack(row),17)
    def test_index_gaps_bad_sequence_geometry_flags_and_time(self):
        row,index=fixture([packet(0),packet(1)]); summary=codec.decode_info(pack(row),17)
        for field,val in [(0,8192),(1,1),(2,1<<32),(3,1<<32),(4,3),(5,70000),(6,9),(7,4),(8,5000001)]:
            data=bytearray(index);struct.pack_into('<Q',data,72+field*8,val)
            with self.subTest(field=field),self.assertRaises(ValueError): codec.decode_index(bytes(data),summary)
        with self.assertRaises(ValueError): codec.decode_index(index[:-1],summary)
    def test_index_rejects_timestamp_regression(self):
        row,index=fixture([packet(0),packet(1)]); data=bytearray(index)
        struct.pack_into('<Q',data,64,10)
        with self.assertRaises(ValueError): codec.decode_index(bytes(data),row)
    def test_index_rejects_event_after_terminal(self):
        row,index=fixture([packet(0,0x1001,bytes(4)),packet(1)])
        with self.assertRaises(ValueError): codec.decode_index(index,row)
    def test_terminal_summary_must_match_packet(self):
        row,index=fixture([packet(0)],1)
        with self.assertRaises(ValueError): codec.decode_index(index,row)
    def test_capture_index_batch_limit(self):
        packets=[packet(i) for i in range(62)]; row,_=fixture(packets,3);backend=Backend(packets)
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(len(codec.capture(backend,codec.decode_info(pack(row),17),Path(tmp))),62)
        self.assertEqual([c[1] for c in backend.calls if c[0]==17],[(0,32),(32,30)])
    def test_capture_corrupted_payload_is_saved_but_rejected(self):
        packets=[packet(0,0x1001,bytes(4))]; row,_=fixture(packets,1); backend=Backend(packets)
        bad=bytearray(backend.data);bad[80]^=1;backend.data=bytes(bad)
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError): codec.capture(backend,row,Path(tmp))
            self.assertTrue((Path(tmp)/'event-000.bin').exists())
    def test_capture_independent_rpc_metadata(self):
        packets=[packet(0)]; row,_=fixture(packets); backend=Backend(packets)
        backend.data=packet(0,0x101f)
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError): codec.capture(backend,row,Path(tmp))
    def test_packet_transport_sequence_checked(self):
        packets=[packet(0),packet(1)]; row,_=fixture(packets); backend=Backend(packets)
        backend.data=packets[0]+packet(2)
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError): codec.capture(backend,row,Path(tmp))
            self.assertEqual(len(__import__('json').loads((Path(tmp)/'events.json').read_text())),1)
    def test_sequencer_parsed_without_execution(self):
        packets=[packet(0,0x1002,struct.pack('<10I',1,0,*([0]*8)))]; row,_=fixture(packets,2)
        with tempfile.TemporaryDirectory() as tmp:
            details=codec.capture(Backend(packets),codec.decode_info(pack(row),17),Path(tmp))
            self.assertFalse(details[0]['sequencer']['operations_executed'])
    def test_bad_diagnostic_body_is_explicit_not_discarded(self):
        for fn in (0x1002,0x1020):
            packets=[packet(0,fn)]; row,_=fixture(packets,2 if fn==0x1002 else 4)
            with tempfile.TemporaryDirectory() as tmp:
                details=codec.capture(Backend(packets),codec.decode_info(pack(row),17),Path(tmp))
                self.assertTrue(details[0]['framing_verified']); self.assertIn('body_decode_error',details[0])
    def test_nocat_truncation_overflow_unterminated(self):
        data=bytearray(1208)
        for raw in (bytes(1207),bytes(1213),bytearray(1208)):
            with self.assertRaises(ValueError): codec.decode_nocat(raw)
        struct.pack_into('<I',data,176,1025)
        with self.assertRaises(ValueError): codec.decode_nocat(bytes(data))
        struct.pack_into('<I',data,176,0);data[24:89]=b'A'*65
        with self.assertRaises(ValueError): codec.decode_nocat(bytes(data))
    def test_nocat_ignores_trailing_string_bytes(self):
        data=bytearray(1212);data[24:89]=b'ASSERT\0'+b'Z'*58
        self.assertEqual(codec.decode_nocat(bytes(data))['source'],'ASSERT')
    def test_backend_bounds_and_close_without_abort(self):
        backend=Backend([]);backend.event_info();backend.close();backend.close()
        self.assertEqual(backend.calls,[(16,(),256)]);self.assertEqual(backend.closes,1)
        with self.assertRaises(client.BindingError): backend.event_info()
        for start,count in [(-1,1),(62,1),(61,2),(0,33),(0,0),(True,1)]:
            with self.assertRaises(ValueError): Backend([]).event_index(start,count)
        for off,count in [(-1,1),(253952,1),(253951,2),(0,4097),(0,0),(True,1)]:
            with self.assertRaises(ValueError): Backend([]).event_data(off,count)


if __name__ == '__main__': unittest.main()
