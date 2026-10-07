"""Validate 0.4 capture/mapping evidence without hardware access."""
import hashlib
import struct

ROM_SIZE = 1 << 20


def decode_rom(node, mmio_passed):
    blob = node.get("VBIOSShadow")
    valid_rom = (isinstance(blob, bytes) and len(blob) == ROM_SIZE
                 and int.from_bytes(blob[:2], "little") in (0xaa55, 0x4e56, 0xbb77)
                 and node.get("ROMStable") is True and node.get("ROMCapturedBytes") == ROM_SIZE
                 and node.get("ROMReadWords") == ROM_SIZE // 2 + 1 and mmio_passed)
    if node.get("ROMPassed") and not valid_rom:
        raise ValueError("ROM success disagrees with bytes, read counts or restoration")
    rom = {"status": node.get("ROMStatus"), "passed": bool(node.get("ROMPassed") and valid_rom),
           "stable": node.get("ROMStable", False), "captured_bytes": node.get("ROMCapturedBytes", 0),
           "read_words": node.get("ROMReadWords", 0),
           "sha256": hashlib.sha256(blob).hexdigest() if valid_rom else None}
    return rom


def decode_preparation(node, mmio_passed):
    rom = decode_rom(node, mmio_passed)
    raw = node.get("DMASegments")
    if not isinstance(raw, bytes) or len(raw) != 64:
        raise ValueError("Invalid DMA segment snapshot")
    rows = list(struct.iter_unpack("<QQ", raw))
    count = node.get("DMASegmentCount", 0)
    if not isinstance(count, int) or not 0 <= count <= 4:
        raise ValueError("Invalid DMA segment count")
    valid_segments = (count == 4 and node.get("DMAEndOffset") == 16384
                      and all(a > 0 and a % 4096 == 0 and a <= (1 << 40) - 4096 and n == 4096 for a, n in rows)
                      and len({a for a, _ in rows}) == 4)
    valid_dma = (valid_segments and node.get("DMAPrepared") is True and node.get("DMACPUContentsIntact") is True
                 and node.get("DMACleanupVerified") is True and node.get("DMATransferExecuted") is False
                 and node.get("DMACommandBefore") == node.get("DMACommandAfter") == 0
                 and node.get("DMAMapperMode") in ("device-mapper", "system-mapper", "system-no-mapper")
                 and all(node.get(k) == 0 for k in ("DMALastIOReturn", "DMAClearIOReturn", "DMACompleteIOReturn",
                                                   "DMAMemoryCompleteIOReturn")))
    if node.get("DMAPassed") and not valid_dma:
        raise ValueError("DMA mapping success disagrees with raw evidence")
    dma = {"status": node.get("DMAStatus"), "passed": bool(node.get("DMAPassed") and valid_dma),
           "mapper_mode": node.get("DMAMapperMode"), "prepared": node.get("DMAPrepared", False),
           "segments": [{"address": hex(a), "length": n} for a, n in rows[:count]],
           "end_offset": node.get("DMAEndOffset"), "cpu_contents_intact": node.get("DMACPUContentsIntact", False),
           "cleanup_verified": node.get("DMACleanupVerified", False),
           "command_before": node.get("DMACommandBefore"), "command_after": node.get("DMACommandAfter"),
           "ioreturns": {key: node.get(key) for key in ("DMALastIOReturn", "DMAClearIOReturn", "DMACompleteIOReturn",
                                                      "DMAMemoryCompleteIOReturn")},
           "gpu_transfer_tested": False, "addresses_still_valid": False}
    return rom, dma
