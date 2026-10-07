import copy
from pathlib import Path
import struct
import unittest

from decode_fwsec_preflight import decode_fwsec_preflight


OFFSETS = (0x820c04, 0x625f04, 0x1fa824, 0x1fa828, 0x110100,
           0x111388, 0x1103c0, 0x110108, 0x111668, 0x1438)


def fixture():
    values = (0, 0, 0, 0, 0x10, 0x10, 0, 0x80420100, 1, 0)
    return {
        "PreflightStatus": "fwsec-preflight-measured", "PreflightComplete": True,
        "PreflightPassed": True, "PreflightDisplaySupported": True,
        "PreflightWorkspaceValid": False, "PreflightRequiresRelocation": False,
        "PreflightLayoutValid": True, "PreflightEngineIdle": True, "PreflightWprClear": True,
        "PreflightVramBytes": 0x180000000, "PreflightWorkspaceAddress": 0,
        "PreflightWorkspaceBoundary": 0x17ff00000, "PreflightFrtsOffset": 0x17fe00000,
        "PreflightFrtsEnd": 0x17ff00000, "PreflightWprLo": 0, "PreflightWprHi": 0,
        "PreflightRegionOwned": False, "PreflightExecutionReady": False,
        "FirmwareExecuted": False, "DMATransferExecuted": False, "MMIOReadOnly": True,
        "PreflightRegisters": b"".join(struct.pack("<4I", offset, value, value, 2)
                                        for offset, value in zip(OFFSETS, values)),
    }


def change_row(node, index, value=None, second=None, reads=2, offset=None):
    data = bytearray(node["PreflightRegisters"])
    old = struct.unpack_from("<4I", data, index * 16)
    value = old[1] if value is None else value
    struct.pack_into("<4I", data, index * 16, old[0] if offset is None else offset,
                     value, value if second is None else second, reads)
    node["PreflightRegisters"] = bytes(data)


def decode(node, ready=True, vram_mib=6144):
    return decode_fwsec_preflight(node, ready, vram_mib)


class FWSECPreflightDecodeTests(unittest.TestCase):
    def test_full_default_capture_is_measurement_only(self):
        result = decode(fixture())
        self.assertTrue(result["passed"])
        self.assertEqual(result["layout_candidate"]["frts_offset"], 0x17fe00000)
        self.assertEqual(result["diagnostics"], [])
        for key in ("firmware_executed", "firmware_execution_tested", "compute_tested",
                    "dma_transfer_tested", "region_owned", "frts_region_reserved",
                    "execution_ready", "ready_to_boot"):
            self.assertIs(result[key], False)

    def test_unsupported_display_skips_workspace_only(self):
        n = fixture()
        change_row(n, 0, 1)
        change_row(n, 1, 0, reads=0)
        n["PreflightDisplaySupported"] = False
        self.assertTrue(decode(n)["passed"])
        change_row(n, 1, 0, reads=2)
        with self.assertRaises(ValueError):
            decode(n)

    def test_supported_display_cannot_skip_workspace(self):
        n = fixture(); change_row(n, 1, 0, reads=0)
        with self.assertRaises(ValueError):
            decode(n)

    def test_workspace_validity_bit_and_low_bits(self):
        n = fixture(); change_row(n, 1, 0x017ff507)
        self.assertTrue(decode(n)["passed"])
        change_row(n, 1, 0x017ff508)
        n.update(PreflightWorkspaceValid=True, PreflightWorkspaceAddress=0x17ff50000,
                 PreflightWorkspaceBoundary=0x17ff50000, PreflightFrtsEnd=0x17ff40000,
                 PreflightFrtsOffset=0x17fe40000)
        result = decode(n)
        self.assertTrue(result["passed"])
        self.assertFalse(result["requires_relocation"])

    def test_workspace_relocation_candidate_is_not_reservation(self):
        n = fixture(); change_row(n, 1, 0x017fc008)
        n.update(PreflightWorkspaceValid=True, PreflightWorkspaceAddress=0x17fc00000,
                 PreflightWorkspaceBoundary=0x17ffe0000, PreflightRequiresRelocation=True,
                 PreflightFrtsEnd=0x17ffe0000, PreflightFrtsOffset=0x17fee0000)
        result = decode(n)
        self.assertTrue(result["passed"])
        self.assertTrue(result["requires_relocation"])
        self.assertFalse(result["region_owned"])

    def test_exact_default_boundary_does_not_relocate(self):
        n = fixture(); change_row(n, 1, 0x017ff008)
        n.update(PreflightWorkspaceValid=True, PreflightWorkspaceAddress=0x17ff00000)
        self.assertTrue(decode(n)["passed"])

    def test_out_of_vram_workspace_is_complete_failed_diagnostic(self):
        n = fixture(); change_row(n, 1, 0x01800008)
        n.update(PreflightPassed=False, PreflightStatus="fwsec-preflight-layout-invalid",
                 PreflightWorkspaceValid=True, PreflightWorkspaceAddress=0x180000000,
                 PreflightWorkspaceBoundary=0x17ff00000, PreflightFrtsOffset=0, PreflightFrtsEnd=0,
                 PreflightLayoutValid=False)
        result = decode(n)
        self.assertTrue(result["complete"])
        self.assertFalse(result["passed"])
        self.assertEqual(result["diagnostics"], [])
        n["PreflightPassed"] = True
        with self.assertRaises(ValueError): decode(n)

    def test_wpr_page_arithmetic_and_busy_engine_do_not_imply_execution(self):
        n = fixture()
        change_row(n, 2, 0x1234567b); change_row(n, 3, 0x123456ab)
        change_row(n, 4, 2); change_row(n, 5, 0x80); change_row(n, 6, 1); change_row(n, 8, 0x11)
        n.update(PreflightEngineIdle=False, PreflightWprClear=False,
                 PreflightWprLo=0x1234567000, PreflightWprHi=0x123456a000)
        result = decode(n)
        self.assertTrue(result["passed"])
        self.assertFalse(result["execution_ready"])

    def test_each_engine_idle_condition_is_checked(self):
        for index, value in ((4, 0), (4, 0x12), (5, 0x80), (6, 1), (8, 0x11)):
            n = fixture(); change_row(n, index, value)
            with self.subTest(index=index), self.assertRaises(ValueError): decode(n)
            n["PreflightEngineIdle"] = False
            self.assertTrue(decode(n)["passed"])

    def test_every_claimed_field_is_independently_checked(self):
        n = fixture()
        for key in ("Complete", "DisplaySupported", "WorkspaceValid", "RequiresRelocation",
                    "LayoutValid", "EngineIdle", "WprClear", "VramBytes", "WorkspaceAddress",
                    "WorkspaceBoundary", "FrtsOffset", "FrtsEnd", "WprLo", "WprHi"):
            altered = copy.deepcopy(n)
            value = altered["Preflight" + key]
            altered["Preflight" + key] = not value if type(value) is bool else value + 1
            with self.subTest(key=key), self.assertRaises(ValueError): decode(altered)

    def test_readability_stability_order_and_count_cannot_be_forged(self):
        for index in range(10):
            for value in (0xffffffff, 0xbad01234, 0xbadf1234):
                n = fixture(); change_row(n, index, value)
                with self.subTest(index=index, value=value), self.assertRaises(ValueError): decode(n)
            for kwargs in ({"second": 17}, {"reads": 1}, {"reads": 3}, {"offset": 0x110130}):
                n = fixture(); change_row(n, index, **kwargs)
                with self.subTest(index=index, kwargs=kwargs), self.assertRaises(ValueError): decode(n)

    def test_incomplete_unreadable_capture_returns_diagnostics(self):
        n = fixture(); change_row(n, 4, 0xbad01234, reads=1)
        n.update(PreflightComplete=False, PreflightPassed=False,
                 PreflightStatus="fwsec-preflight-register-unreadable")
        result = decode(n)
        self.assertFalse(result["complete"])
        self.assertFalse(result["passed"])
        self.assertTrue(result["diagnostics"])

    def test_failed_malformed_evidence_returns_diagnostics(self):
        for raw in (None, b"", bytes(159), "bad"):
            n = fixture(); n.update(PreflightRegisters=raw, PreflightPassed=False, PreflightComplete=False)
            self.assertFalse(decode(n)["passed"])
            n["PreflightPassed"] = True
            with self.assertRaises(ValueError): decode(n)

    def test_no_effect_flag_can_claim_dangerous_state(self):
        for key in ("PreflightRegionOwned", "PreflightExecutionReady", "FirmwareExecuted",
                    "DMATransferExecuted", "MMIOReadOnly"):
            for passed in (True, False):
                n = fixture(); n[key] = not n[key]; n["PreflightPassed"] = passed
                with self.subTest(key=key, passed=passed), self.assertRaises(ValueError): decode(n)

    def test_wrong_vram_or_prerequisite_cannot_pass(self):
        for vram in (0, 6143, 6145, 8192, "6144", True):
            with self.subTest(vram=vram), self.assertRaises(ValueError): decode(fixture(), vram_mib=vram)
        with self.assertRaises(ValueError): decode(fixture(), ready=False)
        n = fixture(); n["PreflightPassed"] = False
        self.assertFalse(decode(n, ready=False)["passed"])
        with self.assertRaises(ValueError): decode(n, ready=1)

    def test_success_status_must_agree_with_capture(self):
        for status in ("not-run", "fwsec-preflight-layout-invalid", None):
            n = fixture(); n["PreflightStatus"] = status
            with self.subTest(status=status), self.assertRaises(ValueError): decode(n)

    def test_scalar_types_cannot_use_boolean_integer_equivalence(self):
        for key, value in (("PreflightComplete", 1), ("PreflightWorkspaceAddress", False),
                           ("PreflightFrtsOffset", -1), ("PreflightVramBytes", 1 << 64)):
            n = fixture(); n[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode(n)

    def integration_fixture(self):
        from test_preparation_decode import PreparationDecodeTest
        from test_host_read_decode import fixture as host_fixture
        n = PreparationDecodeTest().fixture()
        for key in list(n):
            if key.startswith("DMA"):
                del n[key]
        n.update({k: v for k, v in host_fixture().items() if k.startswith("Fuse")})
        n.update(fixture())
        n.update(Mode="bounded-fwsec-preflight", ProbeVersion="0.8.0")
        return n

    def test_snapshot_integration_without_dma_mapping_properties(self):
        from decode_probe import decode_snapshot
        path = Path(__file__).parent / "results/probe-20260906T100715Z/vbios-shadow.bin"
        if not path.exists():
            self.skipTest("Captured board ROM is unavailable")
        n = self.integration_fixture(); n["VBIOSShadow"] = path.read_bytes()
        result = decode_snapshot([n])
        self.assertTrue(result["fwsec_preflight"]["passed"])
        self.assertNotIn("dma_mapping", result)
        self.assertFalse(result["compute_tested"])

    def test_snapshot_integration_changed_rom_cannot_pass(self):
        from decode_probe import decode_snapshot
        result = decode_snapshot([self.integration_fixture()])
        self.assertTrue(result["rom"]["passed"])
        self.assertFalse(result["fwsec_preflight"]["board_rom_matches_previous_image"])
        self.assertFalse(result["fwsec_preflight"]["passed"])
        self.assertNotIn("dma_mapping", result)


if __name__ == "__main__":
    unittest.main()
