import copy,hashlib,json,re,struct,unittest
from pathlib import Path
import execution_plan as p
import execution_tables as t

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
RECORD=ROOT/'changes/gsp-bar1-0.21/live/gsp-bar1-20260906T212057Z/rm/record-011.bin'

class ExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw,cls.gr=p.c.load_record(RECORD,5,11)
        assert hashlib.sha256(raw).hexdigest()=='f061c08ec585f56bb9f9796ae67eedd15976337c83b646f15ee321070b8ff67f'
        cls.golden=p.c.context_plan(cls.gr);cls.plan=p.make(cls.gr,cls.golden)
        root=bytearray(12288)
        for off,value in ((0,0x100322),(4096,0x100422),(8192+128*8,0x1122334455667788)):struct.pack_into('<Q',root,off,value)
        cls.child=p.g.build(bytes(root),p.g.mappings(cls.golden))['children']
        struct.pack_into('<Q',root,p.g.PARENT_OFFSET,p.g.PARENT_VALUE);cls.root=bytes(root)

    def test_default_bytes_match_native(self):
        result=t.merge(self.root,self.child,self.golden,self.plan)
        artifacts={'channel.bin':p.channel_parameters(self.golden,3),'physical.bin':p.promotion(self.plan,self.golden,True),
                   'virtual.bin':p.promotion(self.plan,self.golden,False),'root.bin':self.root,'golden-children.bin':self.child,
                   'children.bin':result['children'],'ranges.bin':b''.join(struct.pack('<3Q',*r) for r in p.mappings(self.plan,self.golden))}
        for name,data in artifacts.items():self.assertEqual(data,(HERE/'windows'/name).read_bytes(),name)
        n=json.loads((HERE/'windows/native.json').read_text())
        for key in ('child_bytes','added_leaf_pages','added_ptes','existing_table_ptes'):self.assertEqual(n[key],result[key])
        self.assertEqual(n['private_bytes'],self.plan['backing_bytes']);self.assertEqual(n['physical_end'],self.plan['physical_end'])
        self.assertEqual(n['virtual_end'],self.plan['virtual_end'])
        self.assertEqual([b['size'] for b in self.plan['buffers']],[970752,16384,16384])

    def test_all_old_and_new_mappings_survive(self):
        result=t.merge(self.root,self.child,self.golden,self.plan)
        for va,pa,size in p.g.mappings(self.golden)+p.mappings(self.plan,self.golden):
            for off in range(0,size,4096):
                for edge in (0,4095):self.assertEqual(p.g.walk(self.root,result['children'],va+off+edge),pa+off+edge)
        for va in (p.g.VA_BASE+0x4000,p.CONTEXT_VA-4096,self.plan['virtual_end'],p.g.VA_END-1):
            self.assertIsNone(p.g.walk(self.root,result['children'],va))
        self.assertEqual(struct.unpack_from('<Q',self.root,8192+128*8)[0],0x1122334455667788)
        for off,(before,after) in enumerate(zip(self.child,result['children'])):
            if not (256<=off<272 or 4104<=off<4128):self.assertEqual(before,after)

    def test_corrupted_capture_rejected(self):
        for off in range(0,len(self.child),256):
            bad=bytearray(self.child);bad[off]^=1
            with self.assertRaises(ValueError):t.merge(self.root,bytes(bad),self.golden,self.plan)
        for off in (0,4096,p.g.PARENT_OFFSET):
            bad=bytearray(self.root);bad[off]^=1
            with self.assertRaises(ValueError):t.merge(bytes(bad),self.child,self.golden,self.plan)
        for size in (0,8191,36863,45057):
            with self.assertRaises(ValueError):t.merge(self.root,bytes(size),self.golden,self.plan)
        for root in (None,self.root[:-1],bytearray(self.root)):
            with self.assertRaises(ValueError):t.merge(root,self.child,self.golden,self.plan)

    def test_captured_unowned_root_entries_preserved(self):
        root=bytearray(self.root);struct.pack_into('<Q',root,8192+128*8,0xfedcba9876543210)
        self.assertEqual(t.merge(bytes(root),self.child,self.golden,self.plan)['children'],
                         t.merge(self.root,self.child,self.golden,self.plan)['children'])
        self.assertEqual(struct.unpack_from('<Q',root,8192+128*8)[0],0xfedcba9876543210)

    def test_private_sizes_and_table_lease_boundary(self):
        for total in (0x200000,0x201000,0x400000,0x401000):
            gr=bytearray(self.gr);struct.pack_into('<I',gr,0,total-2*16384-0x40000)
            plan=p.make(bytes(gr),self.golden);self.assertEqual(plan['backing_bytes'],total)
            if total>0x400000:
                with self.assertRaises(ValueError):t.merge(self.root,self.child,self.golden,plan)
            else:
                result=t.merge(self.root,self.child,self.golden,plan)
                self.assertEqual(result['added_leaf_pages'],2 if total>0x200000 else 1)
                for va,pa,size in p.mappings(plan,self.golden):
                    for off in range(0,size,4096):self.assertEqual(p.g.walk(self.root,result['children'],va+off),pa+off)
        for alignment in (256,4096,65536,0x200000):
            gr=bytearray(self.gr);struct.pack_into('<I',gr,4,alignment);struct.pack_into('<I',gr,16*8,16385)
            p.validate(p.make(bytes(gr),self.golden),self.golden)

    def test_invalid_private_reports(self):
        for kind in (0,16):
            for value in (0,3,0x400000,0xffffffff):
                gr=bytearray(self.gr);struct.pack_into('<I',gr,kind*8+4,value)
                with self.assertRaises(ValueError):p.make(bytes(gr),self.golden)
            for value in (0,0xffffffff,0x1000001):
                gr=bytearray(self.gr);struct.pack_into('<I',gr,kind*8,value)
                with self.assertRaises(ValueError):p.make(bytes(gr),self.golden)
        for value in (self.gr[:-1],bytearray(self.gr),None):
            with self.assertRaises(ValueError):p.make(value,self.golden)

    def test_mutated_private_plan(self):
        for i in range(3):
            for key in ('id','kind','allocated','alignment','physical','va'):
                bad=copy.deepcopy(self.plan);bad['buffers'][i][key]+=1
                with self.assertRaises(ValueError):p.validate(bad,self.golden)
            for key in ('size','physical','va'):
                bad=copy.deepcopy(self.plan);bad['buffers'][i][key]=True
                with self.assertRaises(ValueError):p.validate(bad,self.golden)
        for key in ('physical_end','virtual_end','backing_bytes'):
            bad=copy.deepcopy(self.plan);bad[key]+=1
            with self.assertRaises(ValueError):p.validate(bad,self.golden)
        bad=copy.deepcopy(self.plan);bad['valid']=False
        with self.assertRaises(ValueError):p.validate(bad,self.golden)

    def test_mutated_golden_plan(self):
        for key in ('physical_end','virtual_end','total_backing_bytes'):
            bad=copy.deepcopy(self.golden);bad[key]+=1
            with self.assertRaises(ValueError):p.golden_fits(bad)
        for key in ('physical','virtual','gr_kind','allocated_bytes'):
            bad=copy.deepcopy(self.golden);bad['buffers'][0][key]+=4096
            with self.assertRaises(ValueError):p.golden_fits(bad)
        bad=copy.deepcopy(self.golden);bad['buffers'][0]['use_physical']=False
        with self.assertRaises(ValueError):p.golden_fits(bad)

    def test_channel_and_private_promotion_profiles(self):
        for cid in (0,1,2,4,4095,0xffffffff,True,None):
            with self.assertRaises(ValueError):p.channel_parameters(self.golden,cid)
        raw=p.channel_parameters(self.golden,3)
        self.assertEqual(struct.unpack_from('<Q',raw,64)[0],2048)
        self.assertEqual(struct.unpack_from('<QQII',raw,168),(0x03400800,512,2,0))
        self.assertEqual(struct.unpack_from('<I',raw,244)[0],0x14)
        self.assertEqual(raw[248:296],bytes(48)) # No error notifier descriptors.
        phys=p.promotion(self.plan,self.golden,True);virt=p.promotion(self.plan,self.golden,False)
        self.assertEqual(struct.unpack_from('<I',phys,40)[0],3)
        self.assertEqual(struct.unpack_from('<I',virt,40)[0],6)
        for i,b in enumerate(self.plan['buffers']):
            self.assertEqual(struct.unpack_from('<QQQI HBB',phys,48+i*32),(b['physical'],0,b['size'],4,i,1,1))
            self.assertEqual(struct.unpack_from('<QQQI HBB',virt,48+i*32),(0,b['va'],0,0,i,0,0))
        for i,b in enumerate(self.golden['buffers'][6:]):
            self.assertEqual(struct.unpack_from('<QQQI HBB',virt,48+(i+3)*32),(0,b['virtual'],0,0,9+i,0,0))
        for value in (0,1,None):
            with self.assertRaises(ValueError):p.promotion(self.plan,self.golden,value)

    def test_pinned_internal_flags_extract(self):
        reference=HERE.parent/'execution/reference'
        source=(reference/'g_kernel_channel_nvoc.h').read_text()
        manifest=json.loads((reference/'manifest.json').read_text())
        row=next(r for r in manifest if r['file']=='g_kernel_channel_nvoc.h')
        self.assertEqual(hashlib.sha256((reference/row['file']).read_bytes()).hexdigest(),row['sha256'])
        start=source.rfind('typedef enum {',0,source.index('ERROR_NOTIFIER_TYPE_UNKNOWN = 0'))
        end=source.index('} ErrorNotifierType;',start)+len('} ErrorNotifierType;')
        extract=(HERE/'vendor-internal-flags.hpp').read_text()
        self.assertIn(source[start:end],extract)
        self.assertEqual(re.findall(r'^#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_.*$',extract,re.M),
                         re.findall(r'^#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_.*$',source,re.M))
        self.assertIn(source[source.index('/*'):source.index('*/')+2],extract)

if __name__=='__main__':unittest.main()
