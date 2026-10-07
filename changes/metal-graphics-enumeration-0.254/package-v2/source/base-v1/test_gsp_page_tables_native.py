import copy
from pathlib import Path
import struct
import tempfile
import unittest
import gsp_page_tables as runner
import gsp_page_tables_native as p
import gsp_page_tables_codec as wire
import gsp_page_tables_client as client
import gsp_page_tables_bar1_codec as bar1
from test_gsp_bar1 import info as bar1_info, packed as bar1_packed
from test_gsp_page_tables import response


def stage_info():
    r=dict.fromkeys(p.FIELDS,0);r.update({k:1 for k in p.BOOLS})
    r.update(magic=0x5254585047543232,abi=1,generation=42,inspected=3072,written=3072,checked=3072,captured=12288,
             window_before=0x80173d90,window_after=0x80173d90,failed_word=0xffffffff,bar1_base=0x824000000,
             mapping_physical=0x824000000+wire.START,start=wire.START,end=wire.START+wire.SIZE,bytes=wire.SIZE,
             root_bytes=32,va_start=wire.VA_START,va_end=wire.VA_END,owner_phase=17,pci_command=6,
             post_words=3072,post_bytes=wire.SIZE,budget_ns=15_000_000_000,cleanup_budget_ns=5_000_000_000,max_ticks=60000)
    return r


def rm_info():
    r=dict.fromkeys(p.RM_FIELDS,0);r.update({k:1 for k in p.prep.BOOLS})
    r.update(magic=0x5254585044523232,abi=1,generation=42,completed=1,sent=1,doorbells=1,count=1,pages=1,bytes=4096,
             tx_writer=9,tx_reader=9,rx_reader=14,rx_producer=14,rx_sequence=13,initial_reader=13,initial_sequence=12,
             consumer_writes=1,last_function=76,budget_ns=15_000_000_000,max_ticks=150000,client=wire.CLIENT,
             vaspace=wire.VASPACE,control=wire.CONTROL,params_bytes=184,request_bytes=4096,max_records=16,max_pages=32,pci_command=6)
    return r


def pack(fields,r):return struct.pack('<64Q',*(r[k] for k in fields))


class Backend(client.RestrictedBackend):
    def __init__(self):self.calls=[]
    def _invoke(self,selector,scalars,data,output_size):
        self.calls.append((selector,scalars,output_size))
        if selector==30:return wire.image()[scalars[1]:scalars[1]+scalars[2]]
        if selector==32:return struct.pack('<9Q',0,4096,76,0,12,208,0,13,0)
        if selector==33:return response()[scalars[0]:scalars[0]+scalars[1]]
        if selector==34:return wire.request()
        return bytes(output_size)


class PageTableNativeTests(unittest.TestCase):
    def test_stage_identity_success_and_mutations(self):
        r=stage_info();self.assertTrue(p.decode(pack(p.FIELDS,r),42)['passed'])
        for key,value in dict(magic=0,abi=2,generation=43,checked=3071,post_bytes=12284,window_after=0,owner_phase=7,
                              mapped=0,pci_command=0,mapping_physical=1,reserved63=1,rm_passed=0,rm_claimed=0,post_failure=1).items():
            changed=dict(r,**{key:value})
            with self.assertRaises(ValueError,msg=key):p.decode(pack(p.FIELDS,changed),42)

    def test_partial_later_failure_preserves_component_evidence(self):
        r=stage_info();r.update(passed=0,rm_passed=0,post_passed=0,post_attempted=0,post_words=0,post_bytes=0,owner_phase=7)
        self.assertTrue(p.decode(pack(p.FIELDS,r),42)['staged'])
        b=bar1_info();b['owner_phase']=7
        self.assertTrue(bar1.decode(bar1_packed(b),42)['passed'])

    def test_rm_identity_and_mutations(self):
        r=rm_info();self.assertTrue(p.decode_rm(pack(p.RM_FIELDS,r),42)['passed'])
        for key,value in dict(magic=0,vaspace=0xcf000002,control=0,params_bytes=185,sent=2,tx_writer=8,tx_reader=8,
                              last_param_status=1,rx_reader=13,rx_sequence=14,consumer_writes=0,pinned=0,reserved50=1).items():
            with self.assertRaises(ValueError,msg=key):p.decode_rm(pack(p.RM_FIELDS,dict(r,**{key:value})),42)

    def test_selector_bounds(self):
        b=Backend();self.assertEqual(len(b.page_data(1,8192,4096)),4096)
        for args in ((2,0,4),(0,-1,4),(0,12287,2),(0,0,4097),(0,0,0),(True,0,4)):
            with self.assertRaises(ValueError):b.page_data(*args)
        self.assertEqual(len(b.calls),1)
        for args in ((16,1),(0,17),(15,2)):
            with self.assertRaises(ValueError):b.page_rm_index(*args)

    def test_full_capture_independent(self):
        b=Backend()
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)
            self.assertTrue(p.capture_tables(b,stage_info(),out)['captures_verified'])
            self.assertTrue(p.capture_rm(b,rm_info(),out/'rm')['exchanges_verified'])
            self.assertEqual((out/'rm/request.bin').read_bytes(),wire.request())

    def test_short_capture_rejected(self):
        class Short(Backend):
            def page_data(self,*args):return b''
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):p.capture_tables(Short(),stage_info(),Path(folder))

    def test_finalization_requires_every_stage(self):
        r=dict(passed=True,connection_closed=True,init_done_observed=True,rm_exchange=dict(exchanges_verified=True),bar1=dict(passed=True),
               bar1_readback=dict(readback_verified=True),page_tables=dict(passed=True),page_rm=dict(passed=True),
               page_rm_exchange=dict(exchanges_verified=True),page_table_captures=dict(captures_verified=True))
        self.assertTrue(runner.finalize_result(copy.deepcopy(r))['passed'])
        for key in ('page_tables','page_rm','page_rm_exchange','page_table_captures'):
            bad=copy.deepcopy(r);del bad[key];self.assertFalse(runner.finalize_result(bad)['passed'])
        for key in ('error','launch_error','close_error'):
            bad=copy.deepcopy(r);bad[key]='late failure';self.assertFalse(runner.finalize_result(bad)['passed'])


if __name__=='__main__':unittest.main()
