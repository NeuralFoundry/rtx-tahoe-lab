"""Decode the read-only FWSEC layout capture, independently of native flags."""
import struct


REGISTERS = (
    ("display_fuse", 0x820c04), ("vga_workspace", 0x625f04),
    ("wpr_lo", 0x1fa824), ("wpr_hi", 0x1fa828),
    ("cpuctl", 0x110100), ("riscv_cpu", 0x111388),
    ("engine", 0x1103c0), ("hwcfg", 0x110108),
    ("bcr", 0x111668), ("frts_scratch", 0x1438),
)
BOOL_FIELDS = ("Complete", "Passed", "DisplaySupported", "WorkspaceValid",
               "RequiresRelocation", "LayoutValid", "EngineIdle", "WprClear")
NUMBER_FIELDS = ("VramBytes", "WorkspaceAddress", "WorkspaceBoundary", "FrtsOffset",
                 "FrtsEnd", "WprLo", "WprHi")
EFFECTS = {"PreflightRegionOwned": False, "PreflightExecutionReady": False,
           "FirmwareExecuted": False, "DMATransferExecuted": False, "MMIOReadOnly": True}
MIB = 1 << 20
ALIGN = 128 << 10
STATUS = "fwsec-preflight-measured"


def readable(value):
    return value != 0xffffffff and value >> 16 not in (0xbad0, 0xbadf)


def decode_fwsec_preflight(node, ready, vram_mib, *, execution_context=False):
    """Return failed captures diagnostically; never accept an unsupported pass."""
    if type(ready) is not bool:
        raise ValueError("FWSEC preflight prerequisite is not boolean")
    if execution_context and (node.get("ProbeVersion") != "0.9.0" or node.get("Mode") != "bounded-fwsec-execute"):
        raise ValueError("Preflight execution context requires the explicit 0.9 execution mode")
    effects = dict(EFFECTS)
    if execution_context:
        for key in ("FirmwareExecuted", "DMATransferExecuted"):
            if type(node.get(key)) is not bool:
                raise ValueError("Invalid execution-mode effect flag: " + key)
            del effects[key]
        effects["MMIOReadOnly"] = False
        effects["IdentificationMMIOReadOnly"] = True
    for key, expected in effects.items():
        if node.get(key) is not expected:
            raise ValueError("Read-only preflight effect flag invalid: " + key)
    errors = []
    for key in BOOL_FIELDS:
        if type(node.get("Preflight" + key)) is not bool:
            errors.append("invalid boolean: Preflight" + key)
    for key in NUMBER_FIELDS:
        value = node.get("Preflight" + key)
        if type(value) is not int or not 0 <= value < 1 << 64:
            errors.append("invalid unsigned value: Preflight" + key)
    status = node.get("PreflightStatus")
    if not isinstance(status, str):
        errors.append("invalid preflight status")
    data = node.get("PreflightRegisters")
    rows = []
    if not isinstance(data, bytes) or len(data) != len(REGISTERS) * 16:
        errors.append("invalid register evidence size")
    else:
        rows = list(struct.iter_unpack("<4I", data))
    registers = {}
    valid = []
    for index, (name, expected_offset) in enumerate(REGISTERS):
        if index >= len(rows):
            valid.append(False)
            continue
        offset, first, second, reads = rows[index]
        stable = reads == 2 and first == second and readable(first) and readable(second)
        valid.append(stable and offset == expected_offset)
        registers[name] = {"offset": hex(offset), "first": first, "second": second,
                           "reads": reads, "stable_readable": stable}
        if offset != expected_offset:
            errors.append("register offset differs: " + name)
        if reads > 2:
            errors.append("register read count exceeds bound: " + name)
    display_supported = bool(rows and valid[0] and not rows[0][1] & 1)
    skipped_valid = bool(len(rows) == 10 and rows[1][1:] == (0, 0, 0))
    complete = bool(len(rows) == 10 and valid[0] and all(valid[2:])
                    and (valid[1] if display_supported else skipped_valid))
    if not display_supported and len(rows) == 10 and valid[0] and not skipped_valid:
        errors.append("unsupported display must skip workspace reads")
    layout = {"vram_bytes": vram_mib * MIB if type(vram_mib) is int and vram_mib >= 0 else None,
              "workspace_valid": False, "workspace_address": 0, "workspace_boundary": 0,
              "requires_relocation": False, "frts_end": 0, "frts_offset": 0,
              "layout_valid": False}
    engine_idle = wpr_clear = False
    wpr_lo = wpr_hi = 0
    if complete:
        wpr_lo, wpr_hi = (rows[2][1] >> 4) << 12, (rows[3][1] >> 4) << 12
        wpr_clear = rows[3][1] >> 4 == 0
        engine_idle = ((rows[4][1] & 0x12) == 0x10 and not rows[5][1] & 0x80
                       and not rows[6][1] & 1 and not rows[8][1] & 0x10)
        workspace_valid = display_supported and bool(rows[1][1] & 8)
        address = (rows[1][1] >> 8) << 16 if workspace_valid else 0
        layout.update(workspace_valid=workspace_valid, workspace_address=address)
        if layout["vram_bytes"] == 6144 * MIB:
            boundary = layout["vram_bytes"] - MIB
            layout["workspace_boundary"] = boundary
            if not workspace_valid or address < layout["vram_bytes"]:
                relocation = workspace_valid and address < boundary
                if workspace_valid:
                    boundary = layout["vram_bytes"] - ALIGN if relocation else address
                end = boundary & ~(ALIGN - 1)
                layout.update(workspace_boundary=boundary, requires_relocation=relocation,
                              frts_end=end, frts_offset=end - MIB,
                              layout_valid=end >= MIB)
    expected = {"Complete": complete, "DisplaySupported": display_supported,
                "WorkspaceValid": layout["workspace_valid"], "WorkspaceAddress": layout["workspace_address"],
                "WorkspaceBoundary": layout["workspace_boundary"], "RequiresRelocation": layout["requires_relocation"],
                "FrtsOffset": layout["frts_offset"], "FrtsEnd": layout["frts_end"],
                "LayoutValid": layout["layout_valid"], "EngineIdle": engine_idle,
                "WprClear": wpr_clear, "WprLo": wpr_lo, "WprHi": wpr_hi,
                "VramBytes": layout["vram_bytes"]}
    for suffix, value in expected.items():
        if node.get("Preflight" + suffix) != value:
            errors.append("raw evidence contradicts Preflight" + suffix)
    expected_pass = complete and layout["layout_valid"] and ready
    if node.get("PreflightPassed") is not expected_pass:
        errors.append("raw evidence or prerequisite contradicts PreflightPassed")
    if complete and layout["layout_valid"] and status != STATUS:
        errors.append("complete valid layout has unexpected status")
    if status == STATUS and not (complete and layout["layout_valid"]):
        errors.append("success status lacks complete valid layout")
    if node.get("PreflightPassed") is True and errors:
        raise ValueError("Unsupported FWSEC preflight pass: " + "; ".join(errors))
    return {"status": status, "passed": expected_pass and not errors,
            "measurement_phase": "initial-before-execution" if execution_context else "read-only-capture",
            "complete": complete, "prerequisite_passed": ready,
            "display_supported": display_supported, "registers": registers,
            "layout_candidate": layout, "requires_relocation": layout["requires_relocation"],
            "engine_idle": engine_idle, "wpr_clear": wpr_clear,
            "wpr_lo": wpr_lo, "wpr_hi": wpr_hi, "diagnostics": errors,
            "mmio_read_only": True, "dma_transfer_tested": False,
            "firmware_execution_tested": False, "firmware_executed": False,
            "compute_tested": False, "frts_region_reserved": False,
            "region_owned": False, "execution_ready": False, "ready_to_boot": False}
