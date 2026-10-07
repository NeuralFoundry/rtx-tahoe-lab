"""Validate bounded Falcon DMA evidence without opening GPU hardware."""
import struct


REGISTERS = (
    ("engine", 0x3c0), ("hwcfg2", 0xf4), ("hwcfg", 0x108),
    ("cpuctl", 0x100), ("dmactl", 0x10c), ("dmacmd", 0x118),
    ("bcr", 0x1668), ("riscv_cpu", 0x1388), ("transcfg", 0x600),
    ("fbifctl", 0x624), ("dmabase", 0x110), ("dmabase1", 0x128),
    ("dmaoffset", 0x114), ("fboffset", 0x11c),
)
U32_FIELDS = (
    "Count", "CommandBefore", "CommandEnabled", "CommandAfter",
    "DeviceStatusBefore", "DeviceStatusAfter", "InitialReads", "FinalReads",
    "ResetCount", "ResetPolls", "DrainPolls", "MismatchPhase", "MismatchWord",
    "MismatchValue", "LastIOReturn", "ClearIOReturn", "CompleteIOReturn",
    "MemoryCompleteIOReturn", "Publish0IOReturn", "Publish1IOReturn",
)
BOOL_FIELDS = (
    "Prepared", "MemoryAttempted", "MasterAttempted", "ResetAttempted", "TargetsAttempted",
    "Quiescent", "TargetsCleared", "CleanupVerified", "ResourcesRetained",
    "CpuIntact", "Passed",
)
RETURN_FIELDS = (
    "LastIOReturn", "ClearIOReturn", "CompleteIOReturn", "MemoryCompleteIOReturn",
    "Publish0IOReturn", "Publish1IOReturn",
)


def pattern(index, phase):
    return (0x4636444d ^ (index * 2654435761) ^ (0xe3a1957b if phase else 0)) & 0xffffffff


def readable(value):
    return value != 0xffffffff and value & 0xffff0000 not in (0xbadf0000, 0xbad00000)


def _blob(node, name, size):
    value = node.get("Falcon" + name)
    if not isinstance(value, bytes) or len(value) != size:
        raise ValueError("Invalid Falcon " + name + " evidence size")
    return value


def _registers(values, count):
    return {name: {"offset": hex(0x110000 + offset), "value": value,
                   "hex": hex(value), "observed": index < count}
            for index, ((name, offset), value) in enumerate(zip(REGISTERS, values))}


def decode_falcon(node, prerequisite_passed):
    if type(prerequisite_passed) is not bool:
        raise ValueError("Falcon prerequisite result is not a boolean")
    node = dict(node)
    # OSNumber(32) may appear as a signed plist integer, including error codes.
    for suffix in U32_FIELDS:
        key = "Falcon" + suffix
        value = node.get(key)
        if type(value) is not int or not -(1 << 31) <= value < 1 << 32:
            raise ValueError("Invalid Falcon 32-bit field: " + key)
        node[key] = value & 0xffffffff
    for suffix in BOOL_FIELDS:
        if type(node.get("Falcon" + suffix)) is not bool:
            raise ValueError("Invalid Falcon boolean field: " + suffix)
    end = node.get("FalconEnd")
    if type(end) is not int or not 0 <= end < 1 << 64:
        raise ValueError("Invalid Falcon DMA end offset")
    if not isinstance(node.get("FalconStatus"), str) or not isinstance(node.get("FalconMapperMode"), str):
        raise ValueError("Missing Falcon status/mapper mode")
    if "FirmwareExecuted" in node and node["FirmwareExecuted"] is not False:
        raise ValueError("Unexpected firmware execution in Falcon DMA probe")

    segments = list(struct.iter_unpack("<QQ", _blob(node, "Segments", 64)))
    initial = struct.unpack("<14I", _blob(node, "Initial", 56))
    final = struct.unpack("<14I", _blob(node, "Final", 56))
    environment = struct.unpack("<3I", _blob(node, "Environment", 12))
    rows = list(struct.iter_unpack("<71I", _blob(node, "Phases", 568)))
    for suffix, limit in (("Count", 4), ("InitialReads", 14), ("FinalReads", 14),
                          ("ResetCount", 2), ("ResetPolls", 630), ("DrainPolls", 400)):
        if node["Falcon" + suffix] > limit:
            raise ValueError("Falcon count exceeds bound: " + suffix)
    for suffix in ("CommandBefore", "CommandEnabled", "CommandAfter", "DeviceStatusBefore", "DeviceStatusAfter"):
        if node["Falcon" + suffix] > 0xffff:
            raise ValueError("Invalid Falcon PCI field: " + suffix)
    if node["FalconMismatchPhase"] not in (0, 1, 0xffffffff):
        raise ValueError("Invalid Falcon mismatch phase")
    if node["FalconMismatchWord"] not in tuple(range(64)) + (0xffffffff,):
        raise ValueError("Invalid Falcon mismatch word")

    phases = []
    phase_ok = True
    for phase, row in enumerate(rows):
        pio_matched, submitted, before, after, polls, reads, matched = row[:7]
        data = row[7:]
        if (pio_matched > 64 or submitted not in (0, 1) or polls > 600
                or reads > 64 or matched > reads or (reads and not submitted)):
            raise ValueError("Invalid Falcon phase counts")
        data_matches = [data[i] == pattern(i, phase) for i in range(reads)]
        verified = (pio_matched == reads == matched == 64 and submitted == 1
                    and readable(before) and readable(after)
                    and before & 3 == after & 3 == 2 and 3 <= polls <= 600
                    and all(data_matches))
        phase_ok = phase_ok and verified
        phases.append({"phase": phase, "pio_matched": pio_matched, "submitted": bool(submitted),
                       "command_before": before, "command_after": after, "polls": polls,
                       "reads": reads, "matched": matched, "data": list(data),
                       "observed_pattern_matches": sum(data_matches), "verified": verified})

    segment_ok = (node["FalconCount"] == 4 and end == 16384
                  and all(0 < address <= (1 << 40) - 4096 and address % 4096 == 0 and length == 4096
                          for address, length in segments)
                  and len({address for address, _ in segments}) == 4)
    # Pre-reset DMA/FBIF registers can be privilege-protected. The five
    # registers used by the protocol's initial safety gate must be readable.
    initial_ok = (node["FalconInitialReads"] == 14 and all(readable(initial[i]) for i in (0, 1, 3, 6, 7))
                  and not initial[0] & 1 and not initial[7] & 0x80
                  and not (initial[3] & 2 and not initial[3] & 0x10))
    final_ok = (node["FalconFinalReads"] == 14 and all(map(readable, final))
                and not final[0] & 1 and not final[1] & 0x1000
                and not final[3] & 2 and not final[7] & 0x80
                and final[5] & 3 == 2 and final[4] & 1 == 1
                and not final[9] & 0x80 and final[10:] == (0, 0, 0, 0))
    pci_ok = (node["FalconCommandBefore"] == node["FalconCommandAfter"] == 0
              and node["FalconCommandEnabled"] == 6
              and all(node["FalconDeviceStatus" + suffix] != 0xffff
                      and not node["FalconDeviceStatus" + suffix] & 0x20 for suffix in ("Before", "After")))
    environment_ok = (all(map(readable, environment)) and environment[0] == 0
                      and environment[1] & 1 == 1 and environment[2] & 0xff == 0xff)
    errors = {"Falcon" + suffix: node["Falcon" + suffix] for suffix in RETURN_FIELDS}
    flags_ok = (all(node["Falcon" + suffix] for suffix in BOOL_FIELDS
                    if suffix not in ("ResourcesRetained", "Passed"))
                and not node["FalconResourcesRetained"])
    checks = {"prerequisite_passed": prerequisite_passed, "segments_valid": segment_ok,
              "initial_registers_valid": bool(initial_ok), "final_registers_safe": bool(final_ok),
              "environment_ready": bool(environment_ok), "pci_state_valid": bool(pci_ok),
              "two_phases_verified": phase_ok, "lifecycle_flags_valid": flags_ok,
              "os_returns_success": all(value == 0 for value in errors.values()),
              "mapper_valid": node["FalconMapperMode"] in ("device-mapper", "system-mapper", "system-no-mapper"),
              "two_resets": node["FalconResetCount"] == 2 and 4 <= node["FalconResetPolls"] <= 630,
              "drain_observed": 2 <= node["FalconDrainPolls"] <= 400,
              "no_mismatch": node["FalconMismatchPhase"] == node["FalconMismatchWord"] == 0xffffffff,
              "status_verified": node["FalconStatus"] == "Falcon-DMA-host-to-DMEM-verified"}
    consistent = all(checks.values())
    if node["FalconPassed"] and not consistent:
        failed = ", ".join(key for key, value in checks.items() if not value)
        raise ValueError("Falcon DMA success flag disagrees with raw evidence: " + failed)

    return {
        "passed": node["FalconPassed"] and consistent, "status": node["FalconStatus"],
        "method": "256-byte host RAM to Falcon DMEM DMA, followed by PIO comparison, in two phases",
        "dma_engine_tested": any(row[1] for row in rows),
        "gpu_transfer_verified": node["FalconPassed"] and consistent,
        "firmware_executed": False, "compute_tested": False,
        "prepared": node["FalconPrepared"], "mapper_mode": node["FalconMapperMode"],
        "segments": [{"address": hex(a), "length": length} for a, length in segments],
        "segment_count": node["FalconCount"], "end_offset": end,
        "command_before": node["FalconCommandBefore"], "command_enabled": node["FalconCommandEnabled"],
        "command_after": node["FalconCommandAfter"],
        "device_status_before": node["FalconDeviceStatusBefore"], "device_status_after": node["FalconDeviceStatusAfter"],
        "memory_attempted": node["FalconMemoryAttempted"], "master_attempted": node["FalconMasterAttempted"],
        "targets_attempted": node["FalconTargetsAttempted"],
        "reset_attempted": node["FalconResetAttempted"], "reset_count": node["FalconResetCount"],
        "reset_polls": node["FalconResetPolls"], "drain_polls": node["FalconDrainPolls"],
        "initial_reads": node["FalconInitialReads"], "final_reads": node["FalconFinalReads"],
        "initial_registers": _registers(initial, node["FalconInitialReads"]),
        "final_registers": _registers(final, node["FalconFinalReads"]),
        "environment": dict(zip(("wpr2_hi", "scratch_protection", "scratch05"), environment)),
        "phases": phases, "ioreturns": errors, "evidence_checks": checks,
        "quiescent": node["FalconQuiescent"], "targets_cleared": node["FalconTargetsCleared"],
        "cleanup_verified": node["FalconCleanupVerified"], "resources_retained": node["FalconResourcesRetained"],
        "addresses_still_valid": None if node["FalconResourcesRetained"] else False,
        "cpu_contents_intact": node["FalconCpuIntact"],
        "mismatch": {"phase": node["FalconMismatchPhase"], "word": node["FalconMismatchWord"],
                     "value": node["FalconMismatchValue"], "value_hex": hex(node["FalconMismatchValue"])},
    }
