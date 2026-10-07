"""Classify actual vector decoder output without touching a GPU."""
import copy
import unittest
import gsp_vector as runner
import test_gsp_vector_native as fixture

class VectorRunner(unittest.TestCase):
    def base(self):
        vector,_=fixture.Compute().collect(fixture.Fake())
        self.assertTrue(vector['passed'],vector.get('error'))
        return dict(passed=True,connection_closed=True,init_done_observed=True,
            rm_exchange={'exchanges_verified':True},bar1={'passed':True},bar1_readback={'readback_verified':True},
            page_tables={'passed':True},page_rm={'passed':True},page_rm_exchange={'exchanges_verified':True},
            page_table_captures={'captures_verified':True},channel={'passed':True},
            execution={'passed':True,'host_command_verified':True},compute=vector)

    def test_cpu_evidence_never_claims_hardware_or_metal(self):
        result=runner.finalize_result(self.base())
        self.assertTrue(result['passed']);self.assertFalse(result['compute_verified']);self.assertFalse(result['metal_verified'])

    def test_classification_requires_entire_same_run_and_close(self):
        base=self.base()
        for key,value in [('compute',{}),('execution',{}),('channel',{}),('connection_closed',False),
                          ('error','late decode'),('launch_error','launch failed'),('close_error','close failed')]:
            bad=copy.deepcopy(base);bad.update(hardware_backend=True);bad[key]=value
            with self.subTest(key=key):
                result=runner.finalize_result(bad);self.assertFalse(result['passed']);self.assertFalse(result['compute_verified'])

    def test_scalar_profile_or_partial_vector_cannot_pass(self):
        base=self.base()
        for key,value in [('table_readback_verified',False),('active_elements',60),('inactive_elements',0),
                          ('completion',0x306025f0),('output',[0]*63)]:
            bad=copy.deepcopy(base);bad['hardware_backend']=True;bad['compute']['bytes'][key]=value
            with self.subTest(key=key):self.assertFalse(runner.finalize_result(bad)['compute_verified'])
        for profile in ('first-compute','',None):
            bad=copy.deepcopy(base);bad['hardware_backend']=True;bad['compute']['profile']=profile
            self.assertFalse(runner.finalize_result(bad)['passed'])

    def test_hardware_classification_still_excludes_metal(self):
        # This exercises only a classification branch. It is not live evidence.
        base=self.base();base['hardware_backend']=True
        result=runner.finalize_result(base)
        self.assertTrue(result['compute_verified']);self.assertFalse(result['metal_verified'])

if __name__=='__main__':unittest.main()
