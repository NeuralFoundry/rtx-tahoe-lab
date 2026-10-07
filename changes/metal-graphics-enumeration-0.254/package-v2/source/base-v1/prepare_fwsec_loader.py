"""Prepare this board's FWSEC payload offline; never reserve VRAM or execute firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import struct

from decode_probe import decode_snapshot
from extract_fwsec import extract, span, unpack
from select_fwsec_signature import BOARD_ROM_SHA256


FRTS_COMMAND = 0x15
SIGNATURE_BYTES = 384
# Opaque Reserved field observed in this board's descriptor. Its name does not
# imply a zero value; retain and pin the actual bytes without interpreting them.
BOARD_DESCRIPTOR_RESERVED = 0x9249
MAPPER_FIELDS = (
    "signature", "version", "size", "cmd_in_buffer_offset", "cmd_in_buffer_size",
    "cmd_out_buffer_offset", "cmd_out_buffer_size", "nvf_img_data_buffer_offset",
    "nvf_img_data_buffer_size", "printf_buffer_header", "ucode_build_timestamp",
    "ucode_signature", "init_cmd", "ucode_feature", "ucode_cmd_mask0",
    "ucode_cmd_mask1", "multi_target_table",
)
BOARD_GEOMETRY = {
    "stored_size": 59648, "imem_size": 57600, "dmem_size": 2048,
    "imem_base": 0, "imem_virtual_base": 0, "dmem_base": 0,
    "pkc_data_offset": 1444, "interface_offset": 28, "dmem_mapper_offset": 1376,
    "command_input_offset": 1984, "command_input_size": 64,
}


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def disjoint(regions):
    """Regions are (name, offset, size), all in one address space."""
    ordered = sorted(regions, key=lambda r: r[1])
    for left, right in zip(ordered, ordered[1:]):
        if left[1] + left[2] > right[1]:
            raise ValueError(f"Overlapping FWSEC regions: {left[0]} and {right[0]}")


def _header(report, original, prepared, descriptor):
    fw = report["fwsec"]
    constants = {
        "ImageSize": len(prepared), "ImemSize": fw["imem_size"], "DmemSize": fw["dmem_size"],
        "ImemBase": fw["imem_base"], "ImemVirtualBase": fw["imem_virtual_base"],
        "DmemBase": fw["dmem_base"], "PkcOffset": fw["pkc_data_offset"],
        "MapperOffset": fw["dmem_mapper_offset"], "CommandOffset": fw["command_input_offset"],
        "DescriptorOffset": fw["descriptor_offset"], "DescriptorSize": fw["descriptor_size"],
        "ImageRomOffset": fw["image_offset"], "SignatureIndex": report["signature_selected"],
        "FuseValue": report["fuse"]["raw_first"],
    }
    lines = ["// Generated offline by prepare_fwsec_loader.py. Do not edit.",
             "// Payload preparation is not firmware execution or hardware signature verification.",
             "#pragma once", "namespace FWSECPayload {"]
    lines += [f"constexpr unsigned {name} = {value}U;" for name, value in constants.items()]
    for name, value in (("ImageSHA256", digest(prepared)), ("OriginalImageSHA256", digest(original)),
                        ("RomSHA256", report["rom_sha256"])):
        lines.append(f'constexpr const char* {name} = "{value}";')
    lines += ["constexpr bool ReadyForHardwareBoot = false;", "constexpr bool FirmwareExecuted = false;"]
    for name, size, blob in (("Image", "ImageSize", prepared), ("OriginalImage", "ImageSize", original),
                             ("Descriptor", "DescriptorSize", descriptor)):
        lines.append(f"alignas(256) constexpr unsigned char {name}[{size}] = {{")
        lines.extend("  " + ", ".join(f"0x{x:02x}" for x in blob[i:i + 16]) + ","
                     for i in range(0, len(blob), 16))
        lines.append("};")
    lines += ["static_assert(ImageSize == ImemSize + DmemSize, \"FWSEC image geometry\");",
              "static_assert((ImageSize | ImemSize | DmemSize | ImemBase | ImemVirtualBase | DmemBase) % 256 == 0,",
              '              "FWSEC DMA alignment");', "} // namespace FWSECPayload", ""]
    return "\n".join(lines).encode("ascii")


def prepare(nodes, expected_rom_sha256=BOARD_ROM_SHA256):
    """Return (JSON report, filename->bytes assets) from a proven 0.6 snapshot.

    expected_rom_sha256 is injectable for synthetic tests. The CLI deliberately
    offers no hash override. This function has no hardware or filesystem effects.
    """
    if not isinstance(nodes, list) or any(not isinstance(n, dict) for n in nodes):
        raise ValueError("Expected a list of IORegistry snapshot dictionaries")
    decoded = decode_snapshot(nodes)
    if (decoded["probe_version"] != "0.6.0" or not decoded.get("falcon_dma", {}).get("passed")
            or not decoded.get("fwsec_fuse", {}).get("passed")
            or not decoded.get("rom", {}).get("passed")
            or not decoded.get("gpu_state", {}).get("passed")):
        raise ValueError("A completed 0.6 ROM/fuse/Falcon DMA measurement is required")
    if (decoded["power_state"] != "D0" or decoded["memory_decode_enabled"]
            or decoded["bus_master_enabled"] or decoded["compute_tested"]):
        raise ValueError("Unexpected measured PCI power/command state or compute claim")
    if (not isinstance(expected_rom_sha256, str) or len(expected_rom_sha256) != 64
            or any(c not in "0123456789abcdef" for c in expected_rom_sha256)
            or decoded["rom"]["sha256"] != expected_rom_sha256):
        raise ValueError("Fresh ROM does not match the pinned board ROM")
    node = next(n for n in nodes if n.get("ProbeVersion") == "0.6.0")
    rom = node["VBIOSShadow"]
    # Always re-extract the bytes that passed this snapshot's decoder. Never trust
    # an earlier manifest, extracted image, or selected-signature file.
    extracted, assets = extract(rom, decoded["gpu_state"]["reported_vram_mib"])
    fw = extracted["fwsec"]
    if (fw["version"], fw["target_id"], fw["engine_id_mask"], fw["ucode_id"],
            fw["signature_count"], fw["signature_versions_mask"]) != (3, 7, "0x400", 9, 3, "0x7"):
        raise ValueError("FWSEC descriptor identity does not match this GA106 board")
    descriptor = assets["fwsec-descriptor.bin"]
    vdesc = unpack(descriptor, 0, "I")[0]
    reserved = unpack(descriptor, 42, "H")[0]
    if vdesc != (len(descriptor) << 16) | 0x301 or reserved != BOARD_DESCRIPTOR_RESERVED:
        raise ValueError("Unexpected FWSEC descriptor flags or reserved bits")
    original = assets["fwsec-image-unmodified.bin"]
    for key in ("stored_size", "imem_size", "dmem_size", "imem_base", "imem_virtual_base", "dmem_base"):
        if fw[key] & 255:
            raise ValueError(f"FWSEC {key} is not 256-byte aligned")
    if len(original) != fw["stored_size"] or fw["imem_size"] + fw["dmem_size"] != len(original):
        raise ValueError("FWSEC image contains unexplained padding or overlapping load spans")
    hwcfg = decoded["falcon_dma"]["final_registers"]["hwcfg"]["value"]
    imem_capacity, dmem_capacity = (hwcfg & 0x1ff) << 8, (hwcfg & 0x3fe00) >> 1
    if (fw["imem_base"] + fw["imem_size"] > imem_capacity
            or fw["dmem_base"] + fw["dmem_size"] > dmem_capacity
            or fw["imem_virtual_base"] + fw["imem_size"] > 1 << 24):
        raise ValueError("FWSEC load does not fit measured Falcon memory/address limits")
    imem = span(original, 0, fw["imem_size"])
    dmem = span(original, fw["imem_size"], fw["dmem_size"])
    av, ah, ae, ac = unpack(dmem, fw["interface_offset"], "4B")
    if (av, ah, ae, ac) != (1, 4, 8, 2):
        raise ValueError("Unexpected board application-interface geometry")
    interface_size = ah + ae * ac
    span(dmem, fw["interface_offset"], interface_size)
    mapper = dict(zip(MAPPER_FIELDS, unpack(dmem, fw["dmem_mapper_offset"], "IHH14I")))
    if (mapper["signature"], mapper["version"], mapper["size"]) != (0x50414d44, 3, 64):
        raise ValueError("Unsupported DMAP mapper header")
    # The observed command masks are pinned, not interpreted as (1 << init_cmd).
    # Nouveau's FRTS patch uses 0x15 without deriving it from these fields.
    if (mapper["init_cmd"], mapper["ucode_feature"], mapper["ucode_cmd_mask0"],
            mapper["ucode_cmd_mask1"]) != (0, 4, 0x44000, 0):
        raise ValueError("DMAP initial command/features/masks differ from the board image")
    regions = [("application-interface", fw["interface_offset"], interface_size),
               ("dmem-mapper", fw["dmem_mapper_offset"], 64),
               ("pkc-signature", fw["pkc_data_offset"], SIGNATURE_BYTES),
               ("command-input-buffer", fw["command_input_offset"], fw["command_input_size"])]
    for name, offset, size in regions:
        if offset & 3 or size <= 0:
            raise ValueError(f"Invalid aligned FWSEC region: {name}")
        span(dmem, offset, size)
    disjoint(regions)
    if any(fw[key] != value for key, value in BOARD_GEOMETRY.items()):
        raise ValueError("FWSEC placements differ from the fixed board loader geometry")
    if decoded["gpu_state"]["reported_vram_mib"] != 6144:
        raise ValueError("Measured VRAM size differs from this 6 GiB board")
    index = decoded["fwsec_fuse"]["signature_index_candidate"]
    if type(index) is not int or not 0 <= index < fw["signature_count"]:
        raise ValueError("Invalid fuse-selected signature index")
    signature = span(descriptor, 44 + index * SIGNATURE_BYTES, SIGNATURE_BYTES)
    if signature != assets[f"fwsec-signature-{index}.bin"]:
        raise ValueError("Signature extraction disagrees with the fresh descriptor")
    command = assets["frts-command-candidate.bin"]
    frts = extracted["frts_candidate"]
    expected_command = struct.pack("<IIQIIIIIII", 1, 24, 0, 0, 2, 1, 20,
                                   frts["region_offset"] >> 12, 0x100, 2)
    if (len(command) != 44 or command != expected_command or frts["region_offset"] & 4095
            or frts["region_offset"] + frts["region_size"] > 6144 << 20):
        raise ValueError("Invalid bounded FRTS candidate command")
    patches = [("pkc-signature", fw["pkc_data_offset"], signature),
               ("mapper-init-command", fw["dmem_mapper_offset"] + 44, struct.pack("<I", FRTS_COMMAND)),
               ("frts-command-input", fw["command_input_offset"], command)]
    disjoint([(name, offset, len(blob)) for name, offset, blob in patches])
    prepared = bytearray(original)
    allowed = bytearray(len(original))
    patch_report = []
    for name, offset, blob in patches:
        image_offset = fw["imem_size"] + offset
        before = span(original, image_offset, len(blob))
        prepared[image_offset:image_offset + len(blob)] = blob
        allowed[image_offset:image_offset + len(blob)] = b"\1" * len(blob)
        patch_report.append({"name": name, "dmem_offset": offset, "image_offset": image_offset,
                             "size": len(blob), "before_sha256": digest(before), "after_sha256": digest(blob),
                             "changed_bytes": sum(a != b for a, b in zip(before, blob))})
    prepared = bytes(prepared)
    if (len(prepared) != len(original) or prepared[:fw["imem_size"]] != imem
            or any(a != b and not allowed[i] for i, (a, b) in enumerate(zip(original, prepared)))):
        raise ValueError("Prepared firmware changed bytes outside the three permitted DMEM ranges")
    report = {
        "format": "RTXProbe-board-FWSEC-package-v1", "status": "offline-package-prepared",
        "board": {"identity": decoded["identity"], "subsystem": decoded["subsystem"], "vram_mib": 6144},
        "source_probe_version": decoded["probe_version"], "rom_sha256": extracted["rom_sha256"],
        "board_hash_matches_production_pin": extracted["rom_sha256"] == BOARD_ROM_SHA256,
        "fwsec": fw, "mapper_before": mapper, "mapper_init_command_after": FRTS_COMMAND,
        "descriptor_header": {"vdesc": hex(vdesc), "flags_version": "0x0301",
                              "reserved_opaque": hex(reserved), "reserved_interpreted": False},
        "mapper_masks_interpreted_as_command_support": False,
        "fuse": decoded["fwsec_fuse"], "signature_selected": index,
        "signature_source_descriptor_offset": 44 + index * SIGNATURE_BYTES,
        "signature_sha256": digest(signature), "signature_verified_by_hardware": False,
        "falcon_dma_passed": True,
        "measured_falcon_capacity": {"imem_bytes": imem_capacity, "dmem_bytes": dmem_capacity},
        "load_regions": [
            {"memory": "IMEM", "source_offset": 0, "bytes": fw["imem_size"],
             "physical_base": fw["imem_base"], "virtual_base": fw["imem_virtual_base"], "secure": True},
            {"memory": "DMEM", "source_offset": fw["imem_size"], "bytes": fw["dmem_size"],
             "physical_base": fw["dmem_base"], "virtual_base": 0, "secure": False}],
        "allowed_patch_regions": patch_report, "allowed_patch_bytes": sum(len(p[2]) for p in patches),
        "changed_bytes": sum(a != b for a, b in zip(original, prepared)),
        "imem_unchanged": True, "bytes_outside_patch_regions_unchanged": True,
        "original_image_sha256": digest(original), "prepared_image_sha256": digest(prepared),
        "frts_candidate": dict(frts, reserved=False, allocation_verified=False),
        "firmware_executed": False, "ready_for_hardware_boot": False,
        "remaining_checks": [
            "revalidate board identity, fuse, descriptor and original image during the native load",
            "load full secure IMEM and DMEM through fresh OS-owned DMA mappings and verify cleanup",
            "validate and reserve FRTS VRAM region before any firmware execution",
            "implement bounded authenticated boot, status decoding and failure recovery"],
        "references": [
            "https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/nouveau/nvkm/subdev/gsp/fwsec.c",
            "vendor/tinygrad-reference/nv/ip.py", "vendor/tinygrad-reference/nv.py"],
    }
    assets = dict(assets)
    assets["fwsec-selected-signature.bin"] = signature
    assets["fwsec-image-prepared.bin"] = prepared
    assets["fwsec-payload.hpp"] = _header(report, original, prepared, descriptor)
    report["assets"] = {name: {"bytes": len(blob), "sha256": digest(blob)} for name, blob in assets.items()}
    return report, assets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        raw = args.snapshot.read_bytes()
        report, assets = prepare(plistlib.loads(raw))
        report["snapshot_sha256"] = digest(raw)
        # Do not leave a mixture of new and stale products in a prior package.
        if args.output.exists() and (not args.output.is_dir() or any(args.output.iterdir())):
            raise ValueError("Output must be a new or empty package directory")
        args.output.mkdir(parents=True, exist_ok=True)
        for name, blob in assets.items():
            (args.output / name).write_bytes(blob)
        # The manifest is the completion marker, written only after every asset.
        (args.output / "fwsec-loader-manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
    except (ValueError, KeyError, TypeError, IndexError, OSError, struct.error, plistlib.InvalidFileException) as error:
        parser.exit(2, f"FWSEC package preparation refused: {error}\n")


if __name__ == "__main__":
    main()
