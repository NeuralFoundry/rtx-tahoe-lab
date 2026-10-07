import struct
import unittest

from decode_falcon import BOOL_FIELDS, RETURN_FIELDS, decode_falcon


def payload(phase):
    # Independent fixture, deliberately not importing decoder.pattern.
    return tuple((0x4636444d ^ ((i * 0x9e3779b1) & 0xffffffff)
                  ^ (0xe3a1957b if phase == 1 else 0)) & 0xffffffff for i in range(64))


def phase_row(phase):
    return (64, 1, 2, 0x602, 3, 64, 64) + payload(phase)


def set_phase(node, phase, row):
    rows = list(struct.iter_unpack("<71I", node["FalconPhases"]))
    rows[phase] = row
    node["FalconPhases"] = b"".join(struct.pack("<71I", *r) for r in rows)


def set_register(node, snapshot, index, value):
    key = "Falcon" + snapshot
    values = list(struct.unpack("<14I", node[key]))
    values[index] = value
    node[key] = struct.pack("<14I", *values)


def fixture():
    node = {
        "FalconStatus": "Falcon-DMA-host-to-DMEM-verified", "FalconMapperMode": "system-no-mapper",
        "FalconCount": 4, "FalconEnd": 16384, "FalconCommandBefore": 0, "FalconCommandEnabled": 6,
        "FalconCommandAfter": 0, "FalconDeviceStatusBefore": 0, "FalconDeviceStatusAfter": 0,
        "FalconInitialReads": 14, "FalconFinalReads": 14, "FalconResetCount": 2,
        "FalconResetPolls": 4, "FalconDrainPolls": 2,
        "FalconMismatchPhase": 0xffffffff, "FalconMismatchWord": 0xffffffff, "FalconMismatchValue": 0,
        "FalconSegments": b"".join(struct.pack("<QQ", 0x100000000 + i * 4096, 4096) for i in range(4)),
        "FalconInitial": struct.pack("<14I", 0, 0, 0x200, 0x10, 1, 2, 1, 0, 4, 0, 0, 0, 0, 0),
        "FalconFinal": struct.pack("<14I", 0, 0, 0x200, 0, 1, 2, 1, 0, 4, 0, 0, 0, 0, 0),
        "FalconEnvironment": struct.pack("<3I", 0, 0x8b8f, 0x3ff),
        "FalconPhases": b"".join(struct.pack("<71I", *phase_row(p)) for p in range(2)),
        "FirmwareExecuted": False,
    }
    node.update(("Falcon" + suffix, suffix != "ResourcesRetained") for suffix in BOOL_FIELDS)
    node.update(("Falcon" + suffix, 0) for suffix in RETURN_FIELDS)
    return node


class FalconDecodeTest(unittest.TestCase):
    def test_two_independent_payloads_prove_dma_only(self):
        result = decode_falcon(fixture(), True)
        self.assertTrue(result["passed"])
        self.assertTrue(result["dma_engine_tested"])
        self.assertTrue(result["gpu_transfer_verified"])
        self.assertFalse(result["firmware_executed"])
        self.assertFalse(result["compute_tested"])
        self.assertFalse(result["addresses_still_valid"])
        self.assertEqual([p["observed_pattern_matches"] for p in result["phases"]], [64, 64])
        self.assertTrue(all(result["evidence_checks"].values()))

    def test_false_success_flags_and_lifecycle(self):
        mutations = [("Falcon" + suffix, False) for suffix in BOOL_FIELDS if suffix not in ("ResourcesRetained", "Passed")]
        mutations += [("FalconResourcesRetained", True), ("FalconCommandAfter", 4),
                      ("FalconCommandBefore", 2), ("FalconCommandEnabled", 2),
                      ("FalconDeviceStatusAfter", 0xffff), ("FalconDeviceStatusBefore", 0x20),
                      ("FalconDeviceStatusAfter", 0x20), ("FalconInitialReads", 13), ("FalconFinalReads", 13),
                      ("FalconResetCount", 1), ("FalconResetPolls", 0), ("FalconDrainPolls", 1),
                      ("FalconMapperMode", "bypassed"), ("FalconStatus", "not-run"),
                      ("FalconMismatchWord", 0), ("FalconEnd", 4096), ("FalconCount", 3)]
        mutations += [("Falcon" + suffix, 1) for suffix in RETURN_FIELDS]
        for key, value in mutations:
            node = fixture(); node[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode_falcon(node, True)
        with self.assertRaises(ValueError): decode_falcon(fixture(), False)

    def test_final_registers_prove_quiescence_and_cleared_targets(self):
        for index, value in ((0, 1), (1, 0x1000), (3, 2), (4, 0), (5, 3), (7, 0x80),
                             (9, 0x80), (10, 1), (11, 1), (12, 1), (13, 1)):
            node = fixture(); set_register(node, "Final", index, value)
            with self.subTest(index=index), self.assertRaises(ValueError): decode_falcon(node, True)

    def test_initial_register_and_environment_preconditions(self):
        for index, value in ((0, 1), (3, 2), (7, 0x80), (0, 0xffffffff), (1, 0xbad0fb00), (6, 0xbadf0001)):
            node = fixture(); set_register(node, "Initial", index, value)
            with self.subTest(index=index), self.assertRaises(ValueError): decode_falcon(node, True)
        for environment in ((1, 1, 255), (0, 0, 255), (0, 1, 254), (0, 0xffffffff, 255)):
            node = fixture(); node["FalconEnvironment"] = struct.pack("<3I", *environment)
            with self.subTest(environment=environment), self.assertRaises(ValueError): decode_falcon(node, True)

    def test_pre_reset_dma_register_protection_does_not_invalidate_later_proof(self):
        node = fixture()
        for index in (2, 4, 5, 8, 9, 10, 11, 12, 13):
            set_register(node, "Initial", index, 0xbad0fb00)
        result = decode_falcon(node, True)
        self.assertTrue(result["passed"])
        self.assertEqual(result["initial_registers"]["dmabase"]["hex"], "0xbad0fb00")

    def test_false_phase_counts_or_commands(self):
        for index, value in ((0, 63), (1, 0), (2, 0xffffffff), (2, 3), (3, 0), (4, 2), (5, 63), (6, 63)):
            node = fixture(); row = list(phase_row(1)); row[index] = value; set_phase(node, 1, row)
            with self.subTest(index=index), self.assertRaises(ValueError): decode_falcon(node, True)

    def test_replayed_corrupt_and_canary_payloads_cannot_pass(self):
        alternatives = [payload(0), tuple(v ^ 0xffffffff for v in payload(1)),
                        payload(1)[:-1] + (payload(1)[-1] ^ 1,)]
        for data in alternatives:
            node = fixture(); set_phase(node, 1, phase_row(1)[:7] + data)
            with self.subTest(data=data[:2]), self.assertRaises(ValueError): decode_falcon(node, True)

    def test_segment_address_constraints(self):
        for address, length in ((0, 4096), (0x100001001, 4096), (1 << 40, 4096),
                                (0x100001000, 4096), (0x100000000, 256)):
            node = fixture(); node["FalconSegments"] = struct.pack("<QQ", address, length) + node["FalconSegments"][16:]
            with self.subTest(address=address, length=length), self.assertRaises(ValueError): decode_falcon(node, True)

    def test_failure_preserves_canary_observation_without_claiming_dma(self):
        node = fixture()
        node.update(FalconPassed=False, FalconCpuIntact=False, FalconStatus="falcon-PIO-canary-failed",
                    FalconMasterAttempted=False, FalconCommandEnabled=0xffff,
                    FalconMismatchPhase=0, FalconMismatchWord=0, FalconMismatchValue=0xbad0fb00)
        empty = (0, 0, 0xffffffff, 0xffffffff, 0, 0, 0) + (0,) * 64
        set_phase(node, 0, empty); set_phase(node, 1, empty)
        result = decode_falcon(node, True)
        self.assertFalse(result["passed"])
        self.assertFalse(result["dma_engine_tested"])
        self.assertTrue(result["cleanup_verified"])
        self.assertEqual(result["mismatch"]["value_hex"], "0xbad0fb00")

    def test_timeout_cleanup_failure_preserves_retained_resource_state(self):
        node = fixture()
        node.update(FalconPassed=False, FalconStatus="falcon-quiescence-failed-resources-retained",
                    FalconQuiescent=False, FalconTargetsCleared=False, FalconCleanupVerified=False,
                    FalconResourcesRetained=True, FalconCpuIntact=False, FalconCommandAfter=4,
                    FalconDeviceStatusAfter=0x20)
        set_phase(node, 0, (64, 1, 2, 0xffffffff, 202, 0, 0) + (0,) * 64)
        set_phase(node, 1, (0, 0, 0xffffffff, 0xffffffff, 0, 0, 0) + (0,) * 64)
        result = decode_falcon(node, True)
        self.assertFalse(result["passed"])
        self.assertTrue(result["dma_engine_tested"])
        self.assertFalse(result["gpu_transfer_verified"])
        self.assertTrue(result["resources_retained"])
        self.assertIsNone(result["addresses_still_valid"])
        self.assertEqual(result["command_after"], 4)

    def test_signed_ioreg_sentinels_and_ioreturns(self):
        node = fixture(); node.update(FalconMismatchPhase=-1, FalconMismatchWord=-1)
        self.assertTrue(decode_falcon(node, True)["passed"])
        node.update(FalconPassed=False, FalconStatus="falcon-DMA-data-mismatch",
                    FalconMismatchPhase=0, FalconMismatchWord=0, FalconMismatchValue=-1160709376,
                    FalconPublish1IOReturn=-536870184)
        result = decode_falcon(node, True)
        self.assertEqual(result["mismatch"]["value_hex"], "0xbad0fb00")
        self.assertEqual(result["ioreturns"]["FalconPublish1IOReturn"], 0xe00002d8)

    def test_missing_wrong_types_sizes_and_out_of_bounds_evidence(self):
        for key, value in (("FalconPassed", 1), ("FalconCount", True), ("FalconEnd", -1),
                           ("FalconInitialReads", 15), ("FalconFinalReads", 15), ("FalconResetCount", 3),
                           ("FalconResetPolls", 631), ("FalconDrainPolls", 401), ("FalconCount", 5),
                           ("FalconCommandBefore", 65536), ("FalconMismatchWord", 64),
                           ("FalconMismatchPhase", 2), ("FalconMismatchValue", -(1 << 31) - 1),
                           ("FalconMismatchValue", 1 << 32), ("FirmwareExecuted", True)):
            node = fixture(); node[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode_falcon(node, True)
        for key in ("FalconSegments", "FalconInitial", "FalconFinal", "FalconEnvironment", "FalconPhases"):
            node = fixture(); node[key] = node[key][:-1]
            with self.subTest(key=key), self.assertRaises(ValueError): decode_falcon(node, True)
        node = fixture(); del node["FalconCleanupVerified"]
        with self.assertRaises(ValueError): decode_falcon(node, True)

    def test_failure_does_not_silently_accept_impossible_phase_counts(self):
        for index, value in ((0, 65), (1, 2), (4, 601), (5, 65), (6, 65)):
            node = fixture(); node["FalconPassed"] = False
            row = list(phase_row(0)); row[index] = value; set_phase(node, 0, row)
            with self.subTest(index=index), self.assertRaises(ValueError): decode_falcon(node, True)


if __name__ == "__main__": unittest.main()
