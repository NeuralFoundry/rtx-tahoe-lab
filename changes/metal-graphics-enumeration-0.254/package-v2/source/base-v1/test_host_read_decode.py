import struct
import unittest
from decode_host_read import signature_index, pattern, phase_hash
from decode_probe import decode_snapshot
import test_preparation_decode


def fixture():
    node = test_preparation_decode.PreparationDecodeTest().fixture()
    node.update(Mode="bounded-host-read", ProbeVersion="0.5.0", MMIOReadOnly=False,
                IdentificationMMIOReadOnly=True, FuseOffset=0x8241e0, FuseFirst=3, FuseSecond=3,
                FuseReads=2, FuseSignatureMask=7, FuseSignatureCount=3, FuseSignatureIndex=2,
                FusePassed=True, FirmwareExecuted=False, HostSegments=node["DMASegments"],
                HostSegmentCount=4, HostEndOffset=16384, HostCommandBefore=0, HostCommandEnabled=6,
                HostCommandAfter=0, HostWindowBefore=0x1234, HostWindowSecond=0x1234, HostWindowAfter=0x1234,
                HostWindowProgrammed=0x02000010, HostWindowObserved=0x02000010,
                HostMapperMode="system-no-mapper", HostPassed=True, HostResourcesRetained=False,
                HostStatus="GPU-host-page-read-verified", HostMismatchPhase=0xffffffff,
                HostMismatchWord=0xffffffff, HostMismatchValue=0,
                HostPhases=b"".join(struct.pack("<5I", 1024, 1024, phase_hash(p), pattern(0, p), pattern(1023, p)) for p in range(2)))
    for key in ("HostMemoryAttempted", "HostMasterAttempted", "HostWindowAttempted", "HostWindowRestored",
                "HostPrepared", "HostCPUContentsIntact", "HostCleanupVerified"):
        node[key] = True
    for key in ("HostLastIOReturn", "HostClearIOReturn", "HostCompleteIOReturn", "HostMemoryCompleteIOReturn",
                "HostPublish0IOReturn", "HostPublish1IOReturn"):
        node[key] = 0
    return node


class HostReadDecodeTest(unittest.TestCase):
    def test_success_is_host_read_only(self):
        r = decode_snapshot([fixture()])
        self.assertEqual(r["fwsec_fuse"]["signature_index_candidate"], 2)
        self.assertTrue(r["gpu_host_read"]["passed"])
        self.assertFalse(r["gpu_host_read"]["dma_engine_tested"])
        self.assertFalse(r["gpu_host_read"]["firmware_executed"])
        self.assertFalse(r["compute_tested"])

    def test_fuse_selection_edges(self):
        self.assertEqual([signature_index(i) for i in range(8)], [0, 1, 2, 2, None, None, None, None])
        for v in (0xffffffff, 0x80000000, 0xbadf0000): self.assertIsNone(signature_index(v))
        for v in (-1, 1 << 32):
            with self.assertRaises(ValueError): signature_index(v)

    def test_rejects_wrong_fuse_evidence(self):
        for key, value in (("FuseFirst", 2), ("FuseSignatureIndex", 1), ("FuseOffset", 0x8241dc),
                           ("FuseReads", 1), ("FuseSignatureMask", 3), ("FirmwareExecuted", True)):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode_snapshot([n])

    def test_rejects_false_success(self):
        for key, value in (("HostCommandAfter", 4), ("HostCommandEnabled", 2), ("HostWindowRestored", False),
                           ("HostWindowAfter", 0), ("HostResourcesRetained", True), ("HostPublish1IOReturn", 1),
                           ("HostCPUContentsIntact", False), ("HostMapperMode", "bypassed"),
                           ("HostWindowProgrammed", 0x01000010), ("HostEndOffset", 4096)):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode_snapshot([n])

    def test_rejects_stale_second_phase(self):
        n = fixture(); n["HostPhases"] = n["HostPhases"][:20] * 2
        with self.assertRaises(ValueError): decode_snapshot([n])

    def test_rejects_incomplete_or_corrupt_hash(self):
        for row in ((1023, 1023, phase_hash(0, 1023), pattern(0, 0), pattern(1022, 0)),
                    (1024, 1024, phase_hash(0) ^ 1, pattern(0, 0), pattern(1023, 0)),
                    (1025, 1025, 0, 0, 0)):
            n = fixture(); n["HostPhases"] = struct.pack("<5I", *row) + n["HostPhases"][20:]
            with self.assertRaises(ValueError): decode_snapshot([n])

    def test_failure_preserves_observations(self):
        n = fixture(); n.update(HostPassed=False, HostStatus="host-data-mismatch")
        n["HostPhases"] = struct.pack("<10I", 1, 0, 0, 0, 0, 0, 0, 2166136261, 0, 0)
        r = decode_snapshot([n])["gpu_host_read"]
        self.assertFalse(r["passed"])
        self.assertTrue(r["gpu_read_attempted"])

    def test_signed_ioreg_sentinels_and_error_value(self):
        n = fixture(); n.update(HostMismatchPhase=-1, HostMismatchWord=-1)
        self.assertTrue(decode_snapshot([n])["gpu_host_read"]["passed"])
        n.update(HostPassed=False, HostStatus="host-data-mismatch", HostMismatchPhase=0,
                 HostMismatchWord=0, HostMismatchValue=-1160709376, HostPublish1IOReturn=-536870184)
        result = decode_snapshot([n])["gpu_host_read"]
        self.assertEqual(result["mismatch"]["value_hex"], "0xbad0fb00")
        self.assertEqual(result["ior_returns"]["HostPublish1IOReturn"], 0xe00002d8)

    def test_unreadable_signed_fuse_is_a_failure_result(self):
        n = fixture(); n.update(FuseFirst=-1, FuseSecond=0, FuseReads=1, FuseSignatureIndex=-1,
                               FusePassed=False, MMIOPassed=False, StatePassed=False, ROMPassed=False, HostPassed=False)
        result = decode_snapshot([n])
        self.assertFalse(result["fwsec_fuse"]["passed"])
        self.assertEqual(result["fwsec_fuse"]["raw_first"], 0xffffffff)


if __name__ == "__main__": unittest.main()
