import hashlib
import struct
import unittest
import test_fwsec
import test_host_read_decode
from select_fwsec_signature import select


class SignatureSelectionTest(unittest.TestCase):
    def fixture(self):
        n = test_host_read_decode.fixture()
        n["VBIOSShadow"] = bytes(test_fwsec.fixture())
        return n

    def test_selects_unchanged_correct_block(self):
        for raw, index in ((0, 0), (1, 1), (2, 2), (3, 2)):
            n = self.fixture(); n.update(FuseFirst=raw, FuseSecond=raw, FuseSignatureIndex=index)
            r, signature = select([n], hashlib.sha256(n["VBIOSShadow"]).hexdigest())
            self.assertEqual(signature, bytes([index + 1]) * 384)
            self.assertEqual(r["signature_selected"], index)
            self.assertFalse(r["signature_verified_by_hardware"])
            self.assertFalse(r["firmware_executed"])

    def test_rejects_rom_from_other_snapshot(self):
        with self.assertRaises(ValueError): select([self.fixture()])

    def test_rejects_changed_ucode_and_mask(self):
        for off, fmt, value in ((38, "B", 8), (40, "H", 3)):
            n = self.fixture(); rom = bytearray(n["VBIOSShadow"])
            struct.pack_into("<" + fmt, rom, 0x2000 + off, value); n["VBIOSShadow"] = bytes(rom)
            with self.assertRaises(ValueError): select([n], hashlib.sha256(rom).hexdigest())

    def test_can_select_even_when_host_read_did_not_pass(self):
        n = self.fixture(); n.update(HostPassed=False, HostStatus="host-data-mismatch")
        r, signature = select([n], hashlib.sha256(n["VBIOSShadow"]).hexdigest())
        self.assertEqual(len(signature), 384)
        self.assertFalse(r["gpu_host_read_passed"])

    def test_old_probe_refused(self):
        n = self.fixture(); n["ProbeVersion"] = "0.4.0"
        with self.assertRaises(ValueError): select([n], hashlib.sha256(n["VBIOSShadow"]).hexdigest())


if __name__ == "__main__": unittest.main()
