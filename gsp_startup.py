"""Offline NVIDIA 570.144 bootstrap messages and bounded event parsing.

No device access, firmware execution, register executor, allocation or live
freshness assertion exists here. The native caller must collect current facts
and retain the DMA/VRAM ownership described in GSP-STARTUP-REFERENCE.md.
"""
import hashlib
import re
import struct

import gsp_rpc

SYSTEM_INFO_SIZE = 928
PAGE = 4096
DMA_LIMIT = 1 << 40
REGISTRY_HEADER_SIZE = 8
REGISTRY_ENTRY_SIZE = 16
MAX_REGISTRY_ENTRIES = 64
MAX_REGISTRY_NAME_BYTES = 127
MAX_SEQUENCE_WORDS = 4096
MAX_SEQUENCE_COMMANDS = 256
MAX_POLL_US = 4_000_000
MAX_DELAY_US = 1_000_000
MAX_TOTAL_WAIT_US = 10_000_000
SEQUENCE_HEADER_SIZE = 40

# Natural ABI alignment, independently checked against NVIDIA's C declarations
# and pinned tinygrad nv.py: GspSystemInfo.SIZE=928, hostPageSize.offset=920.
U64_FIELDS = {
    "gpuPhysAddr": 0, "gpuPhysFbAddr": 8, "gpuPhysInstAddr": 16,
    "gpuPhysIoAddr": 24, "nvDomainBusDeviceFunc": 32,
    "simAccessBufPhysAddr": 40, "notifyOpSharedSurfacePhysAddr": 48,
    "pcieAtomicsOpMask": 56, "consoleMemSize": 64, "maxUserVa": 72,
    "clPdbProperties": 112, "sysTimerOffsetNs": 848, "hostPageSize": 920,
}
U32_FIELDS = {
    "pciConfigMirrorBase": 80, "pciConfigMirrorSize": 84, "PCIDeviceID": 88,
    "PCISubDeviceID": 92, "PCIRevisionID": 96, "pcieAtomicsCplDeviceCapMask": 100,
    "Chipset": 120, "chipsetL1ssEnable": 128, "hypervisorType": 836,
    "pcieConfigReg": 900, "gridBuildCsp": 904,
}
BOOL_FIELDS = {
    "bGpuBehindBridge": 124, "bFlrSupported": 125, "b64bBar0Supported": 126,
    "bMnocAvailable": 127, "bUpstreamL0sUnsupported": 132,
    "bUpstreamL1Unsupported": 133, "bUpstreamL1PorSupported": 134,
    "bUpstreamL1PorMobileOnly": 135, "bSystemHasMux": 136, "bIsPassthru": 840,
    "bIsPrimary": 896, "isGridBuild": 897, "bPreserveVideoMemoryAllocations": 908,
    "bTdrEventSupported": 909, "bFeatureStretchVblankCapable": 910,
    "bEnableDynamicGranularityPageArrays": 911, "bClockBoostSupported": 912,
    "bRouteDispIntrsToCPU": 913,
}
U8_FIELDS = {"oorArch": 104, "upstreamAddressValid": 137}
BLOB_FIELDS = {"FHBBusInfo": (138, 10), "chipsetIDInfo": (148, 10),
               "acpiMethodData": (160, 676), "gspVFInfo": (856, 40)}
REQUIRED_SYSTEM_FIELDS = frozenset({
    "gpuPhysAddr", "gpuPhysFbAddr", "gpuPhysInstAddr", "nvDomainBusDeviceFunc",
    "PCIDeviceID", "PCISubDeviceID", "PCIRevisionID", "pciConfigMirrorBase",
    "pciConfigMirrorSize", "maxUserVa", "hostPageSize", "bIsPassthru",
})
_SYSTEM_FIELDS = set(U64_FIELDS) | set(U32_FIELDS) | set(BOOL_FIELDS) | set(U8_FIELDS) | set(BLOB_FIELDS)
_REGISTRY_NAME = re.compile(r"[A-Za-z][A-Za-z0-9_]{0,126}\Z")
_OP_NAMES = ("reg_write", "reg_modify", "reg_poll", "delay_us", "reg_store",
             "core_reset", "core_start", "core_wait_for_halt", "core_resume")
_OP_ARGS = (2, 3, 5, 1, 2, 0, 0, 0, 0)


def _uint(value, bits, name):
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError(name + " must be an unsigned " + str(bits) + "-bit integer")
    return value


def _bytes(value, name):
    if type(value) is not bytes:
        raise ValueError(name + " must be immutable bytes")
    return value


def encode_system_info(fields):
    """Encode explicit current facts using the fixed GA106 board profile.

    Required values have no defaults. Unspecified optional fields are zero,
    meaning no capability/auxiliary surface/platform data is supplied; zero is
    not a claim that ACPI/MUX/platform discovery was performed. Opaque platform
    blobs are size-checked only and require their own native semantic validation.
    pcieConfigReg is its single u32 linkCap member. BAR3 is gpuPhysInstAddr.
    """
    if type(fields) is not dict or not REQUIRED_SYSTEM_FIELDS <= fields.keys() or fields.keys() - _SYSTEM_FIELDS:
        raise ValueError("Missing required or unknown SYSTEM_INFO fields")
    data = bytearray(SYSTEM_INFO_SIZE)
    for table, fmt, bits in ((U64_FIELDS, "<Q", 64), (U32_FIELDS, "<I", 32), (U8_FIELDS, "<B", 8)):
        for name, offset in table.items():
            struct.pack_into(fmt, data, offset, _uint(fields.get(name, 0), bits, name))
    for name, offset in BOOL_FIELDS.items():
        value = fields.get(name, 0)
        if type(value) is bool:
            value = int(value)
        if type(value) is not int or value not in (0, 1):
            raise ValueError("Invalid boolean field: " + name)
        data[offset] = value
    for name, (offset, size) in BLOB_FIELDS.items():
        value = _bytes(fields.get(name, bytes(size)), name)
        if len(value) != size:
            raise ValueError("Wrong platform blob size: " + name)
        if name == "gspVFInfo" and any(value):
            raise ValueError("Virtual-function startup is outside this board profile")
        data[offset:offset + size] = value
    bar0, bar1, bar3 = (fields[key] for key in ("gpuPhysAddr", "gpuPhysFbAddr", "gpuPhysInstAddr"))
    if not 0 < bar0 <= 0xff000000 or bar0 % (16 << 20):
        raise ValueError("BAR0 must describe the current 16 MiB 32-bit aperture")
    if not 0 < bar1 <= DMA_LIMIT - (64 << 20) or bar1 % (64 << 20):
        raise ValueError("BAR1 must describe the current 64 MiB aperture")
    if not PAGE <= bar3 <= DMA_LIMIT - PAGE or bar3 % PAGE:
        raise ValueError("BAR3 must be a measured nonzero aligned physical base")
    if bar0 < bar1 + (64 << 20) and bar1 < bar0 + (16 << 20):
        raise ValueError("BAR0 and BAR1 overlap")
    if bar0 <= bar3 < bar0 + (16 << 20) or bar1 <= bar3 < bar1 + (64 << 20):
        raise ValueError("BAR3 starts inside another aperture")
    if (fields["PCIDeviceID"], fields["PCISubDeviceID"], fields["nvDomainBusDeviceFunc"]) != (0x252010de, 0x104c1043, 0x100):
        raise ValueError("Unexpected GA106 board identity or domain:BDF")
    if fields["PCIRevisionID"] > 0xff:
        raise ValueError("PCI revision exceeds its measured byte")
    if (fields["pciConfigMirrorBase"], fields["pciConfigMirrorSize"], fields["hostPageSize"]) != (0x88000, 0x1000, PAGE):
        raise ValueError("Unsupported PCI mirror or host page profile")
    if not PAGE <= fields["maxUserVa"] <= 1 << 47 or fields["maxUserVa"] % PAGE:
        raise ValueError("Explicit x86_64 maximum user VA must be page aligned")
    for name in ("simAccessBufPhysAddr", "notifyOpSharedSurfacePhysAddr"):
        address = fields.get(name, 0)
        if address and (address < PAGE or address > DMA_LIMIT - PAGE or address % PAGE):
            raise ValueError("Invalid auxiliary sysmem surface address: " + name)
    return bytes(data)


def _system_info_fields(data):
    _bytes(data, "SYSTEM_INFO")
    if len(data) != SYSTEM_INFO_SIZE:
        raise ValueError("SYSTEM_INFO must contain the complete 928-byte ABI")
    fields = {}
    for table, fmt in ((U64_FIELDS, "<Q"), (U32_FIELDS, "<I"), (U8_FIELDS, "<B"), (BOOL_FIELDS, "<B")):
        fields.update((name, struct.unpack_from(fmt, data, offset)[0]) for name, offset in table.items())
    fields.update((name, data[offset:offset + size]) for name, (offset, size) in BLOB_FIELDS.items())
    if encode_system_info(fields) != data:
        raise ValueError("SYSTEM_INFO contains nonzero ABI padding")
    return fields


def encode_registry(entries):
    """Encode only explicitly requested DWORD entries; no registry defaults."""
    if type(entries) is not dict or len(entries) > MAX_REGISTRY_ENTRIES:
        raise ValueError("Registry must be a dictionary of at most 64 entries")
    names = bytearray()
    rows = bytearray(len(entries) * REGISTRY_ENTRY_SIZE)
    name_start = REGISTRY_HEADER_SIZE + len(rows)
    for index, (name, value) in enumerate(entries.items()):
        if type(name) is not str or not _REGISTRY_NAME.fullmatch(name):
            raise ValueError("Registry names must be 1..127 ASCII identifier bytes")
        value = _uint(value, 32, "registry value")
        # DWORD type=1; the three alignment bytes must remain zero.
        struct.pack_into("<IB3xII", rows, index * REGISTRY_ENTRY_SIZE,
                         name_start + len(names), 1, value, 4)
        names.extend(name.encode("ascii") + b"\0")
    total = name_start + len(names)
    return struct.pack("<II", total, len(entries)) + bytes(rows) + bytes(names)


def _registry_entries(data):
    _bytes(data, "registry")
    if not REGISTRY_HEADER_SIZE <= len(data) <= 8 + MAX_REGISTRY_ENTRIES * (REGISTRY_ENTRY_SIZE + 128):
        raise ValueError("Registry table length outside bounded profile")
    size, count = struct.unpack_from("<II", data)
    if size != len(data) or count > MAX_REGISTRY_ENTRIES or 8 + count * 16 > size:
        raise ValueError("Registry size/count contradict its bytes")
    entries = {}
    cursor = 8 + count * 16
    for index in range(count):
        start = 8 + index * 16
        offset, kind, value, length = struct.unpack_from("<IB3xII", data, start)
        if kind != 1 or length != 4 or any(data[start + 5:start + 8]) or offset != cursor:
            raise ValueError("Unsupported registry row or overlapping name offset")
        end = data.find(b"\0", cursor, min(size, cursor + MAX_REGISTRY_NAME_BYTES + 1))
        if end < 0:
            raise ValueError("Truncated or overlong registry name")
        try:
            name = data[cursor:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError("Registry name is not ASCII") from error
        if name in entries or not _REGISTRY_NAME.fullmatch(name):
            raise ValueError("Duplicate or invalid registry name")
        entries[name] = value
        cursor = end + 1
    if cursor != size or encode_registry(entries) != data:
        raise ValueError("Registry table has unconsumed or noncanonical bytes")
    return entries


def prefill_queue(page_addresses, system_info, registry):
    """Place asynchronous requests72 then73 in a fresh command queue.

    No reply is awaited before boot. No doorbell is rung by this offline
    encoder. The firmware-owned status queue remains completely zero.
    """
    _system_info_fields(system_info)
    _registry_entries(registry)
    queue = gsp_rpc.build_queue_template(page_addresses)
    shared = bytearray(queue["shared_memory"])
    layout = gsp_rpc.QueueLayout()
    ring = gsp_rpc.OfflineRing(layout)
    ring.push(gsp_rpc.GSP_SET_SYSTEM_INFO, system_info)
    ring.push(gsp_rpc.SET_REGISTRY, registry)
    command_offset = queue["metadata"]["command_offset"]
    shared[command_offset:command_offset + gsp_rpc.TX_HEADER_SIZE] = layout.tx_header(ring.write_index)
    start = command_offset + PAGE
    shared[start:start + len(ring.data)] = ring.data
    metadata = dict(queue["metadata"])
    metadata.update({"bootstrap_functions": [72, 73], "bootstrap_record_count": 2,
                     "command_write_index": ring.write_index, "next_transport_sequence": 2,
                     "bootstrap_rpc_payload_bytes": [len(system_info), len(registry)],
                     "hardware_accessed": False, "firmware_executed": False,
                     "facts_live_verified": False, "status_header_initialized": False})
    return {"metadata": metadata, "shared_memory": bytes(shared), "gsp_arguments": queue["gsp_arguments"]}


def parse_cpu_sequencer(payload):
    """Parse vendor command order with finite local limits; never execute it.

    The published capacity may exceed transmitted used words; accept only
    complete dwords with cmdIndex <= wire_words <= bufferSizeDWord. Trailing
    allocated words are not interpreted. Register range checking is not a
    hardware-operation allowlist, which must be enforced by the native layer.
    """
    _bytes(payload, "sequencer payload")
    if not SEQUENCE_HEADER_SIZE <= len(payload) <= SEQUENCE_HEADER_SIZE + MAX_SEQUENCE_WORDS * 4 or len(payload) % 4:
        raise ValueError("Sequencer payload length outside bounded dword ABI")
    capacity, used = struct.unpack_from("<II", payload)
    wire_words = (len(payload) - SEQUENCE_HEADER_SIZE) // 4
    if not 0 < capacity <= MAX_SEQUENCE_WORDS or not used < capacity or not used <= wire_words <= capacity:
        raise ValueError("Sequencer capacity, cmdIndex and received bytes disagree")
    words = struct.unpack_from("<" + str(used) + "I", payload, SEQUENCE_HEADER_SIZE)
    operations, cursor, wait_us = [], 0, 0
    while cursor < used:
        index = cursor
        opcode = words[cursor]
        cursor += 1
        if opcode >= len(_OP_ARGS) or len(operations) >= MAX_SEQUENCE_COMMANDS:
            raise ValueError("Unknown opcode or command-count limit")
        count = _OP_ARGS[opcode]
        if count > used - cursor:
            raise ValueError("Truncated sequencer opcode arguments")
        args = words[cursor:cursor + count]
        cursor += count
        operation = {"opcode": opcode, "name": _OP_NAMES[opcode], "word_index": index}
        if opcode in (0, 1, 2, 4):
            address = args[0]
            if address % 4 or address > (16 << 20) - 4:
                raise ValueError("Sequencer register outside aligned BAR0")
            operation["address"] = address
        if opcode == 0:
            operation["value"] = args[1]
        elif opcode in (1, 2):
            # NVIDIA ABI is addr, mask, val. The pinned tinygrad parser swaps
            # mask/val for REG_MODIFY and is not authoritative on that detail.
            mask, value = args[1:3]
            if value & ~mask:
                raise ValueError("Sequencer value extends outside declared mask")
            operation.update(mask=mask, value=value)
            if opcode == 2:
                timeout = args[3] or MAX_POLL_US
                if timeout > MAX_POLL_US:
                    raise ValueError("Sequencer poll exceeds bounded timeout")
                operation.update(timeout_raw=args[3], timeout_us=timeout, error_code=args[4])
                wait_us += timeout
        elif opcode == 3:
            if args[0] > MAX_DELAY_US:
                raise ValueError("Sequencer delay exceeds bounded timeout")
            operation["duration_us"] = args[0]
            wait_us += args[0]
        elif opcode == 4:
            if args[1] >= 8:
                raise ValueError("Sequencer save index exceeds eight registers")
            operation["save_index"] = args[1]
        elif opcode in (7, 8):
            operation["native_timeout_us"] = 2_000_000
            wait_us += 2_000_000
        if wait_us > MAX_TOTAL_WAIT_US:
            raise ValueError("Sequencer aggregate wait exceeds bounded budget")
        operations.append(operation)
    return {"buffer_size_dwords": capacity, "cmd_index": used, "wire_words": wire_words,
            "register_save_area": list(struct.unpack_from("<8I", payload, 8)),
            "operations": operations, "maximum_declared_wait_us": wait_us,
            "payload_sha256": hashlib.sha256(payload).hexdigest(),
            "register_allowlist_validated": False, "operations_executed": False,
            "firmware_executed": False, "hardware_accessed": False}


def parse_init_done(record, *, expected_sequence):
    """Validate framed event0x1001; callers establish its live capture origin."""
    _uint(expected_sequence, 32, "expected transport sequence")
    decoded = gsp_rpc.decode_record(record, expected_sequence=expected_sequence)
    packet = decoded.rpc
    if packet.function != gsp_rpc.GSP_INIT_DONE or packet.result != 0 or len(packet.payload) != 4:
        raise ValueError("INIT_DONE requires event0x1001, success result and four-byte payload")
    return {"validated_init_done_record": True, "transport_sequence": decoded.sequence,
            "rpc_sequence": packet.sequence, "rpc_result": packet.result,
            "rpc_private_result": packet.private_result,
            "unused_payload_word": struct.unpack("<I", packet.payload)[0],
            "record_sha256": hashlib.sha256(record).hexdigest(),
            "live_execution_proven": False, "hardware_accessed": False,
            "compute_tested": False, "metal_supported": False}
