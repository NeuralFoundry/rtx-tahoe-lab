import copy
import hashlib
from pathlib import Path
import struct
import unittest
import gmmu_plan as g
import channel_plan as c
from test_channel_plan import recorded_gr


def old_fixture():
    # Synthetic old table image, explicitly not a 0.22 live result.
    old=bytearray(g.OLD_BYTES)
    struct.pack_into('<Q',old,0,0x100322);struct.pack_into('<Q',old,g.PAGE,0x100422)
    struct.pack_into('<Q',old,2*g.PAGE+128*8,0x1122334455667788)
    old[-16:]=bytes(range(16))
    return bytes(old)


class GMMUTests(unittest.TestCase):
    def setUp(self):
        self.ranges=g.mappings(c.context_plan(recorded_gr()))
        self.old=old_fixture();self.built=g.build(self.old,self.ranges)
        published=bytearray(self.old);struct.pack_into('<Q',published,g.PARENT_OFFSET,g.PARENT_VALUE)
        self.published=bytes(published)

    def test_actual_gr_layout_and_every_page(self):
        self.assertEqual((self.built['mapped_pages'],self.built['leaf_tables'],len(self.built['children'])),(3237,8,9*4096))
        for va,pa,size in self.ranges:
            for off in range(0,size,4096):
                for byte in (0,17,4095):self.assertEqual(g.walk(self.published,self.built['children'],va+off+byte),pa+off+byte)

    def test_all_holes_unmapped(self):
        child=self.built['children']
        for va in range(g.VA_BASE,g.VA_END,g.PAGE):
            expected=next((pa+va-base for base,pa,size in self.ranges if base<=va<base+size),None)
            self.assertEqual(g.walk(self.published,child,va),expected)

    def test_old_tables_preserved_and_publication_separate(self):
        self.assertEqual(self.old,old_fixture())
        self.assertEqual(self.built['old_sha256'],hashlib.sha256(self.old).hexdigest())
        self.assertEqual(self.published[:g.PARENT_OFFSET],self.old[:g.PARENT_OFFSET])
        self.assertEqual(self.published[g.PARENT_OFFSET+8:],self.old[g.PARENT_OFFSET+8:])
        with self.assertRaises(ValueError):g.walk(self.old,self.built['children'],g.VA_BASE)
        self.assertFalse(self.built['hardware_accessed']);self.assertFalse(self.built['gpu_translation_verified'])
        self.assertTrue(self.built['requires_022_live_success'])

    def test_invalid_ranges(self):
        for field,value in ((0,True),(0,-1),(0,1<<64),(0,g.VA_BASE-4096),(0,g.VA_END),(0,g.VA_BASE+1),
                            (1,0),(1,g.NEW_BASE),(1,c.BAR1_BYTES),(1,c.CONTEXT_PHYS_START+1),(2,0),(2,1),(2,17<<20)):
            ranges=copy.deepcopy(self.ranges);item=list(ranges[1]);item[field]=value;ranges[1]=tuple(item)
            with self.assertRaises(ValueError,msg=str((field,value))):g.build(self.old,ranges)
        for ranges in ([],self.ranges+self.ranges[:1],self.ranges+[self.ranges[0]]):
            with self.assertRaises(ValueError):g.build(self.old,ranges)
        for index in (0,1):
            ranges=copy.deepcopy(self.ranges);item=list(ranges[2]);item[index]=ranges[1][index];ranges[2]=tuple(item)
            with self.assertRaises(ValueError):g.build(self.old,ranges)

    def test_table_capacity_and_boundary(self):
        # One 16 MiB unaligned group span consumes nine PTs, plus one distinct ring group: exact lease fit.
        ranges=[self.ranges[0],(g.VA_BASE+0x401000,c.CONTEXT_PHYS_START,16<<20)]
        result=g.build(self.old,ranges);self.assertEqual(len(result['children']),11*g.PAGE)
        ranges.append((g.VA_BASE+0x2000000,c.CONTEXT_PHYS_START+(16<<20),4096))
        with self.assertRaises(ValueError):g.build(self.old,ranges)
        result=g.build(self.old,[(g.VA_END-4096,c.BAR1_BYTES-4096,4096)])
        self.assertEqual(g.walk(self.published,result['children'],g.VA_END-1),c.BAR1_BYTES-1)

    def test_corrupt_old_table_and_capture_sizes(self):
        for off in (0,g.PAGE,g.PARENT_OFFSET):
            old=bytearray(self.old);old[off]^=1
            with self.assertRaises(ValueError):g.build(bytes(old),self.ranges)
        for old in (self.old[:-1],self.old+b'\0',bytearray(self.old)):
            with self.assertRaises(ValueError):g.build(old,self.ranges)
        for children in (b'',self.built['children'][:-1],bytes(g.LEASE_END-g.NEW_BASE+4096)):
            with self.assertRaises(ValueError):g.walk(self.published,children,g.VA_BASE)
        for va in (g.VA_BASE-1,g.VA_END,-1,True,1<<49):
            with self.assertRaises(ValueError):g.walk(self.published,self.built['children'],va)

    def test_corrupt_pte_flags_and_pointers(self):
        original=self.built['children']
        # Every unsupported flag/format bit must fail. Address bits are tested separately.
        for off,bits in ((0,range(64)),(8,list(range(8))+list(range(33,64))),(4096,list(range(8))+list(range(33,64)))):
            for bit in bits:
                child=bytearray(original);child[off+bit//8]^=1<<(bit%8)
                with self.assertRaises(ValueError,msg=str((off,bit))):g.walk(self.published,bytes(child),g.VA_BASE)
        for physical in (g.NEW_BASE,g.LEASE_END,0):
            child=bytearray(original);struct.pack_into('<Q',child,8,(physical>>4)|2)
            with self.assertRaises(ValueError):g.walk(self.published,bytes(child),g.VA_BASE)
        child=bytearray(original);struct.pack_into('<Q',child,4096,(6<<56)|(g.NEW_BASE>>4)|1)
        with self.assertRaises(ValueError):g.walk(self.published,bytes(child),g.VA_BASE)

    def test_native_fixture_if_present(self):
        fixture=Path(__file__).parent/'gmmu-children-windows.bin'
        self.assertTrue(fixture.exists(),'Build independent C++ fixture before this test')
        self.assertEqual(self.built['children'],fixture.read_bytes())


if __name__=='__main__':unittest.main()
