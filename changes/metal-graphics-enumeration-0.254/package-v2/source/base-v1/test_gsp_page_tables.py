import struct
import unittest
from pathlib import Path
import gsp_page_tables_codec as p


def response():
    data=bytearray(p.request())
    struct.pack_into('<I',data,36,12)
    struct.pack_into('<II',data,64,0,0)
    struct.pack_into('<I',data,32,0)
    struct.pack_into('<I',data,32,p.checksum(data))
    return bytes(data)


class PageTablesTests(unittest.TestCase):
    def test_layout(self):
        data=p.image()
        self.assertEqual(len(data),12288)
        words=struct.unpack('<1536Q',data)
        self.assertEqual([(i,v) for i,v in enumerate(words) if v],[(0,0x100322),(512,0x100422)])
        self.assertTrue(p.decode_tables(data,True)['initial_image_verified'])

    def test_control_layout(self):
        data=p.parameters()
        self.assertEqual(len(data),184)
        self.assertEqual(struct.unpack_from('<IIQQQI',data),(0,0,0x20000000,0x1000000000,0x101fffffff,3))
        self.assertEqual(struct.unpack_from('<QQIB',data,40),(0x1002000,32,1,47))
        self.assertEqual(struct.unpack_from('<QQIB',data,64),(0x1003000,4096,1,38))
        self.assertEqual(struct.unpack_from('<QQIB',data,88),(0x1004000,4096,1,29))
        self.assertFalse(any(data[112:]))

    def test_native_fixture_when_available(self):
        # Both platforms emit the same deterministic fixture, not a hardware result.
        fixture=Path('changes/gsp-page-tables-0.22/tables-control-fixture.bin')
        if not fixture.exists(): self.fail('Run native page-table serializer before Python tests')
        self.assertEqual(fixture.read_bytes(),p.image()+p.parameters()+p.request())

    def test_response(self):
        self.assertEqual(p.checksum(p.request()),0)
        self.assertTrue(p.decode_response(response(),12)['parameters_verified'])
        with self.assertRaises(ValueError): p.decode_response(p.request(),8)
        with self.assertRaises(ValueError): p.decode_response(response(),13)
        with self.assertRaises(ValueError): p.decode_response(response()[:-1],12)

    def test_all_echo_bytes_and_header_mutations(self):
        for off in list(range(104,288))+[36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100]:
            data=bytearray(response());data[off]^=1
            struct.pack_into('<I',data,32,0)
            try: struct.pack_into('<I',data,32,p.checksum(data))
            except ValueError: pass
            with self.assertRaises(ValueError,msg=str(off)): p.decode_response(bytes(data),12)

    def test_vaspace_upper_bound_not_length(self):
        data=bytearray(48)
        struct.pack_into('<Q',data,8,p.VA_END)
        struct.pack_into('<Q',data,40,0x4000000)
        self.assertEqual(p.vaspace_range(bytes(data))['usable_bytes'],p.VA_END-0x4000000)
        struct.pack_into('<QQ',data,16,0xffffffffffffffff,0xffffffffffffffff)
        self.assertEqual(p.vaspace_range(bytes(data))['end'],p.VA_END)
        struct.pack_into('<Q',data,8,p.VA_END-1)
        with self.assertRaises(ValueError): p.vaspace_range(bytes(data))

    def test_decode_modified_postimage_is_observation(self):
        data=bytearray(p.image());struct.pack_into('<Q',data,8192+128*8,0x1005022)
        decoded=p.decode_tables(bytes(data))
        self.assertFalse(decoded['initial_image_verified'])
        self.assertFalse(decoded['gpu_translation_verified'])
        self.assertEqual(decoded['pages'][2]['entries'],[dict(index=128,raw=0x1005022)])
        with self.assertRaises(ValueError): p.decode_tables(bytes(data),True)


if __name__=='__main__': unittest.main()
