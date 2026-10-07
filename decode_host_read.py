"""Validate fixed fuse/PRAMIN evidence; never opens hardware."""
import struct


def signature_index(raw):
    if not isinstance(raw, int) or not 0 <= raw <= 0xffffffff:
        raise ValueError("Fuse is not a u32")
    if raw == 0xffffffff or raw & 0xffff0000 == 0xbadf0000 or raw.bit_length() >= 3:
        return None
    return raw.bit_length()  # rank of BIT(fls(raw)) in the fixed mask 0b111


def pattern(i, phase):
    return (0x52745834 ^ (i * 2654435761) ^ (0xd7c39a65 if phase else 0)) & 0xffffffff


def phase_hash(phase, count=1024):
    value = 2166136261
    for i in range(count):
        value = ((value ^ pattern(i, phase)) * 16777619) & 0xffffffff
    return value


def normalise_registers(node):
    # IORegistry serializes some OSNumber(32) values as signed plist integers.
    # Normalize register/status bits before comparing sentinel values or hashes.
    node = dict(node)
    keys = ("FuseOffset", "FuseFirst", "FuseSecond", "FuseReads", "FuseSignatureMask", "FuseSignatureCount",
            "FuseSignatureIndex", "HostWindowBefore", "HostWindowSecond", "HostWindowProgrammed",
            "HostWindowObserved", "HostWindowAfter", "HostMismatchPhase", "HostMismatchWord", "HostMismatchValue",
            "HostLastIOReturn", "HostClearIOReturn", "HostCompleteIOReturn", "HostMemoryCompleteIOReturn",
            "HostPublish0IOReturn", "HostPublish1IOReturn")
    for key in keys:
        if key in node:
            v = node[key]
            if not isinstance(v, int) or not -(1 << 31) <= v < 1 << 32:
                raise ValueError("Invalid 32-bit register/status value: " + key)
            node[key] = v & 0xffffffff
    return node


def decode_fuse(node, identification_passed, *, execution_context=False):
    if execution_context and (node.get("ProbeVersion") != "0.9.0" or node.get("Mode") != "bounded-fwsec-execute"):
        raise ValueError("Fuse execution context requires the explicit 0.9 execution mode")
    node = normalise_registers(node)
    first, second, reads = (node.get(k) for k in ("FuseFirst", "FuseSecond", "FuseReads"))
    if node.get("FuseOffset") != 0x8241e0 or node.get("FuseSignatureMask") != 7 or node.get("FuseSignatureCount") != 3:
        raise ValueError("Unexpected fixed FWSEC descriptor/fuse register")
    if reads not in (0, 1, 2):
        raise ValueError("Invalid fuse read count")
    index = signature_index(first) if reads == 2 and first == second else None
    fuse_passed = identification_passed and index is not None
    if bool(node.get("FusePassed")) != fuse_passed or (fuse_passed and node.get("FuseSignatureIndex") != index):
        raise ValueError("Fuse selection flag disagrees with register evidence")
    if (type(node.get("FirmwareExecuted")) is not bool or
            (not execution_context and node.get("FirmwareExecuted") is not False)):
        raise ValueError("Unexpected firmware execution in host-read probe")
    fuse = {"passed": fuse_passed, "status": node.get("FuseStatus"), "offset": "0x8241e0",
            "raw_first": first, "raw_second": second, "reads": reads,
            "signature_index_candidate": index if fuse_passed else None,
            "hardware_signature_verification_tested": False}
    if execution_context:
        fuse["measurement_phase"] = "initial-before-execution"
    return fuse


def decode_host_read(node, identification_passed):
    node = normalise_registers(node)
    fuse = decode_fuse(node, identification_passed)
    fuse_passed = fuse["passed"]
    blob = node.get("HostSegments")
    phases_blob = node.get("HostPhases")
    if not isinstance(blob, bytes) or len(blob) != 64 or not isinstance(phases_blob, bytes) or len(phases_blob) != 40:
        raise ValueError("Invalid host-read segment/phase evidence size")
    segments = list(struct.iter_unpack("<QQ", blob))
    rows = list(struct.iter_unpack("<5I", phases_blob))
    if any(reads > 1024 or matched > reads for reads, matched, *_ in rows):
        raise ValueError("Host-read count exceeds bound")
    segment_ok = (node.get("HostSegmentCount") == 4 and node.get("HostEndOffset") == 16384
                  and all(0 < a <= (1 << 40) - 4096 and a % 4096 == 0 and n == 4096 for a, n in segments)
                  and len({a for a, _ in segments}) == 4)
    phases = [{"reads": r, "matched": m, "hash": hex(h), "first": hex(f), "last": hex(l)}
              for r, m, h, f, l in rows]
    phase_ok = all(row == (1024, 1024, phase_hash(p), pattern(0, p), pattern(1023, p)) for p, row in enumerate(rows))
    before, after = node.get("HostWindowBefore"), node.get("HostWindowAfter")
    window_ok = (isinstance(before, int) and 0 <= before < 1 << 24
                 and before == node.get("HostWindowSecond") == after
                 and node.get("HostWindowProgrammed") == node.get("HostWindowObserved") == (0x02000000 | segments[0][0] >> 16))
    errors = {k: node.get(k) for k in ("HostLastIOReturn", "HostClearIOReturn", "HostCompleteIOReturn",
              "HostMemoryCompleteIOReturn", "HostPublish0IOReturn", "HostPublish1IOReturn")}
    flags = ("HostMemoryAttempted", "HostMasterAttempted", "HostWindowAttempted", "HostWindowRestored",
             "HostPrepared", "HostCPUContentsIntact", "HostCleanupVerified")
    consistent = (fuse_passed and segment_ok and phase_ok and window_ok and all(v == 0 for v in errors.values())
                  and all(node.get(k) is True for k in flags) and node.get("HostResourcesRetained") is False
                  and node.get("HostCommandBefore") == node.get("HostCommandAfter") == 0
                  and node.get("HostCommandEnabled") == 6
                  and node.get("HostMapperMode") in ("device-mapper", "system-mapper", "system-no-mapper")
                  and node.get("HostStatus") == "GPU-host-page-read-verified"
                  and node.get("HostMismatchPhase") == node.get("HostMismatchWord") == 0xffffffff)
    if node.get("HostPassed") is True and not consistent:
        raise ValueError("GPU host-read success flag disagrees with raw evidence")
    host = {"passed": node.get("HostPassed") is True and consistent, "status": node.get("HostStatus"),
            "method": "synchronous BAR0 PRAMIN reads targeting coherent system memory",
            "gpu_read_attempted": any(row[0] for row in rows), "phases": phases,
            "segments": [{"address": hex(a), "length": n} for a, n in segments],
            "mapper_mode": node.get("HostMapperMode"), "ior_returns": errors,
            "command_before": node.get("HostCommandBefore"), "command_enabled": node.get("HostCommandEnabled"),
            "command_after": node.get("HostCommandAfter"), "window_before": before, "window_after": after,
            "window_programmed": node.get("HostWindowProgrammed"), "window_observed": node.get("HostWindowObserved"),
            "window_restored": node.get("HostWindowRestored"), "cleanup_verified": node.get("HostCleanupVerified"),
            "resources_retained": node.get("HostResourcesRetained"), "addresses_still_valid": False,
            "mismatch": {"phase": node.get("HostMismatchPhase"), "word": node.get("HostMismatchWord"),
                         "value": node.get("HostMismatchValue"), "value_hex": hex(node.get("HostMismatchValue", 0))},
            "dma_engine_tested": False, "gpu_memory_write_tested": False, "firmware_executed": False,
            "compute_tested": False}
    return fuse, host
