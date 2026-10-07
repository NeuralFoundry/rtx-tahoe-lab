import copy
import hashlib
import struct
import unittest
from unittest.mock import patch

from decode_fwsec_execution import decode_fwsec_execution, BOOL_FIELDS, DISPLAY_OFFSETS
from decode_fwsec_stage import decode_fwsec_stage
from decode_host_read import decode_fuse
from decode_fwsec_preflight import decode_fwsec_preflight
from test_fwsec_stage_decode import fixture as stage_fixture
from test_decode_fwsec_preflight import fixture as preflight_fixture, change_row
from test_falcon_decode import set_register


def fixture():
    n = stage_fixture()
    for key in list(n):
        if key.startswith("DMA"):
            del n[key]
    n.update(preflight_fixture())
    n.update(ProbeVersion="0.9.0", Mode="bounded-fwsec-execute", MMIOReadOnly=False,
             IdentificationMMIOReadOnly=True, FirmwareExecuted=True, DMATransferExecuted=True,
             FalconStatus="fwsec-host-cleanup-verified-vram-retained",
             FWBootStatus="fwsec-frts-execution-verified", FWBootInitialReads=12,
             FWBootCpuBeforeStart=0x10, FWBootLastCpu=0x12, FWBootHaltPolls=1, FWBootDelayCalls=1,
             FWBootMailbox0=0, FWBootMailbox1=0x123, FWBootScratch=1,
             FWBootWprLo=0x017fe00b, FWBootWprHi=0x017ff00a,
             FWBootFailedRegister=19, FWBootWriteAttempts=8, FWBootVerifiedWrites=7,
             FWRegionOwned=True, FWRegionPersistent=True, FWProviderHeld=True,
             FWRegionRechecked=True, FWExecutionPassed=True,
             FWRegionOffset=0x17fe00000, FWRegionSize=0x100000, FWRegionVram=0x180000000)
    for name in BOOL_FIELDS:
        n["FWBoot" + name] = name not in ("AliasUsed", "RunningObserved")
    n["FWBootInitial"] = struct.pack("<12I", 0xb76000a1, 0, 0x47f7, 0x10, 0x10, 1,
                                      0x616, 0x1ffffe00, 0, 0, 0, 0)
    region = preflight_fixture()
    change_row(region, 1, 1); change_row(region, 2, 0x1ffffe00)
    n["FWRegionRegisters"] = region["PreflightRegisters"]
    n["FWDisplayRegisters"] = b"".join(struct.pack("<4I", offset, value, value, 2)
                                       for offset, value in zip(DISPLAY_OFFSETS, (15, 4, 0, 0, 0, 0)))
    set_register(n, "Final", 3, 0x10)
    set_register(n, "Final", 6, 1)
    return n


def decode(n, ready=True, snapshot=False):
    with patch("decode_fwsec_stage.BOARD_ROM_SHA256", hashlib.sha256(fixture()["VBIOSShadow"]).hexdigest()):
        if snapshot:
            from decode_probe import decode_snapshot
            return decode_snapshot([n])
        return decode_fwsec_execution(n, ready)


def raw_boot(n, index, value):
    data = bytearray(n["FWBootInitial"])
    struct.pack_into("<I", data, index * 4, value)
    n["FWBootInitial"] = bytes(data)


def raw_row(n, key, index, value=None, second=None, reads=None, offset=None):
    data = bytearray(n[key]); old = struct.unpack_from("<4I", data, index * 16)
    first = old[1] if value is None else value
    struct.pack_into("<4I", data, index * 16, old[0] if offset is None else offset,
                     first, first if second is None else second, old[3] if reads is None else reads)
    n[key] = bytes(data)


class FWSECExecutionDecodeTests(unittest.TestCase):
    def test_verified_execution_is_distinct_from_compute_and_host_release(self):
        r = decode(fixture())
        self.assertTrue(r["passed"])
        self.assertTrue(r["boot_verified"])
        self.assertTrue(r["firmware_executed"])
        self.assertTrue(r["authenticated_execution_inferred"])
        self.assertFalse(r["direct_signature_status_verified"])
        self.assertTrue(r["host_dma_cleanup_verified"])
        self.assertFalse(r["host_dma_resources_retained"])
        self.assertTrue(r["region"]["persistent"])
        self.assertTrue(r["region"]["provider_held"])
        self.assertTrue(r["side_effects_may_remain"])
        self.assertFalse(r["compute_tested"])
        self.assertFalse(r["nvidia_metal_supported"])

    def test_halt_alone_and_nonzero_mailbox_cannot_prove_success(self):
        for key, value in (("FWBootMailbox0", 1), ("FWBootScratch", 0x10000),
                           ("FWBootWprHi", 0xf), ("FWBootWprLo", 0x017fd00b),
                           ("FWBootLastCpu", 2), ("FWBootStatus", "fwsec-boot-halt-timeout")):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)

    def test_register_sentinels_cannot_prove_execution(self):
        for key in ("FWBootCpuBeforeStart", "FWBootLastCpu", "FWBootMailbox0", "FWBootMailbox1",
                    "FWBootScratch", "FWBootWprLo", "FWBootWprHi"):
            for value in (0xffffffff, 0xbad01234, 0xbadf1234):
                n = fixture(); n[key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError): decode(n)

    def test_initial_boot_registers_are_all_required(self):
        for index in range(12):
            n = fixture(); raw_boot(n, index, 0xffffffff)
            with self.subTest(index=index), self.assertRaises(ValueError): decode(n)
        for index, value in ((0, 0xb74000a1), (1, 1), (2, 0x1000), (3, 0),
                              (4, 0x80), (5, 0x11), (6, 3), (8, 0x10)):
            n = fixture(); raw_boot(n, index, value)
            with self.subTest(index=index), self.assertRaises(ValueError): decode(n)

    def test_alias_selection_uses_fresh_cpu_value(self):
        n = fixture(); n.update(FWBootCpuBeforeStart=0x50, FWBootAliasUsed=True)
        self.assertTrue(decode(n)["boot_verified"])
        n["FWBootAliasUsed"] = False
        with self.assertRaises(ValueError): decode(n)
        n = fixture(); n["FWBootAliasUsed"] = True
        with self.assertRaises(ValueError): decode(n)

    def test_poll_write_and_read_counts_are_bounded(self):
        for key, value in (("FWBootInitialReads", 11), ("FWBootInitialReads", 13),
                           ("FWBootWriteAttempts", 7), ("FWBootWriteAttempts", 9),
                           ("FWBootVerifiedWrites", 6), ("FWBootVerifiedWrites", 8),
                           ("FWBootFailedRegister", 18), ("FWBootFailedRegister", 20),
                           ("FWBootHaltPolls", 0), ("FWBootHaltPolls", 20001),
                           ("FWBootDelayCalls", 0), ("FWBootDelayCalls", 20001)):
            n = fixture(); n[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError): decode(n)
        n = fixture(); n.update(FWBootHaltPolls=20000, FWBootDelayCalls=20000, FWBootRunningObserved=True)
        self.assertTrue(decode(n)["passed"])
        n["FWBootRunningObserved"] = False
        with self.assertRaises(ValueError): decode(n)

    def test_native_boot_flags_cannot_substitute_for_evidence(self):
        for name in BOOL_FIELDS:
            if name in ("AliasUsed", "RunningObserved", "Passed"):
                continue
            n = fixture(); n["FWBoot" + name] = False
            with self.subTest(name=name), self.assertRaises(ValueError): decode(n)

    def test_region_geometry_retention_and_recheck_are_required(self):
        for key, value in (("FWRegionOffset", 0x17fd00000), ("FWRegionSize", 0x200000),
                           ("FWRegionVram", 0x200000000), ("FWRegionOwned", False),
                           ("FWRegionPersistent", False), ("FWProviderHeld", False), ("FWRegionRechecked", False)):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)

    def test_fresh_region_profile_not_replaced_by_initial_snapshot(self):
        for index, value in ((0, 1), (1, 9), (2, 0), (3, 0x10), (4, 0x50), (5, 0),
                              (6, 1), (7, 0x80420101), (8, 0x11), (9, 1)):
            n = fixture(); raw_row(n, "FWRegionRegisters", index, value=value)
            with self.subTest(index=index), self.assertRaises(ValueError): decode(n)
        for index in range(10):
            for kwargs in ({"reads": 1}, {"second": 0xffffffff}, {"offset": 0x110130}):
                n = fixture(); raw_row(n, "FWRegionRegisters", index, **kwargs)
                with self.subTest(index=index, kwargs=kwargs), self.assertRaises(ValueError): decode(n)

    def test_active_or_unknown_display_heads_prevent_success(self):
        for index in range(2, 6):
            for value in (0x100, 0x200, 0x300, 0xffffffff):
                n = fixture(); raw_row(n, "FWDisplayRegisters", index, value=value)
                with self.subTest(index=index, value=value), self.assertRaises(ValueError): decode(n)
        for index, value in ((0, 0x80), (1, 0), (1, 5)):
            n = fixture(); raw_row(n, "FWDisplayRegisters", index, value=value)
            with self.assertRaises(ValueError): decode(n)

    def test_display_head_skip_rule_uses_count_and_mask(self):
        n = fixture()
        raw_row(n, "FWDisplayRegisters", 0, value=1)
        raw_row(n, "FWDisplayRegisters", 1, value=2)
        for index in (3, 4, 5): raw_row(n, "FWDisplayRegisters", index, value=0, reads=0)
        self.assertTrue(decode(n)["passed"])
        raw_row(n, "FWDisplayRegisters", 3, value=0, reads=2)
        with self.assertRaises(ValueError): decode(n)

    def test_staged_payload_and_transfer_evidence_are_still_required(self):
        for key, value in (("FWSECDmemMatched", 511), ("FWSECImemCompleted", 224),
                           ("FWSECPublishCount", 3), ("FWSECBoardMatched", False)):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)
        n = fixture(); data = bytearray(n["FWSECDmem"]); data[1444] ^= 1; n["FWSECDmem"] = bytes(data)
        with self.assertRaises(ValueError): decode(n)
        n = fixture(); n["FWSECCompletions"] = struct.pack("<233I", *((0x602,) * 233))
        with self.assertRaises(ValueError): decode(n)

    def test_global_effect_claims_must_agree(self):
        for key, value in (("FirmwareExecuted", False), ("DMATransferExecuted", False),
                           ("FWExecutionPassed", False), ("MMIOReadOnly", True),
                           ("IdentificationMMIOReadOnly", False)):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)

    def test_failed_boot_retains_host_and_region_even_if_quiescent(self):
        n = fixture()
        n.update(FWBootPassed=False, FirmwareExecuted=False, FWExecutionPassed=False,
                 FWBootAuthenticatedExecutionInferred=False, FWBootWprTransitionObserved=False,
                 FWBootStatus="fwsec-boot-mailbox-error", FWBootMailbox0=7,
                 FalconPassed=False, FalconResourcesRetained=True, FalconCleanupVerified=False,
                 FalconCpuIntact=False, FalconStatus="fwsec-host-retained-for-uncertain-firmware")
        result = decode(n)
        self.assertFalse(result["passed"])
        self.assertTrue(result["staging"]["staging_verified"])
        self.assertTrue(result["firmware_execution_tested"])
        self.assertTrue(result["host_dma_resources_retained"])
        self.assertTrue(result["region"]["persistent"])
        self.assertEqual(result["diagnostics"], [])
        n["FalconResourcesRetained"] = False
        self.assertTrue(decode(n)["diagnostics"])

    def test_verified_boot_with_failed_host_cleanup_is_separate_result(self):
        n = fixture()
        n.update(FWExecutionPassed=False, FalconPassed=False, FalconResourcesRetained=True,
                 FalconCleanupVerified=False, FalconCpuIntact=False,
                 FalconStatus="fwsec-host-buffer-changed-resources-retained")
        result = decode(n)
        self.assertTrue(result["boot_verified"])
        self.assertTrue(result["firmware_executed"])
        self.assertFalse(result["passed"])
        self.assertFalse(result["host_dma_cleanup_verified"])

    def test_no_start_after_failed_fresh_recheck_can_release_transaction(self):
        n = fixture()
        for name in BOOL_FIELDS: n["FWBoot" + name] = False
        for name in ("InitialReads", "CpuBeforeStart", "LastCpu", "HaltPolls", "DelayCalls",
                     "Mailbox0", "Mailbox1", "Scratch", "WprLo", "WprHi", "WriteAttempts", "VerifiedWrites"):
            n["FWBoot" + name] = 0
        n.update(FWBootInitial=bytes(48), FWBootStatus="fwsec-boot-fresh-region-check-failed",
                 FirmwareExecuted=False, FWExecutionPassed=False, FWRegionRechecked=False,
                 FWRegionOwned=False, FWRegionPersistent=False, FWProviderHeld=False,
                 FalconStatus="FWSEC-staged-DMEM-verified-not-executed")
        result = decode(n)
        self.assertFalse(result["passed"])
        self.assertFalse(result["firmware_execution_tested"])
        self.assertTrue(result["staging"]["staging_verified"])
        self.assertFalse(result["region"]["persistent"])
        self.assertEqual(result["diagnostics"], [])

    def test_additional_native_retention_and_recheck_fields_must_agree(self):
        n = fixture()
        n.update(FWDisplayStatus="fwsec-display-measured", FWRecheckPolls=1,
                 FWRetainedUntilPlatformReset=True)
        self.assertTrue(decode(n)["passed"])
        for key, value in (("FWDisplayStatus", "not-run"), ("FWRecheckPolls", 0),
                           ("FWRecheckPolls", 201), ("FWRetainedUntilPlatformReset", False)):
            altered = copy.deepcopy(n); altered[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError): decode(altered)

    def test_post_start_final_snapshot_requires_halt_and_falcon_selection(self):
        for reg, value in ((3, 0), (3, 2), (6, 0), (6, 0x11), (10, 1)):
            n = fixture(); set_register(n, "Final", reg, value)
            with self.subTest(reg=reg), self.assertRaises(ValueError): decode(n)

    def test_old_modes_cannot_enable_execution_context(self):
        n = fixture(); n.update(ProbeVersion="0.7.0", Mode="bounded-fwsec-stage")
        for callback in (lambda: decode_fuse(n, True, execution_context=True),
                         lambda: decode_fwsec_preflight(n, True, 6144, execution_context=True),
                         lambda: decode_fwsec_stage(n, True, execution_context=True),
                         lambda: decode(n)):
            with self.assertRaises(ValueError): callback()

    def test_old_default_decoders_still_reject_global_execution(self):
        n = fixture()
        for callback in (lambda: decode_fuse(n, True), lambda: decode_fwsec_preflight(n, True, 6144),
                         lambda: decode_fwsec_stage(n, True)):
            with self.assertRaises(ValueError): callback()

    def test_snapshot_integration_has_initial_views_and_no_fake_dma_mapping(self):
        result = decode(fixture(), snapshot=True)
        self.assertTrue(result["fwsec_execution"]["passed"])
        self.assertTrue(result["firmware_executed"])
        self.assertFalse(result["compute_tested"])
        self.assertNotIn("dma_mapping", result)
        self.assertNotIn("fwsec_stage", result)
        self.assertEqual(result["fwsec_preflight"]["measurement_phase"], "initial-before-execution")
        self.assertEqual(result["fwsec_fuse"]["measurement_phase"], "initial-before-execution")
        self.assertFalse(result["fwsec_preflight"]["ready_to_boot"])

    def test_scalar_types_bounds_and_evidence_lengths_are_enforced(self):
        for key, value in (("FWBootPassed", 1), ("FWBootHaltPolls", True),
                           ("FWRegionSize", False), ("FWRegionOffset", -1),
                           ("FWRegionVram", 1 << 64), ("FWBootInitial", bytes(47)),
                           ("FWRegionRegisters", bytes(159)), ("FWDisplayRegisters", bytes(95))):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)

    def test_prerequisite_cannot_be_inferred_from_success_flags(self):
        with self.assertRaises(ValueError): decode(fixture(), ready=False)
        with self.assertRaises(ValueError): decode(fixture(), ready=1)


if __name__ == "__main__":
    unittest.main()
