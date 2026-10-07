import copy
import unittest
from decode_gsp_booter_probe import EFFECTS, decode_booter_probe


def fixture(raw=0):
    return dict(EFFECTS, ProbeVersion="0.10.0", Mode="gsp-booter-fuse-readonly", ProbeComplete=True,
                ProbePassed=True, TargetBDF=True, BARTypesValid=True, MemoryEnableAttempted=True,
                RestoreVerified=True, TargetIdentity=0x252010de, TargetSubsystem=0x104c1043,
                PMCSR=8, LinkStatus=0x83, BAR0=0xfb000000, BAR0Descriptor=0xfb000000,
                BAR1=0x824000000, BAR1Descriptor=0x824000000, BAR0Length=16 << 20, BAR1Length=64 << 20,
                Boot0First=0xb76000a1, Boot0Second=0xb76000a1, Boot0Reads=2,
                CommandBefore=0, CommandDuring=2, CommandAfter=0, BooterFuseOffset=0x824148,
                BooterFuseFirst=raw, BooterFuseSecond=raw, BooterFuseReads=2,
                BooterSignatureIndex=1 - raw, Boot0Status="GA106-BOOT0-read",
                BooterFuseStatus="gsp-booter-signature-candidate-selected")


class GSPBooterDecodeTest(unittest.TestCase):
    def test_actual_ioreg_signed_osnumber_encoding(self):
        node = fixture()
        node["Boot0First"] = node["Boot0Second"] = -1218445151
        node["BAR0"] = node["BAR0Descriptor"] = 0xfb000000
        self.assertTrue(decode_booter_probe(node)["passed"])
        node.update(BooterFuseFirst=-1, BooterFuseSecond=-1, BooterSignatureIndex=-1,
                    ProbePassed=False, Boot0Status="invalid-fuse")
        self.assertFalse(decode_booter_probe(node)["passed"])

    def test_two_supported_signatures_from_sec2_fuse_only(self):
        for raw in (0, 1):
            result = decode_booter_probe(fixture(raw))
            self.assertTrue(result["passed"])
            self.assertEqual(result["fuse"]["signature_index"], 1 - raw)
            self.assertFalse(result["signature_gpu_acceptance_tested"])
            self.assertFalse(result["firmware_executed"])

    def test_unsupported_fuse_is_diagnostic_not_selected(self):
        for raw in (2, 3, 0x80000000, 0xffffffff, 0xbad0fb00):
            node = fixture()
            node.update(BooterFuseFirst=raw, BooterFuseSecond=raw, BooterSignatureIndex=0xffffffff,
                        ProbePassed=False, Boot0Status="unsupported-fuse")
            result = decode_booter_probe(node)
            self.assertFalse(result["passed"])
            self.assertIsNone(result["fuse"]["signature_index"])

    def test_mutated_success_evidence_is_rejected(self):
        mutations = {"TargetIdentity": 0, "TargetSubsystem": 0, "TargetBDF": False, "PMCSR": 3,
                     "LinkStatus": 0, "BAR0Descriptor": 0, "BAR1Length": 8 << 30,
                     "BARTypesValid": False, "CommandDuring": 6, "CommandAfter": 2,
                     "MemoryEnableAttempted": False, "RestoreVerified": False,
                     "Boot0First": 0xffffffff, "Boot0Reads": 1, "BooterFuseOffset": 0x8241e0,
                     "BooterFuseSecond": 1, "BooterFuseReads": 1, "BooterSignatureIndex": 0,
                     "Boot0Status": "not-run", "BooterFuseStatus": "not-run"}
        for key, value in mutations.items():
            node = fixture()
            node[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                decode_booter_probe(node)

    def test_effects_version_completion_and_types_cannot_be_faked(self):
        for key in EFFECTS:
            node = fixture()
            node[key] = not node[key]
            with self.subTest(key=key), self.assertRaises(ValueError):
                decode_booter_probe(node)
        for key, value in (("ProbeVersion", "0.9.0"), ("ProbeComplete", False),
                           ("Mode", "bounded-fwsec-execute"), ("ProbePassed", 1),
                           ("BooterFuseFirst", True), ("BooterFuseReads", -1), ("BAR0", 1 << 64),
                           ("PMCSR", 1 << 16), ("CommandBefore", 1 << 16),
                           ("Boot0First", -(1 << 31) - 1), ("BooterFuseStatus", None)):
            node = fixture()
            node[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                decode_booter_probe(node)

    def test_input_not_modified_and_false_failure_rejected(self):
        node = fixture()
        before = copy.deepcopy(node)
        decode_booter_probe(node)
        self.assertEqual(node, before)
        node["ProbePassed"] = False
        with self.assertRaises(ValueError):
            decode_booter_probe(node)


if __name__ == "__main__":
    unittest.main()
