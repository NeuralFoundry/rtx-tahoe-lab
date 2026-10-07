"""Native 0.13 bootstrap memory adapter test; no GPU or firmware execution."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import prepare_gsp
from runtime_dma_client import BindingError, MacIOKitBackend, NAMES
from tinygrad_bootstrap_memory import BootstrapMemory, UPSTREAM_COMMIT


def run(backend, firmware, fuse_snapshot):
    report = dict(schema="rtx-bootstrap-memory-v1", probe_version="0.13.0", passed=False,
                  backend=backend.kind, upstream_contract_commit=UPSTREAM_COMMIT,
                  firmware_executed=False, gpu_dma_transfer_executed=False,
                  nvidia_compute_executed=False, bindings_are_historical=True,
                  cleanup_verified=False, connection_closed=False, checks={}, cleanup_errors=[])
    memory = None
    try:
        memory = BootstrapMemory(backend)
        report["initial"] = memory.info()
        report["initial_sync"] = memory.sync_info()
        report["bindings"] = {name: list(memory.pages(name)) for name in NAMES}
        bound, buffers = prepare_gsp.bind(firmware, report["bindings"], fuse_snapshot)
        report["package_bound_hashes"] = bound["bound_buffer_hashes"]
        if bound["booter_signature_selection"]["signature_index"] != 0:
            raise BindingError("Pinned image signature differs from live SEC2 fuse")
        if set(buffers) != set(NAMES): raise BindingError("Unexpected bound buffer set")
        for name in NAMES:
            view = memory.resource(name)
            data = buffers[name]
            if len(data) != view.nbytes: raise BindingError("Bound resource size mismatch")
            view[:] = data
            actual = hashlib.sha256(view[:]).hexdigest()
            expected = bound["bound_buffer_hashes"][name]
            report["checks"][name] = dict(bytes=len(data), expected_sha256=expected,
                                          readback_sha256=actual, passed=actual == expected)
            if actual != expected: raise BindingError("Full native readback differs: " + name)
        report["host_bytes_checked"] = sum(len(v) for v in buffers.values())
        rounds = []
        for name in NAMES:
            view = memory.resource(name)
            offsets = sorted(set((0, max(0, min(view.nbytes - 32, 4088)), view.nbytes - 32)))
            for offset in offsets:
                alias = view.view(offset, 32)
                original = buffers[name][offset:offset+32]
                for phase in (0, 1):
                    data = bytes(((i * 17 + offset) & 255) ^ (255 if phase else 0) for i in range(32))
                    alias[:] = data
                    if view[offset:offset+32] != data: raise BindingError("Repeated alias readback mismatch")
                    rounds.append(dict(resource=name, offset=offset, phase=phase, bytes=32, passed=True))
                alias[:] = original
                if alias[:] != original: raise BindingError("Original bound bytes were not restored")
        # Exercise exactly the typed scalar/view shape used by tinygrad queues.
        queue = memory.resource("queues").view(4096, 16, "Q")
        original = buffers["queues"][4096:4112]
        queue[:] = [0x123456789abcdef0, 0xfedcba9876543210]
        if queue[:] != [0x123456789abcdef0, 0xfedcba9876543210]: raise BindingError("Typed queue view mismatch")
        memory.resource("queues")[4096:4112] = original
        if memory.resource("queues")[4096:4112] != original: raise BindingError("Typed queue bytes not restored")
        report["repeat_checks"] = rounds
        report["typed_queue_passed"] = True
        report["owned_after_repeats"] = memory.info()
        report["sync_after_repeats"] = memory.sync_info()
        for name in NAMES:
            if list(memory.pages(name)) != report["bindings"][name]: raise BindingError("DMA pages changed")
            row = report["sync_after_repeats"]["resources"][name]
            if row["write_epoch"] != row["out_epoch"] or row["in_epoch"] != row["out_epoch"]:
                raise BindingError("Final synchronization epochs differ")
            if row["out_count"] < 5 or row["in_count"] < 4:
                raise BindingError("Native repeated synchronization was not recorded")
        memory.close(finish=True)
        report["final"] = memory.final_info
        report["final_sync"] = memory.final_sync
        report["cleanup_verified"] = report["connection_closed"] = True
        report["passed"] = True
    except BaseException as error:
        report["error"] = type(error).__name__ + ": " + str(error)
    finally:
        if memory is not None and not memory.closed:
            try:
                memory.close()
                report["cleanup_verified"] = True
            except BaseException as error: report["cleanup_errors"].append(str(error))
        if not getattr(backend, "closed", False):
            try: backend.close()
            except BaseException as error: report["cleanup_errors"].append(str(error))
        report["connection_closed"] = bool(getattr(backend, "closed", False))
        if report["cleanup_errors"]: report["passed"] = False
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--fuse-snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.output.open("x") as output:
        try: result = run(MacIOKitBackend(), args.firmware, args.fuse_snapshot)
        except (BindingError, ValueError, OSError) as error: result = dict(passed=False, error=str(error))
        json.dump(result, output, indent=2); output.write("\n")
    print(json.dumps({key: result.get(key) for key in ("passed", "host_bytes_checked", "typed_queue_passed",
                     "cleanup_verified", "connection_closed", "error", "cleanup_errors")}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__": sys.exit(main())
