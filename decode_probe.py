"""Decode RTXProbe's snapshot without opening any hardware interfaces."""
import argparse
import json
import pathlib
import plistlib
import struct
from decode_preparation import decode_preparation, decode_rom
from decode_host_read import decode_host_read, decode_fuse
from decode_falcon import decode_falcon
from decode_fwsec_stage import decode_fwsec_stage
from decode_fwsec_preflight import decode_fwsec_preflight
from decode_fwsec_execution import decode_fwsec_execution

STATE_OFFSETS = (0xa00, 0x1fa828, 0x118128, 0x118234, 0x1183a4)
STATE_NAMES = ("boot42", "wpr2_hi", "scratch05_protection", "scratch05", "vram_mib")


def decode_state(node, transaction_passed):
    blob = node.get("StateRegisters")
    if not isinstance(blob, bytes) or len(blob) != 80:
        raise ValueError("Invalid state register snapshot length")
    rows = list(struct.iter_unpack("<4I", blob))
    if tuple(r[0] for r in rows) != STATE_OFFSETS or any(r[3] > 2 for r in rows):
        raise ValueError("Unexpected state register address/order/read count")
    def stable(row):
        _, first, second, reads = row
        return reads == 2 and first == second and first != 0xffffffff and first & 0xffff0000 != 0xbadf0000
    readable = [stable(row) for row in rows]
    values = [r[1] if readable[i] else None for i, r in enumerate(rows)]
    protected_skip = readable[2] and not values[2] & 1 and rows[3][1:] == (0, 0, 0)
    raw_complete = (all(readable[i] for i in (0, 1, 2, 4)) and (readable[3] or protected_skip)
                    and (values[0] >> 20) & 0x3ff == 0x176 and 1024 <= values[4] <= 65536
                    and (bool(values[2] & 1) == readable[3]))
    if node.get("StateComplete") and not raw_complete:
        raise ValueError("State completion flag disagrees with raw registers")
    passed = raw_complete and transaction_passed and node.get("StateComplete") is True
    if bool(node.get("StatePassed")) != bool(passed):
        raise ValueError("State success flag disagrees with transaction evidence")
    return {"passed": bool(passed), "complete": node.get("StateComplete", False),
            "registers": {name: {"offset": hex(row[0]), "first": hex(row[1]), "second": hex(row[2]),
                                  "reads": row[3], "stable_and_readable": readable[i]}
                          for i, (name, row) in enumerate(zip(STATE_NAMES, rows))},
            "reported_vram_mib": values[4] if readable[4] and 1024 <= values[4] <= 65536 else None,
            "wpr2_hi_is_zero": values[1] == 0 if readable[1] else None,
            "reset_progress_complete": (values[3] & 0xff) == 0xff if readable[3] else None,
            "firmware_executed": False, "dma_tested": False, "ready_to_boot": False}


def decode_header(header):
    if not isinstance(header, bytes) or len(header) != 64:
        raise ValueError("Expected exactly 64 bytes of PCI configuration header")
    words = struct.unpack("<16I", header)
    if words[0] != 0x252010de or words[11] != 0x104c1043:
        raise ValueError("Snapshot is not the exact development GPU")
    bars = []
    idx = 0
    while idx < 6:
        raw = words[4 + idx]
        io = bool(raw & 1)
        wide = not io and (raw & 6) == 4
        if wide and idx == 5:
            raise ValueError("64-bit BAR missing its upper register")
        if not io and (raw & 6) == 6:
            raise ValueError("Reserved PCI memory BAR type")
        address = raw & (0xfffffffc if io else 0xfffffff0)
        if wide:
            address |= words[5 + idx] << 32
        bars.append({"index": idx, "register": hex(0x10 + idx * 4),
                     "address": hex(address), "space": "io" if io else "memory",
                     "width": 64 if wide else 32, "prefetchable": not io and bool(raw & 8),
                     "unassigned_or_absent": raw == 0})
        idx += 2 if wide else 1
    return {"identity": "10de:2520", "subsystem": "1043:104c",
            "memory_decode_enabled": bool(words[1] & 2), "bus_master_enabled": bool(words[1] & 4),
            "bars": bars}


def decode_rebar(blob):
    if not isinstance(blob, bytes) or not blob or len(blob) > 48 or len(blob) % 8:
        raise ValueError("Invalid Resizable BAR snapshot")
    rows = []
    seen = set()
    for cap, ctrl in struct.iter_unpack("<II", blob):
        index = ctrl & 7
        if index > 5 or index in seen:
            raise ValueError("Invalid or duplicate Resizable BAR index")
        seen.add(index)
        sizes = (cap >> 4) | ((ctrl >> 16) << 28)
        exponent = (ctrl >> 8) & 31
        rows.append({"bar_index": index, "current_length": 1 << (20 + exponent),
                     "current_size_supported": bool(sizes & (1 << exponent)),
                     "supported_lengths": [1 << (20 + n) for n in range(44) if sizes & (1 << n)]})
    return rows


def decode_snapshot(nodes):
    nodes = [n for n in nodes if n.get("IOObjectClass") == "RTXProbe" or n.get("ProbeVersion")]
    if len(nodes) != 1:
        raise ValueError("Expected one loaded RTXProbe; no successful measurement is available")
    node = nodes[0]
    if not node.get("ProbeComplete"):
        raise ValueError("Probe did not finish a snapshot")
    if not node.get("ReadOnly") and node.get("Mode") not in ("bounded-boot0", "bounded-state", "bounded-preparation", "bounded-host-read", "bounded-falcon-dma", "bounded-fwsec-stage", "bounded-fwsec-preflight", "bounded-fwsec-execute"):
        raise ValueError("Unknown probe operation mode")
    result = decode_header(node["PCIHeader"])
    result["probe_version"] = node["ProbeVersion"]
    result["power_state"] = "D" + str(node["PCIDState"]) if "PCIDState" in node else "unknown"
    result["mmio_tested"] = False
    result["compute_tested"] = False
    if node.get("Mode") in ("bounded-boot0", "bounded-state", "bounded-preparation", "bounded-host-read", "bounded-falcon-dma", "bounded-fwsec-stage", "bounded-fwsec-preflight", "bounded-fwsec-execute"):
        reads = node.get("MMIOReadCount", 0)
        first, second = node.get("MMIOBoot0First", 0) & 0xffffffff, node.get("MMIOBoot0Second", 0) & 0xffffffff
        after, before = node.get("MMIOCommandAfter"), node.get("MMIOCommandBefore")
        consistent = (reads == 2 and first == second and ((first >> 20) & 0x1ff) == 0x176
                      and before == after == 0 and node.get("MMIOCommandDuring") == 2
                      and node.get("MMIORestoreVerified") is True
                      and node.get("IdentificationMMIOReadOnly" if node.get("Mode") in ("bounded-host-read", "bounded-falcon-dma", "bounded-fwsec-stage", "bounded-fwsec-execute") else "MMIOReadOnly") is True)
        if node.get("MMIOPassed") and not consistent:
            raise ValueError("MMIO success flag disagrees with the raw evidence")
        result["mmio_tested"] = reads > 0
        result["mmio"] = {"status": node.get("MMIOStatus"), "passed": bool(node.get("MMIOPassed")) and consistent,
                          "read_count": reads, "boot0_first": hex(first), "boot0_second": hex(second),
                          "chipset": hex((first >> 20) & 0x1ff) if reads else None,
                          "command_before": before, "command_during": node.get("MMIOCommandDuring"),
                          "command_after": after, "restore_verified": node.get("MMIORestoreVerified", False),
                          "memory_enable_attempted": node.get("MMIOMemoryEnableAttempted", False)}
        if node.get("Mode") in ("bounded-state", "bounded-preparation", "bounded-host-read", "bounded-falcon-dma", "bounded-fwsec-stage", "bounded-fwsec-preflight", "bounded-fwsec-execute"):
            result["gpu_state"] = decode_state(node, result["mmio"]["passed"])
            if result["mmio"]["passed"] and not result["gpu_state"]["passed"]:
                raise ValueError("Transaction claims success without a complete GPU state")
        if node.get("Mode") in ("bounded-preparation", "bounded-host-read", "bounded-falcon-dma", "bounded-fwsec-stage"):
            result["rom"], result["dma_mapping"] = decode_preparation(node, result["mmio"]["passed"])
        if node.get("Mode") == "bounded-host-read":
            result["fwsec_fuse"], result["gpu_host_read"] = decode_host_read(node, result["mmio"]["passed"])
        if node.get("Mode") == "bounded-falcon-dma":
            result["fwsec_fuse"] = decode_fuse(node, result["mmio"]["passed"])
            ready = result["fwsec_fuse"]["passed"] and result["dma_mapping"]["passed"] and result["rom"]["passed"]
            result["falcon_dma"] = decode_falcon(node, ready)
        if node.get("Mode") == "bounded-fwsec-stage":
            result["fwsec_fuse"] = decode_fuse(node, result["mmio"]["passed"])
            ready = result["fwsec_fuse"]["passed"] and result["dma_mapping"]["passed"] and result["rom"]["passed"]
            result["fwsec_stage"] = decode_fwsec_stage(node, ready)
        if node.get("Mode") == "bounded-fwsec-preflight":
            result["rom"] = decode_rom(node, result["mmio"]["passed"])
            result["fwsec_fuse"] = decode_fuse(node, result["mmio"]["passed"])
            ready = result["mmio"]["passed"] and result["rom"]["passed"] and result["fwsec_fuse"]["passed"]
            result["fwsec_preflight"] = decode_fwsec_preflight(node, ready, result["gpu_state"]["reported_vram_mib"])
            result["fwsec_preflight"]["board_rom_matches_previous_image"] = (
                result["rom"].get("sha256") == "b7b3d5a2b1698a5a1f24303b0fa915ef8da944ff3ede888377c2d02df5048b91")
            if not result["fwsec_preflight"]["board_rom_matches_previous_image"]:
                result["fwsec_preflight"]["passed"] = False
        if node.get("Mode") == "bounded-fwsec-execute":
            result["gpu_state"]["measurement_phase"] = "initial-before-execution"
            result["rom"] = decode_rom(node, result["mmio"]["passed"])
            result["fwsec_fuse"] = decode_fuse(node, result["mmio"]["passed"], execution_context=True)
            ready = result["mmio"]["passed"] and result["rom"]["passed"] and result["fwsec_fuse"]["passed"]
            result["fwsec_preflight"] = decode_fwsec_preflight(
                node, ready, result["gpu_state"]["reported_vram_mib"], execution_context=True)
            result["fwsec_execution"] = decode_fwsec_execution(node, ready and result["fwsec_preflight"]["passed"])
            result["firmware_executed"] = result["fwsec_execution"]["firmware_executed"]
    if "PCIeLinkStatus" in node:
        status = node["PCIeLinkStatus"]
        result["pcie_link"] = {"speed_code": status & 15, "width": (status >> 4) & 63,
                               "training": bool(status & 0x800)}
    for bar in result["bars"]:
        prefix = "BAR" + str(bar["index"])
        if prefix + "DescriptorAddress" in node:
            bar["descriptor_address"] = hex(node[prefix + "DescriptorAddress"] & 0xffffffffffffffff)
            bar["descriptor_length"] = node[prefix + "DescriptorLength"]
            bar["descriptor_matches_config"] = bar["descriptor_address"] == bar["address"]
    result["warnings"] = {k: v for k, v in node.items() if k.endswith("Error")}
    if "ResizableBAREntries" in node:
        result["resizable_bars"] = decode_rebar(node["ResizableBAREntries"])
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=pathlib.Path)
    parser.add_argument("--expected-version")
    parser.add_argument("--rom-output", type=pathlib.Path)
    args = parser.parse_args()
    try:
        raw = args.snapshot.read_bytes()
        if not raw.strip():
            raise ValueError("Snapshot is empty: RTXProbe is not loaded")
        nodes = plistlib.loads(raw)
        result = decode_snapshot(nodes)
        if args.expected_version and result["probe_version"] != args.expected_version:
            raise ValueError("Loaded module version differs from the requested bundle")
        if args.rom_output and result.get("rom", {}).get("passed"):
            node = next(n for n in nodes if n.get("ProbeVersion") == result["probe_version"])
            args.rom_output.write_bytes(node["VBIOSShadow"])
        print(json.dumps(result, indent=2))
        if "mmio" in result and not result["mmio"]["passed"]:
            parser.exit(4, "MMIO experiment did not pass; inspect status and restoration evidence\n")
        if "dma_mapping" in result and (not result["dma_mapping"]["passed"] or not result["rom"]["passed"]):
            parser.exit(5, "Preparation incomplete; inspect ROM and DMA evidence\n")
        if "gpu_host_read" in result and not result["gpu_host_read"]["passed"]:
            parser.exit(6, "GPU host-read experiment did not pass; inspect fuse and restoration evidence\n")
        if "falcon_dma" in result and not result["falcon_dma"]["passed"]:
            parser.exit(7, "Falcon DMA experiment did not pass; inspect PIO, DMA and quiescence evidence\n")
        if "fwsec_stage" in result and not result["fwsec_stage"]["passed"]:
            parser.exit(8, "FWSEC staging did not pass; inspect transfer, DMEM and quiescence evidence\n")
        if "fwsec_preflight" in result and not result["fwsec_preflight"]["passed"]:
            parser.exit(9, "FWSEC preflight did not pass; inspect register and layout evidence\n")
        if "fwsec_execution" in result and not result["fwsec_execution"]["passed"]:
            parser.exit(10, "FWSEC execution did not pass; inspect boot, host cleanup and retained region evidence\n")
    except (ValueError, KeyError, OSError, plistlib.InvalidFileException) as e:
        parser.exit(3, f"No valid GPU measurement: {e}\n")
