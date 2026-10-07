"""Bounded GA10x VBIOS/FWSEC extraction; no GPU access or firmware execution."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROM_SIZE = 1 << 20


def span(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"VBIOS range outside captured bytes: {offset:#x}+{size:#x}")
    return data[offset:offset + size]


def unpack(data, offset, fmt):
    return struct.unpack("<" + fmt, span(data, offset, struct.calcsize("<" + fmt)))


def extract(data, vram_mib):
    if len(data) != ROM_SIZE or not 1024 <= vram_mib <= 65536:
        raise ValueError("Expected 1 MiB shadow and a plausible measured VRAM size")
    images, offset, base_size, bias = [], 0, None, None
    # NVIDIA extension follows the legacy image even when its PCI last bit is set.
    for _ in range(16):
        if unpack(data, offset, "H")[0] not in (0xaa55, 0x4e56, 0xbb77):
            raise ValueError("Invalid ROM image signature")
        pcir_relative = unpack(data, offset + 0x18, "H")[0]
        if pcir_relative < 0x1a:
            raise ValueError("Invalid PCI data structure pointer")
        pcir = offset + pcir_relative
        if span(data, pcir, 4) not in (b"PCIR", b"NPDS", b"RGIS"):
            raise ValueError("Invalid PCI data structure signature")
        vendor, device = unpack(data, pcir + 4, "HH")
        if not images and (vendor, device) != (0x10de, 0x2520):
            raise ValueError("Legacy ROM identity differs from the development GPU")
        length = unpack(data, pcir + 0x10, "H")[0] * 512
        if not length or pcir_relative + 0x18 > length:
            raise ValueError("Invalid ROM image extent")
        image = span(data, offset, length)
        code_type, last = unpack(data, pcir + 0x14, "BB")
        images.append({"offset": offset, "length": length, "code_type": code_type,
                       "vendor": hex(vendor), "device": hex(device), "last_bit": bool(last & 0x80),
                       "checksum_mod256": sum(image) & 255})
        if code_type == 0:
            if base_size is not None:
                raise ValueError("Ambiguous legacy VBIOS base")
            base_size = length
        if code_type == 0xe0:
            if base_size is None or offset < base_size:
                raise ValueError("Missing legacy base for NVIDIA extension")
            bias = offset - base_size
            break
        offset += length
    if bias is None:
        raise ValueError("NVIDIA VBIOS extension not found within bounded image walk")
    legacy = span(data, 0, base_size)
    marker = b"\xff\xb8BIT\x00"
    bit = legacy.find(marker)
    if bit < 0 or legacy.find(marker, bit + 1) >= 0:
        raise ValueError("Missing or ambiguous BIT header")
    ident, signature, bcd, header_size, token_size, token_count, checksum = unpack(data, bit, "HIH4B")
    if ident != 0xb8ff or signature != 0x00544942 or bcd != 0x100 or header_size != 12 or token_size not in (6, 8):
        raise ValueError("Unsupported BIT header layout")
    if sum(span(legacy, bit, header_size)) & 255:
        raise ValueError("BIT header checksum mismatch")
    span(legacy, bit + header_size, token_count * token_size)
    pointers = []
    for i in range(token_count):
        token = bit + header_size + i * token_size
        tag, version, size = unpack(data, token, "BBH")
        ptr = unpack(data, token + 4, "H" if token_size == 6 else "I")[0]
        if tag == 0x70 and version == 2:
            if size < 4:
                raise ValueError("Truncated Falcon BIT token")
            span(legacy, ptr, size)
            pointers.append(bias + unpack(data, ptr, "I")[0])
    if len(pointers) != 1:
        raise ValueError("Expected exactly one Falcon v2 token")
    table = pointers[0]
    version, header, entry_size, count, desc_version, desc_size = unpack(data, table, "6B")
    if version != 1 or header < 6 or entry_size < 6 or not 1 <= count <= 64:
        raise ValueError("Unsupported Falcon table layout")
    span(data, table + header, count * entry_size)
    candidates = []
    for i in range(count):
        app, target, ptr = unpack(data, table + header + i * entry_size, "BBI")
        if app == 0x85:
            candidates.append((bias + ptr, target))
    if len(candidates) != 1:
        raise ValueError("Expected exactly one production FWSEC entry")
    desc, target = candidates[0]
    fields = unpack(data, desc, "9IHBBHH")
    vdesc, stored, pkc, interface, imem_base, imem_size, imem_va, dmem_base, dmem_size, engine, ucode, sig_count, sig_versions, reserved = fields
    descriptor_size = vdesc >> 16
    if not vdesc & 1 or (vdesc >> 8) & 255 != 3:
        raise ValueError("Only explicit FWSEC descriptor v3 is supported")
    if not 1 <= sig_count <= 8 or descriptor_size != 44 + sig_count * 384:
        raise ValueError("FWSEC signature count/descriptor size mismatch")
    if not imem_size or not dmem_size or imem_size + dmem_size > stored or stored > ROM_SIZE:
        raise ValueError("FWSEC image and IMEM/DMEM sizes disagree")
    descriptor = span(data, desc, descriptor_size)
    image_offset = desc + descriptor_size
    image = span(data, image_offset, (stored + 255) & ~255)
    dmem = span(image, imem_size, dmem_size)
    span(dmem, pkc, 384)
    av, ah, ae, ac = unpack(dmem, interface, "4B")
    if av != 1 or ah < 4 or ae < 8 or not 1 <= ac <= 64:
        raise ValueError("Unsupported application interface")
    span(dmem, interface + ah, ac * ae)
    mappers = []
    for i in range(ac):
        app_id, dmem_offset = unpack(dmem, interface + ah + i * ae, "II")
        if app_id == 4:
            mappers.append(dmem_offset)
    if len(mappers) != 1:
        raise ValueError("Missing or duplicate DMEM mapper entry")
    mapper_offset = mappers[0]
    mapper = unpack(dmem, mapper_offset, "IHH14I")
    if mapper[:3] != (0x50414d44, 3, 64):
        raise ValueError("Unsupported DMEM mapper signature/version/size")
    command_offset, command_size = mapper[3:5]
    span(dmem, command_offset, command_size)
    if command_size < 44:
        raise ValueError("FRTS command cannot fit the input buffer")
    # Offline candidate command only. No signature selection or firmware patching.
    frts_offset = (vram_mib << 20) - (2 << 20)
    frts_command = struct.pack("<IIQII", 1, 24, 0, 0, 2) + struct.pack("<IIIII", 1, 20, frts_offset >> 12, 0x100, 2)
    assets = {"fwsec-descriptor.bin": descriptor, "fwsec-image-unmodified.bin": image,
              "fwsec-signatures.bin": descriptor[44:], "frts-command-candidate.bin": frts_command}
    for i in range(sig_count):
        assets[f"fwsec-signature-{i}.bin"] = descriptor[44 + i * 384:44 + (i + 1) * 384]
    report = {"rom_sha256": hashlib.sha256(data).hexdigest(), "images_to_extension": images,
              "expansion_bias": bias, "bit_offset": bit, "bit_tokens": token_count, "falcon_table_offset": table,
              "fwsec": {"descriptor_offset": desc, "descriptor_size": descriptor_size, "version": 3,
                        "target_id": target, "stored_size": stored, "image_offset": image_offset,
                        "imem_size": imem_size, "dmem_size": dmem_size, "imem_base": imem_base,
                        "imem_virtual_base": imem_va, "dmem_base": dmem_base, "pkc_data_offset": pkc,
                        "interface_offset": interface, "engine_id_mask": hex(engine), "ucode_id": ucode,
                        "signature_count": sig_count, "signature_versions_mask": hex(sig_versions),
                        "dmem_mapper_offset": mapper_offset, "command_input_offset": command_offset,
                        "command_input_size": command_size},
              "frts_candidate": {"measured_vram_mib": vram_mib, "region_offset": frts_offset, "region_size": 1 << 20,
                                 "command_size": len(frts_command), "executed": False},
              "extraction_complete": True, "signature_selected": None, "signature_verified": False,
              "firmware_executed": False, "ready_for_hardware_boot": False,
              "remaining_checks": ["read and interpret FWSEC fuse-version selector before choosing a signature",
                                   "prove GPU access to OS DMA buffers", "firmware/MMU/RPC setup and teardown"]}
    report["assets"] = {name: {"bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest()} for name, blob in assets.items()}
    return report, assets


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--vram-mib", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        data = args.rom.read_bytes()
        if hashlib.sha256(data).hexdigest() != args.expected_sha256:
            raise ValueError("Input ROM hash differs from the measured snapshot")
        report, assets = extract(data, args.vram_mib)
        args.output.mkdir(parents=True, exist_ok=True)
        for name, blob in assets.items():
            (args.output / name).write_bytes(blob)
        (args.output / "fwsec-manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(2, f"FWSEC extraction failed: {error}\n")
