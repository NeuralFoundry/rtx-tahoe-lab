"""Offline 570.144 LibOS arguments. Addresses are supplied IOVM candidates, not mappings.

Layout reference: NVIDIA libos_init_args.h; pinned tinygrad nv/ip.py
init_libos_args. This module neither allocates DMA memory nor touches hardware.
Each directly addressed 64 KiB log window must be contiguous independently;
contiguity of an entire larger allocation is never inferred from its first page.
"""
import struct

PAGE = 4096
DMA_LIMIT = 1 << 40
LOG_ALLOCATION_SIZE = 2 << 20
LOG_SIZE = 0x10000
LOG_NAMES = ("LOGINIT", "LOGINTR", "LOGRM", "LOGMNOC", "LOGKRNL")
REGION = struct.Struct("<QQQBB6x")


def plan_libos():
    return {
        "binding_state": "unbound",
        "log_allocation_bytes": LOG_ALLOCATION_SIZE,
        "log_pages": LOG_ALLOCATION_SIZE // PAGE,
        "log_region_bytes": LOG_SIZE,
        "log_regions": list(LOG_NAMES),
        "args_bytes": PAGE,
        "rmargs_bytes": PAGE,
        "descriptor_bytes": REGION.size,
        "descriptor_count": len(LOG_NAMES) + 1,
        "physical_contiguity_required": "each individual 64 KiB log region",
        "live_dma_verified": False,
    }


def _pages(value, count, name):
    if not isinstance(value, (list, tuple)) or len(value) != count:
        raise ValueError("Wrong page count for " + name)
    for address in value:
        if type(address) is not int or not PAGE <= address <= DMA_LIMIT - PAGE or address % PAGE:
            raise ValueError("Invalid 40-bit page for " + name)
    if len(set(value)) != count:
        raise ValueError("Duplicate page in " + name)
    return list(value)


def encode_libos_args(log_pages, rmargs_pages, args_pages):
    """Build one argument page after validating all explicit page bindings.

    The descriptor ID is an ASCII identifier interpreted as a big-endian
    integer, serialized inside the little-endian 32-byte ABI structure.
    Remaining descriptors/padding stay zero. A bound result is still offline.
    """
    logs = _pages(log_pages, LOG_ALLOCATION_SIZE // PAGE, "logs")
    rmargs = _pages(rmargs_pages, 1, "rmargs")
    args = _pages(args_pages, 1, "libos args")
    all_pages = logs + rmargs + args
    if len(set(all_pages)) != len(all_pages):
        raise ValueError("LibOS allocations overlap")
    rows = []
    for index, name in enumerate(LOG_NAMES):
        window = logs[index * 16:(index + 1) * 16]
        if window != list(range(window[0], window[0] + LOG_SIZE, PAGE)):
            raise ValueError("Noncontiguous direct log region: " + name)
        rows.append((name, window[0], LOG_SIZE))
    rows.append(("RMARGS", rmargs[0], PAGE))
    result = bytearray(PAGE)
    for index, (name, address, size) in enumerate(rows):
        REGION.pack_into(result, index * REGION.size,
                         int.from_bytes(name.encode("ascii"), "big"), address, size, 1, 1)
    return {
        "data": bytes(result),
        "address": args[0],
        "binding_state": "explicit-addresses-validated-offline",
        "live_dma_verified": False,
        "regions": [{"id": name, "address": address, "size": size,
                     "kind": "contiguous", "location": "sysmem"}
                    for name, address, size in rows],
    }
