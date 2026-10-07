import copy
import hashlib
from pathlib import Path
import plistlib
import struct
import unittest
from unittest.mock import patch

import decode_fwsec_stage as stage
from test_falcon_decode import BOOL_FIELDS, fixture as lifecycle_fixture, set_register


def fixture(real=False):
    # Tests explicitly substitute only the ROM pin for synthetic board-shaped
    # data. Expected DMEM is built independently rather than calling the decoder.
    if real:
        source = Path(__file__).parent / "results/probe-20260906T092946Z/snapshot.plist"
        if not source.exists():
            raise unittest.SkipTest("Real board snapshot is unavailable")
        nodes = plistlib.loads(source.read_bytes())
        n = next(v for v in nodes if v.get("ProbeVersion") == "0.6.0")
        from extract_fwsec import extract
        report, assets = extract(n["VBIOSShadow"], 6144)
        original = assets["fwsec-image-unmodified.bin"]
        signature = assets["fwsec-signature-2.bin"]
    else:
        from test_fwsec_loader import fixture as board_fixture, IMAGE, DESC
        n = board_fixture()
        original = n["VBIOSShadow"][IMAGE:IMAGE + 59648]
        signature = n["VBIOSShadow"][DESC + 44 + 2 * 384:DESC + 44 + 3 * 384]
    n = copy.deepcopy(n)
    n.update(lifecycle_fixture())
    n.pop("FalconPhases")
    n.pop("FalconPublish0IOReturn")
    n.pop("FalconPublish1IOReturn")
    n.update(ProbeVersion="0.7.0", Mode="bounded-fwsec-stage", FalconStatus=stage.STATUS)
    for snapshot in ("Initial", "Final"):
        set_register(n, snapshot, 2, 0x80420100)
    prepared = bytearray(original)
    prepared[59044:59428] = signature
    prepared[59020:59024] = b"\x15\0\0\0"
    command_words = (1, 24, 0, 0, 0, 2, 1, 20, 0x17fe00, 0x100, 2)
    prepared[59584:59628] = struct.pack("<11I", *command_words)
    n.update(FWSECBoardMatched=True, FWSECImageSize=59648, FWSECSignatureIndex=2,
             FWSECImageSHA256=hashlib.sha256(prepared).hexdigest(),
             FWSECOriginalImageSHA256=hashlib.sha256(original).hexdigest(),
             FWSECRomSHA256=hashlib.sha256(n["VBIOSShadow"]).hexdigest(),
             FWSECHwcfg=0x80420100, FWSECCanaryMatched=512, FWSECPublishCount=4,
             FWSECDmaPolls=699, FWSECImemSubmitted=225, FWSECImemCompleted=225,
             FWSECDmemSubmitted=8, FWSECDmemCompleted=8, FWSECDmemReads=512, FWSECDmemMatched=512,
             FWSECMismatchWord=0xffffffff, FWSECMismatchValue=0, FWSECPublishIOReturn=0,
             FWSECCompletions=struct.pack("<233I", *((0x616,) * 225 + (0x602,) * 8)),
             FWSECDmem=bytes(prepared[57600:]))
    return n


def decode(n, prerequisite=True):
    with patch.object(stage, "BOARD_ROM_SHA256", hashlib.sha256(fixture()["VBIOSShadow"]).hexdigest()):
        return stage.decode_fwsec_stage(n, prerequisite)


class FWSECStageDecodeTest(unittest.TestCase):
    def test_full_raw_dmem_and_completions_prove_staging_only(self):
        result = decode(fixture())
        self.assertTrue(result["passed"])
        self.assertTrue(result["dmem_integrity_verified"])
        self.assertEqual(result["observed_dmem_matches"], 512)
        self.assertEqual((result["imem_completed"], result["dmem_completed"]), (225, 8))
        for key in ("imem_integrity_verified", "hardware_signature_verification_tested",
                    "signature_verified_by_hardware", "firmware_executed", "compute_tested",
                    "ready_for_hardware_boot", "frts_region_reserved", "addresses_still_valid"):
            self.assertIs(result[key], False)
        self.assertTrue(all(result["evidence_checks"].values()))

    def test_success_counts_and_returns_cannot_be_forged(self):
        mutations = (("FWSECCanaryMatched", 511), ("FWSECPublishCount", 3), ("FWSECDmaPolls", 698),
                     ("FWSECImemSubmitted", 224), ("FWSECImemCompleted", 224),
                     ("FWSECDmemSubmitted", 7), ("FWSECDmemCompleted", 7),
                     ("FWSECDmemReads", 511), ("FWSECDmemMatched", 511),
                     ("FWSECMismatchWord", 0), ("FWSECImageSize", 59647), ("FWSECSignatureIndex", 1),
                     ("FWSECBoardMatched", False), ("FWSECHwcfg", 0x200),
                     ("ProbeVersion", "0.6.0"), ("Mode", "bounded-falcon-dma"))
        for key, value in mutations + tuple((k, 1) for k in stage.RETURN_FIELDS):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)
        with self.assertRaises(ValueError): decode(fixture(), False)

    def test_changed_dmem_and_replayed_canary_cannot_pass(self):
        for word in (0, 355, 361, 496, 511):
            n = fixture(); data = bytearray(n["FWSECDmem"]); data[word * 4] ^= 1; n["FWSECDmem"] = bytes(data)
            with self.subTest(word=word), self.assertRaises(ValueError): decode(n)
        n = fixture(); n["FWSECDmem"] = bytes(v ^ 255 for v in n["FWSECDmem"])
        with self.assertRaises(ValueError): decode(n)

    def test_completion_idle_and_readability_required_for_every_block(self):
        for index in (0, 63, 224, 225, 232):
            for value in (0, 1, 3, 0xffffffff, 0xbad0fb02, 0xbadf0002):
                n = fixture(); data = bytearray(n["FWSECCompletions"])
                struct.pack_into("<I", data, index * 4, value); n["FWSECCompletions"] = bytes(data)
                with self.subTest(index=index, value=value), self.assertRaises(ValueError): decode(n)

    def test_board_rom_fuse_and_exported_hashes_are_independent_checks(self):
        mutations = (("FuseFirst", 1), ("FuseSecond", 2), ("FuseReads", 1), ("FuseOffset", 0x8241dc),
                     ("FuseSignatureMask", 3), ("FuseSignatureCount", 2), ("FuseSignatureIndex", 1),
                     ("FusePassed", False), ("FuseFirst", True), ("FWSECImageSHA256", "0" * 64),
                     ("FWSECOriginalImageSHA256", "0" * 64), ("FWSECRomSHA256", "0" * 64))
        for key, value in mutations:
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)
        n = fixture(); data = bytearray(n["VBIOSShadow"]); data[100000] ^= 1; n["VBIOSShadow"] = bytes(data)
        with self.assertRaises(ValueError): decode(n)
        with self.assertRaises(ValueError): stage.decode_fwsec_stage(fixture(), True)

    def test_lifecycle_flags_and_final_registers_must_prove_cleanup(self):
        mutations = [("Falcon" + suffix, False) for suffix in BOOL_FIELDS if suffix not in ("Passed", "ResourcesRetained")]
        mutations += [("FalconResourcesRetained", True), ("FalconCommandAfter", 4), ("FalconCommandBefore", 2),
                      ("FalconCommandEnabled", 2), ("FalconDeviceStatusAfter", 0x20), ("FalconDeviceStatusBefore", 0xffff),
                      ("FalconResetCount", 1), ("FalconDrainPolls", 1), ("FalconMapperMode", "bypass")]
        for key, value in mutations:
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)
        for index, value in ((0, 1), (1, 0x1000), (3, 2), (4, 0), (5, 3), (7, 0x80),
                             (9, 0x80), (10, 1), (11, 1), (12, 1), (13, 1), (2, 0xffffffff)):
            n = fixture(); set_register(n, "Final", index, value)
            with self.subTest(index=index), self.assertRaises(ValueError): decode(n)

    def test_segment_bounds_and_environment_gate(self):
        for address, length in ((0, 4096), (1 << 40, 4096), (0x100001001, 4096),
                                (0x100001000, 4096), (0x100000000, 256)):
            n = fixture(); n["FalconSegments"] = struct.pack("<QQ", address, length) + n["FalconSegments"][16:]
            with self.subTest(address=address), self.assertRaises(ValueError): decode(n)
        for values in ((1, 1, 255), (0, 0, 255), (0, 1, 254), (0, 0xffffffff, 255)):
            n = fixture(); n["FalconEnvironment"] = struct.pack("<3I", *values)
            with self.subTest(values=values), self.assertRaises(ValueError): decode(n)

    def test_partial_failure_is_reported_without_imem_or_signature_claims(self):
        n = fixture()
        n.update(FalconPassed=False, FalconCpuIntact=False, FalconStatus="falcon-DMA-timeout",
                 FWSECImemSubmitted=10, FWSECImemCompleted=9, FWSECDmemSubmitted=0,
                 FWSECDmemCompleted=0, FWSECDmemReads=0, FWSECDmemMatched=0,
                 FWSECPublishCount=1, FWSECDmaPolls=228, FWSECCompletions=struct.pack("<233I", *((0x616,) * 9 + (0,) * 224)))
        result = decode(n)
        self.assertFalse(result["passed"])
        self.assertTrue(result["dma_engine_tested"])
        self.assertTrue(result["cleanup_verified"])
        self.assertFalse(result["imem_integrity_verified"])
        self.assertFalse(result["signature_verified_by_hardware"])

    def test_not_run_board_mismatch_preserves_diagnostic(self):
        n = fixture()
        n.update(FalconPassed=False, FalconStatus="fwsec-board-image-mismatch", FWSECBoardMatched=False,
                 FWSECImemSubmitted=0, FWSECImemCompleted=0, FWSECDmemSubmitted=0, FWSECDmemCompleted=0,
                 FWSECDmemReads=0, FWSECDmemMatched=0, FWSECCanaryMatched=0, FWSECPublishCount=0,
                 FWSECDmaPolls=0, FWSECHwcfg=0xffffffff, FWSECCompletions=bytes(932), FWSECDmem=bytes(2048),
                 FuseFirst=0, FuseSecond=0, FuseSignatureIndex=0)
        result = decode(n, False)
        self.assertFalse(result["passed"])
        self.assertFalse(result["dma_engine_tested"])
        self.assertIn("FuseFirst", result["source_validation_error"])

    def test_failed_cleanup_retains_buffer_uncertainty(self):
        n = fixture()
        n.update(FalconPassed=False, FalconQuiescent=False, FalconTargetsCleared=False,
                 FalconCleanupVerified=False, FalconResourcesRetained=True, FalconCommandAfter=4,
                 FalconStatus="falcon-quiescence-failed-resources-retained")
        result = decode(n)
        self.assertTrue(result["resources_retained"])
        self.assertIsNone(result["addresses_still_valid"])
        self.assertFalse(result["passed"])

    def test_types_blob_sizes_and_impossible_failure_counts_are_rejected(self):
        mutations = (("FWSECBoardMatched", 1), ("FWSECDmemReads", True), ("FWSECDmaPolls", 139801),
                     ("FWSECCanaryMatched", 513), ("FWSECImemSubmitted", 226), ("FWSECDmemSubmitted", 9),
                     ("FWSECPublishCount", 5), ("FWSECMismatchWord", 512), ("FalconCount", 5),
                     ("FalconCommandAfter", 65536), ("FalconEnd", -1), ("FWSECImageSHA256", "X" * 64),
                     ("FirmwareExecuted", True), ("FWSECImemSubmitted", 224))
        for key, value in mutations:
            n = fixture(); n.update(FalconPassed=False); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)
        for key in ("FalconSegments", "FalconInitial", "FalconFinal", "FalconEnvironment", "FWSECCompletions", "FWSECDmem"):
            n = fixture(); n[key] = n[key][:-1]
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)

    def test_signed_ioreg_sentinels_and_ioreturns(self):
        n = fixture(); n.update(FalconMismatchPhase=-1, FalconMismatchWord=-1, FWSECMismatchWord=-1)
        self.assertTrue(decode(n)["passed"])
        n.update(FalconPassed=False, FalconStatus="fwsec-publish-failed", FWSECPublishIOReturn=-536870184)
        self.assertEqual(decode(n)["ioreturns"]["FWSECPublishIOReturn"], 0xe00002d8)

    def test_real_board_reconstruction_matches_separately_derived_pin(self):
        n = fixture(real=True)
        result = stage.decode_fwsec_stage(n, True)
        self.assertTrue(result["passed"])
        self.assertEqual(result["image_sha256"], "8c0785316a8506f192692d036c3b098e31cc4d3e4673f9760b6f3862d8757ca8")


if __name__ == "__main__":
    unittest.main()
