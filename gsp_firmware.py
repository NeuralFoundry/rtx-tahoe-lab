"""Strict offline inspection of the pinned GA10x GSP 570.144 assets.

Public directory inspection verifies all file hashes before parsing descriptors.
The parse_* helpers validate structure only, for parser fault tests; they never
authenticate bytes, select a live signature, patch a payload or access hardware.
"""
import hashlib
from pathlib import Path
import struct


FIRMWARE_COMMIT = "0a6871b19abf5d6e024b5d208b101ae53e7fa0de"
HASHES = {
    "gsp-570.144.bin": "a8c3ebeed280323aedb51c061f321e73379cce7a9ae643a33dd03915df027f7f",
    "booter_load-570.144.bin": "4497e3eff7e95c774b8a569d17b27c08c9650158d10b229d2be81cdcad9a085b",
    "bootloader-570.144.bin": "82428f532240727e95bb3083fbaaba9b2cc7b937314323f2d546ce7245f27fad",
}
MAX_FILE = 128 << 20
SOURCES = {
    "hs_envelope": "https://github.com/torvalds/linux/blob/v6.15/drivers/gpu/drm/nouveau/include/nvfw/hs.h",
    "booter_layout_and_signature": "https://github.com/NVIDIA/open-gpu-kernel-modules/blob/570.144/src/nvidia/src/kernel/gpu/gsp/kernel_gsp_booter.c",
    "booter_envelope_offsets": "https://github.com/torvalds/linux/blob/v6.15/drivers/gpu/drm/nouveau/nvkm/subdev/gsp/ga102.c",
    "signature_fuse_selection": "https://github.com/torvalds/linux/blob/v6.15/drivers/gpu/drm/nouveau/nvkm/falcon/ga100.c",
    "signature_table_indexing": "https://github.com/torvalds/linux/blob/v6.15/drivers/gpu/drm/nouveau/nvkm/falcon/fw.c",
    "riscv_descriptor": "https://github.com/NVIDIA/open-gpu-kernel-modules/blob/570.144/src/nvidia/arch/nvalloc/common/inc/rmRiscvUcode.h",
    "bootloader_payload_offsets": "https://github.com/torvalds/linux/blob/v6.15/drivers/gpu/drm/nouveau/nvkm/subdev/gsp/r535.c",
}
RISCV_FIELDS = (
    "version", "bootloaderOffset", "bootloaderSize", "bootloaderParamOffset", "bootloaderParamSize",
    "riscvElfOffset", "riscvElfSize", "appVersion", "manifestOffset", "manifestSize",
    "monitorDataOffset", "monitorDataSize", "monitorCodeOffset", "monitorCodeSize", "bIsMonitorEnabled",
    "swbromCodeOffset", "swbromCodeSize", "swbromDataOffset", "swbromDataSize", "fbReservedSize", "bSignedAsCode",
)
HS_FIELDS = ("sig_prod_offset", "sig_prod_size", "patch_loc", "patch_sig", "meta_data_offset",
             "meta_data_size", "num_sig", "header_offset", "header_size")
LOAD_FIELDS = ("osCodeOffset", "osCodeSize", "osDataOffset", "osDataSize", "numApps",
               "appCodeOffset", "appCodeSize", "appDataOffset", "appDataSize")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _slice(data, offset, size, label):
    if type(offset) is not int or type(size) is not int or offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError("Out-of-file range: " + label)
    return data[offset:offset + size]


def _words(data, offset, count, label):
    return struct.unpack("<" + str(count) + "I", _slice(data, offset, count * 4, label))


def _disjoint(ranges, label):
    ordered = sorted((offset, offset + size, name) for name, offset, size in ranges if size)
    for left, right in zip(ordered, ordered[1:]):
        if left[1] > right[0]:
            raise ValueError("Overlapping " + label + ": " + left[2] + " / " + right[2])


def _range(data, offset, size, label, file_base=0):
    payload = _slice(data, offset, size, label)
    return {"offset": offset, "size": size, "end": offset + size,
            "file_offset": file_base + offset, "sha256": sha256(payload)}


def _envelope(data, descriptor_size):
    if not isinstance(data, bytes) or len(data) > MAX_FILE:
        raise ValueError("Firmware must be bounded immutable bytes")
    magic, version, declared, header, payload, size = _words(data, 0, 6, "binary envelope")
    if magic != 0x10de or version != 1:
        raise ValueError("Unsupported NVIDIA envelope magic/version")
    # NVIDIA's bin_size rounds the physical file up to 256 bytes. It does not
    # create readable bytes: all nested ranges are bounded by the actual file.
    if declared != (len(data) + 255) & ~255 or header != 24 or not size:
        raise ValueError("Unsupported NVIDIA envelope size/header")
    _slice(data, header, descriptor_size, "nested descriptor")
    _slice(data, payload, size, "payload")
    if payload + size != len(data) or payload < header + descriptor_size:
        raise ValueError("Payload overlaps descriptors or has unaccounted trailing bytes")
    return {"magic": "0x10de", "version": version, "declared_size": declared,
            "actual_size": len(data), "header_offset": header, "payload_offset": payload,
            "payload_size": size, "payload_sha256": sha256(data[payload:])}


def parse_booter(data):
    """Inspect the HSv2 one-application SEC2 booter; do not select/patch a signature."""
    envelope = _envelope(data, 36)
    hs = dict(zip(HS_FIELDS, _words(data, envelope["header_offset"], 9, "HSv2 header")))
    if hs["header_size"] != 36 or hs["meta_data_size"] != 12:
        raise ValueError("Unsupported HSv2 load-header or metadata size")
    metadata_ranges = [("envelope", 0, 24), ("HSv2", 24, 36),
                       ("signatures", hs["sig_prod_offset"], hs["sig_prod_size"]),
                       ("patch location pointer", hs["patch_loc"], 4),
                       ("patch signature pointer", hs["patch_sig"], 4),
                       ("patch metadata", hs["meta_data_offset"], 12),
                       ("signature count pointer", hs["num_sig"], 4),
                       ("load descriptor", hs["header_offset"], 36)]
    for name, offset, size in metadata_ranges:
        if offset & 3 or not size or offset + size > envelope["payload_offset"]:
            raise ValueError("Invalid booter metadata range: " + name)
        _slice(data, offset, size, name)
    _disjoint(metadata_ranges, "booter metadata")
    count, = _words(data, hs["num_sig"], 1, "signature count")
    patch_location, = _words(data, hs["patch_loc"], 1, "patch location")
    patch_signature, = _words(data, hs["patch_sig"], 1, "patch signature")
    fuse_version, engine, ucode = _words(data, hs["meta_data_offset"], 3, "patch metadata")
    if count != 2 or hs["sig_prod_size"] != count * 384 or (fuse_version, engine, ucode) != (1, 1, 3):
        raise ValueError("Unsupported GA10x booter signature count/size/metadata")
    # patch_sig is a byte offset from sig_prod_offset, not a signature index.
    # Nouveau copies count * sig_size bytes starting there. The pinned table
    # describes the entire production block, so a nonzero shift is invalid.
    if patch_signature + count * 384 > hs["sig_prod_size"]:
        raise ValueError("Signature source table exceeds production signatures")
    load = dict(zip(LOAD_FIELDS, _words(data, hs["header_offset"], 9, "load descriptor")))
    if load["numApps"] != 1 or load["appDataSize"] != 0:
        raise ValueError("Unsupported booter application topology")
    image = data[envelope["payload_offset"]:]
    segments = {
        "nonsecure_imem": (load["osCodeOffset"], load["osCodeSize"]),
        "secure_imem": (load["appCodeOffset"], load["appCodeSize"]),
        "dmem": (load["osDataOffset"], load["osDataSize"]),
    }
    for name, (offset, size) in segments.items():
        if not size or (offset | size) & 255:
            raise ValueError("Unaligned or empty booter segment: " + name)
        _slice(image, offset, size, name)
    _slice(image, load["appDataOffset"], load["appDataSize"], "unused application data")
    _disjoint([(name, *span) for name, span in segments.items()], "booter image")
    dmem_offset, dmem_size = segments["dmem"]
    if patch_location < dmem_offset or patch_location + 384 > dmem_offset + dmem_size or patch_location & 3:
        raise ValueError("Signature patch does not fit entirely in DMEM")
    signatures = []
    for index in range(count):
        offset = hs["sig_prod_offset"] + patch_signature + index * 384
        signatures.append(dict(_range(data, offset, 384, "production signature"), index=index))
    segment_report = {name: _range(image, offset, size, name, envelope["payload_offset"])
                      for name, (offset, size) in segments.items()}
    return {"format": "NVIDIA-HSv2-SEC2-booter", "structurally_valid": True, "sha256_verified": False,
            "envelope": envelope, "payload_size": envelope["payload_size"], "hs_header": hs,
            "load_header": load, "segments": segment_report,
            "application_data": {"offset": load["appDataOffset"], "size": load["appDataSize"],
                                 "used_by_verified_hs_loader": False},
            "hs_load_targets": {"secure_imem_physical_base": 0, "secure_imem_virtual_base": load["appCodeOffset"],
                                "dmem_physical_base": 0, "nonsecure_segment_loaded_by_hs_path": False},
            "patch": {"location_pointer_file_offset": hs["patch_loc"], "destination_image_offset": patch_location,
                      "destination_file_offset": envelope["payload_offset"] + patch_location,
                      "destination_dmem_offset": patch_location - dmem_offset, "size": 384,
                      "signature_pointer_file_offset": hs["patch_sig"], "signature_source_byte_offset": patch_signature,
                      "signature_count_pointer_file_offset": hs["num_sig"], "signature_count": count,
                      "metadata_file_offset": hs["meta_data_offset"], "metadata_size": hs["meta_data_size"],
                      "fuse_version": fuse_version, "engine_id": engine, "ucode_id": ucode,
                      "applied": False},
            "signatures": signatures, "selected_signature": None,
            "signature_selection": {"requires_register": "0x824148", "requires_fresh_stable_hardware_evidence": True,
                                    "raw_zero_candidate_index": 1, "raw_one_candidate_index": 0,
                                    "other_raw_values_supported": False,
                                    "rule": "raw=0: count-1; otherwise metadata_fuse_version-bit_length(raw)",
                                    "prior_fwsec_fuse_0x8241e0_is_not_this_evidence": True},
            "signature_cryptographically_verified": False, "ready_for_hardware_boot": False}


def parse_bootloader(data):
    """Inspect the exact 84-byte RM_RISCV_UCODE_DESC version-5 family."""
    envelope = _envelope(data, 84)
    descriptor = dict(zip(RISCV_FIELDS, _words(data, envelope["header_offset"], 21, "RISC-V descriptor")))
    if descriptor["version"] != 5 or descriptor["bIsMonitorEnabled"] != 1 or descriptor["bSignedAsCode"] != 0:
        raise ValueError("Unsupported GA10x RISC-V descriptor version/monitor/signing mode")
    if envelope["payload_offset"] != envelope["header_offset"] + 84:
        raise ValueError("Unexpected RISC-V descriptor extension")
    image = data[envelope["payload_offset"]:]
    names = ("bootloader", "bootloaderParam", "riscvElf", "manifest", "monitorData", "monitorCode", "swbromCode", "swbromData")
    regions = {}
    for name in names:
        offset, size = descriptor[name + "Offset"], descriptor[name + "Size"]
        regions[name] = _range(image, offset, size, name, envelope["payload_offset"])
        if not size and offset:
            raise ValueError("Nonzero offset for an absent RISC-V region: " + name)
    _disjoint([(name, r["offset"], r["size"]) for name, r in regions.items()], "RISC-V payload regions")
    if any(not regions[name]["size"] for name in ("bootloader", "bootloaderParam", "manifest", "monitorData", "monitorCode")):
        raise ValueError("Required RISC-V monitor/loader region is empty")
    if not envelope["payload_size"] <= descriptor["fbReservedSize"] <= MAX_FILE:
        raise ValueError("RISC-V reserved image size cannot contain the payload")
    return {"format": "NVIDIA-RM-RISCV-UCODE-DESC-v5", "structurally_valid": True, "sha256_verified": False,
            "envelope": envelope, "descriptor": descriptor, "payload_size": envelope["payload_size"],
            "regions": regions, "offset_coordinate_system": "relative to copied binary payload, not file header",
            "manifest_contents_interpreted": False, "signature_cryptographically_verified": False,
            "ready_for_hardware_boot": False}


def parse_gsp_elf(data):
    if not isinstance(data, bytes) or len(data) > MAX_FILE:
        raise ValueError("GSP ELF must be bounded immutable bytes")
    header = struct.unpack("<16sHHIQQQIHHHHHH", _slice(data, 0, 64, "ELF header"))
    if header[0][:7] != b"\x7fELF\x02\x01\x01" or header[2] != 243 or header[3] != 1 or header[8] != 64:
        raise ValueError("Unsupported ELF64 little-endian RISC-V header")
    table, size, count, strings_index = header[6], header[11], header[12], header[13]
    if size != 64 or not 1 <= count <= 4096 or not 0 < strings_index < count:
        raise ValueError("Unsupported ELF section table")
    sections = list(struct.iter_unpack("<IIQQQQIIQQ", _slice(data, table, count * 64, "section table")))
    string_section = sections[strings_index]
    if string_section[1] != 3:
        raise ValueError("ELF names section is not a string table")
    names = _slice(data, string_section[4], string_section[5], "section names")
    ranges = [("ELF header", 0, 64), ("section table", table, count * 64)]
    report = {}
    for index, section in enumerate(sections):
        name_index, kind, _, _, offset, length, *_ = section
        if name_index >= len(names):
            raise ValueError("ELF section name out of bounds")
        end = names.find(b"\0", name_index)
        if end < 0:
            raise ValueError("Unterminated ELF section name")
        try:
            name = names[name_index:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError("Non-ASCII ELF section name") from error
        if name in report:
            raise ValueError("Duplicate ELF section name")
        if kind != 8:
            _slice(data, offset, length, name)
            ranges.append((name or "null section", offset, length))
        if name in (".fwimage", ".fwsignature_ga10x") and (kind != 1 or not length):
            raise ValueError("Missing data in required GSP section")
        report[name] = {"index": index, "type": kind, "offset": offset, "size": length}
    _disjoint(ranges, "ELF file ranges")
    if not all(name in report for name in (".fwimage", ".fwsignature_ga10x")):
        raise ValueError("Missing GA10x GSP image/signature")
    if report[".fwsignature_ga10x"]["size"] != 4096:
        raise ValueError("Unsupported GA10x GSP signature size")
    required = {name: _range(data, report[name]["offset"], report[name]["size"], name)
                for name in (".fwimage", ".fwsignature_ga10x")}
    return {"format": "ELF64-LE-RISCV", "structurally_valid": True, "sha256_verified": False,
            "machine": 243, "section_count": count, "required_sections": required,
            "other_signature_sections": {name: value for name, value in report.items()
                                         if name.startswith(".fwsignature") and name != ".fwsignature_ga10x"},
            "selected_signature_section": ".fwsignature_ga10x", "signature_selection_basis": "pinned GA106 belongs to GA10x family",
            "signature_cryptographically_verified": False, "ready_for_hardware_boot": False}


def inspect_gsp_assets(directory: Path) -> dict:
    """Read and hash-check all three pinned files, then inspect without mutation."""
    directory = Path(directory)
    if directory.is_symlink():
        raise ValueError("Firmware directory must not be a symlink")
    verified = {}
    for name, expected in HASHES.items():
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise ValueError("Missing or symlinked pinned firmware: " + name)
        if path.stat().st_size > MAX_FILE:
            raise ValueError("Oversized pinned firmware: " + name)
        data = path.read_bytes()
        if len(data) > MAX_FILE or sha256(data) != expected:
            raise ValueError("Pinned SHA-256 mismatch: " + name)
        verified[name] = data
    # No descriptor is interpreted until every asset has passed its pin.
    result = {
        "format": "RTXProbe-offline-GSP-assets-v1", "firmware_version": "570.144",
        "linux_firmware_commit": FIRMWARE_COMMIT, "all_pinned_assets_verified": True,
        "gsp": parse_gsp_elf(verified["gsp-570.144.bin"]),
        "booter_load": parse_booter(verified["booter_load-570.144.bin"]),
        "bootloader": parse_bootloader(verified["bootloader-570.144.bin"]),
        "sources": SOURCES,
        "uncertainties": ["SEC2 booter signature requires fresh fuse 0x824148 evidence; no signature selected or patched",
                          "Manifest and GSP signature bytes are bounded and hashed, not cryptographically verified",
                          "No descriptor or file hash proves GPU memory placement, boot success or RPC readiness"],
        "payloads_modified": False, "hardware_accessed": False, "firmware_executed": False,
        "ready_for_hardware_boot": False,
    }
    for key, name in (("gsp", "gsp-570.144.bin"), ("booter_load", "booter_load-570.144.bin"),
                      ("bootloader", "bootloader-570.144.bin")):
        result[key].update(sha256_verified=True, file=name, sha256=HASHES[name], file_size=len(verified[name]))
    result["bootloader_info"] = dict(result["bootloader"]["descriptor"], payload_size=result["bootloader"]["payload_size"])
    return result
