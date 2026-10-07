"""Select an unchanged FWSEC signature from a validated fuse snapshot, offline."""
import argparse
import hashlib
import json
import pathlib
import plistlib
from decode_probe import decode_snapshot
from extract_fwsec import extract

BOARD_ROM_SHA256 = "b7b3d5a2b1698a5a1f24303b0fa915ef8da944ff3ede888377c2d02df5048b91"


def select(nodes, expected_rom_sha256=BOARD_ROM_SHA256):
    decoded = decode_snapshot(nodes)
    if decoded["probe_version"] not in ("0.5.0", "0.6.0") or not decoded.get("fwsec_fuse", {}).get("passed"):
        raise ValueError("A completed 0.5/0.6 fuse measurement is required")
    if not decoded["rom"]["passed"] or decoded["rom"]["sha256"] != expected_rom_sha256:
        raise ValueError("Fresh ROM does not match the measured board ROM")
    node = next(n for n in nodes if n.get("ProbeVersion") == decoded["probe_version"])
    report, assets = extract(node["VBIOSShadow"], decoded["gpu_state"]["reported_vram_mib"])
    fw = report["fwsec"]
    if (fw["engine_id_mask"], fw["ucode_id"], fw["signature_count"], fw["signature_versions_mask"]) != ("0x400", 9, 3, "0x7"):
        raise ValueError("Fresh FWSEC descriptor disagrees with fixed fuse selector")
    index = decoded["fwsec_fuse"]["signature_index_candidate"]
    name = f"fwsec-signature-{index}.bin"
    signature = assets[name]
    result = {"rom_sha256": report["rom_sha256"], "fuse": decoded["fwsec_fuse"],
              "signature_selected": index, "source_asset": name, "bytes": len(signature),
              "sha256": hashlib.sha256(signature).hexdigest(), "signature_verified_by_hardware": False,
              "gpu_host_read_passed": decoded.get("gpu_host_read", {}).get("passed", False),
              "falcon_dma_passed": decoded.get("falcon_dma", {}).get("passed", False),
              "firmware_executed": False, "ready_for_hardware_boot": False}
    return result, signature


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        raw = args.snapshot.read_bytes()
        report, signature = select(plistlib.loads(raw))
        report["snapshot_sha256"] = hashlib.sha256(raw).hexdigest()
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "fwsec-selected-signature.bin").write_bytes(signature)
        (args.output / "signature-selection.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
    except (ValueError, KeyError, OSError, plistlib.InvalidFileException) as e:
        parser.exit(2, f"Signature selection refused: {e}\n")
