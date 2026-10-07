"""Offline NVIDIA 570.144 GSP queue ABI. No MMIO, DMA or tinygrad dependency.

Only unencrypted, unfragmented RPC v3.0 records are supported. This is a byte
encoder and ring simulator, not a live transport or a GSP bootstrap engine.
"""
from dataclasses import dataclass
import struct

PAGE_SIZE = 4096
QUEUE_SIZE = 0x40000
TX_HEADER_SIZE = 32
RX_HEADER_OFFSET = 32
TRANSPORT_HEADER_SIZE = 48
RPC_HEADER_SIZE = 32
MAX_ELEMENTS = 16
MAX_RECORD_SIZE = MAX_ELEMENTS * PAGE_SIZE
MAX_RPC_SIZE = MAX_RECORD_SIZE - TRANSPORT_HEADER_SIZE
# A maximum-size first chunk has no framing flag distinguishing it from an
# unfragmented record. Reject that ambiguous boundary until reassembly exists.
MAX_PAYLOAD_SIZE = MAX_RPC_SIZE - RPC_HEADER_SIZE - 1
HEADER_VERSION = 0x03000000
SIGNATURE = 0x43505256
RPC_PENDING = 0xffffffff
CONTINUATION_RECORD = 71
GSP_SET_SYSTEM_INFO = 72
SET_REGISTRY = 73
GSP_INIT_DONE = 0x1001
GSP_RUN_CPU_SEQUENCER = 0x1002
GSP_ARGUMENTS_SIZE = 72

ABI_OFFSETS = {
    "tx": {"version": 0, "size": 4, "msgSize": 8, "msgCount": 12,
           "writePtr": 16, "flags": 20, "rxHdrOff": 24, "entryOff": 28},
    "transport": {"authTag": 0, "aad": 16, "checksum": 32, "sequence": 36,
                  "elementCount": 40, "alignmentPadding": 44, "rpc": 48},
    "rpc": {"version": 0, "signature": 4, "length": 8, "function": 12,
            "result": 16, "privateResult": 20, "sequence": 24, "cpuRmGfid": 28,
            "payload": 32},
    "gsp_arguments": {"sharedMemPhysAddr": 0, "pageTableEntryCount": 8,
                      "cmdQueueOffset": 16, "statQueueOffset": 24,
                      "srOldLevel": 32, "srFlags": 36, "srInPMTransition": 40,
                      "gpuInstance": 44, "bDmemStack": 48, "profilerPA": 56,
                      "profilerSize": 64},
}


class PacketError(ValueError):
    pass


class UnsupportedFragmentation(PacketError):
    pass


class UnsupportedMode(PacketError):
    pass


class RingFull(BufferError):
    pass


def _uint(value, bits, name):
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise PacketError(name + " must be an unsigned " + str(bits) + "-bit integer")
    return value


def _bytes(value, name):
    if type(value) is not bytes:
        raise PacketError(name + " must be immutable bytes")
    return value


def _align(value, alignment):
    return (value + alignment - 1) & -alignment


@dataclass(frozen=True)
class QueueLayout:
    queue_size: int = QUEUE_SIZE

    def __post_init__(self):
        _uint(self.queue_size, 32, "queue_size")
        # Smaller layouts are useful in the offline simulator. This bounded
        # profile excludes large queues requiring multi-page PTE tables.
        if self.queue_size % PAGE_SIZE or not 3 * PAGE_SIZE <= self.queue_size <= QUEUE_SIZE:
            raise PacketError("queue_size must be 3..64 aligned pages")

    @property
    def slot_count(self):
        return self.queue_size // PAGE_SIZE - 1

    @property
    def usable_slots(self):
        return self.slot_count - 1

    def tx_header(self, write_index=0):
        _index(write_index, self.slot_count)
        return struct.pack("<8I", 0, self.queue_size, PAGE_SIZE, self.slot_count,
                           write_index, 1, RX_HEADER_OFFSET, PAGE_SIZE)


def decode_queue_header(data, layout=QueueLayout()):
    _bytes(data, "queue header")
    if not isinstance(layout, QueueLayout):
        raise PacketError("layout must be QueueLayout")
    if len(data) < TX_HEADER_SIZE:
        raise PacketError("truncated queue header")
    fields = struct.unpack_from("<8I", data)
    expected = struct.unpack("<8I", layout.tx_header())
    if fields[:4] != expected[:4] or fields[5:] != expected[5:]:
        raise PacketError("queue header contradicts the selected ABI/layout")
    _index(fields[4], layout.slot_count)
    return dict(zip(ABI_OFFSETS["tx"], fields))


def _index(value, count):
    _uint(value, 32, "ring index")
    if value >= count:
        raise PacketError("ring index outside slot count")


def ring_used(read_index, write_index, count):
    _uint(count, 32, "slot count")
    if count < 2:
        raise PacketError("ring needs one usable and one reserved slot")
    _index(read_index, count)
    _index(write_index, count)
    return (write_index - read_index) % count


def ring_free(read_index, write_index, count):
    return count - 1 - ring_used(read_index, write_index, count)


@dataclass(frozen=True)
class RpcPacket:
    function: int
    payload: bytes
    result: int
    private_result: int
    sequence: int
    cpu_rm_gfid: int

    @property
    def bootstrap_event(self):
        return {GSP_INIT_DONE: "gsp-init-done", GSP_RUN_CPU_SEQUENCER:
                "cpu-sequencer-handler-required"}.get(self.function)


def encode_rpc(function, payload=b"", *, result=RPC_PENDING,
               private_result=RPC_PENDING, sequence=0, cpu_rm_gfid=0):
    _bytes(payload, "payload")
    for name, value in (("function", function), ("result", result),
                        ("private_result", private_result), ("sequence", sequence),
                        ("cpu_rm_gfid", cpu_rm_gfid)):
        _uint(value, 32, name)
    if function == CONTINUATION_RECORD or len(payload) > MAX_PAYLOAD_SIZE:
        raise UnsupportedFragmentation("continuations and ambiguous maximum-size first fragments are unsupported")
    if cpu_rm_gfid:
        raise UnsupportedMode("this bare-metal queue profile requires cpuRmGfid=0")
    return struct.pack("<8I", HEADER_VERSION, SIGNATURE, RPC_HEADER_SIZE + len(payload),
                       function, result, private_result, sequence, cpu_rm_gfid) + payload


def decode_rpc(data):
    _bytes(data, "RPC")
    if not RPC_HEADER_SIZE <= len(data) <= MAX_RPC_SIZE:
        raise PacketError("RPC length outside bounded header/record limits")
    version, signature, length, function, result, private, sequence, gfid = struct.unpack_from("<8I", data)
    if version != HEADER_VERSION or signature != SIGNATURE:
        raise PacketError("unsupported RPC version or signature")
    if length != len(data):
        raise PacketError("RPC length does not include exactly its header and payload")
    if function == CONTINUATION_RECORD or length == MAX_RPC_SIZE:
        raise UnsupportedFragmentation("continuation or ambiguous maximum-size first fragment")
    if gfid:
        raise UnsupportedMode("nonzero cpuRmGfid is outside this bare-metal profile")
    return RpcPacket(function, data[RPC_HEADER_SIZE:], result, private, sequence, gfid)


def checksum32(data):
    """Vendor XOR-fold checksum, padded to 8 bytes. Not authentication."""
    _bytes(data, "checksum input")
    if not data or len(data) > MAX_RECORD_SIZE:
        raise PacketError("checksum input length outside one transport record")
    padded = data + bytes((-len(data)) % 8)
    value = 0
    for (word,) in struct.iter_unpack("<Q", padded):
        value ^= word
    return (value >> 32) ^ (value & 0xffffffff)


@dataclass(frozen=True)
class TransportRecord:
    sequence: int
    element_count: int
    checksum: int
    rpc: RpcPacket


def encode_record(function, payload=b"", *, transport_sequence=0, **rpc_fields):
    _uint(transport_sequence, 32, "transport_sequence")
    rpc = encode_rpc(function, payload, **rpc_fields)
    length = TRANSPORT_HEADER_SIZE + len(rpc)
    count = (length + PAGE_SIZE - 1) // PAGE_SIZE
    prefix = bytes(32) + struct.pack("<4I", 0, transport_sequence, count, 0)
    record = bytearray(prefix + rpc)
    struct.pack_into("<I", record, 32, checksum32(bytes(record)))
    return bytes(record) + bytes(count * PAGE_SIZE - len(record))


def decode_record(data, *, expected_sequence=None):
    _bytes(data, "transport record")
    if not PAGE_SIZE <= len(data) <= MAX_RECORD_SIZE or len(data) % PAGE_SIZE:
        raise PacketError("transport record must contain 1..16 complete slots")
    checksum, sequence, count, padding = struct.unpack_from("<4I", data, 32)
    if not 1 <= count <= MAX_ELEMENTS or len(data) != count * PAGE_SIZE:
        raise PacketError("transport element count contradicts byte length")
    if data[:32] != bytes(32):
        raise UnsupportedMode("encrypted/authenticated CC records are unsupported")
    if padding:
        raise PacketError("transport alignment field must be zero")
    rpc_length = struct.unpack_from("<I", data, TRANSPORT_HEADER_SIZE + 8)[0]
    if not RPC_HEADER_SIZE <= rpc_length <= MAX_RPC_SIZE:
        raise PacketError("untrusted RPC length outside record limits")
    used = TRANSPORT_HEADER_SIZE + rpc_length
    if (used + PAGE_SIZE - 1) // PAGE_SIZE != count:
        raise PacketError("RPC length and transport element count disagree")
    padded_end = _align(used, 8)
    if any(data[used:padded_end]):
        raise PacketError("nonzero checksum-alignment padding")
    if checksum32(data[:padded_end]) != 0:
        raise PacketError("transport checksum mismatch")
    if expected_sequence is not None:
        _uint(expected_sequence, 32, "expected_sequence")
        if sequence != expected_sequence:
            raise PacketError("transport sequence mismatch")
    rpc = decode_rpc(data[TRANSPORT_HEADER_SIZE:used])
    # Unused bytes after 8-byte checksum alignment may contain stale slot data
    # in the real vendor queue. They are neither payload nor checksum input.
    return TransportRecord(sequence, count, checksum, rpc)


class OfflineRing:
    """One directional ring; public bytearray permits adversarial simulation.

    No cache coherency, memory barriers, doorbells, IRQs, or event execution are
    modeled. Failed validation does not consume slots or advance sequence state.
    """
    def __init__(self, layout=QueueLayout(), *, initial_index=0, initial_sequence=0):
        if not isinstance(layout, QueueLayout):
            raise PacketError("layout must be QueueLayout")
        _index(initial_index, layout.slot_count)
        _uint(initial_sequence, 32, "initial_sequence")
        self.layout = layout
        self.data = bytearray(layout.slot_count * PAGE_SIZE)
        self.read_index = self.write_index = initial_index
        self.next_tx_sequence = self.next_rx_sequence = initial_sequence

    @property
    def available(self):
        if type(self.data) is not bytearray or len(self.data) != self.layout.slot_count * PAGE_SIZE:
            raise PacketError("simulated ring backing storage changed shape")
        return ring_used(self.read_index, self.write_index, self.layout.slot_count)

    @property
    def free(self):
        return self.layout.usable_slots - self.available

    def push(self, function, payload=b"", **rpc_fields):
        frame = encode_record(function, payload, transport_sequence=self.next_tx_sequence, **rpc_fields)
        count = len(frame) // PAGE_SIZE
        if count > self.free:
            raise RingFull("insufficient space; reserved slot cannot be consumed")
        offset = self.write_index * PAGE_SIZE
        first = min(len(frame), len(self.data) - offset)
        self.data[offset:offset + first] = frame[:first]
        if first < len(frame):
            self.data[:len(frame) - first] = frame[first:]
        sequence = self.next_tx_sequence
        self.write_index = (self.write_index + count) % self.layout.slot_count
        self.next_tx_sequence = (self.next_tx_sequence + 1) & 0xffffffff
        return sequence

    def peek(self):
        if not self.available:
            return None
        offset = self.read_index * PAGE_SIZE
        count = struct.unpack_from("<I", self.data, offset + 40)[0]
        if not 1 <= count <= min(MAX_ELEMENTS, self.layout.usable_slots):
            raise PacketError("untrusted element count exceeds ring/record capacity")
        if count > self.available:
            raise PacketError("incomplete transport record; producer has not published all slots")
        length = count * PAGE_SIZE
        first = min(length, len(self.data) - offset)
        frame = bytes(self.data[offset:offset + first])
        if first < length:
            frame += bytes(self.data[:length - first])
        return decode_record(frame, expected_sequence=self.next_rx_sequence)

    def pop(self):
        record = self.peek()
        if record is None:
            return None
        self.read_index = (self.read_index + record.element_count) % self.layout.slot_count
        self.next_rx_sequence = (self.next_rx_sequence + 1) & 0xffffffff
        return record


def build_queue_template(page_addresses=None, *, queue_size=QUEUE_SIZE):
    """Prepare bytes and metadata; addresses are explicit GPU-visible bindings.

    A bound result still does not assert allocation, DMA readiness or execution
    readiness. It is the caller's job to own/pin/synchronize those exact pages.
    """
    layout = QueueLayout(queue_size)
    queue_pages = 2 * queue_size // PAGE_SIZE
    page_count = queue_pages + (queue_pages * 8 + PAGE_SIZE - 1) // PAGE_SIZE
    table_size = _align(page_count * 8, PAGE_SIZE)
    total_size = table_size + 2 * queue_size
    if total_size // PAGE_SIZE != page_count:
        raise PacketError("unsupported page-table self-mapping geometry")
    cmd_offset, status_offset = table_size, table_size + queue_size
    shared = bytearray(total_size)
    shared[cmd_offset:cmd_offset + TX_HEADER_SIZE] = layout.tx_header()
    arguments = None
    bound = page_addresses is not None
    if bound:
        if not isinstance(page_addresses, (list, tuple)) or len(page_addresses) != page_count:
            raise PacketError("binding must provide every shared-memory page, including the PTE table")
        seen = set()
        for index, address in enumerate(page_addresses):
            _uint(address, 40, "GPU-visible page address")
            if not address or address % PAGE_SIZE or address in seen:
                raise PacketError("page bindings must be nonzero, aligned and non-aliasing")
            seen.add(address)
            struct.pack_into("<Q", shared, index * 8, address)
        args = bytearray(GSP_ARGUMENTS_SIZE)
        struct.pack_into("<QI4xQQ", args, 0, page_addresses[0], page_count, cmd_offset, status_offset)
        # NVIDIA570.144 normal-boot default uses a DMEM stack; no profiler,
        # suspend/resume transition, or nonzero GPU instance in this profile.
        args[48] = 1
        arguments = bytes(args)
    metadata = {
        "schema": 1, "firmware_abi": "NVIDIA-570.144", "binding_state": "bound" if bound else "unbound",
        "allocation_performed": False, "dma_prepared": False, "execution_ready": False,
        "queue_size": queue_size, "slot_size": PAGE_SIZE, "slot_count": layout.slot_count,
        "usable_slots": layout.usable_slots, "page_count": page_count, "page_table_bytes": table_size,
        "total_shared_bytes": total_size, "command_offset": cmd_offset, "status_offset": status_offset,
        "status_header_initialized": False, "gsp_arguments_size": GSP_ARGUMENTS_SIZE,
        "pointer_locations": {"host_command_write": cmd_offset + 16,
                              "gsp_command_read": status_offset + RX_HEADER_OFFSET,
                              "gsp_status_write": status_offset + 16,
                              "host_status_read": cmd_offset + RX_HEADER_OFFSET},
        "required_bindings": [] if bound else ["GPU-visible address for each of " + str(page_count) + " shared pages"],
        "blockers": ["native pinned DMA allocation and synchronization", "status queue initialization by firmware",
                     "GSP_SET_SYSTEM_INFO and SET_REGISTRY bootstrap RPC payloads",
                     "GSP_RUN_CPU_SEQUENCER handler and completion semantics",
                     "observed GSP_INIT_DONE; interrupts, notifications and timeout recovery"],
    }
    return {"metadata": metadata, "shared_memory": bytes(shared), "gsp_arguments": arguments}
