import copy
import hashlib
import json
from pathlib import Path
import struct
import unittest
import channel_plan as p

BASE=Path(__file__).resolve().parent
GR_MAC=Path('/Users/DEVELOPER/rtx-tahoe-lab/results/gsp-bar1-20260906T212057Z/rm/record-011.bin')
GR_LOCAL=p.ROOT/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/record-011.bin'


def recorded_gr():
    path=GR_MAC if GR_MAC.exists() else GR_LOCAL
    raw,params=p.load_record(path,5,11)
    if hashlib.sha256(raw).hexdigest()!='f061c08ec585f56bb9f9796ae67eedd15976337c83b646f15ee321070b8ff67f':raise ValueError('Historical GR fixture changed')
    return params


class ChannelPlanTests(unittest.TestCase):
    def test_actual_c_header_fixture(self):
        self.assertEqual(p.channel_parameters(),(BASE/'channel-parameters-windows.bin').read_bytes())
        abi=json.loads((BASE/'channel-abi-windows.json').read_text())
        self.assertEqual(abi['channel_bytes'],368)
        self.assertEqual((abi['promotion_bytes'],abi['promotion_entry_bytes'],abi['promotion_entry_offset']),(560,32,48))

    def test_request_identity(self):
        record=p.gsp_rpc.decode_record(p.channel_request(),expected_sequence=9)
        self.assertEqual(record.rpc.function,103)
        self.assertEqual(struct.unpack_from('<8I',record.rpc.payload),(p.prep.CLIENT,p.prep.OBJECTS[1],0xcf000004,0xc56f,0,368,0,0))
        self.assertEqual(record.rpc.payload[32:],p.channel_parameters())

    def test_live_fixture_dimensions_and_no_hardware_claim(self):
        plan=p.context_plan(recorded_gr())
        self.assertEqual([b['buffer_id'] for b in plan['buffers']],[0,2,3,4,5,6,9,10,11])
        self.assertEqual(plan['buffers'][0]['size'],970752)
        self.assertEqual(plan['buffers'][4]['size'],10<<20)
        self.assertTrue(plan['requires_live_post_channel_gr_query'])
        self.assertFalse(plan['gpu_allocations_verified'])
        self.assertFalse(plan['compute_verified'])
        self.assertFalse(plan['metal_verified'])
        self.assertLessEqual(plan['physical_end'],p.BAR1_BYTES)
        self.assertGreaterEqual(plan['physical_start'],p.CHANNEL_PHYS_END)

    def test_required_sentinels_and_alignment_rejected(self):
        original=recorded_gr()
        for kind in (0,16,17,18,19,20,23,24):
            for value,alignment in ((0xffffffff,4096),(0,4096),(4096,0xffffffff),(4096,0),(4096,3),(0xfffff000,4096)):
                data=bytearray(original);struct.pack_into('<II',data,kind*8,value,alignment)
                with self.assertRaises(ValueError,msg=str((kind,value,alignment))):p.context_plan(bytes(data))

    def test_irrelevant_unsupported_buffers_not_allocated(self):
        raw=recorded_gr()
        self.assertEqual(struct.unpack_from('<II',raw,8),(0xffffffff,0xffffffff))
        plan=p.context_plan(raw)
        self.assertNotIn(1,[b['gr_kind'] for b in plan['buffers']])

    def test_promotion_byte_layout(self):
        plan=p.context_plan(recorded_gr());data=p.promote_parameters(plan)
        self.assertEqual(data,(BASE/'promote-parameters-windows.bin').read_bytes())
        self.assertEqual(len(data),560)
        self.assertEqual(struct.unpack_from('<6I',data),(1,0,0,p.prep.CLIENT,0xcf000004,0))
        self.assertEqual(struct.unpack_from('<I',data,40),(9,))
        for i,b in enumerate(plan['buffers']):
            physical,virtual,size,attr,buffer_id,init,nonmapped=struct.unpack_from('<QQQIHBB',data,48+32*i)
            self.assertEqual(buffer_id,b['buffer_id'])
            self.assertEqual(physical,b['physical'] if b['use_physical'] else 0)
            self.assertEqual(virtual,b['virtual'] if b['use_virtual'] else 0)
            self.assertEqual(size,b['size'] if b['use_physical'] else 0)
            self.assertEqual((attr,init,nonmapped),(4 if b['use_physical'] else 0,int(b['use_physical']),int(b['use_physical'] and not b['use_virtual'])))
        self.assertFalse(any(data[48+9*32:]))

    def test_corrupt_promotion_backing_rejected(self):
        original=p.context_plan(recorded_gr())
        for key,value in (('physical',0),('physical',p.BAR1_BYTES),('physical',p.CONTEXT_PHYS_START+1),('virtual',0),('alignment',3),('size',0),('allocated_bytes',1),('use_physical',2)):
            plan=copy.deepcopy(original);plan['buffers'][1][key]=value
            with self.assertRaises(ValueError,msg=key):p.promote_parameters(plan)
        plan=copy.deepcopy(original);plan['buffers'][1]['physical']=plan['buffers'][0]['physical']
        with self.assertRaises(ValueError):p.promote_parameters(plan)

    def test_allocator_bounds(self):
        for value,alignment in ((-1,4096),(1<<49,4096),(1,0),(1,3),(1,1<<22),(True,4096)):
            with self.assertRaises(ValueError):p.align(value,alignment)
        with self.assertRaises(ValueError):p.align((1<<49)-1,4096)
        raw=bytearray(recorded_gr())
        for kind in (0,16,17,18,19,20,23,24):struct.pack_into('<II',raw,kind*8,16<<20,4096)
        with self.assertRaises(ValueError):p.context_plan(bytes(raw))


if __name__=='__main__':unittest.main()
