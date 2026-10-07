import copy,unittest
import gsp_batch as b
class Finalize(unittest.TestCase):
    def sample(self):
        compute=dict(passed=True,profile='batch-031',bytes=dict(table_readback_verified=True,active_elements=158,inactive_elements=98,
            completions=list(range(0x306031f0,0x306031f4)),outputs=[[0]*64 for _ in range(4)]))
        compute.update({'submit-'+str(j):dict(passed=True,session_completed=4) for j in range(4)})
        return dict(passed=True,connection_closed=True,init_done_observed=True,hardware_backend=False,rm_exchange=dict(exchanges_verified=True),
            bar1=dict(passed=True),bar1_readback=dict(readback_verified=True),page_tables=dict(passed=True),page_rm=dict(passed=True),
            page_rm_exchange=dict(exchanges_verified=True),page_table_captures=dict(captures_verified=True),channel=dict(passed=True),
            execution=dict(passed=True,host_command_verified=True),compute=compute)
    def test_cpu_never_claims_hardware(self):
        r=b.finalize_result(self.sample());self.assertTrue(r['passed']);self.assertFalse(r['compute_verified']);self.assertFalse(r['metal_verified'])
    def test_missing_or_old_job_fails(self):
        for j in range(4):
            r=self.sample();del r['compute']['submit-'+str(j)];self.assertFalse(b.finalize_result(r)['passed'])
            r=self.sample();r['compute']['submit-'+str(j)]['session_completed']=j;self.assertFalse(b.finalize_result(r)['passed'])
    def test_late_errors_and_incomplete_shape(self):
        for key in ('error','launch_error','close_error'):
            r=self.sample();r[key]='simulated';self.assertFalse(b.finalize_result(r)['passed'])
        for key,value in (('outputs',[[0]*64]),('active_elements',61),('inactive_elements',3),('completions',[0x306030f0])):
            r=self.sample();r['compute']['bytes'][key]=value;self.assertFalse(b.finalize_result(r)['passed'])
if __name__=='__main__':unittest.main()
