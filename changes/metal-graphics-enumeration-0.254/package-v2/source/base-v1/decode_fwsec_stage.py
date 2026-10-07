"""Validate the no-start FWSEC staging experiment from raw IORegistry evidence."""
import hashlib
import struct

from decode_falcon import BOOL_FIELDS, REGISTERS, _registers, readable
from extract_fwsec import extract


BOARD_ROM_SHA256 = "b7b3d5a2b1698a5a1f24303b0fa915ef8da944ff3ede888377c2d02df5048b91"
IMEM_BYTES, DMEM_BYTES, IMAGE_BYTES = 57600, 2048, 59648
IMEM_BLOCKS, DMEM_BLOCKS, BLOCKS, WORDS = 225, 8, 233, 512
STATUS = "FWSEC-staged-DMEM-verified-not-executed"
COMMON_U32 = (
    "Count", "CommandBefore", "CommandEnabled", "CommandAfter", "DeviceStatusBefore", "DeviceStatusAfter",
    "InitialReads", "FinalReads", "ResetCount", "ResetPolls", "DrainPolls", "MismatchPhase", "MismatchWord",
    "MismatchValue", "LastIOReturn", "ClearIOReturn", "CompleteIOReturn", "MemoryCompleteIOReturn",
)
STAGE_U32 = (
    "ImageSize", "SignatureIndex", "Hwcfg", "CanaryMatched", "PublishCount", "DmaPolls", "ImemSubmitted",
    "ImemCompleted", "DmemSubmitted", "DmemCompleted", "DmemReads", "DmemMatched", "MismatchWord",
    "MismatchValue", "PublishIOReturn",
)
RETURN_FIELDS = tuple("Falcon" + k for k in
                      ("LastIOReturn", "ClearIOReturn", "CompleteIOReturn", "MemoryCompleteIOReturn")) + ("FWSECPublishIOReturn",)


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def blob(node, key, size):
    value = node.get(key)
    if not isinstance(value, bytes) or len(value) != size:
        raise ValueError("Invalid FWSEC evidence size: " + key)
    return value


def expected_image(node):
    """Rebuild three permitted DMEM patches from this snapshot's own pinned ROM."""
    rom = blob(node, "VBIOSShadow", 1 << 20)
    if digest(rom) != BOARD_ROM_SHA256:
        raise ValueError("FWSEC source ROM differs from the pinned board")
    expected_fuse = {"FuseOffset": 0x8241e0, "FuseFirst": 3, "FuseSecond": 3, "FuseReads": 2,
                     "FuseSignatureMask": 7, "FuseSignatureCount": 3, "FuseSignatureIndex": 2}
    for key, value in expected_fuse.items():
        if type(node.get(key)) is not int or node[key] != value:
            raise ValueError("FWSEC source fuse identity differs: " + key)
    if node.get("FusePassed") is not True:
        raise ValueError("FWSEC source fuse measurement did not pass")
    report, assets = extract(rom, 6144)
    fw = report["fwsec"]
    geometry = {"version": 3, "target_id": 7, "descriptor_size": 1196, "stored_size": IMAGE_BYTES,
                "imem_size": IMEM_BYTES, "dmem_size": DMEM_BYTES, "imem_base": 0, "imem_virtual_base": 0,
                "dmem_base": 0, "pkc_data_offset": 1444, "interface_offset": 28, "dmem_mapper_offset": 1376,
                "command_input_offset": 1984, "command_input_size": 64, "engine_id_mask": "0x400",
                "ucode_id": 9, "signature_count": 3, "signature_versions_mask": "0x7"}
    if any(fw[k] != value for k, value in geometry.items()):
        raise ValueError("FWSEC source descriptor geometry differs from the board")
    original = assets["fwsec-image-unmodified.bin"]
    if len(original) != IMAGE_BYTES:
        raise ValueError("Unexpected FWSEC source image size")
    descriptor = assets["fwsec-descriptor.bin"]
    signature = descriptor[44 + 2 * 384:44 + 3 * 384]
    command = struct.pack("<IIQIIIIIII", 1, 24, 0, 0, 2, 1, 20, 0x17fe00, 0x100, 2)
    prepared = bytearray(original)
    prepared[59044:59428] = signature
    struct.pack_into("<I", prepared, 59020, 0x15)
    prepared[59584:59628] = command
    return original, bytes(prepared)


def decode_fwsec_stage(node, prerequisite_passed, *, execution_context=False):
    if type(prerequisite_passed) is not bool:
        raise ValueError("FWSEC prerequisite result is not a boolean")
    node = dict(node)
    if execution_context and (node.get("ProbeVersion") != "0.9.0" or node.get("Mode") != "bounded-fwsec-execute"):
        raise ValueError("Staging execution context requires the explicit 0.9 execution mode")
    for prefix, fields in (("Falcon", COMMON_U32), ("FWSEC", STAGE_U32)):
        for suffix in fields:
            key = prefix + suffix
            value = node.get(key)
            if type(value) is not int or not -(1 << 31) <= value < 1 << 32:
                raise ValueError("Invalid FWSEC 32-bit field: " + key)
            node[key] = value & 0xffffffff
    for key in tuple("Falcon" + suffix for suffix in BOOL_FIELDS) + ("FWSECBoardMatched",):
        if type(node.get(key)) is not bool:
            raise ValueError("Invalid FWSEC boolean field: " + key)
    if (type(node.get("FirmwareExecuted")) is not bool or
            (not execution_context and node.get("FirmwareExecuted") is not False)):
        raise ValueError("Unexpected firmware execution in no-start FWSEC stage")
    end = node.get("FalconEnd")
    if type(end) is not int or not 0 <= end < 1 << 64:
        raise ValueError("Invalid FWSEC DMA end offset")
    for key in ("FalconStatus", "FalconMapperMode", "FWSECImageSHA256", "FWSECOriginalImageSHA256", "FWSECRomSHA256"):
        if not isinstance(node.get(key), str):
            raise ValueError("Missing FWSEC string field: " + key)
    for key in ("FWSECImageSHA256", "FWSECOriginalImageSHA256", "FWSECRomSHA256"):
        if len(node[key]) != 64 or any(c not in "0123456789abcdef" for c in node[key]):
            raise ValueError("Invalid FWSEC SHA256 field: " + key)
    segments = list(struct.iter_unpack("<QQ", blob(node, "FalconSegments", 64)))
    initial = struct.unpack("<14I", blob(node, "FalconInitial", 56))
    final = struct.unpack("<14I", blob(node, "FalconFinal", 56))
    environment = struct.unpack("<3I", blob(node, "FalconEnvironment", 12))
    completions = struct.unpack("<233I", blob(node, "FWSECCompletions", BLOCKS * 4))
    dmem = struct.unpack("<512I", blob(node, "FWSECDmem", DMEM_BYTES))
    bounds = {"FalconCount": 4, "FalconInitialReads": 14, "FalconFinalReads": 14,
              "FalconResetCount": 2, "FalconResetPolls": 630, "FalconDrainPolls": 400,
              "FWSECCanaryMatched": WORDS, "FWSECPublishCount": 4, "FWSECDmaPolls": BLOCKS * 600,
              "FWSECImemSubmitted": IMEM_BLOCKS, "FWSECImemCompleted": IMEM_BLOCKS,
              "FWSECDmemSubmitted": DMEM_BLOCKS, "FWSECDmemCompleted": DMEM_BLOCKS,
              "FWSECDmemReads": WORDS, "FWSECDmemMatched": WORDS}
    for key, limit in bounds.items():
        if node[key] > limit:
            raise ValueError("FWSEC count exceeds bound: " + key)
    for suffix in ("CommandBefore", "CommandEnabled", "CommandAfter", "DeviceStatusBefore", "DeviceStatusAfter"):
        if node["Falcon" + suffix] > 0xffff:
            raise ValueError("Invalid FWSEC PCI field: " + suffix)
    for key, limit in (("FalconMismatchPhase", 2), ("FalconMismatchWord", 64), ("FWSECMismatchWord", WORDS)):
        if not (node[key] < limit or node[key] == 0xffffffff):
            raise ValueError("Invalid FWSEC mismatch index: " + key)
    for kind in ("Imem", "Dmem"):
        if node["FWSEC" + kind + "Completed"] > node["FWSEC" + kind + "Submitted"]:
            raise ValueError("FWSEC completed count exceeds submitted count")
    if (node["FWSECDmemMatched"] > node["FWSECDmemReads"]
            or (node["FWSECDmemSubmitted"] and node["FWSECImemCompleted"] != IMEM_BLOCKS)
            or (node["FWSECDmemReads"] and node["FWSECDmemCompleted"] != DMEM_BLOCKS)):
        raise ValueError("FWSEC observation counts contradict transfer order")

    source_error = None
    try:
        original, prepared = expected_image(node)
    except (ValueError, KeyError, struct.error) as error:
        original = prepared = None
        source_error = str(error)
    image_ok = (prepared is not None and node["FWSECBoardMatched"] and node["FWSECImageSize"] == IMAGE_BYTES
                and node["FWSECSignatureIndex"] == 2 and node["FWSECRomSHA256"] == BOARD_ROM_SHA256
                and node["FWSECImageSHA256"] == digest(prepared)
                and node["FWSECOriginalImageSHA256"] == digest(original))
    expected_dmem = struct.unpack("<512I", prepared[IMEM_BYTES:]) if prepared is not None else None
    data_matches = [dmem[i] == expected_dmem[i] for i in range(node["FWSECDmemReads"])] if expected_dmem else []
    segment_ok = (node["FalconCount"] == 4 and end == 16384
                  and all(0 < a <= (1 << 40) - 4096 and a % 4096 == 0 and n == 4096 for a, n in segments)
                  and len({a for a, _ in segments}) == 4)
    initial_ok = (node["FalconInitialReads"] == len(REGISTERS)
                  and all(readable(initial[i]) for i in (0, 1, 3, 6, 7))
                  and not initial[0] & 1 and not initial[7] & 0x80
                  and not (initial[3] & 2 and not initial[3] & 0x10))
    final_ok = (node["FalconFinalReads"] == len(REGISTERS) and all(map(readable, final))
                and not final[0] & 1 and not final[1] & 0x1000 and not final[3] & 2 and not final[7] & 0x80
                and final[5] & 3 == 2 and final[4] & 1 == 1 and not final[9] & 0x80 and final[10:] == (0, 0, 0, 0))
    if execution_context and node.get("FWBootStartAttempted"):
        final_ok = final_ok and final[3] & 0x12 == 0x10 and final[6] & 0x11 == 1
    hwcfg = node["FWSECHwcfg"]
    capacity_ok = readable(hwcfg) and ((hwcfg & 0x1ff) << 8) >= IMEM_BYTES and ((hwcfg & 0x3fe00) >> 1) >= DMEM_BYTES
    pci_ok = (node["FalconCommandBefore"] == node["FalconCommandAfter"] == 0 and node["FalconCommandEnabled"] == 6
              and all(node["FalconDeviceStatus" + s] != 0xffff and not node["FalconDeviceStatus" + s] & 0x20
                      for s in ("Before", "After")))
    environment_ok = all(map(readable, environment)) and environment[0] == 0 and environment[1] & 1 == 1 and environment[2] & 255 == 255
    flags_ok = (all(node["Falcon" + s] for s in BOOL_FIELDS if s not in ("ResourcesRetained", "Passed"))
                and not node["FalconResourcesRetained"])
    errors = {key: node[key] for key in RETURN_FIELDS}
    transfer_ok = (node["FWSECImemSubmitted"] == node["FWSECImemCompleted"] == IMEM_BLOCKS
                   and node["FWSECDmemSubmitted"] == node["FWSECDmemCompleted"] == DMEM_BLOCKS
                   and all(readable(v) and v & 3 == 2 for v in completions)
                   and BLOCKS * 3 <= node["FWSECDmaPolls"] <= BLOCKS * 600)
    if execution_context:
        transfer_ok = transfer_ok and completions == (0x616,) * IMEM_BLOCKS + (0x602,) * DMEM_BLOCKS
    dmem_ok = (node["FWSECDmemReads"] == node["FWSECDmemMatched"] == len(data_matches) == WORDS and all(data_matches))
    expected_version, expected_mode = (("0.9.0", "bounded-fwsec-execute") if execution_context
                                       else ("0.7.0", "bounded-fwsec-stage"))
    expected_status = ("fwsec-host-cleanup-verified-vram-retained"
                       if execution_context and node.get("FWBootStartAttempted") else STATUS)
    checks = {"prerequisite_passed": prerequisite_passed,
              "probe_mode_valid": node.get("ProbeVersion") == expected_version and node.get("Mode") == expected_mode,
              "board_image_reconstructed": bool(image_ok), "segments_valid": segment_ok,
              "initial_registers_valid": bool(initial_ok), "final_registers_safe": bool(final_ok),
              "falcon_capacity_valid": bool(capacity_ok), "pci_state_valid": bool(pci_ok),
              "environment_ready": bool(environment_ok), "lifecycle_flags_valid": flags_ok,
              "os_returns_success": all(v == 0 for v in errors.values()),
              "mapper_valid": node["FalconMapperMode"] in ("device-mapper", "system-mapper", "system-no-mapper"),
              "two_resets": node["FalconResetCount"] == 2 and 4 <= node["FalconResetPolls"] <= 630,
              "drain_observed": 2 <= node["FalconDrainPolls"] <= 400,
              "canary_verified": node["FWSECCanaryMatched"] == WORDS,
              "four_windows_published": node["FWSECPublishCount"] == 4,
              "all_dma_completions_observed": bool(transfer_ok), "dmem_verified": bool(dmem_ok),
              "no_mismatch": node["FalconMismatchPhase"] == node["FalconMismatchWord"] == node["FWSECMismatchWord"] == 0xffffffff,
              "status_verified": node["FalconStatus"] == expected_status}
    staging_keys = ("prerequisite_passed", "probe_mode_valid", "board_image_reconstructed", "segments_valid",
                    "initial_registers_valid", "falcon_capacity_valid", "environment_ready", "mapper_valid",
                    "canary_verified", "four_windows_published", "all_dma_completions_observed", "dmem_verified", "no_mismatch")
    staging_verified = all(checks[key] for key in staging_keys) and node["FWSECPublishIOReturn"] == 0
    consistent = all(checks.values())
    if node["FalconPassed"] and not consistent:
        raise ValueError("FWSEC success flag disagrees with raw evidence: " + ", ".join(k for k, v in checks.items() if not v))
    passed = node["FalconPassed"] and consistent
    return {
        "passed": passed, "status": node["FalconStatus"],
        "staging_verified": bool(staging_verified), "host_cleanup_verified": passed,
        "method": ("233 host RAM to secure IMEM/DMEM DMA blocks; full DMEM PIO comparison; execution checked separately"
                   if execution_context else "233 host RAM to secure IMEM/DMEM DMA blocks; full DMEM PIO comparison; reset without CPU start"),
        "measurement_phase": "staging-before-execution" if execution_context else "stage-without-execution",
        "dma_engine_tested": bool(node["FWSECImemSubmitted"] or node["FWSECDmemSubmitted"]),
        "gpu_transfer_verified": bool(staging_verified) if execution_context else passed,
        "dmem_integrity_verified": bool(staging_verified) if execution_context else passed,
        "imem_integrity_verified": False, "hardware_signature_verification_tested": False,
        "signature_verified_by_hardware": False, "firmware_executed": False, "compute_tested": False,
        "ready_for_hardware_boot": False, "frts_region_reserved": False,
        "board_matched": node["FWSECBoardMatched"], "source_validation_error": source_error,
        "image_sha256": node["FWSECImageSHA256"], "original_image_sha256": node["FWSECOriginalImageSHA256"],
        "rom_sha256": node["FWSECRomSHA256"], "image_bytes": node["FWSECImageSize"], "signature_index": node["FWSECSignatureIndex"],
        "hwcfg": hwcfg, "canary_matched": node["FWSECCanaryMatched"], "publish_count": node["FWSECPublishCount"],
        "dma_polls": node["FWSECDmaPolls"], "imem_submitted": node["FWSECImemSubmitted"],
        "imem_completed": node["FWSECImemCompleted"], "dmem_submitted": node["FWSECDmemSubmitted"],
        "dmem_completed": node["FWSECDmemCompleted"], "dmem_reads": node["FWSECDmemReads"],
        "dmem_matched": node["FWSECDmemMatched"], "observed_dmem_matches": sum(data_matches),
        "completions": list(completions), "dmem": list(dmem),
        "prepared": node["FalconPrepared"], "mapper_mode": node["FalconMapperMode"],
        "segments": [{"address": hex(a), "length": n} for a, n in segments], "segment_count": node["FalconCount"], "end_offset": end,
        "command_before": node["FalconCommandBefore"], "command_enabled": node["FalconCommandEnabled"],
        "command_after": node["FalconCommandAfter"], "device_status_before": node["FalconDeviceStatusBefore"],
        "device_status_after": node["FalconDeviceStatusAfter"], "reset_count": node["FalconResetCount"],
        "reset_polls": node["FalconResetPolls"], "drain_polls": node["FalconDrainPolls"],
        "initial_registers": _registers(initial, node["FalconInitialReads"]),
        "final_registers": _registers(final, node["FalconFinalReads"]),
        "environment": dict(zip(("wpr2_hi", "scratch_protection", "scratch05"), environment)),
        "ioreturns": errors, "evidence_checks": checks, "quiescent": node["FalconQuiescent"],
        "targets_cleared": node["FalconTargetsCleared"], "cleanup_verified": node["FalconCleanupVerified"],
        "resources_retained": node["FalconResourcesRetained"],
        "addresses_still_valid": None if node["FalconResourcesRetained"] else False,
        "cpu_contents_intact": node["FalconCpuIntact"],
        "mismatch": {"word": node["FWSECMismatchWord"], "value": node["FWSECMismatchValue"], "value_hex": hex(node["FWSECMismatchValue"])},
    }
