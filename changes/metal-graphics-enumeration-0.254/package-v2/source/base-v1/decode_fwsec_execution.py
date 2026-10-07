"""Check FWSEC execution evidence; never treat a halt or native flag as proof alone."""
import struct

from decode_falcon import readable
from decode_fwsec_preflight import REGISTERS as REGION_REGISTERS
from decode_fwsec_stage import blob, decode_fwsec_stage


VERSION, MODE = "0.9.0", "bounded-fwsec-execute"
STATUS = "fwsec-frts-execution-verified"
BOOT_REGISTERS = (
    ("boot0", 0), ("engine", 0x1103c0), ("hwcfg2", 0x1100f4),
    ("cpuctl", 0x110100), ("riscv_cpu", 0x111388), ("bcr", 0x111668),
    ("dmacmd", 0x110118), ("wpr_lo", 0x1fa824), ("wpr_hi", 0x1fa828),
    ("frts_scratch", 0x1438), ("mailbox0", 0x110040), ("mailbox1", 0x110044),
)
U32_FIELDS = ("InitialReads", "CpuBeforeStart", "LastCpu", "HaltPolls", "DelayCalls",
              "Mailbox0", "Mailbox1", "Scratch", "WprLo", "WprHi", "FailedRegister",
              "WriteAttempts", "VerifiedWrites")
BOOL_FIELDS = ("RegistersTouched", "StartAttempted", "StartWriteAccepted", "AliasUsed",
               "Halted", "RunningObserved", "SideEffectsMayRemain", "RequiresCallerQuiescence",
               "OutcomeRead", "WprTransitionObserved", "Passed", "AuthenticatedExecutionInferred")
REGION_BOOLS = ("FWRegionOwned", "FWRegionPersistent", "FWProviderHeld", "FWRegionRechecked", "FWExecutionPassed")
DISPLAY_OFFSETS = (0x610060, 0x610074, 0x612078, 0x612878, 0x613078, 0x613878)
FRTS_OFFSET, FRTS_SIZE, VRAM = 0x17fe00000, 0x100000, 0x180000000


def _rows(node, key, offsets):
    rows = list(struct.iter_unpack("<4I", blob(node, key, len(offsets) * 16)))
    if any(row[0] != offset or row[3] > 2 for row, offset in zip(rows, offsets)):
        raise ValueError("Unexpected register order or read bound: " + key)
    return rows


def _stable(row):
    return row[3] == 2 and row[1] == row[2] and readable(row[1]) and readable(row[2])


def _region_evidence(node):
    rows = _rows(node, "FWRegionRegisters", [offset for _, offset in REGION_REGISTERS])
    values = [row[1] for row in rows]
    baseline = [0, 1, 0x1ffffe00, 0, 0x10, 0x10, 0, 0x80420100, 1, 0]
    layout_ok = all(map(_stable, rows)) and values == baseline
    display = _rows(node, "FWDisplayRegisters", DISPLAY_OFFSETS)
    display_header_ok = all(map(_stable, display[:2]))
    mask, count = display[0][1] & 0xff, display[1][1] & 0xf
    display_ok = display_header_ok and 1 <= count <= 4 and not mask & ~((1 << count) - 1)
    for index, row in enumerate(display[2:]):
        eligible = index < count and bool(mask & (1 << index))
        display_ok = display_ok and (_stable(row) and row[1] & 0x300 == 0
                                     if eligible else row[1:] == (0, 0, 0))
    return {
        "fresh_profile_verified": bool(layout_ok), "display_inactive_verified": bool(display_ok),
        "head_mask": mask, "head_count": count,
        "registers": {name: {"offset": hex(row[0]), "first": row[1], "second": row[2], "reads": row[3]}
                      for (name, _), row in zip(REGION_REGISTERS, rows)},
        "display_registers": [{"offset": hex(row[0]), "first": row[1], "second": row[2], "reads": row[3]}
                              for row in display],
    }


def decode_fwsec_execution(node, prerequisite_passed):
    if type(prerequisite_passed) is not bool:
        raise ValueError("FWSEC execution prerequisite is not boolean")
    if node.get("ProbeVersion") != VERSION or node.get("Mode") != MODE:
        raise ValueError("Unexpected FWSEC execution mode/version")
    node = dict(node)
    for suffix in U32_FIELDS:
        key = "FWBoot" + suffix
        value = node.get(key)
        if type(value) is not int or not -(1 << 31) <= value < 1 << 32:
            raise ValueError("Invalid FWSEC boot u32: " + key)
        node[key] = value & 0xffffffff
    for key in tuple("FWBoot" + name for name in BOOL_FIELDS) + REGION_BOOLS + ("FirmwareExecuted", "DMATransferExecuted"):
        if type(node.get(key)) is not bool:
            raise ValueError("Invalid FWSEC execution boolean: " + key)
    for key in ("FWRegionOffset", "FWRegionSize", "FWRegionVram"):
        if type(node.get(key)) is not int or not 0 <= node[key] < 1 << 64:
            raise ValueError("Invalid FWSEC region value: " + key)
    if not isinstance(node.get("FWBootStatus"), str):
        raise ValueError("Missing FWSEC boot status")
    if node.get("MMIOReadOnly") is not False or node.get("IdentificationMMIOReadOnly") is not True:
        raise ValueError("Execution and identification MMIO contexts disagree")
    bounds = {"InitialReads": 12, "HaltPolls": 20000, "DelayCalls": 20000,
              "WriteAttempts": 8, "VerifiedWrites": 7, "FailedRegister": 19}
    if any(node["FWBoot" + key] > limit for key, limit in bounds.items()):
        raise ValueError("FWSEC execution counter exceeds the protocol bound")
    initial = struct.unpack("<12I", blob(node, "FWBootInitial", 48))
    stage = decode_fwsec_stage(node, prerequisite_passed, execution_context=True)
    region = _region_evidence(node)
    raw_initial_ok = (node["FWBootInitialReads"] == 12 and all(map(readable, initial))
                      and initial[0] == 0xb76000a1 and not initial[1] & 1
                      and not initial[2] & 0x1000 and initial[3] & 0x12 == 0x10
                      and not initial[4] & 0x80 and initial[5] & 0x11 == 1
                      and initial[6] & 3 == 2 and initial[8] >> 4 == 0)
    cpu_before = node["FWBootCpuBeforeStart"]
    last_cpu = node["FWBootLastCpu"]
    start_ok = (readable(cpu_before) and cpu_before & 0x12 == 0x10
                and node["FWBootAliasUsed"] == bool(cpu_before & 0x40))
    counts_ok = (node["FWBootWriteAttempts"] == 8 and node["FWBootVerifiedWrites"] == 7
                 and node["FWBootFailedRegister"] == 19
                 and 1 <= node["FWBootHaltPolls"] == node["FWBootDelayCalls"] <= 20000
                 and node["FWBootRunningObserved"] == (node["FWBootHaltPolls"] > 1))
    outcomes = [node["FWBoot" + name] for name in ("Mailbox0", "Mailbox1", "Scratch", "WprLo", "WprHi")]
    outcome_ok = (all(map(readable, outcomes)) and node["FWBootMailbox0"] == 0
                  and node["FWBootScratch"] >> 16 == 0
                  and node["FWBootWprHi"] >> 4 != 0 and node["FWBootWprLo"] >> 4 == FRTS_OFFSET >> 12)
    halt_ok = readable(last_cpu) and bool(last_cpu & 0x10)
    boot_flags_ok = all(node["FWBoot" + name] for name in BOOL_FIELDS
                        if name not in ("AliasUsed", "RunningObserved", "Passed"))
    region_geometry_ok = (node["FWRegionOffset"] == FRTS_OFFSET and node["FWRegionSize"] == FRTS_SIZE
                          and node["FWRegionVram"] == VRAM)
    held_ok = all(node[name] for name in ("FWRegionOwned", "FWRegionPersistent", "FWProviderHeld", "FWRegionRechecked"))
    display_status_ok = "FWDisplayStatus" not in node or node["FWDisplayStatus"] == "fwsec-display-measured"
    recheck_count_ok = ("FWRecheckPolls" not in node or
                        (type(node["FWRecheckPolls"]) is int and 1 <= node["FWRecheckPolls"] <= 200))
    checks = {"prerequisite_passed": prerequisite_passed, "staging_verified": stage["staging_verified"],
              "fresh_region_profile": region["fresh_profile_verified"], "inactive_display_heads": region["display_inactive_verified"],
              "region_geometry_valid": region_geometry_ok, "region_provider_retained": held_ok,
              "display_status_valid": display_status_ok, "fresh_recheck_count_valid": recheck_count_ok,
              "boot_initial_registers_valid": bool(raw_initial_ok), "start_cpu_and_alias_valid": bool(start_ok),
              "bounded_write_and_poll_counts": bool(counts_ok), "halt_observed": bool(halt_ok),
              "mailbox_frts_wpr_outcome_valid": bool(outcome_ok), "boot_lifecycle_flags_valid": boot_flags_ok,
              "boot_status_valid": node["FWBootStatus"] == STATUS}
    boot_verified = node["FWBootPassed"] and all(checks.values())
    diagnostics = []
    if node["FWBootPassed"] and not boot_verified:
        raise ValueError("FWSEC boot success contradicts evidence: " + ", ".join(k for k, v in checks.items() if not v))
    if node["FirmwareExecuted"] != boot_verified:
        raise ValueError("FirmwareExecuted differs from verified FWSEC execution")
    dma_submitted = bool(stage["imem_submitted"] or stage["dmem_submitted"])
    if node["DMATransferExecuted"] != dma_submitted:
        raise ValueError("DMA execution flag contradicts staging submissions")
    touched = node["FWBootRegistersTouched"] or node["FWBootStartAttempted"]
    if touched and not node["FWRegionRechecked"]:
        diagnostics.append("firmware register access lacks a successful fresh region recheck")
    if node["FWBootStartAttempted"]:
        if not all(node[k] for k in ("FWRegionOwned", "FWRegionPersistent", "FWProviderHeld")):
            diagnostics.append("CPU start attempt lacks persistent region/provider retention")
        if not boot_verified and (not stage["resources_retained"] or stage["cleanup_verified"]):
            diagnostics.append("unverified CPU start did not retain host DMA resources")
    if node["FWBootStartWriteAccepted"] and not node["FWBootStartAttempted"]:
        diagnostics.append("accepted CPU start without an attempted write")
    if node["FWBootAuthenticatedExecutionInferred"] != boot_verified:
        diagnostics.append("authentication inference lacks verified execution")
    if "FWRetainedUntilPlatformReset" in node:
        expected_retained = node["FWProviderHeld"] or stage["resources_retained"]
        if node["FWRetainedUntilPlatformReset"] is not expected_retained:
            diagnostics.append("module retention flag contradicts provider/host retention")
    overall = bool(boot_verified and stage["passed"] and held_ok and not diagnostics)
    if node["FWExecutionPassed"] != overall:
        raise ValueError("Overall FWSEC execution flag contradicts boot/cleanup/retention evidence")
    return {
        "passed": overall, "status": node["FWBootStatus"], "boot_verified": bool(boot_verified),
        "firmware_execution_tested": node["FWBootStartAttempted"], "firmware_executed": bool(boot_verified),
        "authenticated_execution_inferred": bool(boot_verified), "direct_signature_status_verified": False,
        "compute_tested": False, "nvidia_metal_supported": False,
        "staging": stage, "region": dict(region, owned=node["FWRegionOwned"], persistent=node["FWRegionPersistent"],
                                          rechecked=node["FWRegionRechecked"], provider_held=node["FWProviderHeld"],
                                          ownership_scope="software exclusion ledger under exclusive provider open; not a hardware lock",
                                          offset=hex(node["FWRegionOffset"]), size=node["FWRegionSize"], vram_bytes=node["FWRegionVram"]),
        "boot_initial_registers": {name: {"offset": hex(offset), "value": value,
                                          "observed": i < node["FWBootInitialReads"]}
                                   for i, ((name, offset), value) in enumerate(zip(BOOT_REGISTERS, initial))},
        "boot_counters": {name: node["FWBoot" + name] for name in U32_FIELDS},
        "boot_flags": {name: node["FWBoot" + name] for name in BOOL_FIELDS},
        "wpr_outcome": {"lo_raw": node["FWBootWprLo"], "hi_raw": node["FWBootWprHi"],
                        "lo_address": (node["FWBootWprLo"] >> 4) << 12,
                        "hi_address": (node["FWBootWprHi"] >> 4) << 12},
        "side_effects_may_remain": node["FWBootSideEffectsMayRemain"],
        "caller_quiescence_required": node["FWBootRequiresCallerQuiescence"],
        "host_dma_resources_retained": stage["resources_retained"],
        "host_dma_cleanup_verified": stage["passed"],
        "evidence_checks": checks, "diagnostics": diagnostics,
    }
