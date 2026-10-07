"""Offline GA106 GSP570.144 layout and binding encoders; no allocation or GPU I/O.

Addresses passed here are explicit caller-supplied GPU-visible addresses. Shape
validation cannot establish that they are live IODMACommand mappings. All plans
and radix reports therefore say live_dma_verified=False and never claim VRAM.
"""
import hashlib
import struct

PAGE = 4096
MIB = 1 << 20
VRAM_BYTES = 6 << 30
DMA_LIMIT = 1 << 40
FRTS_OFFSET = 0x17FE00000
BIOS_OFFSET = 0x17FF00000
REFERENCE_HEAP_SIZE = 0x8100000
MAX_IMAGE_SIZE = 128 << 20
MAX_BOOTLOADER_SIZE = 16 << 20
META_MAGIC = 0xDC3AAE21371A60B3
META_REVISION = 1
REFERENCES = {
    "tinygrad": "https://github.com/tinygrad/tinygrad/blob/5ae6526d4787fa498f7876982a72b517b93b4888/tinygrad/runtime/support/nv/ip.py",
    "radix": "https://github.com/NVIDIA/open-gpu-kernel-modules/blob/570.144/src/nvidia/src/kernel/gpu/gsp/kernel_gsp.c",
    "metadata": "https://github.com/NVIDIA/open-gpu-kernel-modules/blob/570.144/src/nvidia/arch/nvalloc/common/inc/gsp/gsp_fw_wpr_meta.h",
    "layout": "https://github.com/NVIDIA/open-gpu-kernel-modules/blob/570.144/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_tu102.c",
    "nouveau": "https://github.com/torvalds/linux/blob/v6.15/drivers/gpu/drm/nouveau/nvkm/subdev/gsp/r535.c",
}


def _uint(value, name, maximum=(1 << 64) - 1, minimum=0):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{name} is outside the supported integer range")
    return value


def _down(value, alignment):
    if value < 0:
        raise ValueError("VRAM subtraction underflow")
    return value & -alignment


def _up(value, alignment):
    return (value + alignment - 1) & -alignment


def plan_vram(vram_bytes, image_size, bootloader_size, *, workspace_raw=1,
              display_fuse_raw=0, heap_size=REFERENCE_HEAP_SIZE,
              non_wpr_heap_size=MIB):
    """Plan the current board's initial boot, preserving its established FRTS.

    The 129 MiB heap default is the pinned tinygrad profile, not a hardware-tested
    minimum. NVIDIA's driver computes a configurable heap size. This planner
    permits explicit MiB-aligned 88..256 MiB choices, subject to its 256 MiB window.
    No margin/retry, workspace relocation, PMU carveout, or other board supported.
    """
    _uint(vram_bytes, "vram_bytes")
    _uint(image_size, "image_size", MAX_IMAGE_SIZE, 1)
    _uint(bootloader_size, "bootloader_size", MAX_BOOTLOADER_SIZE, 1)
    _uint(workspace_raw, "workspace_raw", 0xFFFFFFFF)
    _uint(display_fuse_raw, "display_fuse_raw", 0xFFFFFFFF)
    _uint(heap_size, "heap_size", 256 * MIB, 88 * MIB)
    _uint(non_wpr_heap_size, "non_wpr_heap_size")
    if vram_bytes != VRAM_BYTES or workspace_raw != 1 or display_fuse_raw != 0:
        raise ValueError("Only the measured GA106 6 GiB workspace/fuse profile is supported")
    if heap_size % MIB or non_wpr_heap_size != MIB:
        raise ValueError("Unsupported heap alignment or non-WPR heap size")

    boot_offset = _down(FRTS_OFFSET - bootloader_size, PAGE)
    image_offset = _down(boot_offset - image_size, 0x10000)
    heap_offset = _down(image_offset - heap_size, MIB)
    # Reserve a page for the 256-byte handoff and align WPR start down to 1 MiB,
    # matching the pinned tinygrad/Nouveau non-FMC path.
    wpr_start = _down(heap_offset - PAGE, MIB)
    reserved_start = wpr_start - non_wpr_heap_size
    if reserved_start < VRAM_BYTES - 256 * MIB:
        raise ValueError("GSP reservation exceeds the pre-scrubbed final 256 MiB")
    fields = {
        "sizeOfRadix3Elf": image_size, "sizeOfBootloader": bootloader_size,
        "gspFwRsvdStart": reserved_start, "nonWprHeapOffset": reserved_start,
        "nonWprHeapSize": non_wpr_heap_size, "gspFwWprStart": wpr_start,
        "gspFwHeapOffset": heap_offset, "gspFwHeapSize": heap_size,
        "gspFwOffset": image_offset, "bootBinOffset": boot_offset,
        "frtsOffset": FRTS_OFFSET, "frtsSize": MIB, "gspFwWprEnd": BIOS_OFFSET,
        "fbSize": VRAM_BYTES, "vgaWorkspaceOffset": BIOS_OFFSET,
        "vgaWorkspaceSize": MIB,
    }
    specs = [
        ("non_wpr_heap", reserved_start, non_wpr_heap_size),
        ("wpr_metadata", wpr_start, PAGE),
        ("gsp_heap", heap_offset, heap_size),
        ("gsp_image", image_offset, image_size),
        ("bootloader", boot_offset, bootloader_size),
        ("frts", FRTS_OFFSET, MIB), ("bios_exclusion", BIOS_OFFSET, MIB),
    ]
    ranges = [{"name": name, "offset": offset, "size": size, "end": offset + size}
              for name, offset, size in specs]
    for before, after in zip(ranges, ranges[1:]):
        if before["end"] > after["offset"]:
            raise ValueError("Planned VRAM ranges overlap")
    return {
        "schema": "ga106-gsp570.144-vram-v1",
        "parameters": dict(vram_bytes=vram_bytes, image_size=image_size,
                           bootloader_size=bootloader_size, workspace_raw=workspace_raw,
                           display_fuse_raw=display_fuse_raw, heap_size=heap_size,
                           non_wpr_heap_size=non_wpr_heap_size),
        "fields": fields, "ranges": ranges,
        "reserved_start": reserved_start,
        "required_vram_bytes": BIOS_OFFSET - reserved_start,
        "additional_vram_bytes_beyond_frts": FRTS_OFFSET - reserved_start,
        "top_vram_bytes_including_bios": VRAM_BYTES - reserved_start,
        "prescrubbed_window_start": VRAM_BYTES - 256 * MIB,
        "heap_size_provenance": ("tinygrad_reference_profile_not_live_validated"
                                 if heap_size == REFERENCE_HEAP_SIZE else "explicit_offline_heap_choice"),
        "region_reserved": False, "live_dma_verified": False,
        "bindings_required": ["radix3", "bootloader_pages", "signature_pages",
                              "signature_size", "metadata_pages"],
    }


def radix_geometry(image_size):
    """Return bounded page counts without allocating buffers or inventing PAs."""
    _uint(image_size, "image_size", MAX_IMAGE_SIZE, 1)
    counts = [0, 0, 0, (image_size + PAGE - 1) // PAGE]
    for level in (2, 1, 0):
        counts[level] = (counts[level + 1] + 511) // 512
    if counts[0] != 1:
        raise ValueError("Radix3 root must fit one 4 KiB page")
    offsets = [sum(counts[:level]) * PAGE for level in range(4)]
    return {"level_page_counts": counts, "level_offsets": offsets,
            "table_size": offsets[3], "image_size": image_size,
            "image_padded_size": counts[3] * PAGE,
            "total_pages": sum(counts), "total_size": sum(counts) * PAGE}


def _pages(pages, count, name, contiguous=False):
    if not isinstance(pages, (list, tuple)) or len(pages) != count:
        raise ValueError(f"{name} needs exactly {count} explicit pages")
    checked = []
    for index, address in enumerate(pages):
        _uint(address, f"{name}[{index}]", DMA_LIMIT - PAGE, PAGE)
        if address % PAGE:
            raise ValueError(f"{name} contains an unaligned page")
        if contiguous and index and address != checked[-1] + PAGE:
            raise ValueError(f"{name} must be contiguous for a direct firmware pointer")
        checked.append(address)
    if len(set(checked)) != len(checked):
        raise ValueError(f"{name} aliases a page")
    return checked


def _radix_tables(geometry, pages):
    counts, offsets = geometry["level_page_counts"], geometry["level_offsets"]
    tables = bytearray(geometry["table_size"])
    for level in range(3):
        next_page = offsets[level + 1] // PAGE
        for index in range(counts[level + 1]):
            # Entries are plain 64-bit GPU-visible byte addresses, no PTE flags.
            struct.pack_into("<Q", tables, offsets[level] + index * 8, pages[next_page + index])
    return bytes(tables)


def build_radix3(image, physical_pages):
    """Bind 3 table levels followed by image pages in logical buffer order.

    Noncontiguous physical_pages are supported. Caller must copy the returned
    table_bytes and image_bytes into those exact prepared pages and synchronize
    them before any later use. This function performs none of those operations.
    """
    if type(image) is not bytes:
        raise ValueError("image must be immutable bytes")
    geometry = radix_geometry(len(image))
    pages = _pages(physical_pages, geometry["total_pages"], "radix3 pages")
    return {
        **geometry, "table_bytes": _radix_tables(geometry, pages),
        "image_bytes": image + bytes(geometry["image_padded_size"] - len(image)),
        "image_sha256": hashlib.sha256(image).hexdigest(),
        "root_address": pages[0], "physical_pages": pages,
        "address_provenance": "caller_supplied_gpu_visible_pages_unverified",
        "live_dma_verified": False,
    }


def _validate_radix(radix, image_size):
    if not isinstance(radix, dict):
        raise ValueError("radix3 binding is missing")
    geometry = radix_geometry(image_size)
    if any(radix.get(key) != value for key, value in geometry.items()):
        raise ValueError("radix3 geometry does not match the planned image")
    pages = _pages(radix.get("physical_pages"), geometry["total_pages"], "radix3 pages")
    if radix.get("root_address") != pages[0] or radix.get("table_bytes") != _radix_tables(geometry, pages):
        raise ValueError("radix3 root or table entries were altered")
    data = radix.get("image_bytes")
    if type(data) is not bytes or len(data) != geometry["image_padded_size"]:
        raise ValueError("radix3 image storage is truncated")
    if any(data[image_size:]) or hashlib.sha256(data[:image_size]).hexdigest() != radix.get("image_sha256"):
        raise ValueError("radix3 image or zero padding was altered")
    return pages


def encode_wpr_meta(layout, bootloader_info, bindings):
    """Encode initial-boot metadata only, using explicit disjoint DMA bindings.

    bindings: radix3 (build_radix3 result), bootloader_pages (contiguous),
    signature_pages (one page), signature_size (4096), metadata_pages (one page).
    bootloader_info: payload_size plus monitorCodeOffset/Size,
    monitorDataOffset/Size, manifestOffset/Size from the validated descriptor.
    Authentication, freshness and live DMA ownership remain caller obligations.
    """
    if not isinstance(layout, dict) or not isinstance(bootloader_info, dict) or not isinstance(bindings, dict):
        raise ValueError("Layout, descriptor and explicit bindings are required")
    try:
        canonical = plan_vram(**layout["parameters"])
    except (KeyError, TypeError) as error:
        raise ValueError("Malformed VRAM plan") from error
    if layout != canonical:
        raise ValueError("VRAM plan was altered after calculation")
    f = canonical["fields"]
    if bootloader_info.get("payload_size") != f["sizeOfBootloader"]:
        raise ValueError("Bootloader descriptor size does not match layout")
    portions = []
    for prefix in ("monitorCode", "monitorData", "manifest"):
        offset = _uint(bootloader_info.get(prefix + "Offset"), prefix + "Offset")
        size = _uint(bootloader_info.get(prefix + "Size"), prefix + "Size", minimum=1)
        if offset >= f["sizeOfBootloader"] or size > f["sizeOfBootloader"] - offset:
            raise ValueError("Bootloader descriptor region is truncated")
        portions.append((offset, offset + size))
    ordered = sorted(portions)
    if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
        raise ValueError("Bootloader descriptor regions overlap")
    radix_pages = _validate_radix(bindings.get("radix3"), f["sizeOfRadix3Elf"])
    boot_pages = _pages(bindings.get("bootloader_pages"),
                        _up(f["sizeOfBootloader"], PAGE) // PAGE, "bootloader pages", True)
    if type(bindings.get("signature_size")) is not int or bindings["signature_size"] != PAGE:
        raise ValueError("Pinned GA10x signature must occupy 4096 bytes")
    signature_pages = _pages(bindings.get("signature_pages"), 1, "signature pages")
    metadata_pages = _pages(bindings.get("metadata_pages"), 1, "metadata pages")
    all_pages = radix_pages + boot_pages + signature_pages + metadata_pages
    if len(all_pages) != len(set(all_pages)):
        raise ValueError("DMA allocations overlap across metadata bindings")

    values = [
        META_MAGIC, META_REVISION, radix_pages[0], f["sizeOfRadix3Elf"],
        boot_pages[0], f["sizeOfBootloader"], bootloader_info["monitorCodeOffset"],
        bootloader_info["monitorDataOffset"], bootloader_info["manifestOffset"],
        signature_pages[0], PAGE, f["gspFwRsvdStart"], f["nonWprHeapOffset"],
        f["nonWprHeapSize"], f["gspFwWprStart"], f["gspFwHeapOffset"],
        f["gspFwHeapSize"], f["gspFwOffset"], f["bootBinOffset"], f["frtsOffset"],
        f["frtsSize"], f["gspFwWprEnd"], f["fbSize"], f["vgaWorkspaceOffset"],
        f["vgaWorkspaceSize"], 0,  # bootCount=0; no resume or partition state
    ]
    metadata = bytearray(256)
    struct.pack_into("<26Q", metadata, 0, *values)
    # Bytes 208..255 remain zero: partition/crash fields, VF/flags, PMU size,
    # padding, and verified. Only Booter may establish successful verification.
    return bytes(metadata)
