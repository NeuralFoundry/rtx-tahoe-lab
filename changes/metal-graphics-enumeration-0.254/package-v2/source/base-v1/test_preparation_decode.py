import struct
import unittest
import test_state_decode
from decode_probe import decode_snapshot


class PreparationDecodeTest(unittest.TestCase):
    def fixture(self):
        node = test_state_decode.StateDecodeTest().fixture()
        node.update(Mode="bounded-preparation", ProbeVersion="0.4.0", ROMStatus="rom-captured", ROMPassed=True,
                    ROMStable=True, ROMCapturedBytes=1048576, ROMReadWords=524289,
                    VBIOSShadow=b"\x55\xaa" + bytes(1048574), DMAPassed=True, DMAPrepared=True,
                    DMACPUContentsIntact=True, DMACleanupVerified=True, DMATransferExecuted=False,
                    DMASegmentCount=4, DMAEndOffset=16384, DMACommandBefore=0, DMACommandAfter=0,
                    DMAMapperMode="system-no-mapper", DMALastIOReturn=0, DMAClearIOReturn=0,
                    DMACompleteIOReturn=0, DMAMemoryCompleteIOReturn=0,
                    DMASegments=b"".join(struct.pack("<QQ", 0x100000 + i * 4096, 4096) for i in range(4)))
        return node

    def test_mapping_is_not_gpu_transfer(self):
        result = decode_snapshot([self.fixture()])
        self.assertTrue(result["rom"]["passed"])
        self.assertTrue(result["dma_mapping"]["passed"])
        self.assertFalse(result["compute_tested"])
        self.assertFalse(result["dma_mapping"]["gpu_transfer_tested"])
        self.assertFalse(result["dma_mapping"]["addresses_still_valid"])

    def test_rom_bad_evidence(self):
        for key, value in [("VBIOSShadow", bytes(1048576)), ("ROMReadWords", 1), ("ROMStable", False),
                           ("ROMCapturedBytes", 0), ("MMIOPassed", False)]:
            node = self.fixture(); node[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode_snapshot([node])

    def test_dma_bad_evidence(self):
        for key, value in [("DMAEndOffset", 4096), ("DMAPrepared", False), ("DMACPUContentsIntact", False),
                           ("DMACleanupVerified", False), ("DMATransferExecuted", True), ("DMACommandAfter", 4),
                           ("DMAMapperMode", "unknown"), ("DMAClearIOReturn", 1)]:
            node = self.fixture(); node[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): decode_snapshot([node])

    def test_dma_invalid_segments(self):
        for address, length in [(0, 4096), (0x100001, 4096), (1 << 40, 4096), (0x100000, 0), (0x101000, 4096)]:
            node = self.fixture()
            node["DMASegments"] = struct.pack("<QQ", address, length) + node["DMASegments"][16:]
            with self.assertRaises(ValueError): decode_snapshot([node])

    def test_rom_failure_can_preserve_dma_diagnostics(self):
        node = self.fixture()
        node.update(ROMPassed=False, ROMStable=False, ROMReadWords=1, ROMCapturedBytes=0,
                    VBIOSShadow=b"", MMIOPassed=False, StatePassed=False)
        result = decode_snapshot([node])
        self.assertFalse(result["rom"]["passed"])
        self.assertTrue(result["dma_mapping"]["passed"])


if __name__ == "__main__": unittest.main()
