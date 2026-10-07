"""Build a pinned, unbound GSP preparation package. Never loads a driver or GPU code."""
import argparse
import hashlib
import json
from pathlib import Path

from decode_gsp_booter_probe import decode_capture
from gsp_boot_args import plan_libos, encode_libos_args
from gsp_firmware import inspect_gsp_assets
from gsp_memory import VRAM_BYTES, plan_vram, radix_geometry, build_radix3, encode_wpr_meta
from gsp_rpc import build_queue_template

PAGE = 4096


def _json_bytes(value):
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def prepare(directory, fuse_snapshot=None):
    directory = Path(directory)
    assets = inspect_gsp_assets(directory)
    source = {}
    for key in ("gsp", "bootloader", "booter_load"):
        record = assets[key]
        data = (directory / record["file"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise ValueError("Firmware changed during preparation")
        source[key] = data
    sections = assets["gsp"]["required_sections"]
    image_range, sig_range = sections[".fwimage"], sections[".fwsignature_ga10x"]
    image = source["gsp"][image_range["offset"]:image_range["offset"] + image_range["size"]]
    signature = source["gsp"][sig_range["offset"]:sig_range["offset"] + sig_range["size"]]
    loader = assets["bootloader"]["envelope"]
    loader_image = source["bootloader"][loader["payload_offset"]:loader["payload_offset"] + loader["payload_size"]]
    booter = assets["booter_load"]["envelope"]
    booter_image = source["booter_load"][booter["payload_offset"]:booter["payload_offset"] + booter["payload_size"]]
    layout = plan_vram(VRAM_BYTES, len(image), len(loader_image))
    radix = radix_geometry(len(image))
    queue = build_queue_template()
    libos = plan_libos()
    sizes = {"radix3": radix["total_size"], "bootloader": len(loader_image), "signature": len(signature),
             "metadata": PAGE, "queues": queue["metadata"]["total_shared_bytes"],
             "rmargs": PAGE, "libos_args": PAGE, "logs": libos["log_allocation_bytes"],
             "booter_load": (len(booter_image) + PAGE - 1) // PAGE * PAGE}
    resources = {name: {"bytes": size, "pages": (size + PAGE - 1) // PAGE,
                        "gpu_visible_pages": None, "allocated": False}
                 for name, size in sizes.items()}
    selection = None
    outputs = {"gsp-fwimage.bin": image, "gsp-signature-ga10x.bin": signature,
               "bootloader-payload.bin": loader_image, "booter-payload-unpatched.bin": booter_image,
               "queues-unbound.bin": queue["shared_memory"]}
    if fuse_snapshot is not None:
        snapshot = Path(fuse_snapshot)
        if snapshot.is_symlink() or snapshot.stat().st_size > 1 << 20:
            raise ValueError("Invalid fuse snapshot path or size")
        evidence = decode_capture(snapshot)
        if not evidence["passed"]:
            raise ValueError("SEC2 fuse capture did not pass")
        index = evidence["fuse"]["signature_index"]
        chosen = assets["booter_load"]["signatures"][index]
        patch = assets["booter_load"]["patch"]
        signature_bytes = source["booter_load"][chosen["offset"]:chosen["offset"] + chosen["size"]]
        if hashlib.sha256(signature_bytes).hexdigest() != chosen["sha256"]:
            raise ValueError("Selected SEC2 signature bytes differ")
        patched = bytearray(booter_image)
        off, size = patch["destination_image_offset"], patch["size"]
        patched[off:off + size] = signature_bytes
        outputs["booter-payload-selected.bin"] = bytes(patched)
        selection = {"signature_index": index, "snapshot_sha256": evidence["snapshot_sha256"],
                     "register": "0x824148", "fuse_raw": evidence["fuse"]["first"],
                     "signature_sha256": chosen["sha256"], "patch_offset": off, "patch_size": size,
                     "gpu_acceptance_tested": False, "capture_is_historical_until_live_rechecked": True}
    prerequisites = [
        "Re-read SEC2 fuse 0x824148 immediately before any later booter use; saved capture alone is not fresh evidence",
        "Live device-owned IODMACommand allocation, page enumeration and synchronization for all resources",
        "Fresh board/workspace/display checks and exclusive reservation of the complete 193 MiB reference VRAM region",
        "Bound WPR metadata, contiguous bootloader/log windows and exact image copies in owned DMA pages",
        "SEC2 engine capability/state and bounded booter staging/start/completion path",
        "GSP_SET_SYSTEM_INFO and SET_REGISTRY bootstrap payloads",
        "Validated CPU sequencer/event handling and observed GSP_INIT_DONE",
        "Post-start DMA/VRAM lifetime and normal shutdown or verified platform reset recovery",
    ]
    if selection is None:
        prerequisites.insert(0, "Fresh stable SEC2 fuse 0x824148 for booter signature selection (not FWSEC fuse 0x8241e0)")
    report = {
        "schema": "ga106-gsp-preparation-v1", "firmware_version": "570.144",
        "status": "offline-preparation-complete-unbound", "assets_verified": True,
        "assets": assets, "layout": layout, "radix_geometry": radix,
        "queues": queue["metadata"], "libos": libos, "host_resources": resources,
        "host_bytes_required": sum(r["pages"] * PAGE for r in resources.values()),
        "booter_signature_selection": selection, "remaining_prerequisites": prerequisites,
        "bindings_present": False, "native_dma_prepared": False, "hardware_accessed": False,
        "gsp_firmware_executed": False, "gsp_init_done_observed": False,
        "compute_tested": False, "metal_supported": False, "ready_for_hardware_boot": False,
    }
    outputs["firmware-inspection.json"] = _json_bytes(assets)
    outputs["vram-layout.json"] = _json_bytes(layout)
    outputs["binding-requirements.json"] = _json_bytes(resources)
    report["files"] = [{"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                       for name, data in outputs.items()]
    return report, outputs


def bind(directory, bindings, fuse_snapshot=None):
    """Encode all resources against one disjoint, explicit offline page list.

    This validates addresses only. It cannot prove their IODMACommand ownership
    or lifetime; a native transport must establish that before using these bytes.
    """
    report, files = prepare(directory, fuse_snapshot)
    resources = report["host_resources"]
    if not isinstance(bindings, dict) or set(bindings) != set(resources):
        raise ValueError("Binding must contain exactly all nine named allocations")
    seen = set()
    for name, requirement in resources.items():
        pages = bindings[name]
        if not isinstance(pages, (tuple, list)) or len(pages) != requirement["pages"]:
            raise ValueError("Wrong binding page count: " + name)
        for address in pages:
            if type(address) is not int or not PAGE <= address <= (1 << 40) - PAGE or address % PAGE:
                raise ValueError("Invalid 40-bit page binding: " + name)
            if address in seen:
                raise ValueError("Page alias across GSP allocations: " + name)
            seen.add(address)
    radix = build_radix3(files["gsp-fwimage.bin"], bindings["radix3"])
    wpr = encode_wpr_meta(report["layout"], report["assets"]["bootloader_info"],
                         {"radix3": radix, "bootloader_pages": bindings["bootloader"],
                          "signature_pages": bindings["signature"], "signature_size": PAGE,
                          "metadata_pages": bindings["metadata"]})
    queues = build_queue_template(bindings["queues"])
    libos = encode_libos_args(bindings["logs"], bindings["rmargs"], bindings["libos_args"])
    def pad(data, size):
        if len(data) > size:
            raise ValueError("Encoded data exceeds its allocation")
        return data + bytes(size - len(data))
    booter = files.get("booter-payload-selected.bin", files["booter-payload-unpatched.bin"])
    buffers = {
        "radix3": radix["table_bytes"] + radix["image_bytes"],
        "bootloader": pad(files["bootloader-payload.bin"], resources["bootloader"]["pages"] * PAGE),
        "signature": files["gsp-signature-ga10x.bin"], "metadata": pad(wpr, PAGE),
        "queues": queues["shared_memory"], "rmargs": pad(queues["gsp_arguments"], PAGE),
        "libos_args": libos["data"], "logs": bytes(resources["logs"]["bytes"]),
        "booter_load": pad(booter, resources["booter_load"]["bytes"]),
    }
    for name, data in buffers.items():
        if len(data) != resources[name]["pages"] * PAGE:
            raise ValueError("Bound buffer size mismatch: " + name)
    report["status"] = "offline-address-bindings-validated-not-dma-prepared"
    report["bindings_present"] = True
    report["bindings"] = {name: list(pages) for name, pages in bindings.items()}
    report["queues"] = queues["metadata"]
    report["libos"]["regions"] = libos["regions"]
    report["libos"]["binding_state"] = libos["binding_state"]
    report["libos"]["address"] = libos["address"]
    report["bound_buffer_hashes"] = {name: hashlib.sha256(data).hexdigest() for name, data in buffers.items()}
    report["native_dma_prepared"] = False
    report["ready_for_hardware_boot"] = False
    return report, buffers


def write_package(directory, output, fuse_snapshot=None):
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists; use a new directory to preserve evidence")
    report, files = prepare(directory, fuse_snapshot)
    # All inputs/geometry are checked before creating an output directory.
    output.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (output / name).write_bytes(data)
    for row in report["files"]:
        if hashlib.sha256((output / row["path"]).read_bytes()).hexdigest() != row["sha256"]:
            raise ValueError("Prepared output hash mismatch: " + row["path"])
    (output / "manifest.json").write_bytes(_json_bytes(report))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, default=Path(__file__).resolve().parent / "firmware/570.144")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--fuse-snapshot", type=Path)
    args = parser.parse_args()
    try:
        report = write_package(args.firmware, args.output, args.fuse_snapshot)
    except (OSError, ValueError) as error:
        parser.exit(2, "GSP preparation failed: " + str(error) + "\n")
    print(json.dumps({"status": report["status"], "files": len(report["files"]),
                      "host_bytes_required": report["host_bytes_required"],
                      "vram_bytes_required": report["layout"]["required_vram_bytes"],
                      "hardware_accessed": False, "ready_for_hardware_boot": False}, indent=2))
